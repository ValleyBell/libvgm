// k007232.c - Adapted to libvgm

#include <stdlib.h>
#include <string.h>

#include "../../stdtype.h"
#include "../EmuStructs.h"
#include "../EmuCores.h"
#include "../snddef.h"
#include "../EmuHelper.h"
#include "../logging.h"
#include "../RatioCntr.h"
#include "k007232.h"

#define KDAC_A_PCM_MAX 2
#define K007232_CLOCKDIV 128

typedef struct _k007232_state k007232_state;

typedef struct
{
    UINT8 vol[2];
    UINT32 addr;
    INT32 counter;
    UINT32 start;
    UINT16 step;
    UINT32 bank;
    UINT8 play;
    UINT8 mute;
} KDAC_Voice;

struct _k007232_state
{
    DEV_DATA _devData;
    DEV_LOGGER logger;
    RATIO_CNTR rateCntr;
    KDAC_Voice voice[KDAC_A_PCM_MAX];

    UINT8 wreg[0x10];
    UINT8 *rom;
    UINT32 rom_size;
    UINT32 rom_mask;
    UINT8 loop_en;
};


static UINT8 device_start_k007232(const DEV_GEN_CFG* cfg, DEV_INFO* retDevInf);
static void device_stop_k007232(void* chip);
static void device_reset_k007232(void* chip);
static void k007232_update(void* chip, UINT32 samples, DEV_SMPL **outputs);

static void k007232_write(void* chip, UINT8 offset, UINT8 data);
static UINT8 k007232_read(void* chip, UINT8 offset);
static void k007232_write_rom(void* chip, UINT32 offset, UINT32 length, const UINT8* data);
static void k007232_alloc_rom(void* chip, UINT32 memsize);
static void k007232_set_mute_mask(void* chip, UINT32 MuteMask);
static void k007232_set_log_cb(void* chip, DEVCB_LOG func, void* param);


static DEVDEF_RWFUNC devFunc[] = {
    {RWF_REGISTER | RWF_WRITE, DEVRW_A8D8, 0, k007232_write},
    {RWF_REGISTER | RWF_READ, DEVRW_A8D8, 0, k007232_read},
    {RWF_MEMORY | RWF_WRITE, DEVRW_BLOCK, 0, k007232_write_rom},
    {RWF_MEMORY | RWF_WRITE, DEVRW_MEMSIZE, 0, k007232_alloc_rom},
    {RWF_CHN_MUTE | RWF_WRITE, DEVRW_ALL, 0, k007232_set_mute_mask},
    {0, 0, 0, NULL}
};
static DEV_DEF devDef = {
    "K007232", "mao7", FCC_RN22,
    device_start_k007232, device_stop_k007232, device_reset_k007232,
    k007232_update, NULL, k007232_set_mute_mask, NULL, NULL, k007232_set_log_cb, NULL,
    devFunc
};
const DEV_DEF* devDefList_K007232[] = { &devDef, NULL };

// --- Device Functions ---
static UINT8 device_start_k007232(const DEV_GEN_CFG* cfg, DEV_INFO* retDevInf)
{
    k007232_state* chip = (k007232_state*)calloc(1, sizeof(k007232_state));
    UINT32 rate = cfg->clock / K007232_CLOCKDIV;

    RC_SET_RATIO(&chip->rateCntr, cfg->clock, rate);
    chip->rom = NULL;
    chip->rom_size = 0;
    chip->rom_mask = 0;

    for (int ch = 0; ch < KDAC_A_PCM_MAX; ch++)
    {
        chip->voice[ch].vol[0] = 128; // Left volume
        chip->voice[ch].vol[1] = 128; // Right volume
    }

    INIT_DEVINF(retDevInf, &chip->_devData, rate, &devDef);
    chip->_devData.chipInf = chip;

    return 0x00;
}

static void device_stop_k007232(void* chip)
{
    free(((k007232_state*)chip)->rom);
    free(chip);
}

static void device_reset_k007232(void* chip)
{
    k007232_state* info = (k007232_state*)chip;
    memset(info->wreg, 0, sizeof(info->wreg));
    info->loop_en = 0;
    for (int i = 0; i < KDAC_A_PCM_MAX; ++i)
    {
        info->voice[i].play = 0;
        info->voice[i].counter = 0x1000;
    }
    RC_RESET(&info->rateCntr);
}

static void k007232_update(void* chip, UINT32 samples, DEV_SMPL **outputs)
{
    k007232_state* info = (k007232_state*)chip;
    for (UINT32 smpl = 0; smpl < samples; smpl++)
    {
        INT32 lsum = 0, rsum = 0;
        for (UINT8 ch = 0; ch < KDAC_A_PCM_MAX; ch++)
        {
            KDAC_Voice* v = &info->voice[ch];
            if (!v->play)
                continue;

            UINT32 addr = v->addr;
            while (v->counter <= v->step)
            {
                if (addr >= info->rom_size)
                    break;

                UINT8 sample = info->rom[(v->bank + addr) & info->rom_mask];
                if (sample & 0x80)
                {
                    if (info->loop_en & (1 << ch))
                        addr = v->start;
                    else
                    {
                        v->play = 0;
                        break;
                    }
                }
                else
                {
                    addr++;
                }
                v->counter += (0x1000 - v->step);
            }
            v->addr = addr;

            if (!v->play)
                continue;

            if (addr < info->rom_size)
            {
                if (!v->mute)
                {
                    UINT8 sample = info->rom[(v->bank + addr) & info->rom_mask];
                    INT16 out = ((sample & 0x7F) << 9) - 0x8000; // Scale to -32768 to +32512
                    lsum += (out * v->vol[0]) >> 8; // Apply volume as 8-bit multiplier
                    rsum += (out * v->vol[1]) >> 8;
                }
            }

            v->counter -= 32;
            if (v->counter < 0)
                v->counter = 0;
        }
        outputs[0][smpl] = lsum;
        outputs[1][smpl] = rsum;
    }
}

static void k007232_write(void* chip, UINT8 offset, UINT8 data)
{
    k007232_state* info = (k007232_state*)chip;
    info->wreg[offset & 0x0F] = data;

    switch (offset)
    {
        case 0x0C:  // stereo volume control in most games like chequered flag.
            // Channel 0: Left = data, Right = 255 - data
            info->voice[0].vol[0] = data;
            info->voice[0].vol[1] = 255 - data;
            // Channel 1: Same as channel 0 (mirrored)
            info->voice[1].vol[0] = data;
            info->voice[1].vol[1] = 255 - data;
            return;
            
        case 0x0D:
            info->loop_en = data;
            return;
            
        case 0x0E: case 0x0F: // should be bankswitch as cam900's suggested.
            info->voice[(offset & 1)].bank = data << 17;
            return;
            
        case 0x10: case 0x11: case 0x12: case 0x13:
            info->voice[((offset >> 1) & 1)].vol[(offset & 1)] = data;
            return;
    }

    KDAC_Voice* v = &info->voice[(offset >= 6) ? 1 : 0];
    UINT8 base = (offset >= 6) ? 6 : 0;
    switch (offset - base)
    {
        case 0: case 1:
        {
            // Supposed to be frequency bits?
            v->step = ((info->wreg[base + 1] & 0x0F) << 8) | info->wreg[base];
            
            UINT8 mode = (info->wreg[base + 1] >> 4) & 3;
            if(mode) { // Only modify if mode bits are set
                if(mode == 1) // 8-bit mode
                    v->step = (256 - info->wreg[base]) << 4;
                else if(mode == 2) // 4-bit mode
                    v->step = (16 - (info->wreg[base + 1] & 0x0F)) << 8;
            }
            break;
        }
        case 2: case 3: case 4:
            v->start = ((info->wreg[base + 4] & 0x01) << 16) |
                       (info->wreg[base + 3] << 8) | info->wreg[base + 2];
            break;
        case 5:
            if (v->start < info->rom_size)
            {
                //Plays the samples.
                v->addr = v->bank + v->start;
                v->counter = 0x1000;
                v->play = 1;
            }
            break;
    }
}

static UINT8 k007232_read(void* chip, UINT8 offset)
{
    k007232_state* info = (k007232_state*)chip;
    if(offset == 5 || offset == 11) {
        KDAC_Voice* v = &info->voice[(offset == 11) ? 1 : 0];
        return v->play ? 0xFF : 0x00;
    }
    return 0;
}

static void k007232_alloc_rom(void* chip, UINT32 memsize)
{
    k007232_state* info = (k007232_state*)chip;
    if (info->rom_size != memsize)
    {
        info->rom = (UINT8*)realloc(info->rom, memsize);
        info->rom_size = memsize;
        info->rom_mask = (1 << (32 - __builtin_clz(memsize - 1))) - 1;
        memset(info->rom, 0xFF, memsize);
    }
}

static void k007232_write_rom(void* chip, UINT32 offset, UINT32 length, const UINT8* data)
{
    k007232_state* info = (k007232_state*)chip;
    if (offset >= info->rom_size) return;
    if (offset + length > info->rom_size) length = info->rom_size - offset;
    memcpy(&info->rom[offset], data, length);
}

static void k007232_set_mute_mask(void* chip, UINT32 MuteMask)
{
    k007232_state* info = (k007232_state*)chip;
    for(UINT8 i = 0; i < KDAC_A_PCM_MAX; i++)
        info->voice[i].mute = (MuteMask & (1 << i)) ? 1 : 0;
}

static void k007232_set_log_cb(void* chip, DEVCB_LOG func, void* param)
{
    k007232_state* info = (k007232_state*)chip;
    dev_logger_set(&info->logger, info, func, param);
}