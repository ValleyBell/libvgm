// license:BSD-3-Clause
// copyright-holders:Aaron Giles
/*
 *   streaming ADPCM driver
 *   by Aaron Giles
 *
 *   Library to transcode from an ADPCM source to raw PCM.
 *   Written by Buffoni Mirko in 08/06/97
 *   References: various sources and documents.
 *
 *   HJB 08/31/98
 *   modified to use an automatically selected oversampling factor
 *   for the current sample rate
 *
 *   01/06/99
 *    separate MSM5205 emulator form adpcm.c and some fix
 *
 *   07/29/12
 *    added basic support for the MSM6585
 */
/*

    MSM 5205 ADPCM chip:

    Data is streamed from a CPU by means of a clock generated on the chip.

    Holding the rate selector lines (S1 and S2) both high places the MSM5205 in an undocumented
    mode which disables the sampling clock generator and makes VCK an input line.

    A reset signal is set high or low to determine whether playback (and interrupts) are occurring.

  MSM6585: is an upgraded MSM5205 voice synth IC.
   Improvements:
    More precise internal DA converter
    Built in low-pass filter
    Expanded sampling frequency

   Differences between MSM6585 & MSM5205:

                              MSM6585                      MSM5205
    Master clock frequency    640kHz                       384k/768kHz
    Sampling frequency        4k/8k/16k/32kHz at 640kHz    4k/6k/8kHz at 384kHz
    ADPCM bit length          4-bit                        3-bit/4-bit
    Data capture timing       3µsec at 640kHz              15.6µsec at 384kHz
    DA converter              12-bit                       10-bit
    Low-pass filter           -40dB/oct                    N/A
    Overflow prevent circuit  Included                     N/A
    Cutoff Frequency          (Sampling Frequency/2.5)kHz  N/A

    Data capture follows VCK falling edge on MSM5205 (VCK rising edge on MSM6585)

   TODO:
   - lowpass filter for MSM6585

 */

/**********************************************************************************************
    OKI MSM5205 ADPCM (Full Working Implementation for libvgm)
// copyright-holders:eito, cam900, Valley Bell, Mao

	References:
	https://vgmrips.net/forum/viewtopic.php?t=3436
	https://gitlab.com/cam900/vgsound_emu/-/tree/main/vgsound_emu/src/msm5205?ref_type=heads
	https://github.com/mamedev/mame/blob/master/src/devices/sound/msm5205.cpp (for the 6585)
	https://consolemods.org/wiki/images/f/f8/MSM5205.pdf
 
***********************************************************************************************/

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "../../stdtype.h"
#include "../snddef.h"
#include "../EmuHelper.h"
#include "../EmuCores.h"
#include "../logging.h"
#include "../SoundDevs.h"
#include "../dac_control.h"
#include "msm5205.h"

#define PIN_S1      0x01
#define PIN_S2      0x02

// ========== Function Prototypes ==========
static UINT8 device_start_msm5205(const MSM5205_CFG *cfg, DEV_INFO *retDevInf);
static void device_stop_msm5205(void *chip);
static void device_reset_msm5205(void *chip);
static void msm5205_update(void *param, UINT32 samples, DEV_SMPL **outputs);
static UINT32 msm5205_get_rate(void *chip);
static void msm5205_set_clock(void *chip, UINT32 clock);
static void msm5205_write(void *chip, UINT8 offset, UINT8 data);
static void msm5205_set_mute_mask(void *chip, UINT32 MuteMask);
static void msm5205_set_srchg_cb(void *chip, DEVCB_SRATE_CHG CallbackFunc, void *DataPtr);
static void msm5205_set_log_cb(void *chip, DEVCB_LOG func, void *param);

// ========== Core Structure ==========
typedef struct _msm5205_state {
    DEV_DATA _devData;
    DEV_LOGGER logger;
    
    UINT32  master_clock;
    INT32   signal;
    INT32   step;
    UINT8   vclk;
    
    UINT8   data_buf[8];
    UINT8   data_in_last;
    UINT8   data_buf_pos;
    UINT8   data_empty;
    
    UINT8   reset;
    UINT8   init_prescaler;
    UINT8   prescaler;
    UINT8   init_bitwidth;
    UINT8   bitwidth;
    UINT8   output_mask;
    UINT8   Muted;

    UINT8   is_msm6585; // 0 = MSM5205, 1 = MSM6585

    DEVCB_SRATE_CHG SmpRateFunc;
    void*   SmpRateData;
} msm5205_state;

// ========== Global Tables ==========
static const int index_shift[8] = {-1, -1, -1, -1, 2, 4, 6, 8};
static int diff_lookup[49*16];
static UINT8 tables_computed = 0;

// ========== Device Definition ==========
static DEVDEF_RWFUNC devFunc[] = {
    {RWF_REGISTER | RWF_WRITE, DEVRW_A8D8, 0, msm5205_write},
    {RWF_CLOCK | RWF_WRITE, DEVRW_VALUE, 0, msm5205_set_clock},
    {RWF_SRATE | RWF_READ, DEVRW_VALUE, 0, msm5205_get_rate},
    {RWF_CHN_MUTE | RWF_WRITE, DEVRW_ALL, 0, msm5205_set_mute_mask},
    {0x00, 0x00, 0, NULL}
};

static DEV_DEF devDef = {
    "MSM5205", "eito", FCC_EITO,

    (DEVFUNC_START)device_start_msm5205,
    device_stop_msm5205,
    device_reset_msm5205,
    msm5205_update,

    NULL,   // SetOptionBits
    msm5205_set_mute_mask,
    NULL,   // SetPanning
    msm5205_set_srchg_cb,	// SetSampleRateChangeCallback
    msm5205_set_log_cb,		// SetLoggingCallback
    NULL,   // LinkDevice

    devFunc	// rwFuncs
};

static const char* DeviceName(const DEV_GEN_CFG* devCfg)
{
	if (devCfg != NULL && devCfg->flags)
		return "MSM6585";
	return "MSM5205";
}

static UINT16 DeviceChannels(const DEV_GEN_CFG* devCfg)
{
	return 1;
}

static const char** DeviceChannelNames(const DEV_GEN_CFG* devCfg)
{
	return NULL;
}

static const DEVLINK_IDS* DeviceLinkIDs(const DEV_GEN_CFG* devCfg)
{
	return NULL;
}

const DEV_DECL sndDev_MSM5205 =
{
	DEVID_MSM5205,
	DeviceName,
	DeviceChannels,
	DeviceChannelNames,
	DeviceLinkIDs,
	{	// cores
		&devDef,
		NULL
	}
};

// ========== Helper Functions ==========
static void compute_tables(void) {
    static const int nbl2bit[16][4] = {
        {1,0,0,0}, {1,0,0,1}, {1,0,1,0}, {1,0,1,1},
        {1,1,0,0}, {1,1,0,1}, {1,1,1,0}, {1,1,1,1},
        {-1,0,0,0}, {-1,0,0,1}, {-1,0,1,0}, {-1,0,1,1},
        {-1,1,0,0}, {-1,1,0,1}, {-1,1,1,0}, {-1,1,1,1}
    };
    int step;

    if (tables_computed) return;

    for (step = 0; step <= 48; step++) {
        int stepval = (int)floor(16.0 * pow(11.0 / 10.0, (double)step));
        int nib;
        for (nib = 0; nib < 16; nib++) {
            diff_lookup[step*16 + nib] = nbl2bit[nib][0] *
                (stepval   * nbl2bit[nib][1] +
                 stepval/2 * nbl2bit[nib][2] +
                 stepval/4 * nbl2bit[nib][3] +
                 stepval/8);
        }
    }
    tables_computed = 1;
}

INLINE UINT32 get_prescaler(msm5205_state *info) {
    if (info->is_msm6585) {
        return (info->prescaler & PIN_S1) ? 
            ((info->prescaler & PIN_S2) ? 20 : 80) : 
            ((info->prescaler & PIN_S2) ? 40 : 160);
    } else {
        return (info->prescaler & PIN_S1) ? 
            ((info->prescaler & PIN_S2) ? 1/* Slave mode */ : 64) : 
            ((info->prescaler & PIN_S2) ? 48 : 96);
    }
}

// ========== Core ADPCM Processing ==========
static INT16 clock_adpcm(msm5205_state *chip, UINT8 data) {
    int sample;

    if (chip->reset) {
        chip->step = 0;
        chip->signal = 0;
        return 0;
    }

    if (chip->bitwidth == 3) data <<= 1;
    data &= 0x0F;

    sample = diff_lookup[chip->step * 16 + (data & 15)];
    chip->signal = ((sample << 8) + (chip->signal * 245)) >> 8;

    chip->signal = (chip->signal > 2047) ? 2047 : 
                  ((chip->signal < -2048) ? -2048 : chip->signal);
    
    chip->step += index_shift[data & 7];
    chip->step = (chip->step > 48) ? 48 : 
                ((chip->step < 0) ? 0 : chip->step);
    
    return (INT16)chip->signal;
}

// ========== Device Interface ==========
static UINT8 device_start_msm5205(const MSM5205_CFG *cfg, DEV_INFO *retDevInf) {
    msm5205_state *info;

    compute_tables();
    
    info = (msm5205_state*)calloc(1, sizeof(msm5205_state));
    if (!info) return 0xFF;

    info->master_clock = cfg->_genCfg.clock;
    info->signal = -2;
    info->step = 0;
    info->vclk = 0;
    info->Muted = 0;
    info->data_empty = 0xFF;
    info->init_prescaler = cfg->prescaler;
    info->prescaler = info->init_prescaler;
    info->init_bitwidth = cfg->adpcmBits;
    if (! info->init_bitwidth)
        info->init_bitwidth = 4;
    info->bitwidth = info->init_bitwidth;
    info->data_buf[0] = 0;
    info->is_msm6585 = (cfg->_genCfg.flags & 0x01); // new flag!

    info->_devData.chipInf = info;
    INIT_DEVINF(retDevInf, &info->_devData, msm5205_get_rate(info), &devDef);
    return 0x00;
}

static void device_stop_msm5205(void *chip) {
    free((msm5205_state*)chip);
}

static void device_reset_msm5205(void *chip) {
    msm5205_state *info = (msm5205_state*)chip;
    
    info->signal = -2;
    info->step = 0;
    info->vclk = 0;
    memset(info->data_buf, 0, sizeof(info->data_buf));
    info->data_buf_pos = 0;
    info->data_empty = 0xFF;
    info->prescaler = info->init_prescaler;
    info->bitwidth = info->init_bitwidth;
    info->data_buf[0] = 0;
    
    if (info->SmpRateFunc)
        info->SmpRateFunc(info->SmpRateData, msm5205_get_rate(info));
}

// ========== Audio Generation ==========
static void msm5205_update(void *param, UINT32 samples, DEV_SMPL **outputs) {
    msm5205_state *info = (msm5205_state*)param;
    DEV_SMPL *bufL = outputs[0];
    DEV_SMPL *bufR = outputs[1];
    UINT32 i;

    for (i = 0; i < samples; i++) {
        INT16 sample = 0;
        
        if (!info->Muted && !(info->reset)) {
            UINT8 read_pos = info->data_buf_pos & 0x0F;
            UINT8 write_pos = (info->data_buf_pos >> 4) & 0x07;
            
            if ((read_pos != write_pos) && (get_prescaler(info) != 1)) { // if not slave mode
                UINT8 data = info->data_buf[read_pos];
                sample = clock_adpcm(info, data);
                info->data_buf_pos = (write_pos << 4) | ((read_pos + 1) & 0x07);
            } else {
                sample = (INT16)info->signal;
            }
        }
        
        bufL[i] = bufR[i] = (DEV_SMPL)sample << 4;
    }
}

// ========== I/O Handling ==========
static void msm5205_write(void *chip, UINT8 offset, UINT8 data) {
    msm5205_state *info = (msm5205_state*)chip;
    
    switch (offset)
    {
        case 0: /* reset */ {
            UINT8 old = info->reset;
            info->reset = data;
            
            if (old ^ data) {
                info->signal = 0;
                info->step = 0;
            }
            if (info->reset)
                info->data_buf_pos = 0;
            break;
        }
        case 1: /* data_w */ {
            UINT8 write_pos = (info->data_buf_pos >> 4) & 0x07;
            UINT8 read_pos = info->data_buf_pos & 0x07;
            
            if (((write_pos + 1) & 0x07) == read_pos) {
                emu_logf(&info->logger, DEVLOG_DEBUG, "MSM5205 FIFO overflow\n");
                return;
            }
            
            info->data_buf[write_pos] = data;
            info->data_buf_pos = ((write_pos + 1) << 4) | read_pos;
            break;
        }
        case 2: { // vclk
            UINT8 old = info->vclk;
            info->vclk = data;

            if (get_prescaler(info) == 1) { // if slave mode
                if (((old ^ data) & 1) && info->vclk) {
                    if (!info->Muted && !(info->reset)) {
                        UINT8 read_pos = info->data_buf_pos & 0x0F;
                        UINT8 write_pos = (info->data_buf_pos >> 4) & 0x07;
                        
                        if (read_pos != write_pos) {
                            UINT8 data = info->data_buf[read_pos];
                            clock_adpcm(info, data);
                            info->data_buf_pos = (write_pos << 4) | ((read_pos + 1) & 0x07);
                        }
                    }
                }
            }
            break;
        }
        case 4: /* set prescaler */ {
            UINT8 old = info->prescaler;
            info->prescaler = data;
            
            if ((old ^ data) & (PIN_S1|PIN_S2)) {
                if (info->SmpRateFunc)
                    info->SmpRateFunc(info->SmpRateData, msm5205_get_rate(info));
            }
            break;
        }
        case 5: /* set bitwidth */ {
            info->bitwidth = data ? 4 : 3;
            break;
        }
    }
}

// ========== Configuration ==========
static UINT32 msm5205_get_rate(void *chip) {
    msm5205_state *info = (msm5205_state*)chip;
    return info->master_clock / get_prescaler(info);
}

static void msm5205_set_clock(void *chip, UINT32 clock) {
    msm5205_state *info = (msm5205_state*)chip;
    info->master_clock = clock;
    if (info->SmpRateFunc)
        info->SmpRateFunc(info->SmpRateData, msm5205_get_rate(info));
}

static void msm5205_set_mute_mask(void *chip, UINT32 MuteMask) {
    msm5205_state *info = (msm5205_state*)chip;
    info->Muted = MuteMask & 0x01;
}

static void msm5205_set_srchg_cb(void *chip, DEVCB_SRATE_CHG CallbackFunc, void *DataPtr) {
    msm5205_state *info = (msm5205_state*)chip;
    info->SmpRateFunc = CallbackFunc;
    info->SmpRateData = DataPtr;
}

static void msm5205_set_log_cb(void *chip, DEVCB_LOG func, void *param) {
    msm5205_state *info = (msm5205_state*)chip;
    dev_logger_set(&info->logger, info, func, param);
}
