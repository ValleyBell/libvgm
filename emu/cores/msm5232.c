// license:GPL-2.0+
// copyright-holders:Jarek Burczynski, Hiromitsu Shioya
/*
    OKI MSM5232RS
    8 channel tone generator

	Modified for libvgm by Mao(RN22), cam900(MATRIX)
	It's basically MAME code with improvements.
*/

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "../../stdtype.h"
#include "../snddef.h"
#include "../EmuHelper.h"
#include "../EmuCores.h"
#include "../logging.h"
#include "../SoundDevs.h"
#include "../EmuHelper.h"
#include "../RatioCntr.h"
#include "msm5232.h"

#define MSM5232_NUM_CHANNELS 8
#define CLOCK_RATE_DIVIDER 16
#define STEP_SH 16
#define VMIN 0
#define VMAX 32768

// MAME's ROM table, 88 entries (12 bits: 9 for counter, 3 for divider)
#define ROM(counter, bindiv) ((counter) | ((bindiv) << 9))
static const uint16_t MSM5232_ROM[88] = {
	ROM(506, 7),	ROM(478, 7),	ROM(451, 7),	ROM(426, 7),	ROM(402, 7),	ROM(379, 7),	ROM(358, 7),	ROM(338, 7),
	ROM(319, 7),	ROM(301, 7),	ROM(284, 7),	ROM(268, 7),	ROM(253, 7),	ROM(478, 6),	ROM(451, 6),	ROM(426, 6),
	ROM(402, 6),	ROM(379, 6),	ROM(358, 6),	ROM(338, 6),	ROM(319, 6),	ROM(301, 6),	ROM(284, 6),	ROM(268, 6),
	ROM(253, 6),	ROM(478, 5),	ROM(451, 5),	ROM(426, 5),	ROM(402, 5),	ROM(379, 5),	ROM(358, 5),	ROM(338, 5),
	ROM(319, 5),	ROM(301, 5),	ROM(284, 5),	ROM(268, 5),	ROM(253, 5),	ROM(478, 4),	ROM(451, 4),	ROM(426, 4),
	ROM(402, 4),	ROM(379, 4),	ROM(358, 4),	ROM(338, 4),	ROM(319, 4),	ROM(301, 4),	ROM(284, 4),	ROM(268, 4),
	ROM(253, 4),	ROM(478, 3),	ROM(451, 3),	ROM(426, 3),	ROM(402, 3),	ROM(379, 3),	ROM(358, 3),	ROM(338, 3),
	ROM(319, 3),	ROM(301, 3),	ROM(284, 3),	ROM(268, 3),	ROM(253, 3),	ROM(478, 2),	ROM(451, 2),	ROM(426, 2),
	ROM(402, 2),	ROM(379, 2),	ROM(358, 2),	ROM(338, 2),	ROM(319, 2),	ROM(301, 2),	ROM(284, 2),	ROM(268, 2),
	ROM(253, 2),	ROM(478, 1),	ROM(451, 1),	ROM(426, 1),	ROM(402, 1),	ROM(379, 1),	ROM(358, 1),	ROM(338, 1),
	ROM(319, 1),	ROM(301, 1),	ROM(284, 1),	ROM(268, 1),	ROM(253, 1),	ROM(253, 1),	ROM(253, 1),	ROM(13, 7)
};
#undef ROM

typedef struct {
	uint8_t mode; // 0=tone, 1=noise
	int TG_count_period;
	int TG_count;
	uint8_t TG_cnt;
	uint8_t TG_out16, TG_out8, TG_out4, TG_out2;
	int egvol;
	int eg_sect; // 0=attack, 1=decay, 2=release, -1=off
	int counter;
	int eg;
	uint8_t eg_arm;
	double ar_rate, dr_rate, rr_rate;
	int pitch;
	int GF;
	uint8_t mute;
} MSM5232_VOICE;

typedef struct {
	DEV_DATA _devData;

	MSM5232_VOICE voi[MSM5232_NUM_CHANNELS];
	uint32_t noise_rng;
	int noise_step;
	int noise_cnt;
	int noise_clocks;

	uint8_t control1, control2;
	uint32_t EN_out16[2], EN_out8[2], EN_out4[2], EN_out2[2];

	uint32_t clock, sample_rate;
	unsigned int UpdateStep;
	RATIO_CNTR cycle_cntr;

	double capacitors[8];
	double ar_tbl[8], dr_tbl[16];
} MSM5232_STATE;

// Forward declarations
static UINT8 device_start_msm5232(const MSM5232_CFG* cfg, DEV_INFO* retDevInf);
static void device_stop_msm5232(void* info);
static void device_reset_msm5232(void* info);
static void device_update_msm5232(void* info, UINT32 samples, DEV_SMPL** outputs);
static void msm5232_write(void* info, UINT8 reg, UINT8 value);
static void msm5232_set_mute_mask(void* info, UINT32 muteMask);

static DEVDEF_RWFUNC devFunc[] = {
    {RWF_REGISTER | RWF_WRITE, DEVRW_A8D8, 0, msm5232_write},
    {RWF_CHN_MUTE | RWF_WRITE, DEVRW_ALL, 0, msm5232_set_mute_mask},
    {0x00, 0x00, 0, NULL}
};

static DEV_DEF devDef = {
    "MSM5232", "MAME", FCC_MAME,
    (DEVFUNC_START)device_start_msm5232,
	device_stop_msm5232,
	device_reset_msm5232,
	device_update_msm5232,
    NULL, msm5232_set_mute_mask, NULL, NULL, NULL, NULL, devFunc
};
static const char* DeviceName(const DEV_GEN_CFG* devCfg)
{
	return "MSM5232";
}
static UINT16 DeviceChannels(const DEV_GEN_CFG* devCfg) { return MSM5232_NUM_CHANNELS; }
static const char** DeviceChannelNames(const DEV_GEN_CFG* devCfg) { return NULL; }

const DEV_DECL sndDev_MSM5232 =
{
	DEVID_MSM5232,
	DeviceName,
	DeviceChannels,
	DeviceChannelNames,
	{	// cores
		&devDef,
		NULL
	}
};

// --- Helper Functions ---

static void init_tables(MSM5232_STATE* chip)
{
	const double R51 = 870.0, R52 = 17400.0, R53 = 101000.0;
	chip->UpdateStep = (unsigned int)((1 << STEP_SH) * (double)chip->sample_rate / (double)chip->clock);

	for (int i = 0; i < 8; i++) {
		double clockscale = (double)chip->clock / 2119040.0;
		int rcp_duty_cycle = 1 << ((i & 4) ? (i & ~2) : i);
		chip->ar_tbl[i] = (rcp_duty_cycle / clockscale) * R51;
	}
	for (int i = 0; i < 16; i++) {
		double clockscale = (double)chip->clock / 2119040.0;
		int rcp_duty_cycle = 1 << ((i & 4) ? (i & ~2) : i);
		chip->dr_tbl[i] = (i < 8) ? (rcp_duty_cycle / clockscale) * R52 : (rcp_duty_cycle / clockscale) * R53;
	}
	chip->noise_step = (int)((1 << STEP_SH) / 128.0 * ((double)chip->clock / chip->sample_rate));
}

static void init_voice(MSM5232_STATE* chip, int i)
{
	MSM5232_VOICE* v = &chip->voi[i];
	v->ar_rate = chip->ar_tbl[0] * chip->capacitors[i];
	v->dr_rate = chip->dr_tbl[0] * chip->capacitors[i];
	v->rr_rate = chip->dr_tbl[0] * chip->capacitors[i];
	v->eg_sect = -1;
	v->eg = 0;
	v->egvol = 0;
	v->counter = 0;
	v->mode = 0;
	v->pitch = -1;
	v->GF = 0;
	v->mute = 0;

	v->TG_count_period = 1;
	v->TG_count = 1;
	v->TG_cnt = 0;
	v->TG_out16 = v->TG_out8 = v->TG_out4 = v->TG_out2 = 0;
}

// --- Core Functions ---

static UINT8 device_start_msm5232(const MSM5232_CFG* cfg, DEV_INFO* retDevInf)
{
    MSM5232_STATE* chip;

    chip = (MSM5232_STATE*)calloc(1, sizeof(MSM5232_STATE));
    if (!chip) return 0xFF;

    chip->clock = cfg->_genCfg.clock;
	for (int i = 0; i < 8; i++)
		chip->capacitors[i] = cfg->capacitors[i];
    chip->sample_rate = chip->clock / CLOCK_RATE_DIVIDER;
    SRATE_CUSTOM_HIGHEST(cfg->_genCfg.srMode, chip->sample_rate, cfg->_genCfg.smplRate);

    init_tables(chip);
    for (int i = 0; i < MSM5232_NUM_CHANNELS; i++)
        init_voice(chip, i);

    chip->noise_rng = 1;
    chip->noise_cnt = 0;
    chip->noise_clocks = 0;
    chip->control1 = chip->control2 = 0;
    chip->EN_out16[0] = chip->EN_out8[0] = chip->EN_out4[0] = chip->EN_out2[0] = 0;
    chip->EN_out16[1] = chip->EN_out8[1] = chip->EN_out4[1] = chip->EN_out2[1] = 0;

    RC_SET_RATIO(&chip->cycle_cntr, chip->clock, chip->sample_rate);
    chip->_devData.chipInf = chip;
    INIT_DEVINF(retDevInf, &chip->_devData, chip->sample_rate, &devDef);
    return 0x00;
}

static void device_stop_msm5232(void* info) { free(info); }

static void device_reset_msm5232(void* info)
{
    MSM5232_STATE* chip = (MSM5232_STATE*)info;
    for (int i = 0; i < MSM5232_NUM_CHANNELS; i++) {
        init_voice(chip, i);
        msm5232_write(chip, i, 0x80);
        msm5232_write(chip, i, 0x00);
    }
    chip->noise_cnt = 0;
    chip->noise_rng = 1;
    chip->noise_clocks = 0;
    chip->control1 = chip->control2 = 0;
    chip->EN_out16[0] = chip->EN_out8[0] = chip->EN_out4[0] = chip->EN_out2[0] = 0;
    chip->EN_out16[1] = chip->EN_out8[1] = chip->EN_out4[1] = chip->EN_out2[1] = 0;
}

// Envelope handling, closely following MAME
static void EG_voices_advance(MSM5232_STATE* chip)
{
	for (int i = 0; i < MSM5232_NUM_CHANNELS; i++) {
		MSM5232_VOICE* v = &chip->voi[i];
		switch (v->eg_sect) {
		case 0: // attack
			if (v->eg < VMAX) {
				v->counter -= (int)((VMAX - v->eg) / v->ar_rate);
				if (v->counter <= 0) {
					int n = -v->counter / chip->sample_rate + 1;
					v->counter += n * chip->sample_rate;
					v->eg += n;
					if (v->eg > VMAX)
						v->eg = VMAX;
				}
			}
			if (!v->eg_arm) {
				if (v->eg >= VMAX * 80 / 100)
					v->eg_sect = 1;
			}
			// ARM=1: stay at max until key off
			v->egvol = v->eg / 16;
			break;

		case 1: // decay
			if (v->eg > VMIN) {
				v->counter -= (int)((v->eg - VMIN) / v->dr_rate);
				if (v->counter <= 0) {
					int n = -v->counter / chip->sample_rate + 1;
					v->counter += n * chip->sample_rate;
					v->eg -= n;
					if (v->eg < VMIN)
						v->eg = VMIN;
				}
			} else {
				v->eg_sect = -1;
			}
			v->egvol = v->eg / 16;
			break;

		case 2: // release
			if (v->eg > VMIN) {
				v->counter -= (int)((v->eg - VMIN) / v->rr_rate);
				if (v->counter <= 0) {
					int n = -v->counter / chip->sample_rate + 1;
					v->counter += n * chip->sample_rate;
					v->eg -= n;
					if (v->eg < VMIN)
						v->eg = VMIN;
				}
			} else {
				v->eg_sect = -1;
			}
			v->egvol = v->eg / 16;
			break;
		default:
			break;
		}
	}
}

// Tone generator, closely following MAME's TG_group_advance
typedef struct {
	int o2, o4, o8, o16;
} MSM5232_GROUP_OUT;

static void TG_group_advance(MSM5232_STATE* chip, int groupidx, MSM5232_GROUP_OUT* out)
{
	memset(out, 0, sizeof(*out));
	MSM5232_VOICE* v = &chip->voi[groupidx*4];
	for (int i = 0; i < 4; i++, v++) {
		int out2 = 0, out4 = 0, out8 = 0, out16 = 0;
		// GUARD: skip if TG_count_period is zero (not initialized yet)
		if (v->TG_count_period == 0)
			continue;
		if (v->mode == 0) { // tone
			int left = 1 << STEP_SH;
			do {
				int nextevent = left;
				if (v->TG_cnt & v->TG_out16) out16 += v->TG_count;
				if (v->TG_cnt & v->TG_out8)  out8  += v->TG_count;
				if (v->TG_cnt & v->TG_out4)  out4  += v->TG_count;
				if (v->TG_cnt & v->TG_out2)  out2  += v->TG_count;

				v->TG_count -= nextevent;
				while (v->TG_count <= 0) {
					v->TG_count += v->TG_count_period;
					v->TG_cnt++;
					if (v->TG_cnt & v->TG_out16) out16 += v->TG_count_period;
					if (v->TG_cnt & v->TG_out8)  out8  += v->TG_count_period;
					if (v->TG_cnt & v->TG_out4)  out4  += v->TG_count_period;
					if (v->TG_cnt & v->TG_out2)  out2  += v->TG_count_period;
					if (v->TG_count > 0) break;
					v->TG_count += v->TG_count_period;
					v->TG_cnt++;
					if (v->TG_cnt & v->TG_out16) out16 += v->TG_count_period;
					if (v->TG_cnt & v->TG_out8)  out8  += v->TG_count_period;
					if (v->TG_cnt & v->TG_out4)  out4  += v->TG_count_period;
					if (v->TG_cnt & v->TG_out2)  out2  += v->TG_count_period;
				}
				if (v->TG_cnt & v->TG_out16) out16 -= v->TG_count;
				if (v->TG_cnt & v->TG_out8)  out8  -= v->TG_count;
				if (v->TG_cnt & v->TG_out4)  out4  -= v->TG_count;
				if (v->TG_cnt & v->TG_out2)  out2  -= v->TG_count;
				left -= nextevent;
			} while (left > 0);
		} else { // noise
			if (chip->noise_clocks & 8) out16 += (1<<STEP_SH);
			if (chip->noise_clocks & 4) out8  += (1<<STEP_SH);
			if (chip->noise_clocks & 2) out4  += (1<<STEP_SH);
			if (chip->noise_clocks & 1) out2  += (1<<STEP_SH);
		}
		// Signed output
		out->o16 += ((out16 - (1 << (STEP_SH-1))) * v->egvol) >> STEP_SH;
		out->o8  += ((out8  - (1 << (STEP_SH-1))) * v->egvol) >> STEP_SH;
		out->o4  += ((out4  - (1 << (STEP_SH-1))) * v->egvol) >> STEP_SH;
		out->o2  += ((out2  - (1 << (STEP_SH-1))) * v->egvol) >> STEP_SH;
	}
	// Mask outputs
	out->o16 &= chip->EN_out16[groupidx];
	out->o8  &= chip->EN_out8 [groupidx];
	out->o4  &= chip->EN_out4 [groupidx];
	out->o2  &= chip->EN_out2 [groupidx];
}

// Noise generator logic from MAME
static void update_noise(MSM5232_STATE* chip)
{
	int cnt = (chip->noise_cnt += chip->noise_step) >> STEP_SH;
	chip->noise_cnt &= ((1<<STEP_SH)-1);
	while (cnt > 0) {
		int tmp = chip->noise_rng & (1<<16);
		if (chip->noise_rng&1)
			chip->noise_rng ^= 0x24000;
		chip->noise_rng >>= 1;
		if ((chip->noise_rng & (1<<16)) != tmp)
			chip->noise_clocks++;
		cnt--;
	}
}

// --- Main Update ---
static void device_update_msm5232(void* info, UINT32 samples, DEV_SMPL** outputs)
{
	MSM5232_STATE* chip = (MSM5232_STATE*)info;
	DEV_SMPL* outL = outputs[0];
	DEV_SMPL* outR = outputs[1];
	if (!outL || !outR) return;

	for (UINT32 i = 0; i < samples; i++) {
		RC_STEP(&chip->cycle_cntr);

		// 1. Advance envelopes
		EG_voices_advance(chip);

		// 2. Tone groups
		MSM5232_GROUP_OUT group1, group2;
		TG_group_advance(chip, 0, &group1);
		TG_group_advance(chip, 1, &group2);

		// 3. Mix output (mono: sum all feet, or split for stereo as desired)
		int32_t mix = group1.o2 + group1.o4 + group1.o8 + group1.o16 +
		              group2.o2 + group2.o4 + group2.o8 + group2.o16;
		// Optionally, use group1/group2 for L/R channels
		outL[i] = (DEV_SMPL)(mix / 8); // normalize to 8 channels
		outR[i] = (DEV_SMPL)(mix / 8);

		// 4. Noise update (advance RNG)
		update_noise(chip);

		RC_MASK(&chip->cycle_cntr);
	}
}

// --- Register Write Handler ---
static void msm5232_write(void* info, UINT8 reg, UINT8 value)
{
    MSM5232_STATE* chip = (MSM5232_STATE*)info;
    if (reg < 0x08) {
        int ch = reg & 7;
        MSM5232_VOICE* v = &chip->voi[ch];
        v->GF = (value & 0x80) >> 7;
        if (value & 0x80) {
            if (value >= 0xd8) {
                v->mode = 1; // noise
                v->eg_sect = 0; // key on
            } else {
                int pitch_index = value & 0x7f;
                if (pitch_index >= 88)
                    pitch_index = 0x57; // Clamp to valid ROM entry

                uint16_t pg = MSM5232_ROM[pitch_index];
                v->pitch = pitch_index;
                v->TG_count_period = ((pg & 0x1ff) * chip->UpdateStep) / 2;
                if (!v->TG_count_period)
                    v->TG_count_period = 1;
                v->TG_count = v->TG_count_period;
                v->TG_cnt = 0;
                int n = (pg >> 9) & 7;
                v->TG_out16 = 1 << n;
                n = (n > 0) ? n-1 : 0; v->TG_out8 = 1 << n;
                n = (n > 0) ? n-1 : 0; v->TG_out4 = 1 << n;
                n = (n > 0) ? n-1 : 0; v->TG_out2 = 1 << n;
                v->mode = 0;
                v->eg_sect = 0;
            }
        } else {
            if (!v->eg_arm)
                v->eg_sect = 2; // release
            else
                v->eg_sect = 1; // decay
        }
    } else {
		switch (reg) {
		case 0x08: // group1 attack
			for (int i = 0; i < 4; i++)
				chip->voi[i].ar_rate = chip->ar_tbl[value & 7] * chip->capacitors[i];
			break;
		case 0x09: // group2 attack
			for (int i = 0; i < 4; i++)
				chip->voi[i+4].ar_rate = chip->ar_tbl[value & 7] * chip->capacitors[i+4];
			break;
		case 0x0A: // group1 decay
			for (int i = 0; i < 4; i++)
				chip->voi[i].dr_rate = chip->dr_tbl[value & 0xf] * chip->capacitors[i];
			break;
		case 0x0B: // group2 decay
			for (int i = 0; i < 4; i++)
				chip->voi[i+4].dr_rate = chip->dr_tbl[value & 0xf] * chip->capacitors[i+4];
			break;
		case 0x0C: // group1 control
			chip->control1 = value;
			for (int i = 0; i < 4; i++) {
				if ((value & 0x10) && (chip->voi[i].eg_sect == 1))
					chip->voi[i].eg_sect = 0;
				chip->voi[i].eg_arm = value & 0x10;
			}
			chip->EN_out16[0] = (value & 1) ? ~0 : 0;
			chip->EN_out8[0]  = (value & 2) ? ~0 : 0;
			chip->EN_out4[0]  = (value & 4) ? ~0 : 0;
			chip->EN_out2[0]  = (value & 8) ? ~0 : 0;
			break;
		case 0x0D: // group2 control
			chip->control2 = value;
			for (int i = 0; i < 4; i++) {
				if ((value & 0x10) && (chip->voi[i+4].eg_sect == 1))
					chip->voi[i+4].eg_sect = 0;
				chip->voi[i+4].eg_arm = value & 0x10;
			}
			chip->EN_out16[1] = (value & 1) ? ~0 : 0;
			chip->EN_out8[1]  = (value & 2) ? ~0 : 0;
			chip->EN_out4[1]  = (value & 4) ? ~0 : 0;
			chip->EN_out2[1]  = (value & 8) ? ~0 : 0;
			break;
		}
	}
}

static void msm5232_set_mute_mask(void* info, UINT32 muteMask)
{
	MSM5232_STATE* chip = (MSM5232_STATE*)info;
	for (int i = 0; i < MSM5232_NUM_CHANNELS; i++)
		chip->voi[i].mute = (muteMask >> i) & 1;
}