#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../../stdtype.h"
#include "../EmuStructs.h"
#include "../EmuCores.h"
#include "../snddef.h"
#include "../EmuHelper.h"
#include "../logging.h"
#include "bsmt2000.h"


#define MAX_VOICES 12
#define ADPCM_VOICE 12
#define FRAC_BITS 14
#define FRAC_ONE (1 << FRAC_BITS)

typedef struct {
    UINT16 reg[8];
    UINT32 position;
    UINT32 loop_start;
    UINT32 loop_end;
    UINT32 rate;
    UINT8 muted;
} BSMT2000_VOICE;

typedef struct {
    DEV_DATA _devData;
    DEV_LOGGER logger;
    
    // Configuration
    UINT32 clock;
    UINT32 sample_rate;
    UINT8 stereo;
    UINT8 voice_count;
    
    // Internal state
    INT8* rom;
    UINT32 rom_size;
    UINT32 rom_mask;
    
    BSMT2000_VOICE voices[MAX_VOICES];
    BSMT2000_VOICE adpcm_voice;
    
    // ADPCM state
    INT32 adpcm_current;
    INT32 adpcm_delta;
    
    // Resampling
    UINT32 output_step;
    UINT32 output_pos;
    INT32 last_l, last_r;
    INT32 curr_l, curr_r;
} BSMT2000_STATE;

static void bsmt2000_update(void* param, UINT32 samples, DEV_SMPL** outputs);
static UINT8 device_start_bsmt2000(const DEV_GEN_CFG* cfg, DEV_INFO* retDevInf);
static void device_stop_bsmt2000(void* info);
static void device_reset_bsmt2000(void* info);
static void bsmt2000_write_data(void* info, UINT8 addr, UINT16 data);
static void bsmt2000_alloc_rom(void* info, UINT32 memsize);
static void bsmt2000_write_rom(void* info, UINT32 offset, UINT32 length, const UINT8* data);
static void bsmt2000_set_mute_mask(void* info, UINT32 MuteMask);
static UINT32 bsmt2000_get_mute_mask(void* info);
static void bsmt2000_set_log_cb(void* info, DEVCB_LOG func, void* param);

static DEVDEF_RWFUNC devFunc[] = {
    {RWF_REGISTER | RWF_WRITE, DEVRW_A16D16, 0, bsmt2000_write_data},
    {RWF_MEMORY | RWF_WRITE, DEVRW_BLOCK, 0, bsmt2000_write_rom},
    {RWF_MEMORY | RWF_WRITE, DEVRW_MEMSIZE, 0, bsmt2000_alloc_rom},
    {RWF_CHN_MUTE | RWF_WRITE, DEVRW_ALL, 0, bsmt2000_set_mute_mask},
    {RWF_CHN_MUTE | RWF_READ, DEVRW_ALL, 0, bsmt2000_get_mute_mask},
    {0x00, 0x00, 0, NULL}
};

static DEV_DEF devDef = {
    "BSMT2000", "MAME", FCC_MAME,
    device_start_bsmt2000,
    device_stop_bsmt2000,
    device_reset_bsmt2000,
    bsmt2000_update,
    NULL,
    bsmt2000_set_mute_mask,
    NULL,
    NULL,
    bsmt2000_set_log_cb,
    NULL,
    devFunc
};

const DEV_DEF* devDefList_BSMT2000[] = { &devDef, NULL };

static void generate_samples(BSMT2000_STATE* chip, INT32* left, INT32* right, UINT32 samples) {
    memset(left, 0, samples * sizeof(INT32));
    memset(right, 0, samples * sizeof(INT32));

    // Process PCM voices
    for (UINT8 v = 0; v < chip->voice_count; v++) {
        BSMT2000_VOICE* voice = &chip->voices[v];
        if (voice->reg[5] >= chip->rom_size/0x10000 || voice->muted) continue;
        
        INT8* base = chip->rom + (voice->reg[5] * 0x10000);
        UINT32 pos = voice->position;
        UINT32 rate = voice->rate;
        INT32 vol_l = voice->reg[7];
        INT32 vol_r = voice->reg[6];
        
        for (UINT32 s = 0; s < samples; s++) {
            if (pos >= voice->loop_end) 
                pos = voice->loop_start + (pos - voice->loop_end);
            
            INT32 sample = base[pos >> 16];
            left[s] += sample * vol_l;
            right[s] += sample * vol_r;
            pos += rate;
        }
        voice->position = pos;
    }

    // Process ADPCM voice
    BSMT2000_VOICE* voice = &chip->adpcm_voice;
    if (chip->voice_count == 11 && !voice->muted && voice->reg[5] < chip->rom_size/0x10000) {
        static const UINT8 delta_tab[] = {58,58,58,58,77,102,128,154};
        INT8* base = chip->rom + (voice->reg[5] * 0x10000);
        UINT32 pos = voice->position;
        INT32 vol_l = voice->reg[7];
        INT32 vol_r = voice->reg[6];
        
        for (UINT32 s = 0; s < samples && pos < voice->loop_end; s++) {
            if ((pos & 0x8000) && !((pos + 1) & 0x8000)) {
                UINT8 nibble = base[pos >> 16] >> ((~pos >> 13) & 4);
                INT32 delta = (INT32)(nibble << 28) >> 28;
                
                chip->adpcm_current += (chip->adpcm_delta * delta) / 4;
                chip->adpcm_current = (chip->adpcm_current, -32768, 32767);
                
                chip->adpcm_delta = (chip->adpcm_delta * delta_tab[abs(delta)]) >> 6;
                chip->adpcm_delta = (chip->adpcm_delta, 1, 2000);
            }
            
            left[s] += (chip->adpcm_current * vol_l) >> 8;
            right[s] += (chip->adpcm_current * vol_r) >> 8;
            pos += voice->rate;
        }
        voice->position = pos;
    }
}

static void bsmt2000_update(void* param, UINT32 samples, DEV_SMPL** outputs) {
    BSMT2000_STATE* chip = (BSMT2000_STATE*)param;
    INT32* mix_l = (INT32*)malloc(samples * sizeof(INT32));
    INT32* mix_r = (INT32*)malloc(samples * sizeof(INT32));
    
    generate_samples(chip, mix_l, mix_r, samples);
    
    for (UINT32 i = 0; i < samples; i++) {
        outputs[0][i] = (mix_l[i] >> 9, -32768, 32767);
        outputs[1][i] = (mix_r[i] >> 9, -32768, 32767);
    }
    
    free(mix_l);
    free(mix_r);
}

static UINT8 device_start_bsmt2000(const DEV_GEN_CFG* cfg, DEV_INFO* retDevInf) {
    BSMT2000_STATE* chip = calloc(1, sizeof(BSMT2000_STATE));
    chip->clock = cfg->clock;
    chip->sample_rate = cfg->clock / 1000;
    chip->voice_count = 12;
    chip->stereo = 1;
    
    // Initialize voices
    for (UINT8 i = 0; i < MAX_VOICES; i++) {
        chip->voices[i].reg[7] = 0x7FFF; // Left vol
        chip->voices[i].reg[6] = 0x7FFF; // Right vol
    }
    
    INIT_DEVINF(retDevInf, &chip->_devData, chip->sample_rate, &devDef);
    return 0;
}

static void device_stop_bsmt2000(void* info) {
    BSMT2000_STATE* chip = (BSMT2000_STATE*)info;
    free(chip->rom);
    free(chip);
}

static void device_reset_bsmt2000(void* info) {
    BSMT2000_STATE* chip = (BSMT2000_STATE*)info;
    memset(chip->voices, 0, sizeof(chip->voices));
    chip->adpcm_current = 0;
    chip->adpcm_delta = 10;
}

static void bsmt2000_write_data(void* info, UINT8 addr, UINT16 data) {
    BSMT2000_STATE* chip = (BSMT2000_STATE*)info;
    UINT8 voice_idx = addr % chip->voice_count;
    UINT8 reg = addr / chip->voice_count;
    
    if (voice_idx >= MAX_VOICES) return;
    
    BSMT2000_VOICE* voice = &chip->voices[voice_idx];
    if (reg < 8) voice->reg[reg] = data;
    
    switch(reg) {
        case 0: voice->position = data << 16; break;
        case 2: voice->rate = data << 5; break;
        case 3: voice->loop_end = data << 16; break;
        case 4: voice->loop_start = data << 16; break;
    }
}

static void bsmt2000_alloc_rom(void* info, UINT32 memsize) {
    BSMT2000_STATE* chip = (BSMT2000_STATE*)info;
    chip->rom = realloc(chip->rom, memsize);
    chip->rom_size = memsize;
    chip->rom_mask = memsize - 1;
}

static void bsmt2000_write_rom(void* info, UINT32 offset, UINT32 length, const UINT8* data) {
    BSMT2000_STATE* chip = (BSMT2000_STATE*)info;
    memcpy(chip->rom + offset, data, (length, chip->rom_size - offset));
}

static void bsmt2000_set_mute_mask(void* info, UINT32 MuteMask) {
    BSMT2000_STATE* chip = (BSMT2000_STATE*)info;
    for (UINT8 i = 0; i < MAX_VOICES; i++)
        chip->voices[i].muted = (MuteMask >> i) & 1;
    chip->adpcm_voice.muted = (MuteMask >> ADPCM_VOICE) & 1;
}

static UINT32 bsmt2000_get_mute_mask(void* info) {
    BSMT2000_STATE* chip = (BSMT2000_STATE*)info;
    UINT32 mask = 0;
    for (UINT8 i = 0; i < MAX_VOICES; i++)
        mask |= (chip->voices[i].muted << i);
    mask |= (chip->adpcm_voice.muted << ADPCM_VOICE);
    return mask;
}

static void bsmt2000_set_log_cb(void* info, DEVCB_LOG func, void* param) {
    BSMT2000_STATE* chip = (BSMT2000_STATE*)info;
    dev_logger_set(&chip->logger, chip, func, param);
}