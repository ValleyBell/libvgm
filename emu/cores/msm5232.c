// license:GPL-2.0+
// copyright-holders:Jarek Burczynski, Hiromitsu Shioya, Angelo Salese
/*
    OKI MSM5232
    8 channel tone generator

	Modified for libvgm by Mao(RN22), cam900(MATRIX)
	It's basically MAME code with improvements.
	TA7630P emulation code (BSD-3-Clause) by Angelo Salese, adapted by Mao(RN22) and fully revised by cam900(MATRIX)
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
#include "emutypes.h"
#include "msm5232.h"

#define MSM5232_NUM_CHANNELS 8
#define MSM5232_NUM_OUTPUTS 11
#define CLOCK_RATE_DIVIDER 16
#define STEP_SH 16
#define VMIN 0
#define VMAX 32768
#define MSM5232_EXTVOL_GROUPS 2

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
} MSM5232_VOICE;

typedef struct {
	DEV_DATA _devData;

	MSM5232_VOICE voi[MSM5232_NUM_CHANNELS];
	uint32_t noise_rng;
	uint32_t noise_out;
	int noise_step;
	int noise_cnt;
	int noise_clocks;

	uint8_t control1, control2;
	uint32_t EN_out16[2], EN_out8[2], EN_out4[2], EN_out2[2];

	uint8_t clock_buffer[4];
	uint32_t initial_clock;

	uint32_t clock, sample_rate;
	unsigned int UpdateStep;
	uint8_t smpRateNative;  // emulating at native sample rate (enable sample rate change callback)

	DEVCB_SRATE_CHG SmpRateFunc;
	void* SmpRateData;

	double capacitors[8];
	double ar_tbl[8], dr_tbl[16];

	// --- TA7630 external volume (0..15, as used by Taito hardware) ---
	uint8_t ext_vol[MSM5232_EXTVOL_GROUPS]; // [0]=group1 0..3, [1]=group2 4..7

	UINT8 per_out_vol[MSM5232_NUM_OUTPUTS]; // per-output volume
	UINT8 Muted[MSM5232_NUM_OUTPUTS];
	double vol_ctrl[16];
} MSM5232_STATE;

// Forward declarations
static UINT8 device_start_msm5232(const MSM5232_CFG* cfg, DEV_INFO* retDevInf);
static void device_stop_msm5232(void* info);
static void device_reset_msm5232(void* info);
static UINT32 msm5232_get_rate(void* info);
static void msm5232_set_clock(void *info, UINT32 clock);
static void device_update_msm5232(void* info, UINT32 samples, DEV_SMPL** outputs);
static void msm5232_write(void* info, UINT8 reg, UINT8 value);
static void msm5232_set_mute_mask(void* info, UINT32 muteMask);
static void msm5232_set_srchg_cb(void *info, DEVCB_SRATE_CHG CallbackFunc, void *DataPtr);

static DEVDEF_RWFUNC devFunc[] = {
    {RWF_REGISTER | RWF_WRITE, DEVRW_A8D8, 0, msm5232_write},
    {RWF_CHN_MUTE | RWF_WRITE, DEVRW_ALL, 0, msm5232_set_mute_mask},
    {0x00, 0x00, 0, NULL}
};

static DEV_DEF devDef =
{
	"MSM5232", "MAME", FCC_MAME,

	(DEVFUNC_START)device_start_msm5232,
	device_stop_msm5232,
	device_reset_msm5232,
	device_update_msm5232,

	NULL,	// SetOptionBits
	msm5232_set_mute_mask,
	NULL,	// SetPanning
	msm5232_set_srchg_cb,	// SetSampleRateChangeCallback
	NULL,	// SetLoggingCallback
	NULL,	// LinkDevice

	devFunc	// rwFuncs
};

static const char* DeviceName(const DEV_GEN_CFG* devCfg)
{
	return "MSM5232";
}

static UINT16 DeviceChannels(const DEV_GEN_CFG* devCfg)
{
	return MSM5232_NUM_OUTPUTS;
}

static const char** DeviceChannelNames(const DEV_GEN_CFG* devCfg)
{
	return NULL;
}

static const DEVLINK_IDS* DeviceLinkIDs(const DEV_GEN_CFG* devCfg)
{
	return NULL;
}

const DEV_DECL sndDev_MSM5232 =
{
	DEVID_MSM5232,
	DeviceName,
	DeviceChannels,
	DeviceChannelNames,
	DeviceLinkIDs,
	{	// cores
		&devDef,
		NULL
	}
};

// --- Helper Functions ---
INLINE UINT32 ReadLE32(const UINT8* buffer)
{
	return	(buffer[0] <<  0) | (buffer[1] <<  8) |
			(buffer[2] << 16) | (buffer[3] << 24);
}

INLINE void WriteLE32(UINT8* buffer, UINT32 value)
{
	buffer[0] = (value >>  0) & 0xFF;
	buffer[1] = (value >>  8) & 0xFF;
	buffer[2] = (value >> 16) & 0xFF;
	buffer[3] = (value >> 24) & 0xFF;
	return;
}

static void init_tables(MSM5232_STATE* chip)
{
	const double R51 = 870.0;		// attack resistance
	const double R52 = 17400.0;		// decay 1 resistance
	const double R53 = 101000.0;	// decay 2 resistance
	int i;

	if (chip->smpRateNative)
	{
		chip->UpdateStep = (1 << STEP_SH) / CLOCK_RATE_DIVIDER;
		chip->noise_step = (1 << STEP_SH) * CLOCK_RATE_DIVIDER / 128;
	}
	else
	{
		chip->UpdateStep = (unsigned int)((1 << STEP_SH) * (double)chip->sample_rate / (double)chip->clock);
		chip->noise_step = (int)((1 << STEP_SH) / 128.0 * ((double)chip->clock / chip->sample_rate));
	}

	for (i = 0; i < 8; i++) {
		double clockscale = (double)chip->clock / 2119040.0;
		int rcp_duty_cycle = 1 << ((i & 4) ? (i & ~2) : i);
		chip->ar_tbl[i] = (rcp_duty_cycle / clockscale) * R51;
	}
	for (i = 0; i < 8; i++) {
		double clockscale = (double)chip->clock / 2119040.0;
		int rcp_duty_cycle = 1 << ((i & 4) ? (i & ~2) : i);
		chip->dr_tbl[i] = (rcp_duty_cycle / clockscale) * R52;
		chip->dr_tbl[i + 8] = (rcp_duty_cycle / clockscale) * R53;
	}
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

	v->TG_count_period = 1;
	v->TG_count = 1;
	v->TG_cnt = 0;
	v->TG_out16 = v->TG_out8 = v->TG_out4 = v->TG_out2 = 0;
}

// --- Core Functions ---

static UINT8 device_start_msm5232(const MSM5232_CFG* cfg, DEV_INFO* retDevInf)
{
    MSM5232_STATE* chip;
    int i;

    chip = (MSM5232_STATE*)calloc(1, sizeof(MSM5232_STATE));
    if (!chip) return 0xFF;

    chip->initial_clock = cfg->_genCfg.clock;
    chip->clock = chip->initial_clock;
    WriteLE32(chip->clock_buffer, chip->clock);
	for (i = 0; i < 8; i++)
		chip->capacitors[i] = cfg->capacitors[i];

    chip->sample_rate = msm5232_get_rate(chip);
    SRATE_CUSTOM_HIGHEST(cfg->_genCfg.srMode, chip->sample_rate, cfg->_genCfg.smplRate);
	{
		UINT32 nativeSRate = chip->clock / CLOCK_RATE_DIVIDER;
		int sRateDiff = (int)chip->sample_rate - (int)nativeSRate;
		chip->smpRateNative = (abs(sRateDiff) <= 2);
	}

	{
		double db = 0.0;
		double db_step = 1.50; /* 1.50 dB step (at least, maybe more) */
		double db_step_inc = 0.125;
		for (i = 0; i < 16; i++)
		{
			double max = 100.0 / pow(10.0, db / 20.0);
			chip->vol_ctrl[15 - i] = max / 100.0;
			db += db_step;
			db_step += db_step_inc;
		}
	}

    init_tables(chip);
    msm5232_set_mute_mask(chip, 0);

    chip->_devData.chipInf = chip;
    INIT_DEVINF(retDevInf, &chip->_devData, chip->sample_rate, &devDef);
    return 0x00;
}

static void device_stop_msm5232(void* info)
{
	free(info);
}

static void device_reset_msm5232(void* info)
{
    MSM5232_STATE* chip = (MSM5232_STATE*)info;
    int i;

    msm5232_set_clock(chip, chip->initial_clock);
    WriteLE32(chip->clock_buffer, chip->clock);

    for (i = 0; i < MSM5232_NUM_CHANNELS; i++) {
        init_voice(chip, i);
        msm5232_write(chip, i, 0x80);
        msm5232_write(chip, i, 0x00);
    }
    chip->noise_rng = 1;
    chip->noise_out = 0;
    chip->noise_cnt = 0;
    chip->noise_clocks = 0;
    chip->control1 = chip->control2 = 0;
    chip->EN_out16[0] = chip->EN_out8[0] = chip->EN_out4[0] = chip->EN_out2[0] = 0;
    chip->EN_out16[1] = chip->EN_out8[1] = chip->EN_out4[1] = chip->EN_out2[1] = 0;

    // --- TA7630 external volume defaults: max (0x0F) ---
    chip->ext_vol[0] = 0x0F;
    chip->ext_vol[1] = 0x0F;

    // initialize per-output volume (0x80 = 100%)
    for (i = 0; i < MSM5232_NUM_OUTPUTS; i++)
    {
        // enable 0..7 by default, mute 8..10 (not connected in Taito machines)
        chip->per_out_vol[i] = (i >= 8) ? 0 : 0x80;
    }

    if (chip->SmpRateFunc)
        chip->SmpRateFunc(chip->SmpRateData, chip->sample_rate);
}

static UINT32 msm5232_get_rate(void* info)
{
	MSM5232_STATE* chip = (MSM5232_STATE*)info;
	return chip->clock / CLOCK_RATE_DIVIDER;
}

static void msm5232_set_clock(void *info, UINT32 clock)
{
	MSM5232_STATE* chip = (MSM5232_STATE*)info;
	UINT32 old_clk = chip->clock;

	if (clock)
		chip->clock = clock;	// set to parameter
	else
		chip->clock = ReadLE32(chip->clock_buffer);	// set to value from buffer

	if (old_clk != chip->clock)
	{
		if (chip->smpRateNative && chip->SmpRateFunc != NULL)
		{
			chip->sample_rate = msm5232_get_rate(chip);
			if (chip->SmpRateFunc != NULL)
				chip->SmpRateFunc(chip->SmpRateData, chip->sample_rate);
		}
		// Note: The AR/DR tables rely on the actual clock (not sample rate),
		//       so they need to be recalculated in any case.
		init_tables(chip);	// recalculate AR/DR tables
	}
}

// Envelope handling, closely following MAME
static void EG_voices_advance(MSM5232_STATE* chip)
{
	int i;
	for (i = 0; i < MSM5232_NUM_CHANNELS; i++) {
		MSM5232_VOICE* v = &chip->voi[i];
		switch (v->eg_sect) {
		case 0: // attack
			// capacitor charge
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
			// when ARM=0, EG switches to decay as soon as cap is charged to VT (EG inversion voltage; about 80% of MAX)
			if (!v->eg_arm) {
				if (v->eg >= VMAX * 80 / 100)
					v->eg_sect = 1;
			}
			// ARM=1: stay at max until key off
			v->egvol = v->eg / 16;
			break;

		case 1: // decay
			// capacitor discharge
			if (v->eg > VMIN) {
				v->counter -= (int)((v->eg - VMIN) / v->dr_rate);
				if (v->counter <= 0) {
					int n = -v->counter / chip->sample_rate + 1;
					v->counter += n * chip->sample_rate;
					v->eg -= n;
					if (v->eg < VMIN)
						v->eg = VMIN;
				}
			} else {	// voi->eg <= VMIN
				v->eg_sect = -1;
			}
			v->egvol = v->eg / 16;
			break;

		case 2: // release
			// capacitor discharge
			if (v->eg > VMIN) {
				v->counter -= (int)((v->eg - VMIN) / v->rr_rate);
				if (v->counter <= 0) {
					int n = -v->counter / chip->sample_rate + 1;
					v->counter += n * chip->sample_rate;
					v->eg -= n;
					if (v->eg < VMIN)
						v->eg = VMIN;
				}
			} else {	// voi->eg <= VMIN
				v->eg_sect = -1;
			}
			v->egvol = v->eg / 16;	// 32768/16 = 2048 max
			break;
		default:
			break;
		}
	}
}

// Tone generator, closely following MAME's TG_group_advance
typedef struct {
	INT32 o2, o4, o8, o16;
	INT32 solo8, solo16;
} MSM5232_GROUP_OUT;

static void TG_group_advance(MSM5232_STATE* chip, int groupidx, MSM5232_GROUP_OUT* out)
{
	int i;
	MSM5232_VOICE* v = &chip->voi[groupidx*4];
	memset(out, 0, sizeof(*out));
	for (i = 0; i < 4; i++, v++) {
		int out2 = 0, out4 = 0, out8 = 0, out16 = 0;
		// GUARD: skip if TG_count_period is zero (not initialized yet)
		if (v->TG_count_period == 0)
			continue;
		if (v->mode == 0) { // generate square tone
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
		} else { // generate noise
			if (chip->noise_clocks & 8) out16 += (1<<STEP_SH);
			if (chip->noise_clocks & 4) out8  += (1<<STEP_SH);
			if (chip->noise_clocks & 2) out4  += (1<<STEP_SH);
			if (chip->noise_clocks & 1) out2  += (1<<STEP_SH);
		}
		// Signed output
		if (!chip->Muted[(groupidx*4)+i])
		{
			out->o16 += ((out16 - (1 << (STEP_SH-1))) * v->egvol) >> STEP_SH;
			out->o8  += ((out8  - (1 << (STEP_SH-1))) * v->egvol) >> STEP_SH;
			out->o4  += ((out4  - (1 << (STEP_SH-1))) * v->egvol) >> STEP_SH;
			out->o2  += ((out2  - (1 << (STEP_SH-1))) * v->egvol) >> STEP_SH;
		}
		if (i == 3 && groupidx == 1)
		{
			if (!chip->Muted[8])
				out->solo16 += ((out16 - (1 << (STEP_SH-1))) << 11) >> STEP_SH;
			if (!chip->Muted[9])
				out->solo8  += ((out8  - (1 << (STEP_SH-1))) << 11) >> STEP_SH;
		}
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
		if ((int)(chip->noise_rng & (1<<16)) != tmp)
			chip->noise_clocks++;
		cnt--;
	}
	if (!chip->Muted[10])
		chip->noise_out = 0;
	else
		chip->noise_out = (chip->noise_rng & (1<<16)) ? 1 : 0;
}

// --- Main Update ---
static void device_update_msm5232(void* info, UINT32 samples, DEV_SMPL** outputs)
{
	MSM5232_STATE* chip = (MSM5232_STATE*)info;
	DEV_SMPL* outL = outputs[0];
	DEV_SMPL* outR = outputs[1];
	UINT32 i;

	for (i = 0; i < samples; i++) {
		MSM5232_GROUP_OUT group1, group2;
		INT32 g1o2, g1o4, g1o8, g1o16, g2o2, g2o4, g2o8, g2o16, so8, so16, nout;
		DEV_SMPL mix;

		// 1. Advance envelopes
		EG_voices_advance(chip);

		// 2. Tone groups
		TG_group_advance(chip, 0, &group1);
		TG_group_advance(chip, 1, &group2);

		// 3. Noise update (advance RNG)
		update_noise(chip);

		// 4. Mix output, apply TA7630 external volume
		g1o2 = (group1.o2 * chip->per_out_vol[0]) >> 7;
		g1o4 = (group1.o4 * chip->per_out_vol[1]) >> 7;
		g1o8 = (group1.o8 * chip->per_out_vol[2]) >> 7;
		g1o16 = (group1.o16 * chip->per_out_vol[3]) >> 7;
		g2o2 = (group2.o2 * chip->per_out_vol[4]) >> 7;
		g2o4 = (group2.o4 * chip->per_out_vol[5]) >> 7;
		g2o8 = (group2.o8 * chip->per_out_vol[6]) >> 7;
		g2o16 = (group2.o16 * chip->per_out_vol[7]) >> 7;
		so8 = (group2.solo8 * chip->per_out_vol[8]) >> 7;
		so16 = (group2.solo16 * chip->per_out_vol[9]) >> 7;
		nout = chip->noise_out * chip->per_out_vol[10];	// is 0 or "maximum volume"
		mix =
			(DEV_SMPL)((double)(g1o2 + g1o4 + g1o8 + g1o16) * chip->vol_ctrl[chip->ext_vol[0]]) +
			(DEV_SMPL)((double)(g2o2 + g2o4 + g2o8 + g2o16) * chip->vol_ctrl[chip->ext_vol[1]]) +
			(DEV_SMPL)(so8 + so16 + nout);

		outL[i] = mix;
		outR[i] = mix;
	}
}

// --- Register Write Handler ---
static void msm5232_write(void* info, UINT8 reg, UINT8 value)
{
    MSM5232_STATE* chip = (MSM5232_STATE*)info;
    int i;
    if (reg < 0x08) {
        int ch = reg & 7;
        MSM5232_VOICE* v = &chip->voi[ch];
        v->GF = (value & 0x80) >> 7;
        if (value & 0x80) {
            if (value >= 0xd8) {
                v->mode = 1; // noise
                v->eg_sect = 0; // key on
            } else {
                int n;
                uint16_t pg;
                int pitch_index = value & 0x7f;
                if (pitch_index > 0x57)
                    pitch_index = 0x57; // Clamp to valid ROM entry

                pg = MSM5232_ROM[pitch_index];
                v->pitch = pitch_index;
                v->TG_count_period = ((pg & 0x1ff) * chip->UpdateStep) / 2;
                if (!v->TG_count_period)
                    v->TG_count_period = 1;
                v->TG_count = v->TG_count_period;
                v->TG_cnt = 0;

                n = (pg >> 9) & 7;
                v->TG_out16 = 1 << n;

                n = (n > 0) ? n-1 : 0;
                v->TG_out8 = 1 << n;

                n = (n > 0) ? n-1 : 0;
                v->TG_out4 = 1 << n;

                n = (n > 0) ? n-1 : 0;
                v->TG_out2 = 1 << n;

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
			for (i = 0; i < 4; i++)
				chip->voi[i].ar_rate = chip->ar_tbl[value & 7] * chip->capacitors[i];
			break;
		case 0x09: // group2 attack
			for (i = 0; i < 4; i++)
				chip->voi[i+4].ar_rate = chip->ar_tbl[value & 7] * chip->capacitors[i+4];
			break;
		case 0x0A: // group1 decay
			for (i = 0; i < 4; i++)
				chip->voi[i].dr_rate = chip->dr_tbl[value & 0xf] * chip->capacitors[i];
			break;
		case 0x0B: // group2 decay
			for (i = 0; i < 4; i++)
				chip->voi[i+4].dr_rate = chip->dr_tbl[value & 0xf] * chip->capacitors[i+4];
			break;
		case 0x0C: // group1 control
			chip->control1 = value;
			for (i = 0; i < 4; i++) {
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
			for (i = 0; i < 4; i++) {
				if ((value & 0x10) && (chip->voi[i+4].eg_sect == 1))
					chip->voi[i+4].eg_sect = 0;
				chip->voi[i+4].eg_arm = value & 0x10;
			}
			chip->EN_out16[1] = (value & 1) ? ~0 : 0;
			chip->EN_out8[1]  = (value & 2) ? ~0 : 0;
			chip->EN_out4[1]  = (value & 4) ? ~0 : 0;
			chip->EN_out2[1]  = (value & 8) ? ~0 : 0;
			break;
		// per output volume (0x80 = 100%)
		case 0x10:	// output 0
		case 0x11:
		case 0x12:
		case 0x13:
		case 0x14:
		case 0x15:
		case 0x16:
		case 0x17:
		case 0x18:
		case 0x19:
		case 0x1A:	// output 10
			chip->per_out_vol[reg & 0x0F] = (value > 0x80) ? 0x80 : value;
			break;
		// --- TA7630 external volume for MSM5232 ---
		case 0x1E: // external volume for group 1 (ch 0..3)
			chip->ext_vol[0] = value & 0x0F;
			break;
		case 0x1F: // external volume for group 2 (ch 4..7)
			chip->ext_vol[1] = value & 0x0F;
			break;
		case 0x20:	// chip clock, 000000xx
		case 0x21:	// chip clock, 0000xx00
		case 0x22:	// chip clock, 00xx0000
			chip->clock_buffer[reg & 0x03] = value;
			break;
		case 0x23:	// chip clock, xx000000
			chip->clock_buffer[reg & 0x03] = value;
			msm5232_set_clock(chip, 0);	// refresh clock from clock_buffer
			break;
		}
	}
}

static void msm5232_set_mute_mask(void* info, UINT32 muteMask)
{
	MSM5232_STATE* chip = (MSM5232_STATE*)info;
	UINT8 i;
	for (i = 0; i < MSM5232_NUM_OUTPUTS; i++)
		chip->Muted[i] = (muteMask >> i) & 1;
}

static void msm5232_set_srchg_cb(void *info, DEVCB_SRATE_CHG CallbackFunc, void *DataPtr)
{
	MSM5232_STATE* chip = (MSM5232_STATE*)info;
	chip->SmpRateFunc = CallbackFunc;
	chip->SmpRateData = DataPtr;
}
