// license:BSD-3-Clause
// copyright-holders:Aaron Giles, Paul Leaman, Miguel Angel Horna, Mao, cam900
/***************************************************************************

  Data East BSMT2000
  ==================

  Core for BSMT2000 sound chip, adapted for libvgm, with fixes based on M1/MAME/PinMAME source.

  The BSMT2000 is a custom TMS320C15 DSP with internal ROM and external sample ROM.
  It supports multiple PCM voices and a single ADPCM channel, with stereo output.

  FIXES:
  - Register layout and handling match M1/MAME/PinMAME (9 registers per voice)
  - Position, rate, and loop logic use fixed-point math (16.16)
  - Linear interpolation for PCM voices
  - ADPCM/compressed channel handling is per M1/MAME/PinMAME
  - Voice init matches M1/MAME/PinMAME

  Modifications for PINMAME by Steve Ellenoff & Martin Adrian & Carsten Waechter

  References:
  https://github.com/vpinball/pinmame/blob/master/src/sound/bsmt2000.c
  https://github.com/vpinball/pinmame/blob/master/src/sound/bsmt2000.h
  M1's BSMT2000 source code. https://vgmrips.net/forum/viewtopic.php?t=110
  https://www.researchgate.net/publication/291338452_Hacking_a_Sega_Whitestar_Pinball
  
***************************************************************************/

#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <math.h>

#include "../../stdtype.h"
#include "../EmuStructs.h"
#include "../SoundDevs.h"
#include "../EmuCores.h"
#include "../snddef.h"
#include "../EmuHelper.h"
#include "../logging.h"
#include "bsmt2000.h"

/* ==== Constants ==== */

#define BSMT2000_CLOCK        24000000    /* default 24MHz clock */
#define BSMT2000_CHANNELS     12          /* up to 12 PCM voices, plus ADPCM */
#define BSMT2000_ADPCM_INDEX  12          /* 0..11 = PCM, 12 = ADPCM/compressed */
#define BSMT2000_REG_CURRPOS  0
#define BSMT2000_REG_RATE 1
#define BSMT2000_REG_LOOPEND     2
#define BSMT2000_REG_LOOPSTART  3
#define BSMT2000_REG_BANK 4
#define BSMT2000_REG_RIGHTVOL     5
#define BSMT2000_REG_LEFTVOL 6
#define BSMT2000_REG_TOTAL    7

#define BSMT2000_MAX_VOICES   (BSMT2000_CHANNELS + 1)  /* 12 PCM + 1 ADPCM/compressed */
#define BSMT2000_SAMPLE_CHUNK 10000

#define BSMT2000_ROM_BANKSIZE 0x10000     /* 64k per bank */

static const UINT8 regmap[8][7] = {
    { 0x00, 0x18, 0x24, 0x30, 0x3c, 0x48, 0xff }, // last one (stereo/leftvol) unused, set to max for mapping
    { 0x00, 0x16, 0x21, 0x2c, 0x37, 0x42, 0x4d },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, // mode 2 only a testmode left channel
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, // mode 3 only a testmode right channel
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, // mode 4 only a testmode left channel
    { 0x00, 0x18, 0x24, 0x30, 0x3c, 0x54, 0x60 },
    { 0x00, 0x10, 0x18, 0x20, 0x28, 0x38, 0x40 },
    { 0x00, 0x12, 0x1b, 0x24, 0x2d, 0x3f, 0x48 } };

/* ==== Internal Voice State ==== */
typedef struct
{
    UINT16 reg[9]; // 9 registers per voice, match MAME/M1
    UINT32 position; // 16.16 fixed-point
    UINT32 loop_start_position;
    UINT32 loop_stop_position;
    UINT32 adjusted_rate;
} bsmt2000_voice;

/* ==== Main Chip State ==== */
typedef struct _bsmt2000_state bsmt2000_state;
struct _bsmt2000_state
{
    DEV_DATA _devData;
    DEV_LOGGER logger;

    // Sample ROM
    UINT8 *sample_rom;
    UINT32 sample_rom_length;
    UINT32 sample_rom_mask;
    UINT32 total_banks;

    // Voices and ADPCM/compressed
    bsmt2000_voice voice[BSMT2000_MAX_VOICES];
    UINT8 right_volume_set; // Right volume is set (from PinMAME)
    UINT8 voices;         // actual number of voices (usually 11 or 12)
    UINT8 stereo;         // stereo output enabled?
    UINT8 adpcm;          // ADPCM/compressed enabled?
    UINT8 mode;           // current mode (0,1,5,6,7)
    UINT8 last_register;  // last register written
    UINT16 latch;         // buffer to combine 8-bit writes to a 16-bit words
    UINT32 clock;         // Chip clock

    // ADPCM/compressed state
    INT32 adpcm_current;
    INT32 adpcm_delta_n;
    UINT16 adpcm_delta_init;    // register 0x76: ADPCM scale factor at start
    UINT16 mod_depth;           // register 0x77: how much voice 1 bends voice 0's pitch, see bsmt2000_update

    // Output sample rate
    double sample_rate;

    // Mute mask
    UINT8 Muted[BSMT2000_MAX_VOICES];

    // Misc
    // (can add more fields as needed)
	DEVCB_SRATE_CHG SmpRateFunc;
	void* SmpRateData;
};

/* ==== Prototypes ==== */
static void bsmt2000_update(void *param, UINT32 samples, DEV_SMPL **outputs);
static UINT8 device_start_bsmt2000(const DEV_GEN_CFG* cfg, DEV_INFO* retDevInf);
static void device_stop_bsmt2000(void *info);
static void device_reset_bsmt2000(void *info);

static void bsmt2000_w(void *info, UINT8 offset, UINT8 data);
static UINT8 bsmt2000_r(void *info, UINT8 offset);
static void bsmt2000_write_data(void *info, UINT8 address, UINT16 data);

static void bsmt2000_alloc_rom(void* info, UINT32 memsize);
static void bsmt2000_write_rom(void *info, UINT32 offset, UINT32 length, const UINT8* data);
static void bsmt2000_set_mute_mask(void *info, UINT32 MuteMask);
static UINT32 bsmt2000_get_mute_mask(void *info);
static void bsmt2000_set_log_cb(void* info, DEVCB_LOG func, void* param);
static void bsmt2000_set_srchg_cb(void *info, DEVCB_SRATE_CHG CallbackFunc, void* DataPtr);

/* ==== Device Function Table ==== */
static DEVDEF_RWFUNC devFunc[] =
{
    {RWF_REGISTER | RWF_WRITE, DEVRW_A8D8, 0, bsmt2000_w},
    {RWF_REGISTER | RWF_READ, DEVRW_A8D8, 0, bsmt2000_r},
    {RWF_REGISTER | RWF_QUICKWRITE, DEVRW_A8D16, 0, bsmt2000_write_data},
    {RWF_MEMORY | RWF_WRITE, DEVRW_BLOCK, 0, bsmt2000_write_rom},
    {RWF_MEMORY | RWF_WRITE, DEVRW_MEMSIZE, 0, bsmt2000_alloc_rom},
    {RWF_CHN_MUTE | RWF_WRITE, DEVRW_ALL, 0, bsmt2000_set_mute_mask},
    {0x00, 0x00, 0, NULL}
};

/* ==== Device Definition ==== */
DEV_DEF devDef =
{
    "BSMT2000", "MAME", FCC_MAME,

    device_start_bsmt2000,
    device_stop_bsmt2000,
    device_reset_bsmt2000,
    bsmt2000_update,

    NULL, // SetOptionBits
    bsmt2000_set_mute_mask,
    NULL, // SetPanning
    bsmt2000_set_srchg_cb,
    bsmt2000_set_log_cb, // SetLoggingCallback
    NULL, // LinkDevice

    devFunc,    // rwFuncs
};

static const char* DeviceName(const DEV_GEN_CFG* devCfg)
{
	return "BSMT2000";
}

static UINT16 DeviceChannels(const DEV_GEN_CFG* devCfg)
{
	return BSMT2000_MAX_VOICES;
}

static const char** DeviceChannelNames(const DEV_GEN_CFG* devCfg)
{
	static const char* names[BSMT2000_MAX_VOICES] =
	{
		"PCM 1", "PCM 2", "PCM 3", "PCM 4", "PCM 5", "PCM 6", "PCM 7", "PCM 8",
		"PCM 9", "PCM 10", "PCM 11", "PCM 12",
		"ADPCM",
	};
	return names;
}

static const DEVLINK_IDS* DeviceLinkIDs(const DEV_GEN_CFG* devCfg)
{
	return NULL;
}

const DEV_DECL sndDev_BSMT2000 =
{
	DEVID_BSMT2000,
	DeviceName,
	DeviceChannels,
	DeviceChannelNames,
	DeviceLinkIDs,
	{	// cores
		&devDef,
		NULL
	}
};

/* ==== Utility Macros ==== */
#define MIN(x,y) ((x)<(y)?(x):(y))

/* ==== Internal Functions ==== */

static void init_voice(bsmt2000_voice *voice)
{
    memset(voice, 0, sizeof(*voice)); // the chip's program clears its RAM on reset, so also all volumes
    voice->reg[BSMT2000_REG_LEFTVOL] = 0;
    voice->reg[BSMT2000_REG_RIGHTVOL] = 0;
    voice->position = 0;
    voice->adjusted_rate = 0;
    voice->loop_start_position = 0;
    voice->loop_stop_position = 0;
}
static void init_all_voices(bsmt2000_state *chip)
{
    int i;
    for (i = 0; i < BSMT2000_MAX_VOICES; i++)
        init_voice(&chip->voice[i]);
}

/* ==== Device Start ==== */
static UINT8 device_start_bsmt2000(const DEV_GEN_CFG* cfg, DEV_INFO* retDevInf)
{
    bsmt2000_state *chip;
    chip = (bsmt2000_state *)calloc(1, sizeof(bsmt2000_state));
    if (!chip)
        return 0xFF;

    chip->sample_rom = NULL;
    chip->sample_rom_length = 0x00;
    chip->sample_rom_mask = 0x00;
    chip->total_banks = 0;

    chip->voices = 12;       // default: 12 PCM (may be overridden later)
    chip->stereo = 1;        // default: stereo enabled
    chip->adpcm = 1;         // default: ADPCM enabled

    chip->right_volume_set = 0;
    chip->mod_depth = 0;

    chip->last_register = 1; // Mode 1
    chip->mode = 1;

    chip->clock = cfg->clock;
    chip->sample_rate = chip->clock / 1000.0;

    bsmt2000_set_mute_mask(chip, 0x0000);

    chip->_devData.chipInf = chip;
    INIT_DEVINF(retDevInf, &chip->_devData, (UINT32)chip->sample_rate, &devDef);

    return 0x00;
}

/* ==== Device Stop ==== */
static void device_stop_bsmt2000(void *info)
{
    bsmt2000_state *chip = (bsmt2000_state *)info;
    free(chip->sample_rom);
    free(chip);
}

/* ==== Device Reset ==== */
static void device_reset_bsmt2000(void *info) {
    bsmt2000_state *chip = (bsmt2000_state *)info;

    init_all_voices(chip);
    chip->adpcm_current = 0;
    chip->adpcm_delta_init = 10; // the chip's program sets 0x76 to this on reset
    chip->adpcm_delta_n = chip->adpcm_delta_init;
    chip->mod_depth = 0; // the chip's program clears its RAM on reset, 0x77 included

    switch (chip->last_register)
    {
        default:
            break;
        /* mode 0: 24kHz, 12 channel PCM, 1 channel ADPCM, mono; from PinMAME */
        case 0:
            chip->sample_rate = chip->clock / 4. / 249.5;
            chip->stereo = 0;
            chip->voices = 12;
            chip->adpcm = 1;
            chip->mode = 0;
            break;
        /* mode 1: 24kHz, 11 channel PCM, 1 channel ADPCM, stereo */
        case 1:
            chip->sample_rate = chip->clock / 4. / 250.5;
            chip->stereo = 1;
            chip->voices = 11;
            chip->adpcm = 1;
            chip->mode = 1;
            break;
        /* mode 5: 24kHz, 12 channel PCM, stereo */
        case 5:
            chip->sample_rate = chip->clock / 4. / 254.;
            chip->stereo = 1;
            chip->voices = 12;
            chip->adpcm = 0;
            chip->mode = 5;
            break;
        /* mode 6: 34kHz (actually more like 31kHz), 8 channel PCM, stereo */
        case 6:
            chip->sample_rate = chip->clock / 4. / 193.;
            chip->stereo = 1;
            chip->voices = 8;
            chip->adpcm = 0;
            chip->mode = 6;
            break;
        /* mode 7: 32kHz (actually more like 29kHz), 9 channel PCM, stereo */
        case 7:
            chip->sample_rate = chip->clock / 4. / 208.;
            chip->stereo = 1;
            chip->voices = 9;
            chip->adpcm = 0;
            chip->mode = 7;
            break;
    }
    if (chip->SmpRateFunc != NULL)
        chip->SmpRateFunc(chip->SmpRateData, (UINT32)chip->sample_rate);
}
/* ==== Register/Command Interface ==== */

static void bsmt2000_w(void *info, UINT8 offset, UINT8 data)
{
    bsmt2000_state *chip = (bsmt2000_state *)info;
    switch (offset)
    {
    case 0:
        chip->latch = (chip->latch & 0x00ff) | (data << 8);
        break;
    case 1:
        chip->latch = (chip->latch & 0xff00) | data;
        break;
    case 2:
        bsmt2000_write_data(chip, data, chip->latch);
        break;
    case 0x10:
        chip->last_register = data;
        device_reset_bsmt2000(info);
        break;
    default:
        emu_logf(&chip->logger, DEVLOG_DEBUG, "unexpected bsmt2000 write to offset %d == %02X\n", offset, data);
        break;
    }
}

static UINT8 bsmt2000_r(void *info, UINT8 offset)
{
    /* Always ready (bit 7 = 1) */
    return 0x80;
}

static void bsmt2000_write_data(void *info, UINT8 address, UINT16 data) {
    bsmt2000_state *chip = (bsmt2000_state *)info;
    bsmt2000_voice *voice;

    chip->last_register = address;

    if (address == 0x77) // modulation depth (used in modes 0 and 1, see bsmt2000_update)
    {
        chip->mod_depth = data;
        return;
    }

    // Standard voices (interleaved register layout)
    if (address < 0x6d) {
        int voice_index;
        int regindex = BSMT2000_REG_TOTAL - 1;
        while (address < regmap[chip->mode][regindex])
            --regindex;

        voice_index = address - regmap[chip->mode][regindex];
        if (voice_index >= chip->voices)
            return;

        voice = &chip->voice[voice_index];
        voice->reg[regindex] = data;

        switch (regindex) {
            case BSMT2000_REG_CURRPOS: // REG_CURRPOS
                // writing the position keeps the fraction as-is, same as on the real chip
                voice->position &= 0xffff;
                voice->position |= data << 16;
                break;
            case BSMT2000_REG_RATE: // REG_RATE
                voice->adjusted_rate = data << 5;
                break;
            case BSMT2000_REG_LOOPSTART: // REG_LOOPSTART
                voice->loop_start_position = data << 16;
                break;
            case BSMT2000_REG_LOOPEND: // REG_LOOPEND
                voice->loop_stop_position = data << 16;
                break;
            case BSMT2000_REG_RIGHTVOL:
                chip->right_volume_set = 1;
                break;
        }
    }
    // Compressed/ADPCM channel (11-voice model only)
    else if (chip->adpcm != 0 && address >= 0x6d) {
        voice = &chip->voice[BSMT2000_ADPCM_INDEX];
        switch (address) {
        case 0x6d:
            voice->reg[BSMT2000_REG_LOOPEND] = data; // REG_LOOPEND
            voice->loop_stop_position = data << 16;
            break;
        case 0x6f:
            voice->reg[BSMT2000_REG_BANK] = data; // REG_BANK
            break;
        case 0x73: //rate - used as a control to tell chip ready to output compressed data (value = 1 for valid data)
            voice->reg[BSMT2000_REG_RATE] = data;
            if(voice->reg[BSMT2000_REG_RATE] != 0)
            {
                /* reset adpcm values also */
                chip->adpcm_current = 0;
                chip->adpcm_delta_n = chip->adpcm_delta_init;
            }
            break;
        case 0x76: //initial scale factor, copied on start (10 after reset)
            chip->adpcm_delta_init = data;
            break;
        case 0x6e: // main right channel volume control, used when ADPCM is already playing
        case 0x74: // right channel volume control, copied on start to 0x6e
            voice->reg[BSMT2000_REG_RIGHTVOL] = data; // REG_RIGHTVOL
            chip->right_volume_set = 1;
            break;
        case 0x75: // sample start position
            voice->reg[BSMT2000_REG_CURRPOS] = data; // REG_CURRPOS
            voice->position = data << 16;
            chip->adpcm_current = 0;
            chip->adpcm_delta_n = 10;
            break;
        case 0x70: // main left channel volume control, used when ADPCM is alreay playing
        case 0x78: // left channel volume control, copied on start to 0x70
            voice->reg[BSMT2000_REG_LEFTVOL] = data; // REG_LEFTVOL
            break;
        }
    }
}

/* ==== Sample ROM Handling ==== */
static void bsmt2000_alloc_rom(void* info, UINT32 memsize) {
    bsmt2000_state* chip = (bsmt2000_state *)info;
    memsize = (memsize + BSMT2000_ROM_BANKSIZE - 1) & ~(BSMT2000_ROM_BANKSIZE - 1);
    chip->total_banks = memsize / BSMT2000_ROM_BANKSIZE;
    if (chip->sample_rom_length == memsize)
        return;
    chip->sample_rom = (UINT8*)realloc(chip->sample_rom, memsize);
    chip->sample_rom_length = memsize;
    chip->sample_rom_mask = pow2_mask(memsize);
    chip->total_banks = memsize / BSMT2000_ROM_BANKSIZE;
    memset(chip->sample_rom, 0, memsize);
}

static void bsmt2000_write_rom(void *info, UINT32 offset, UINT32 length, const UINT8* data) {
    bsmt2000_state* chip = (bsmt2000_state *)info;
    if (offset > chip->sample_rom_length) return;
    if (offset + length > chip->sample_rom_length)
        length = chip->sample_rom_length - offset;
    memcpy(&chip->sample_rom[offset], data, length);
}

/* ==== Mute Mask ==== */
static void bsmt2000_set_mute_mask(void *info, UINT32 MuteMask)
{
    bsmt2000_state* chip = (bsmt2000_state *)info;
    UINT8 CurChn;
    for (CurChn = 0; CurChn < BSMT2000_MAX_VOICES; CurChn++)
        chip->Muted[CurChn] = (MuteMask >> CurChn) & 0x01;
}

static UINT32 bsmt2000_get_mute_mask(void *info)
{
    bsmt2000_state* chip = (bsmt2000_state *)info;
    UINT32 muteMask = 0;
    UINT8 CurChn;
    for (CurChn = 0; CurChn < BSMT2000_MAX_VOICES; CurChn++)
        muteMask |= (chip->Muted[CurChn] << CurChn);
    return muteMask;
}

/* ==== Logging ==== */
static void bsmt2000_set_log_cb(void* info, DEVCB_LOG func, void* param)
{
    bsmt2000_state* chip = (bsmt2000_state *)info;
    dev_logger_set(&chip->logger, chip, func, param);
}

/* ==== Interpolation Macro ==== */
#define INTERPOLATE(s1,s2,frac)  (((s1) * (INT32)(0x10000 - ((frac)&0xffff)) + (s2) * (INT32)((frac)&0xffff)) >> 16)

/* ==== Sound Update ==== */
static void bsmt2000_update(void *param, UINT32 samples, DEV_SMPL **outputs)
{
    bsmt2000_state *chip = (bsmt2000_state *)param;
    INT32 left[BSMT2000_SAMPLE_CHUNK];
    INT32 right[BSMT2000_SAMPLE_CHUNK];
    INT32 modbuf[BSMT2000_SAMPLE_CHUNK]; /* voice 1's samples, the modulator of voice 0 (register 0x77) */
    bsmt2000_voice *voice;
    int samp, v, modulate;
    int length = MIN(samples, BSMT2000_SAMPLE_CHUNK);	// TODO: allow updating in larger chunks

    if (!chip->sample_rom || !chip->sample_rom_length)
    {
        memset(outputs[0], 0, samples * sizeof(*outputs[0]));
        memset(outputs[1], 0, samples * sizeof(*outputs[1]));
        return;
    }
    memset(left, 0, length * sizeof(left[0]));
    memset(right, 0, length * sizeof(right[0]));

    /* Register 0x77: in modes 0/1 the chip's program multiplies it with voice 1's current sample and adds that to voice 0's
       step (mode 1: lt 59; mpy 77; ... add, around word 0x3d0 of bsmt2000.bin; mode 0: lt 55; modes 5-7 don't have it).
       So voice 1, usually at volume 0, bends voice 0's pitch: the BSMT logo jingle's chirp (0x77 ramped 0x0200..0x7e00,
       voice 1 looping the same sample at twice the rate). Voice 1 is computed first then, to have its samples at hand */
    modulate = chip->mod_depth != 0 && chip->mode <= 1 && chip->voices > 1;
    if (modulate)
        memset(modbuf, 0, length * sizeof(modbuf[0]));

    // PCM voices
    for (v = 0; v < chip->voices; v++)
    {
        int vn = (modulate && v < 2) ? (1 - v) : v; // 1, 0, 2, 3, ... when modulating
        UINT8 *base;
        UINT32 rate, pos;
        INT32 lvol, rvol;

        if (chip->Muted[vn])
            continue;
        voice = &chip->voice[vn];
        if (voice->reg[BSMT2000_REG_BANK] >= chip->total_banks) // REG_BANK
            continue;
        base = chip->sample_rom + voice->reg[BSMT2000_REG_BANK] * BSMT2000_ROM_BANKSIZE;
        rate = voice->adjusted_rate;
        pos = voice->position;
        rvol = voice->reg[BSMT2000_REG_RIGHTVOL]; // REG_RIGHTVOL
        lvol = chip->stereo ? voice->reg[BSMT2000_REG_LEFTVOL] : rvol; // REG_LEFTVOL
        if (chip->stereo && !chip->right_volume_set)
            rvol = lvol;

        for (samp = 0; samp < length; samp ++) {
            INT32 idx = pos >> 16;
            INT32 sample;
            if (1)
            {
                // no interpolation (original chip behaviour)
                sample = (INT8)base[idx] << 8;
            }
            else
            {
                // linear interpolation (softer samples, breaks SFX in "Tales from the Crypt")
                INT32 s1 = (INT8)base[idx] << 8;
                INT32 s2 = (INT8)base[idx+1] << 8;
                sample = INTERPOLATE(s1, s2, pos);
            }
            /* apply volumes and add */
            left[samp]  += (sample * lvol) >> 16;
            right[samp] += (sample * rvol) >> 16;

            /* check for loop end, as the chip: it advances, reads the sample, then wraps (so the sample past the loop end
               is played once), comparing the 16.16 positions as 16bit difference, and keeps the fraction */
            {
                const INT32 d = pos - voice->loop_stop_position;
                if (d > 0 || (d == 0 && (pos & 0xffff) != 0))
                    pos = pos + voice->loop_start_position - voice->loop_stop_position;
            }

            /* update position */
            if (modulate && vn == 1)
                modbuf[samp] = base[idx] << 8; // the chip reads the raw sample
            if (modulate && vn == 0)
            {
                // as the chip: ((sample * 0x77) >> 16) * rate >> 16, added to the 16.16 position << 10 (<< 5 here);
                // it can exceed the rate, so the position runs backwards then (16bit, wrapping)
                const INT32 bend = (((modbuf[samp] * (INT32)(INT16)chip->mod_depth) >> 16) * (INT32)(INT16)rate) >> 16;
                const INT32 f = (INT32)(pos & 0xffff) + (INT32)rate + bend;
                pos += f;
            }
            else
            {
                pos += rate;
            }
        }
        voice->position = pos;
    }

    // ADPCM/compressed voice (11-voice model only)
    if (chip->adpcm != 0 && !chip->Muted[BSMT2000_ADPCM_INDEX])
    {
        voice = &chip->voice[BSMT2000_ADPCM_INDEX];
        if (voice->reg[BSMT2000_REG_BANK] < chip->total_banks)
        {
            UINT8 *base = chip->sample_rom + voice->reg[BSMT2000_REG_BANK] * BSMT2000_ROM_BANKSIZE;
            UINT32 rate = 0x02aa << 4;
            UINT32 pos = voice->position;
            INT32 rvol = voice->reg[BSMT2000_REG_RIGHTVOL];
            INT32 lvol = chip->stereo ? voice->reg[BSMT2000_REG_LEFTVOL] : rvol;
            if (chip->stereo && !chip->right_volume_set)
                rvol = lvol;

            for (samp = 0; samp < length && pos < voice->loop_stop_position; samp ++)
            {
                UINT32 oldpos = pos;
                left[samp]  += (chip->adpcm_current * (lvol * 2)) >> 16;
                right[samp] += (chip->adpcm_current * (rvol * 2)) >> 16;

                pos += rate;
                if ((oldpos ^ pos) & 0x8000)
                {
                    static const UINT8 delta_tab[16] = { 154, 154, 128, 102, 77, 58, 58, 58, 58, 58, 58, 58, 77, 102, 128, 154 }; // as the chip's table (at word 0x064 of bsmt2000.bin)
                    // extract corresponding nibble and convert to full integer
                    int nibble = base[oldpos >> 16] >> ((~oldpos >> 13) & 4);
                    INT32 value = (nibble & 0xF) | ((nibble & 0x8) ? ~0xF : 0);
                    int temp;

                    /* compute the delta for this sample: the chip adds +-delta_n/2 in 16.16 and keeps the upper word, so the half step is rounded down on both sides (also for 0) */
                    temp = chip->adpcm_delta_n * value;
                    if (value > 0)
                        temp += chip->adpcm_delta_n >> 1;
                    else
                        temp -= (chip->adpcm_delta_n + 1) >> 1;

                    /* add and clamp against the sample (the chip saturates) */
                    chip->adpcm_current += temp;
                    if (chip->adpcm_current > 32767)
                        chip->adpcm_current = 32767;
                    else if (chip->adpcm_current < -32768)
                        chip->adpcm_current = -32768;

                    /* adjust the delta multiplier, clamped to 1..2000 as the chip does */
                    chip->adpcm_delta_n = (chip->adpcm_delta_n * delta_tab[value + 8]) >> 6;
                    if (chip->adpcm_delta_n > 2000)
                        chip->adpcm_delta_n = 2000;
                    else if (chip->adpcm_delta_n < 1)
                        chip->adpcm_delta_n = 1;
                }
            }
            voice->position = pos;
        }
    }

    // Output clamp and write
    for (samp = 0; samp < length; samp++) {
        INT32 l = left[samp];
        INT32 r = right[samp];
        l = (l > 32767) ? 32767 : (l < -32768) ? -32768 : l;
        r = (r > 32767) ? 32767 : (r < -32768) ? -32768 : r;
        outputs[0][samp] = (DEV_SMPL)l;
        outputs[1][samp] = (DEV_SMPL)r;
    }
}

static void bsmt2000_set_srchg_cb(void *info, DEVCB_SRATE_CHG CallbackFunc, void* DataPtr)
{
    bsmt2000_state *chip = (bsmt2000_state *)info;
	
	// set Sample Rate Change Callback routine
	chip->SmpRateFunc = CallbackFunc;
	chip->SmpRateData = DataPtr;
	
	return;
}
