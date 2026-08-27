// license:BSD-3-Clause
// copyright-holders:superctr
/*
    Yamaha YMF271-F "OPX" emulator

    Written from the datasheet and the application manual.  Where the
    documents leave the behaviour open, the model follows recordings of real
    hardware (no chip was available for direct probing).

    Architecture (from the datasheet / application manual):
      - 48 slots in 12 groups of 4; slot n = 12*bank + group, where bank is the
        register bank (S1..S4) and the slot number is also the order in which the
        chip evaluates operators inside one 44.1 kHz sample period.
      - Every slot runs the same operator pipeline (PG -> oscillator -> EG -> OP).
        The oscillator reads one of 7 internal waveforms (log-sin derived) or, for
        the 12 slots of groups 0/4/8 only (n % 4 == 0), external PCM data.
      - The per-group sync register selects how the 4 slots are connected
        (4op / 2x2op / 3op+PCM / 4xPCM), which slots receive broadcast writes,
        and which slot(s) act as key-on slot.

    Model:
      - Register model with sync broadcast, F-number latch, status / End flags,
        timers, external memory window.
      - OPM/OPZ-style log-sin/exp operator
      - Per-slot LFO: clock-divider rate table 2-6-2, saw / square / triangle,
        PMS depth fnum*k/1024 (table 2-6-3), AMS 63/126/252 units, phase reset
        at key-on; AM starts at full attenuation for every waveform (OPM convention).
      - AccOn = the slot output is accumulated in a saturating 14-bit sum, so
        any sustained tone rails into a full-level square that flips at the
        operator's zero crossings ("distorted" basses and drums).
      - External PCM: FM PG with the implicit fnum bit 11, Fs divider, 8-bit and
        packed 12-bit words, linear interpolation, looping, End flags, external
        key code (manual 2-9), envelope multiply, same pan as FM.

    Open points (need to be verified on real HW):
      - Waveforms 1-6 unverified on HW recordings and based on guesswork.
      - Key code: octave = Block for blocks 0..7, assumed to clamp at 0 for negative
        blocks (the manual's formula wraps to the top rows instead).
      - wave 7 on slots other than 0,4,..,44 assumed to be silent.

    Not implemented (decoded, unused by the available VGMs):
      - PFM (utility 0x0n bit 7)
      - PCM alternate loop (A/L), probably bidirectional loop but no games use it.
      - Timer A register split (0x10 high 8 / 0x11 low 2 bits) and the
        external-memory write address increment follow the previous core.

    Output: the two stereo outputs are CH0 / CH1 (DO1).  CH2 / CH3 (DO2) and
    the extended channels CH4-CH7 (EXT1 / EXT2) are decoded but not output.

    Compile with YMF271_OPX_DEBUG for the debugging interface in ymf271_opx.h
    (live state snapshots, group solo / slot mute) and these environment
    switches:
      OPX_DUMP=file         log every key-on with the decoded registers
      OPX_SOLO=g[,g..]      only these groups (0-11) are audible
      OPX_MUTESLOTS=n[,..]  remove slots 0-47 from the mix
*/

#include <stdlib.h>
#include <string.h>
#include <math.h>
#ifdef YMF271_OPX_DEBUG
#include <stdio.h>
#endif

#include "../../stdtype.h"
#include "../EmuStructs.h"
#include "../SoundDevs.h"
#include "../EmuCores.h"
#include "../snddef.h"
#include "../EmuHelper.h"
#include "../logging.h"
#include "ymf271.h"
#ifdef YMF271_OPX_DEBUG
#include "ymf271_opx.h"
#endif


static void ymf271_update(void *info, UINT32 samples, DEV_SMPL** outputs);
static UINT8 device_start_ymf271(const DEV_GEN_CFG* cfg, DEV_INFO* retDevInf);
static void device_stop_ymf271(void *info);
static void device_reset_ymf271(void *info);

static UINT8 ymf271_r(void *info, UINT8 offset);
static void ymf271_w(void *info, UINT8 offset, UINT8 data);
static void ymf271_alloc_rom(void* info, UINT32 memsize);
static void ymf271_write_rom(void *info, UINT32 offset, UINT32 length, const UINT8* data);

static void ymf271_set_mute_mask(void *info, UINT32 MuteMask);
static void ymf271_set_log_cb(void *info, DEVCB_LOG func, void* param);


static DEVDEF_RWFUNC devFunc[] =
{
	{RWF_REGISTER | RWF_WRITE, DEVRW_A8D8, 0, ymf271_w},
	{RWF_REGISTER | RWF_READ, DEVRW_A8D8, 0, ymf271_r},
	{RWF_MEMORY | RWF_WRITE, DEVRW_BLOCK, 0, ymf271_write_rom},
	{RWF_MEMORY | RWF_WRITE, DEVRW_MEMSIZE, 0, ymf271_alloc_rom},
	{RWF_CHN_MUTE | RWF_WRITE, DEVRW_ALL, 0, ymf271_set_mute_mask},
	{0x00, 0x00, 0, NULL}
};
static DEV_DEF devDef =
{
	"YMF271", "superctr", FCC_CTR_,

	device_start_ymf271,
	device_stop_ymf271,
	device_reset_ymf271,
	ymf271_update,

	NULL,	// SetOptionBits
	ymf271_set_mute_mask,
	NULL,	// SetPanning
	NULL,	// SetSampleRateChangeCallback
	ymf271_set_log_cb,	// SetLoggingCallback
	NULL,	// LinkDevice

	devFunc,	// rwFuncs
};

static const char* DeviceName(const DEV_GEN_CFG* devCfg)
{
	return "YMF271";
}

static UINT16 DeviceChannels(const DEV_GEN_CFG* devCfg)
{
	return 12;
}

static const char** DeviceChannelNames(const DEV_GEN_CFG* devCfg)
{
	return NULL;
}

static const DEVLINK_IDS* DeviceLinkIDs(const DEV_GEN_CFG* devCfg)
{
	return NULL;
}

const DEV_DECL sndDev_YMF271 =
{
	DEVID_YMF271,
	DeviceName,
	DeviceChannels,
	DeviceChannelNames,
	DeviceLinkIDs,
	{	// cores
		&devDef,
		NULL
	}
};


/* ------------------------------------------------------------------------ */
/*  constants and tables                                                    */
/* ------------------------------------------------------------------------ */

#define OPX_SLOTS		48
#define OPX_GROUPS		12

/* register address low nibble -> group (FM banks, utility) / slot (PCM bank).
   Nibbles 3/7/B/F are invalid. */
static const INT8 fm_tab[16]  = { 0, 1, 2, -1, 3, 4, 5, -1, 6, 7, 8, -1, 9, 10, 11, -1 };
static const INT8 pcm_tab[16] = { 0, 4, 8, -1, 12, 16, 20, -1, 24, 28, 32, -1, 36, 40, 44, -1 };

/* sync modes (utility reg 0x0n bits 1:0) */
enum
{
	SYNC_4OP = 0,		/* S1-S2-S3-S4, key-on slot S1              */
	SYNC_2X2OP = 1,		/* S1-S3 and S2-S4, key-on slots S1, S2     */
	SYNC_3OP_PCM = 2,	/* S1-S2-S3 FM + S4 PCM, key-on slots S1,S4 */
	SYNC_PCM = 3		/* 4 independent slots                      */
};

enum
{
	EG_ATTACK = 0,
	EG_DECAY1,
	EG_DECAY2,
	EG_RELEASE,
	EG_OFF
};

/* EG increment patterns (ymfm/OPM): nibble k of entry r = increment for
   EG sub-step k at rate r. */
static const UINT32 eg_inc[64] =
{
	0x00000000, 0x00000000, 0x10101010, 0x10101010,
	0x10101010, 0x10111010, 0x11101110, 0x11111110,
	0x10101010, 0x10111010, 0x11101110, 0x11111110,
	0x10101010, 0x10111010, 0x11101110, 0x11111110,
	0x10101010, 0x10111010, 0x11101110, 0x11111110,
	0x10101010, 0x10111010, 0x11101110, 0x11111110,
	0x10101010, 0x10111010, 0x11101110, 0x11111110,
	0x10101010, 0x10111010, 0x11101110, 0x11111110,
	0x10101010, 0x10111010, 0x11101110, 0x11111110,
	0x10101010, 0x10111010, 0x11101110, 0x11111110,
	0x10101010, 0x10111010, 0x11101110, 0x11111110,
	0x10101010, 0x10111010, 0x11101110, 0x11111110,
	0x11111111, 0x21112111, 0x21212121, 0x22212221,
	0x22222222, 0x42224222, 0x42424242, 0x44424442,
	0x44444444, 0x84448444, 0x84848484, 0x88848884,
	0x88888888, 0x88888888, 0x88888888, 0x88888888
};

/* Rate key scaling (manual table 2-6-7), [keycode][KS] */
static const UINT8 rks_tab[32][8] =
{
	{  0,  0,  0,  0,  0,  2,  4,  8 }, {  0,  0,  0,  0,  1,  3,  5,  9 },
	{  0,  0,  0,  1,  2,  4,  6, 10 }, {  0,  0,  0,  1,  3,  5,  7, 11 },
	{  0,  0,  1,  2,  4,  6,  8, 12 }, {  0,  0,  1,  2,  5,  7,  9, 13 },
	{  0,  0,  1,  3,  6,  8, 10, 14 }, {  0,  0,  1,  3,  7,  9, 11, 15 },
	{  0,  1,  2,  4,  8, 10, 12, 16 }, {  0,  1,  2,  4,  9, 11, 13, 17 },
	{  0,  1,  2,  5, 10, 12, 14, 18 }, {  0,  1,  2,  5, 11, 13, 15, 19 },
	{  0,  1,  3,  6, 12, 14, 16, 20 }, {  0,  1,  3,  6, 13, 15, 17, 21 },
	{  0,  1,  3,  7, 14, 16, 18, 22 }, {  0,  1,  3,  7, 15, 17, 19, 23 },
	{  0,  2,  4,  8, 16, 18, 20, 24 }, {  0,  2,  4,  8, 17, 19, 21, 25 },
	{  0,  2,  4,  9, 18, 20, 22, 26 }, {  0,  2,  4,  9, 19, 21, 23, 27 },
	{  0,  2,  5, 10, 20, 22, 24, 28 }, {  0,  2,  5, 10, 21, 23, 25, 29 },
	{  0,  2,  5, 11, 22, 24, 26, 30 }, {  0,  2,  5, 11, 23, 25, 27, 31 },
	{  0,  3,  6, 12, 24, 26, 28, 31 }, {  0,  3,  6, 12, 25, 27, 29, 31 },
	{  0,  3,  6, 13, 26, 28, 30, 31 }, {  0,  3,  6, 13, 27, 29, 31, 31 },
	{  0,  3,  7, 14, 28, 30, 31, 31 }, {  0,  3,  7, 14, 29, 31, 31, 31 },
	{  0,  3,  7, 15, 30, 31, 31, 31 }, {  0,  3,  7, 15, 31, 31, 31, 31 }
};

/* Detune, [keycode][DT&3], in units of fs/2^20 Hz (= 1 LSB of a 20-bit phase
   increment).  This is the OPM DT1 table; the manual's table 2-6-5 (cents
   column) is exactly this table at fs = 44.1 kHz. */
static const UINT8 detune_tab[32][4] =
{
	{ 0, 0, 1, 2 }, { 0, 0, 1, 2 }, { 0, 0, 1, 2 }, { 0, 0, 1, 2 },
	{ 0, 1, 2, 2 }, { 0, 1, 2, 3 }, { 0, 1, 2, 3 }, { 0, 1, 2, 3 },
	{ 0, 1, 2, 4 }, { 0, 1, 3, 4 }, { 0, 1, 3, 4 }, { 0, 1, 3, 5 },
	{ 0, 2, 4, 5 }, { 0, 2, 4, 6 }, { 0, 2, 4, 6 }, { 0, 2, 5, 7 },
	{ 0, 2, 5, 8 }, { 0, 3, 6, 8 }, { 0, 3, 6, 9 }, { 0, 3, 7, 10 },
	{ 0, 4, 8, 11 }, { 0, 4, 8, 12 }, { 0, 4, 9, 13 }, { 0, 5, 10, 14 },
	{ 0, 5, 11, 16 }, { 0, 6, 12, 17 }, { 0, 6, 13, 19 }, { 0, 7, 14, 20 },
	{ 0, 8, 16, 22 }, { 0, 8, 16, 22 }, { 0, 8, 16, 22 }, { 0, 8, 16, 22 }
};

/* Modulation level (FB register of a modulated slot), manual values:
   0 = 16 pi, 1 = 8 pi, 2 = 4 pi, 3 = 2 pi, 4 = pi, 5 = 32 pi, 6 = 64 pi, 7 = 128 pi.
   Applied as (14-bit modulator sum * table) >> 8 on the 10-bit phase: level 0
   = 8192 * 128 / 256 = 4096 phase units = 4 cycles = +/-8 pi peak, i.e. half
   the manual's figure (read as the peak-to-peak swing) and exactly OPM's
   fixed depth. */
static const UINT16 modlevel[8] = { 128, 64, 32, 16, 8, 256, 512, 1024 };

/* LFO PM depth (manual table 2-6-3): max deviation = fnum * k / 1024,
   k = 0, 2, 3, 4, 6, 12, 24, 48 (3.4 .. 79.3 cents). */
static const UINT8 pms_k[8] = { 0, 2, 3, 4, 6, 12, 24, 48 };

/* Algorithm connection tables.  For each operator position of the voice:
   mods = bitmask of positions whose output modulates it; car = bitmask of
   carrier positions; fbsrc = position whose output is fed back into position
   0 (0 = self, 2 = the S3 loop of algorithms 1/5/7/11).  Positions: 4op
   S1,S2,S3,S4 = 0..3; 2op head,tail = 0,1; 3op S1,S2,S3 = 0..2. */
typedef struct
{
	UINT8 mods[4];
	UINT8 car;
	UINT8 fbsrc;
} OPX_ALG;

static const OPX_ALG alg4[16] =
{
	{ { 0, 0x4, 0x1, 0x2 }, 0x8, 0 },	/* 0: S1>S3>S2>S4           */
	{ { 0, 0x4, 0x1, 0x2 }, 0x8, 2 },	/* 1: same, fb loop S1>S3   */
	{ { 0, 0x5, 0x0, 0x2 }, 0x8, 0 },	/* 2: (S1+S3)>S2>S4         */
	{ { 0, 0x4, 0x0, 0x3 }, 0x8, 0 },	/* 3: (S1 + S3>S2)>S4       */
	{ { 0, 0x0, 0x1, 0x6 }, 0x8, 0 },	/* 4: (S1>S3 + S2)>S4       */
	{ { 0, 0x0, 0x1, 0x6 }, 0x8, 2 },	/* 5: same, fb loop S1>S3   */
	{ { 0, 0x0, 0x1, 0x2 }, 0xC, 0 },	/* 6: S1>S3, S2>S4          */
	{ { 0, 0x0, 0x1, 0x2 }, 0xC, 2 },	/* 7: same, fb loop S1>S3   */
	{ { 0, 0x4, 0x0, 0x2 }, 0x9, 0 },	/* 8: S1, S3>S2>S4          */
	{ { 0, 0x0, 0x0, 0x6 }, 0x9, 0 },	/* 9: S1, (S3+S2)>S4        */
	{ { 0, 0x0, 0x1, 0x0 }, 0xE, 0 },	/* 10: S1>S3, S2, S4        */
	{ { 0, 0x0, 0x1, 0x0 }, 0xE, 2 },	/* 11: same, fb loop S1>S3  */
	{ { 0, 0x1, 0x1, 0x1 }, 0xE, 0 },	/* 12: S1>(S2,S3,S4)        */
	{ { 0, 0x4, 0x0, 0x0 }, 0xB, 0 },	/* 13: S1, S3>S2, S4        */
	{ { 0, 0x0, 0x1, 0x2 }, 0xD, 0 },	/* 14: S1, S1>S3, S2>S4     */
	{ { 0, 0x0, 0x0, 0x0 }, 0xF, 0 }	/* 15: all carriers         */
};

static const OPX_ALG alg2[4] =
{
	{ { 0, 0x1, 0, 0 }, 0x2, 0 },	/* 0: A>B          */
	{ { 0, 0x1, 0, 0 }, 0x2, 1 },	/* 1: A>B, fb loop */
	{ { 0, 0x0, 0, 0 }, 0x3, 0 },	/* 2: A, B         */
	{ { 0, 0x1, 0, 0 }, 0x3, 0 }	/* 3: A, A>B       */
};

static const OPX_ALG alg3[8] =
{
	{ { 0, 0x4, 0x1, 0 }, 0x2, 0 },	/* 0: S1>S3>S2        */
	{ { 0, 0x4, 0x1, 0 }, 0x2, 2 },	/* 1: same, fb loop   */
	{ { 0, 0x5, 0x0, 0 }, 0x2, 0 },	/* 2: (S1+S3)>S2      */
	{ { 0, 0x4, 0x0, 0 }, 0x3, 0 },	/* 3: S1, S3>S2       */
	{ { 0, 0x0, 0x1, 0 }, 0x6, 0 },	/* 4: S1>S3, S2       */
	{ { 0, 0x0, 0x1, 0 }, 0x6, 2 },	/* 5: same, fb loop   */
	{ { 0, 0x0, 0x0, 0 }, 0x7, 0 },	/* 6: S1, S2, S3      */
	{ { 0, 0x0, 0x1, 0 }, 0x7, 0 }	/* 7: S1, S1>S3, S2   */
};

static const OPX_ALG alg_single = { { 0, 0, 0, 0 }, 0x1, 0 };

/* lookup tables (generated at first start, identical to the OPM ROMs) */
static UINT16 opx_logsin[256];	/* -log2(sin) * 256 for a quarter wave */
static UINT16 opx_exp[256];		/* 2^(-(i+1)/256) * 2048 */
static UINT8 opx_tables_ready = 0;


/* ------------------------------------------------------------------------ */
/*  state                                                                   */
/* ------------------------------------------------------------------------ */

typedef struct
{
	/* --- function registers (decoded) --- */
	UINT8 kon;			/* last written KON bit */
	UINT8 ext_en;		/* 0xH bit 7 */
	UINT8 ext_out;		/* 0xH bits 6:3 */
	UINT8 lfo_freq;		/* 1xH */
	UINT8 ams, pms, lfo_wave;	/* 2xH */
	UINT8 dt, mul;		/* 3xH */
	UINT8 tl;			/* 4xH */
	UINT8 ks, ar;		/* 5xH */
	UINT8 d1r;			/* 6xH */
	UINT8 d2r;			/* 7xH */
	UINT8 d1l, rr;		/* 8xH */
	UINT16 fnum;		/* 9xH + latched AxH low nibble (12 bits) */
	UINT8 block;		/* latched AxH high nibble (4-bit two's complement) */
	UINT8 fnum_latch;	/* raw AxH value, committed on the 9xH write */
	UINT8 accon, fb, wave;	/* BxH */
	UINT8 alg;			/* CxH */
	UINT8 ch_level[4];	/* DxH, ExH */

	/* --- PCM attribute registers (bank 5, only meaningful if slot % 4 == 0) --- */
	UINT32 pcm_start;	/* byte address */
	UINT32 pcm_end;		/* word count from start */
	UINT32 pcm_loop;	/* word count from start */
	UINT8 pcm_altloop;
	UINT8 pcm_fs;		/* 0..3 -> 44.1/22.05/11.025/5.5125 kHz */
	UINT8 pcm_12bit;
	UINT8 pcm_srcnote, pcm_srcb;

	/* --- runtime state --- */
	INT8 block_s;		/* block as signed value */
	UINT8 keycode;		/* 0..31 for RKS / detune */
	UINT8 eg_state;
	INT32 eg_att;		/* 10-bit attenuation, 0.09375 dB units */
	UINT32 phase;		/* 32-bit phase accumulator, 2^32 = one cycle */
	INT32 out;			/* last 14-bit operator output */
	INT32 acc;			/* AccOn: running (saturating) sum of the operator output */
	INT32 fb_hist[2];	/* feedback history (written by fb source slot) */
	UINT32 lfo_cnt;		/* sample counter for the LFO clock divider */
	UINT8 lfo_pos;		/* LFO phase, 0..127 */
	UINT32 pcm_pos;		/* PCM: word index from the start address */
	UINT32 pcm_frac;	/* PCM: 16-bit fraction of the position */
	UINT8 pcm_ended;	/* PCM: End flag already raised since the last key-on */

	/* --- connection cache (rebuilt when sync/algorithm changes) --- */
	UINT8 c_nmod;		/* number of modulator slots */
	UINT8 c_mod[3];		/* modulator slot numbers */
	UINT8 c_fbhead;		/* 1: uses feedback input (level from its own FB register) */
	INT8 c_fbtarget;	/* slot whose fb_hist receives this slot's output, -1 = none */
	UINT8 c_carrier;	/* 1: output goes to the accumulator */
} OPX_SLOT;

typedef struct
{
	UINT8 sync;
	UINT8 pfm;
	UINT8 dirty;		/* connection cache needs rebuild */
	UINT8 muted;
} OPX_GROUP;

typedef struct
{
	DEV_DATA _devData;
	DEV_LOGGER logger;

	OPX_SLOT slots[OPX_SLOTS];
	OPX_GROUP groups[OPX_GROUPS];

	UINT8 regs_main[0x10];	/* address latches (even offsets) / last data (odd) */

	/* timers */
	UINT16 timerA;			/* 10-bit preset */
	UINT8 timerB;			/* 8-bit preset */
	UINT8 timer_ctrl;		/* last value written to 0x13 */
	UINT8 timerA_run, timerB_run;
	INT32 timerA_cnt;		/* remaining samples (1 sample = 384 clocks) */
	INT32 timerB_cnt;
	UINT8 status;			/* bit0 TA, bit1 TB, bit7 busy */
	UINT16 end_status;		/* End flags, bit i = PCM slot i*4 */
	UINT8 irqstate;

	/* external memory window (utility 0x14-0x17, read offset 2) */
	UINT32 ext_address;
	UINT8 ext_rw;
	UINT8 ext_readlatch;

	UINT8 *mem_base;
	UINT32 mem_size;
	UINT32 clock;
	UINT32 rate;

	/* synthesis */
	UINT32 eg_cnt;			/* EG clock counter (fs/2) */
	UINT8 eg_phase;			/* sample parity for the fs/2 EG clock */

#ifdef YMF271_OPX_DEBUG
	UINT16 solo_mask;		/* only these groups are audible (0 = all) */
	UINT64 mute_slots;		/* slots (0-47) removed from the mix */
	UINT32 sample_count;	/* samples rendered since reset */
	FILE *dump;
#endif
} OPX_CHIP;


/* ------------------------------------------------------------------------ */
/*  helpers                                                                 */
/* ------------------------------------------------------------------------ */

INLINE UINT8 opx_read_memory(OPX_CHIP *chip, UINT32 offset)
{
	offset &= 0x7FFFFF;
	if (offset < chip->mem_size)
		return chip->mem_base[offset];
	return 0xFF;
}

/* Is slot (bank, group) a key-on slot under the group's sync mode? */
static int opx_is_keyon_slot(const OPX_CHIP *chip, int bank, int group)
{
	switch (chip->groups[group].sync)
	{
	case SYNC_4OP:		return bank == 0;
	case SYNC_2X2OP:	return bank == 0 || bank == 1;
	case SYNC_3OP_PCM:	return bank == 0 || bank == 3;
	default:			return 1;
	}
}

/* Fill 'slots' with the slot numbers that form the voice keyed by (bank, group).
   Returns the count (0 if not a key-on slot). */
static int opx_voice_slots(const OPX_CHIP *chip, int bank, int group, int *slots)
{
	switch (chip->groups[group].sync)
	{
	case SYNC_4OP:
		if (bank != 0) return 0;
		slots[0] = group; slots[1] = group + 12; slots[2] = group + 24; slots[3] = group + 36;
		return 4;
	case SYNC_2X2OP:
		if (bank == 0) { slots[0] = group; slots[1] = group + 24; return 2; }
		if (bank == 1) { slots[0] = group + 12; slots[1] = group + 36; return 2; }
		return 0;
	case SYNC_3OP_PCM:
		if (bank == 0) { slots[0] = group; slots[1] = group + 12; slots[2] = group + 24; return 3; }
		if (bank == 3) { slots[0] = group + 36; return 1; }
		return 0;
	default:
		slots[0] = 12 * bank + group;
		return 1;
	}
}


/* ------------------------------------------------------------------------ */
/*  key on / off                                                            */
/* ------------------------------------------------------------------------ */

/* Key code (manual 2-9): 5 bits, octave = the *bottom 3 bits* of Block (so the
   negative blocks wrap to the top octaves) + N4N3 from the F-number.
   internal waveform: KC = 4 Block + N4N3, fnum thresholds 0x780/0x900/0xA80;
   external waveform: KC = (4 SrcB + SrcNote) + (4 Block + N4N3), 11-bit fnum
   thresholds 0x100/0x300/0x500 (table 2-9-3); the 5-bit sum wraps, which gives
   the arithmetically right result for negative blocks. */
static void opx_update_keycode(OPX_SLOT *s, int slotnum)
{
	int n43, kc;

	if (s->wave == 7 && (slotnum & 3) == 0)
	{
		UINT16 fn = s->fnum & 0x7FF;
		if (fn < 0x100)      n43 = 0;
		else if (fn < 0x300) n43 = 1;
		else if (fn < 0x500) n43 = 2;
		else                 n43 = 3;
		kc = s->pcm_srcb * 4 + s->pcm_srcnote + (s->block & 7) * 4 + n43;
	}
	else
	{
		if (s->fnum < 0x780)      n43 = 0;
		else if (s->fnum < 0x900) n43 = 1;
		else if (s->fnum < 0xA80) n43 = 2;
		else                      n43 = 3;
		/* negative blocks: the manual's 4*Block would wrap to octave 7, but
		   a recorded bass at block -1 (DT 7 / DT 2 on a 1:3 operator pair)
		   stays phase-locked within ~0.5 %, i.e. the detune of the lowest
		   key codes -- so the octave clamps at 0. */
		kc = ((s->block_s < 0) ? 0 : (s->block & 7) * 4) + n43;
	}
	s->keycode = (UINT8)(kc & 31);
}

INLINE int opx_eg_rate(int rate2, int rks)
{
	/* rate2 = 2*AR/D1R/D2R or 4*RR; rate 0 stays 0 (infinite) */
	if (rate2 == 0)
		return 0;
	rate2 += rks;
	return (rate2 > 63) ? 63 : rate2;
}

static void opx_slot_keyon(OPX_CHIP *chip, int slotnum)
{
	OPX_SLOT *s = &chip->slots[slotnum];

	s->eg_state = EG_ATTACK;
	s->phase = 0;
	/* The attenuation continues from its current value (OPM); a maximum
	   attack rate jumps straight to 0 dB (table 2-6-8: rate 63 = 0.07 ms). */
	if (opx_eg_rate(s->ar * 2, rks_tab[s->keycode][s->ks]) >= 63)
		s->eg_att = 0;
	s->lfo_cnt = 0;
	s->lfo_pos = 0;
	s->pcm_pos = 0;
	s->pcm_frac = 0;
	s->pcm_ended = 0;
	s->acc = 0;
	if ((slotnum & 3) == 0)
		chip->end_status &= ~(1 << (slotnum >> 2));
}

static void opx_slot_keyoff(OPX_CHIP *chip, int slotnum)
{
	OPX_SLOT *s = &chip->slots[slotnum];

	if (s->eg_state != EG_OFF)
		s->eg_state = EG_RELEASE;
}


/* ------------------------------------------------------------------------ */
/*  register writes                                                         */
/* ------------------------------------------------------------------------ */

#ifdef YMF271_OPX_DEBUG
static void opx_dump_keyon(OPX_CHIP *chip, int bank, int group)
{
	int slots[4], n, i;
	const char *kind;

	if (chip->dump == NULL)
		return;
	n = opx_voice_slots(chip, bank, group, slots);
	if (n == 0)
		return;	/* key-on written to a non-key-on slot */
	switch (chip->groups[group].sync)
	{
	case SYNC_4OP: kind = "4op"; break;
	case SYNC_2X2OP: kind = "2op"; break;
	case SYNC_3OP_PCM: kind = (bank == 0) ? "3op" : "pcm"; break;
	default: kind = "pcm"; break;
	}
	fprintf(chip->dump, "KON t=%u g=%d bank=%d sync=%d pfm=%d kind=%s",
		chip->sample_count, group, bank, chip->groups[group].sync, chip->groups[group].pfm, kind);
	for (i = 0; i < n; i++)
	{
		const OPX_SLOT *s = &chip->slots[slots[i]];
		fprintf(chip->dump, " | s%d w%d mul%d dt%d tl%d ks%d ar%d d1r%d d2r%d d1l%d rr%d lf%d lw%d ams%d pms%d fn%d bl%d fb%d alg%d acc%d en%d ext%d ch%d,%d,%d,%d",
			slots[i], s->wave, s->mul, s->dt, s->tl, s->ks, s->ar, s->d1r, s->d2r, s->d1l, s->rr,
			s->lfo_freq, s->lfo_wave, s->ams, s->pms, s->fnum, s->block, s->fb, s->alg, s->accon,
			s->ext_en, s->ext_out, s->ch_level[0], s->ch_level[1], s->ch_level[2], s->ch_level[3]);
		if (kind[0] == 'p')
			fprintf(chip->dump, " st%u end%u lp%u al%d fs%d b%d sn%d sb%d",
				s->pcm_start, s->pcm_end, s->pcm_loop, s->pcm_altloop, s->pcm_fs, s->pcm_12bit ? 12 : 8,
				s->pcm_srcnote, s->pcm_srcb);
	}
	fputc('\n', chip->dump);
}
#endif

/* write one decoded function register to one slot */
static void opx_write_slot_reg(OPX_CHIP *chip, int slotnum, int reg, UINT8 data)
{
	OPX_SLOT *s = &chip->slots[slotnum];

	switch (reg)
	{
	case 0x0:
		s->ext_en = (data >> 7) & 1;
		s->ext_out = (data >> 3) & 0xF;
		s->kon = data & 1;
		/* Every KON=1 write (re)triggers the slot: P-47 Aces writes KON=1 onto
		   already keyed slots for note repeats, so edge-triggering would drop notes. */
		if (data & 1)
			opx_slot_keyon(chip, slotnum);
		else
			opx_slot_keyoff(chip, slotnum);
		break;
	case 0x1:
		s->lfo_freq = data;
		break;
	case 0x2:
		s->lfo_wave = data & 3;
		s->pms = (data >> 3) & 7;
		s->ams = (data >> 6) & 3;
		break;
	case 0x3:
		s->mul = data & 0xF;
		s->dt = (data >> 4) & 7;
		break;
	case 0x4:
		s->tl = data & 0x7F;
		break;
	case 0x5:
		s->ar = data & 0x1F;
		s->ks = (data >> 5) & 7;
		break;
	case 0x6:
		s->d1r = data & 0x1F;
		break;
	case 0x7:
		s->d2r = data & 0x1F;
		break;
	case 0x8:
		s->rr = data & 0xF;
		s->d1l = (data >> 4) & 0xF;
		break;
	case 0x9:
		/* F-number low: commits the latched Block / F-number high nibble
		   (manual: Block and F-Number2 must be written before F-Number1) */
		s->fnum = ((s->fnum_latch & 0x0F) << 8) | data;
		s->block = s->fnum_latch >> 4;
		s->block_s = (INT8)((s->block ^ 8) - 8);
		opx_update_keycode(s, slotnum);
		break;
	case 0xA:
		s->fnum_latch = data;
		break;
	case 0xB:
		s->wave = data & 7;
		s->fb = (data >> 4) & 7;
		s->accon = (data >> 7) & 1;
		opx_update_keycode(s, slotnum);
		break;
	case 0xC:
		s->alg = data & 0xF;
		chip->groups[slotnum % 12].dirty = 1;
		break;
	case 0xD:
		s->ch_level[0] = data >> 4;
		s->ch_level[1] = data & 0xF;
		break;
	case 0xE:
		s->ch_level[2] = data >> 4;
		s->ch_level[3] = data & 0xF;
		break;
	default:
		break;
	}
}

/* FM function register write: bank 0..3 (S1..S4), address = reg<<4 | group nibble */
static void opx_write_fm(OPX_CHIP *chip, int bank, UINT8 address, UINT8 data)
{
	int group = fm_tab[address & 0xF];
	int reg = address >> 4;
	int broadcast;

	if (group < 0)
	{
		emu_logf(&chip->logger, DEVLOG_DEBUG, "write to invalid FM group nibble %02X (bank %d, data %02X)\n", address, bank, data);
		return;
	}

	/* Registers managed by the key-on sync mode: EN/EXT out/KON, F-Number,
	   Block, Algorithm, CH0-CH3 level.  Written to the key-on slot they are
	   copied to all slots of the voice. */
	switch (reg)
	{
	case 0x0: case 0x9: case 0xA: case 0xC: case 0xD: case 0xE:
		broadcast = 1;
		break;
	default:
		broadcast = 0;
		break;
	}

	if (broadcast && opx_is_keyon_slot(chip, bank, group) && chip->groups[group].sync != SYNC_PCM)
	{
		int slots[4], n, i;
		n = opx_voice_slots(chip, bank, group, slots);
		for (i = 0; i < n; i++)
			opx_write_slot_reg(chip, slots[i], reg, data);
	}
	else
	{
		opx_write_slot_reg(chip, 12 * bank + group, reg, data);
	}

#ifdef YMF271_OPX_DEBUG
	if (reg == 0x0 && (data & 1))
		opx_dump_keyon(chip, bank, group);
#endif
}

/* PCM attribute register write (bank 5) */
static void opx_write_pcm(OPX_CHIP *chip, UINT8 address, UINT8 data)
{
	int slotnum = pcm_tab[address & 0xF];
	OPX_SLOT *s;

	if (slotnum < 0)
	{
		emu_logf(&chip->logger, DEVLOG_DEBUG, "write to invalid PCM slot nibble %02X (data %02X)\n", address, data);
		return;
	}
	s = &chip->slots[slotnum];

	switch (address >> 4)
	{
	case 0x0: s->pcm_start = (s->pcm_start & 0xFFFF00) | data; break;
	case 0x1: s->pcm_start = (s->pcm_start & 0xFF00FF) | (data << 8); break;
	case 0x2:
		s->pcm_start = (s->pcm_start & 0x00FFFF) | ((data & 0x7F) << 16);
		s->pcm_altloop = data >> 7;
		break;
	case 0x3: s->pcm_end = (s->pcm_end & 0xFFFF00) | data; break;
	case 0x4: s->pcm_end = (s->pcm_end & 0xFF00FF) | (data << 8); break;
	case 0x5: s->pcm_end = (s->pcm_end & 0x00FFFF) | ((data & 0x7F) << 16); break;
	case 0x6: s->pcm_loop = (s->pcm_loop & 0xFFFF00) | data; break;
	case 0x7: s->pcm_loop = (s->pcm_loop & 0xFF00FF) | (data << 8); break;
	case 0x8: s->pcm_loop = (s->pcm_loop & 0x00FFFF) | ((data & 0x7F) << 16); break;
	case 0x9:
		s->pcm_fs = data & 3;
		s->pcm_12bit = (data >> 2) & 1;
		s->pcm_srcnote = (data >> 3) & 3;
		s->pcm_srcb = (data >> 5) & 7;
		opx_update_keycode(s, slotnum);
		break;
	default:
		break;
	}
}

/* utility register write (bank 6): sync, timers, external memory access, test */
static void opx_write_util(OPX_CHIP *chip, UINT8 address, UINT8 data)
{
	if ((address & 0xF0) == 0x00)
	{
		int group = fm_tab[address & 0xF];
		if (group < 0)
		{
			emu_logf(&chip->logger, DEVLOG_DEBUG, "write to invalid sync group nibble %02X (data %02X)\n", address, data);
			return;
		}
		chip->groups[group].sync = data & 3;
		chip->groups[group].pfm = data >> 7;
		chip->groups[group].dirty = 1;
		return;
	}

	switch (address)
	{
	case 0x10:	/* Timer A: the manual says 0x10 = low 8 bits, 0x11 = top 2 bits;
				   seibuspi shows it behaves like other Yamaha chips:
				   0x10 = high 8 bits, 0x11 = low 2 bits. */
		chip->timerA = (chip->timerA & 0x003) | (data << 2);
		break;
	case 0x11:
		chip->timerA = (chip->timerA & 0x3FC) | (data & 0x03);
		break;
	case 0x12:
		chip->timerB = data;
		break;
	case 0x13:
		/* bit0/1 load A/B, bit2/3 enable IRQ A/B, bit4/5 reset flag A/B.
		   A timer is (re)loaded on the rising edge of its load bit; clearing
		   the bit does not stop a running timer (only the IRQ is gated). */
		if (~chip->timer_ctrl & data & 1)
		{
			chip->timerA_cnt = 1024 - chip->timerA;
			chip->timerA_run = 1;
		}
		if (~chip->timer_ctrl & data & 2)
		{
			chip->timerB_cnt = 16 * (256 - chip->timerB);
			chip->timerB_run = 1;
		}
		if (data & 0x10)
		{
			chip->status &= ~0x01;
			chip->irqstate &= ~0x01;
		}
		if (data & 0x20)
		{
			chip->status &= ~0x02;
			chip->irqstate &= ~0x02;
		}
		chip->timer_ctrl = data;
		break;
	case 0x14:
		chip->ext_address = (chip->ext_address & 0xFFFF00) | data;
		break;
	case 0x15:
		chip->ext_address = (chip->ext_address & 0xFF00FF) | (data << 8);
		break;
	case 0x16:
		chip->ext_address = (chip->ext_address & 0x00FFFF) | ((data & 0x7F) << 16);
		chip->ext_rw = data >> 7;
		/* prime the read latch for the read-direction window */
		if (chip->ext_rw)
			chip->ext_readlatch = opx_read_memory(chip, chip->ext_address);
		break;
	case 0x17:
		/* write to external memory (SRAM); the address is incremented before
		   the write (previous core's reading; the documents do not say) */
		chip->ext_address = (chip->ext_address + 1) & 0x7FFFFF;
		if (!chip->ext_rw && chip->ext_address < chip->mem_size)
			chip->mem_base[chip->ext_address] = data;
		break;
	case 0x20: case 0x21: case 0x22:
		/* test registers */
		break;
	default:
		break;
	}
}

static void ymf271_w(void *info, UINT8 offset, UINT8 data)
{
	OPX_CHIP *chip = (OPX_CHIP *)info;

	offset &= 0xF;
	chip->regs_main[offset] = data;

	switch (offset)
	{
	case 0x1: opx_write_fm(chip, 0, chip->regs_main[0x0], data); break;
	case 0x3: opx_write_fm(chip, 1, chip->regs_main[0x2], data); break;
	case 0x5: opx_write_fm(chip, 2, chip->regs_main[0x4], data); break;
	case 0x7: opx_write_fm(chip, 3, chip->regs_main[0x6], data); break;
	case 0x9: opx_write_pcm(chip, chip->regs_main[0x8], data); break;
	case 0xD: opx_write_util(chip, chip->regs_main[0xC], data); break;
	default:
		/* even offsets: address latches; 0xB/0xF: unused banks */
		break;
	}
}

static UINT8 ymf271_r(void *info, UINT8 offset)
{
	OPX_CHIP *chip = (OPX_CHIP *)info;

	switch (offset & 0xF)
	{
	case 0x0:
		/* d0 TiA, d1 TiB, d2 0, d3 End0, d4 End12, d5 End24, d6 End36, d7 Busy
		   end_status bit i = PCM slot 4*i: slots 0,12,24,36 = bits 0,3,6,9.
		   The End flags are cleared by reading them (Brave Blade copies the
		   status registers to RAM every ~100 us and frees a PCM channel when
		   its End bit is seen; a sticky flag would kill the next note started
		   on that slot from the stale copy). */
		{
			UINT16 e = chip->end_status;
			chip->end_status &= ~0x0249;
			return (chip->status & 0x83) | ((e & 1) << 3) | (((e >> 3) & 1) << 4) | (((e >> 6) & 1) << 5) | (((e >> 9) & 1) << 6);
		}
	case 0x1:
		/* d0 End4, d1 End16, d2 End28, d3 End40, d4 End8, d5 End20, d6 End32, d7 End44
		   end_status bit i belongs to slot 4*i: slots 4,16,28,40 = bits 1,4,7,10;
		   slots 8,20,32,44 = bits 2,5,8,11 */
		{
			UINT16 e = chip->end_status;
			chip->end_status &= ~0x0DB6;
			return ((e >> 1) & 1) | (((e >> 4) & 1) << 1) | (((e >> 7) & 1) << 2) | (((e >> 10) & 1) << 3)
			     | (((e >> 2) & 1) << 4) | (((e >> 5) & 1) << 5) | (((e >> 8) & 1) << 6) | (((e >> 11) & 1) << 7);
		}
	case 0x2:
		{
			UINT8 ret;
			if (!chip->ext_rw)
				return 0xFF;
			ret = chip->ext_readlatch;
			chip->ext_address = (chip->ext_address + 1) & 0x7FFFFF;
			chip->ext_readlatch = opx_read_memory(chip, chip->ext_address);
			return ret;
		}
	default:
		break;
	}
	return 0xFF;
}


/* ------------------------------------------------------------------------ */
/*  synthesis                                                               */
/* ------------------------------------------------------------------------ */

static void opx_init_tables(void)
{
	int i;

	if (opx_tables_ready)
		return;
	for (i = 0; i < 256; i++)
	{
		opx_logsin[i] = (UINT16)floor(-log(sin((i + 0.5) * 3.14159265358979323846 / 512.0)) / log(2.0) * 256.0 + 0.5);
		opx_exp[i] = (UINT16)floor(pow(2.0, -(i + 1) / 256.0) * 2048.0 + 0.5);
	}
	opx_tables_ready = 1;
}

/* apply one algorithm table to a list of slots */
static void opx_connect(OPX_CHIP *chip, const OPX_ALG *alg, const int *slots, int n)
{
	int p, q;

	for (p = 0; p < n; p++)
	{
		OPX_SLOT *s = &chip->slots[slots[p]];
		s->c_nmod = 0;
		for (q = 0; q < n; q++)
			if (alg->mods[p] & (1 << q))
				s->c_mod[s->c_nmod++] = (UINT8)slots[q];
		s->c_carrier = (alg->car >> p) & 1;
		/* the head slot takes the feedback input; its history is written by
		   position fbsrc (itself, or S3 for the loop algorithms) */
		s->c_fbhead = (p == 0);
		s->c_fbtarget = (p == alg->fbsrc) ? (INT8)slots[0] : -1;
	}
}

static void opx_rebuild_group(OPX_CHIP *chip, int g)
{
	int slots[4];

	chip->groups[g].dirty = 0;
	switch (chip->groups[g].sync)
	{
	case SYNC_4OP:
		slots[0] = g; slots[1] = g + 12; slots[2] = g + 24; slots[3] = g + 36;
		opx_connect(chip, &alg4[chip->slots[g].alg & 15], slots, 4);
		break;
	case SYNC_2X2OP:
		slots[0] = g; slots[1] = g + 24;
		opx_connect(chip, &alg2[chip->slots[g].alg & 3], slots, 2);
		slots[0] = g + 12; slots[1] = g + 36;
		opx_connect(chip, &alg2[chip->slots[g + 12].alg & 3], slots, 2);
		break;
	case SYNC_3OP_PCM:
		slots[0] = g; slots[1] = g + 12; slots[2] = g + 24;
		opx_connect(chip, &alg3[chip->slots[g].alg & 7], slots, 3);
		slots[0] = g + 36;
		opx_connect(chip, &alg_single, slots, 1);
		break;
	default:
		slots[0] = g;      opx_connect(chip, &alg_single, slots, 1);
		slots[0] = g + 12; opx_connect(chip, &alg_single, slots, 1);
		slots[0] = g + 24; opx_connect(chip, &alg_single, slots, 1);
		slots[0] = g + 36; opx_connect(chip, &alg_single, slots, 1);
		break;
	}
}

/* one EG clock (fs/2) for one slot */
static void opx_eg_tick(OPX_CHIP *chip, OPX_SLOT *s)
{
	int rks, rate, shift, idx, inc;

	if (s->eg_state == EG_OFF)
		return;

	/* state transitions checked first (OPM) */
	if (s->eg_state == EG_ATTACK && s->eg_att <= 0)
	{
		s->eg_att = 0;
		s->eg_state = EG_DECAY1;
	}
	if (s->eg_state == EG_DECAY1)
	{
		int d1l = (s->d1l == 15) ? (31 << 5) : (s->d1l << 5);
		if (s->eg_att >= d1l)
			s->eg_state = EG_DECAY2;
	}

	rks = rks_tab[s->keycode][s->ks];
	switch (s->eg_state)
	{
	case EG_ATTACK:  rate = opx_eg_rate(s->ar * 2, rks); break;
	case EG_DECAY1:  rate = opx_eg_rate(s->d1r * 2, rks); break;
	case EG_DECAY2:  rate = opx_eg_rate(s->d2r * 2, rks); break;
	default:         rate = opx_eg_rate(s->rr * 4, rks); break;
	}

	if (rate < 48)
	{
		shift = 11 - (rate >> 2);
		if (chip->eg_cnt & ((1 << shift) - 1))
			return;
		idx = (chip->eg_cnt >> shift) & 7;
	}
	else
	{
		idx = chip->eg_cnt & 7;
	}
	inc = (eg_inc[rate] >> (idx * 4)) & 15;
	if (inc == 0)
		return;

	if (s->eg_state == EG_ATTACK)
	{
		s->eg_att += ((~s->eg_att) * inc) >> 4;
		if (s->eg_att <= 0)
		{
			s->eg_att = 0;
			s->eg_state = EG_DECAY1;
		}
	}
	else
	{
		s->eg_att += inc;
		if (s->eg_att >= 0x3FF)
		{
			s->eg_att = 0x3FF;
			if (s->eg_state == EG_RELEASE)
				s->eg_state = EG_OFF;
		}
	}
}

/* LFO clock divider (manual table 2-6-2): the LFO advances one of 128 steps
   every K samples, K = (32 - (n & 15)) << (14 - (n >> 4)) for n < 240 and
   K = 16 - (n & 15) for n >= 240  ->  f = fs / 128 / K (0.00066 .. 344.5 Hz). */
INLINE UINT32 opx_lfo_period(UINT8 n)
{
	if (n >= 240)
		return 16 - (n & 15);
	return (32 - (n & 15)) << (14 - (n >> 4));
}

/* advance the LFO of one slot by one sample */
INLINE void opx_lfo_tick(OPX_SLOT *s)
{
	if (s->lfo_wave == 0)
		return;
	if (++s->lfo_cnt >= opx_lfo_period(s->lfo_freq))
	{
		s->lfo_cnt = 0;
		s->lfo_pos = (s->lfo_pos + 1) & 127;
	}
}

/* bipolar LFO value for PM, -128..127 (wave 1 saw, 2 square, 3 triangle),
   all starting at 0 at key-on and rising first */
INLINE INT32 opx_lfo_pm(const OPX_SLOT *s)
{
	INT32 p = s->lfo_pos;
	switch (s->lfo_wave)
	{
	case 1:	return ((p + 64) & 127) * 2 - 128;			/* saw: 0 -> +126, -128 -> -2 */
	case 2:	return (p < 64) ? 127 : -128;				/* square */
	case 3:	/* triangle: 0 -> +124 -> 0 -> -128 -> -4 */
		if (p < 32) return p * 4;
		if (p < 96) return 128 - (p - 32) * 4;
		return (p - 96) * 4 - 128;
	default: return 0;
	}
}

/* unipolar LFO value for AM (attenuation), 0..127.  Every waveform starts
   at full attenuation at key-on and the saw / triangle come down from there
   (the OPM/OPZ convention, ymfm's reading of the manual figures) */
INLINE INT32 opx_lfo_am(const OPX_SLOT *s)
{
	INT32 p = s->lfo_pos;
	switch (s->lfo_wave)
	{
	case 1:	return 127 - p;											/* saw: 127 -> 0 */
	case 2:	return (p < 64) ? 127 : 0;								/* square: attenuated half first */
	case 3:	return (p < 64) ? (127 - p * 2) : (p - 64) * 2 + 1;		/* triangle 127 -> 1 -> 127 */
	default: return 0;
	}
}

/* phase increment per sample, 2^32 = one cycle:
   f = 2 * fnum * 2^(block-7) * MUL * fs / 2^15  ->  inc = fnum << (block + 11) * MUL,
   detune added in units of fs/2^20 (= 1 << 12 here) before the multiplier (OPM) */
INLINE UINT32 opx_phase_inc(const OPX_SLOT *s, INT32 lfo_pm)
{
	INT64 inc;
	int sh = s->block_s + 11;	/* 3..18 */
	int dt;
	INT64 fnum = s->fnum << 7;	/* fnum with 7 fraction bits for the LFO */

	/* LFO PM: fnum * (1 + k * lfo / (1024 * 128)) */
	if (lfo_pm != 0)
		fnum += ((INT64)s->fnum * pms_k[s->pms] * lfo_pm) >> 10;
	inc = (fnum << sh) >> 7;
	dt = detune_tab[s->keycode][s->dt & 3] << 12;
	if (s->dt & 4)
		inc -= dt;
	else
		inc += dt;
	if (inc < 0)
		inc = 0;
	if (s->mul == 0)
		inc >>= 1;
	else
		inc *= s->mul;
	/* the accumulator wraps like OPM's: increments above fs/2 alias (the
	   hi-hat carriers of the Seibu titles are fed by such a modulator) */
	return (UINT32)inc;
}

/* ---- external PCM waveform (wave 7 on slots 0,4,..,44) ----
   Step in source samples per frame (16-bit fraction):
     (fnum | 0x800) / 2048 * 2^block * MUL * {1, 1/2, 1/4, 1/8}[Fs]
   i.e. the same PG as FM with the implicit F-number bit 11 (block 0 /
   fnum 0 / MUL 1 plays a 44.1 kHz sample at its original rate), detune and
   LFO PM applied like FM.  Positions run over [0, End): when the position
   reaches End it wraps back by End-Loop (a looped sample's period is then
   exactly End-Loop words), so word End is only read as the interpolation
   partner of End-1 -- which is why the manual requires Words >= End+1.
   Samples are 8-bit or packed 12-bit (3 bytes per 2 words, see opx_pcm_word)
   and linearly interpolated ("the waveform data is interpolated and the EG
   value multiplied"). */
INLINE UINT32 opx_pcm_step(const OPX_SLOT *s, INT32 lfo_pm)
{
	INT64 inc;
	INT64 fnum = ((s->fnum & 0x7FF) | 0x800) << 7;	/* 7 fraction bits for the LFO */
	int dt;

	if (lfo_pm != 0)
		fnum += ((INT64)((s->fnum & 0x7FF) | 0x800) * pms_k[s->pms] * lfo_pm) >> 10;
	/* fnum(q7) * 2^(block-11) * 65536 = fnum(q7) << (block + 5) >> 7 */
	inc = (fnum << 16) >> (18 - s->block_s);
	dt = detune_tab[s->keycode][s->dt & 3] << 6;	/* FM units (1 << 12) / 2^6 */
	if (s->dt & 4)
		inc -= dt;
	else
		inc += dt;
	if (inc < 0)
		inc = 0;
	if (s->mul == 0)
		inc >>= 1;
	else
		inc *= s->mul;
	inc >>= s->pcm_fs;
	return (UINT32)inc;
}

/* 12-bit sample word at index pos of slot s (8-bit data = upper byte) */
INLINE INT32 opx_pcm_word(OPX_CHIP *chip, const OPX_SLOT *s, UINT32 pos)
{
	UINT32 addr;
	INT32 v;

	if (!s->pcm_12bit)
	{
		v = (INT8)opx_read_memory(chip, s->pcm_start + pos) << 4;
	}
	else
	{
		/* 3 bytes per 2 words: b0 = word0 bits 11..4, b1 = word1 bits 3..0 (high
		   nibble) : word0 bits 3..0 (low nibble), b2 = word1 bits 11..4.
		   (Checked on the Bloody Roar 2 / Beastorizer sample data: this order
		   makes the smooth samples as much smoother than the 8-bit-only decode
		   as true LSBs should, the swapped order makes them rougher than
		   dropping the nibble.) */
		addr = s->pcm_start + (pos >> 1) * 3;
		if (pos & 1)
			v = (opx_read_memory(chip, addr + 2) << 4) | (opx_read_memory(chip, addr + 1) >> 4);
		else
			v = (opx_read_memory(chip, addr) << 4) | (opx_read_memory(chip, addr + 1) & 0x0F);
		v = (INT32)((v & 0xFFF) ^ 0x800) - 0x800;
	}
	return v;
}

/* interpolated 14-bit sample at the current position, then advance */
INLINE INT32 opx_pcm_sample(OPX_CHIP *chip, OPX_SLOT *s, int slotnum, INT32 lfo_pm)
{
	INT32 a = opx_pcm_word(chip, s, s->pcm_pos);
	INT32 b = opx_pcm_word(chip, s, s->pcm_pos + 1);
	INT32 frac = (INT32)(s->pcm_frac >> 8);	/* 8-bit interpolation weight */
	INT32 out = (a * (256 - frac) + b * frac) >> 6;	/* 12 bit -> 14 bit */
	UINT32 step = opx_pcm_step(s, lfo_pm);

	s->pcm_frac += step;
	s->pcm_pos += s->pcm_frac >> 16;
	s->pcm_frac &= 0xFFFF;
	if (s->pcm_pos >= s->pcm_end)
	{
		if (s->pcm_end > s->pcm_loop)
		{
			UINT32 len = s->pcm_end - s->pcm_loop;
			s->pcm_pos = s->pcm_loop + (s->pcm_pos - s->pcm_end) % len;
		}
		else
			s->pcm_pos = s->pcm_loop;	/* degenerate loop: hold */
		/* The End flag is raised once per key-on, when the read address first
		   passes the end address.  Drivers play one-shot samples as a short
		   loop of silence at the end (loop = end - 2) and free the channel from
		   a copy of the status register: re-raising End on every pass of that
		   loop kills a note re-triggered on the same slot between the copy
		   and the free pass (Bloody Roar 2 / Brave Blade lose drum hits). */
		if (!s->pcm_ended)
		{
			s->pcm_ended = 1;
			chip->end_status |= 1 << (slotnum >> 2);
		}
	}
	return out;
}

/* multiply a 14-bit sample by the envelope (10-bit attenuation, 64 = 6 dB) */
INLINE INT32 opx_env_mul(INT32 v, UINT32 env)
{
	return (v * (INT32)opx_exp[(env & 63) << 2]) >> (11 + (env >> 6));
}

/* operator: 10-bit phase, waveform, 10-bit total attenuation -> 14-bit output */
INLINE INT32 opx_op(UINT32 phase, int wave, UINT32 env)
{
	UINT32 p = phase & 1023;
	UINT32 idx, att, neg;
	INT32 out;

	idx = p & 255;
	if (p & 256)
		idx ^= 255;
	neg = 0;
	switch (wave)
	{
	case 0:	/* sine */
		att = opx_logsin[idx];
		neg = p & 512;
		break;
	case 1:	/* +/-sin^2 (manual plot): twice the log-sin attenuation, sign from
			   phase bit 9 -- as ymfm's OPZ wave 1 */
		att = opx_logsin[idx] << 1;
		neg = p & 512;
		break;
	case 2:	/* |sin| */
		att = opx_logsin[idx];
		break;
	case 3:	/* half sine */
		if (p & 512)
			return 0;
		att = opx_logsin[idx];
		break;
	case 4:	/* sin(2wt) on the first half */
	case 5:	/* |sin(2wt)| on the first half */
		if (p & 512)
			return 0;
		idx = (p << 1) & 255;
		if (p & 128)
			idx ^= 255;
		att = opx_logsin[idx];
		if (wave == 4)
			neg = p & 256;
		break;
	default:	/* 6: linear waveform, 7: external PCM -- both handled in ymf271_update */
		return 0;
	}

	att += env << 2;
	if (att >= 4096)
		return 0;
	out = (opx_exp[att & 255] << 2) >> (att >> 8);
	return neg ? -out : out;
}

/* channel level table: x(1 or 0.75) >> (L>>1), L >= 13 mute */
INLINE INT32 opx_pan(INT32 v, UINT8 level)
{
	if (level >= 13)
		return 0;
	if (level & 1)
		v = (v * 3) >> 2;
	return v >> (level >> 1);
}

/* timers, clocked once per sample (384 master clocks) */
static void opx_timers_tick(OPX_CHIP *chip)
{
	if (chip->timerA_run && --chip->timerA_cnt <= 0)
	{
		chip->timerA_cnt += 1024 - chip->timerA;
		chip->status |= 0x01;
		if (chip->timer_ctrl & 0x04)
			chip->irqstate |= 0x01;
	}
	if (chip->timerB_run && --chip->timerB_cnt <= 0)
	{
		chip->timerB_cnt += 16 * (256 - chip->timerB);
		chip->status |= 0x02;
		if (chip->timer_ctrl & 0x08)
			chip->irqstate |= 0x02;
	}
}

static void ymf271_update(void *info, UINT32 samples, DEV_SMPL** outputs)
{
	OPX_CHIP *chip = (OPX_CHIP *)info;
	UINT32 i;
	int n, g;

	for (i = 0; i < samples; i++)
	{
		INT32 acc[2] = { 0, 0 };
		int eg_clock;

		for (g = 0; g < OPX_GROUPS; g++)
			if (chip->groups[g].dirty)
				opx_rebuild_group(chip, g);

		/* EG runs at fs/2 */
		eg_clock = chip->eg_phase;
		chip->eg_phase ^= 1;
		if (eg_clock)
			chip->eg_cnt++;

		/* slots in hardware order: modulators with a lower slot number are
		   taken from the current frame, higher-numbered ones from the previous */
		for (n = 0; n < OPX_SLOTS; n++)
		{
			OPX_SLOT *s = &chip->slots[n];
			INT32 mod, out, pm;
			UINT32 env;

			if (eg_clock)
				opx_eg_tick(chip, s);
			opx_lfo_tick(s);

			if (s->eg_state == EG_OFF)
			{
				s->out = 0;
				s->acc = 0;
				continue;
			}

			/* modulation input, in 10-bit phase units */
			if (s->c_fbhead)
			{
				/* OPM feedback law on the average of the last two outputs; the
				   S3->S1 loop of algorithms 1/5/7/11 follows the same law */
				int fb = s->fb & 7;
				mod = fb ? ((s->fb_hist[0] + s->fb_hist[1]) >> (10 - fb)) : 0;
			}
			else
			{
				int k;
				INT32 sum = 0;
				for (k = 0; k < s->c_nmod; k++)
					sum += chip->slots[s->c_mod[k]].out;
				mod = (sum * modlevel[s->fb]) >> 8;
			}

			env = (UINT32)s->eg_att + ((UINT32)s->tl << 3);
			if (s->ams && s->lfo_wave)
			{
				/* AMS max 63 / 126 / 252 units of 0.09375 dB */
				INT32 am = opx_lfo_am(s);
				env += (s->ams == 1) ? (am >> 1) : (s->ams == 2) ? am : (am << 1);
			}
			if (env > 0x3FF)
				env = 0x3FF;

			pm = (s->pms && s->lfo_wave) ? opx_lfo_pm(s) : 0;

			if (s->wave == 7)
			{
				/* external PCM data: only the slots of groups 0/4/8 can fetch it;
				   a wave-7 select elsewhere produces silence here */
				out = ((n & 3) == 0) ? opx_env_mul(opx_pcm_sample(chip, s, n, pm), env) : 0;
			}
			else if (s->wave == 6)
			{
				/* "linear waveform table": the output does not depend on the
				   phase.  It is a DC level of half scale (drum pitch sweeps use it
				   as a modulator) plus the modulation input passed through as a
				   ramp: the input is scaled by MUL (0 = 1/2) like a phase
				   increment, wraps at 9 bits and is stretched to 15 bits (a
				   hi-hat carrier emits its modulator directly, at up to 2.5x
				   the range of the other waveforms).  The manual only states "1"
				   (D.C.) for this waveform. */
				INT32 mm = s->mul ? mod * (INT32)s->mul : (mod >> 1);
				INT32 lin = 8192 + ((mm << 6) & 32767);
				out = opx_env_mul(lin, env);
				s->phase += opx_phase_inc(s, pm);	/* the PG keeps running */
			}
			else
			{
				out = opx_op((s->phase >> 22) + mod, s->wave, env);
				s->phase += opx_phase_inc(s, pm);
			}
			if (s->accon)
			{
				/* "Acc On": the slot output is accumulated instead of output
				   directly -- modelled as a running sum that saturates at the
				   14-bit operator range.  Any sustained tone rails the sum, so
				   the slot turns into a full-level square-like signal that
				   flips at the operator's zero crossings ("distorted" basses
				   and drums); the sum is cleared at key-on and when the EG
				   reaches off.  Applies to modulators as well. */
				s->acc += out;
				if (s->acc > 8191) s->acc = 8191;
				else if (s->acc < -8192) s->acc = -8192;
				out = s->acc;
			}
			s->out = out;

			if (s->c_fbtarget >= 0)
			{
				OPX_SLOT *t = &chip->slots[s->c_fbtarget];
				t->fb_hist[1] = t->fb_hist[0];
				t->fb_hist[0] = out;
			}

			if (!s->c_carrier || chip->groups[n % 12].muted)
				continue;
#ifdef YMF271_OPX_DEBUG
			if (chip->solo_mask && !((chip->solo_mask >> (n % 12)) & 1))
				continue;
			if ((chip->mute_slots >> n) & 1)
				continue;
#endif
			acc[0] += opx_pan(out, s->ch_level[0]);
			acc[1] += opx_pan(out, s->ch_level[1]);
		}

		/* one carrier at full level = 14-bit +/-8192 */
		outputs[0][i] = acc[0];
		outputs[1][i] = acc[1];
		opx_timers_tick(chip);
#ifdef YMF271_OPX_DEBUG
		chip->sample_count++;
#endif
	}
}


/* ------------------------------------------------------------------------ */
/*  device lifecycle                                                        */
/* ------------------------------------------------------------------------ */

#ifdef YMF271_OPX_DEBUG
#define OPX_DBG_MAXCHIPS	4
static void *opx_dbg_chips[OPX_DBG_MAXCHIPS];

/* parse a comma separated list of numbers into a bit mask */
static UINT64 opx_parse_mask(const char *e, int limit)
{
	UINT64 mask = 0;
	while (e != NULL && *e != '\0')
	{
		char *end;
		long v = strtol(e, &end, 10);
		if (end == e)
			break;
		if (v >= 0 && v < limit)
			mask |= (UINT64)1 << v;
		e = (*end == ',') ? end + 1 : end;
	}
	return mask;
}

static void opx_dbg_start(OPX_CHIP *chip)
{
	const char *e;
	int i;

	e = getenv("OPX_DUMP");
	if (e != NULL && e[0] != '\0')
		chip->dump = fopen(e, "w");
	chip->solo_mask = (UINT16)opx_parse_mask(getenv("OPX_SOLO"), OPX_GROUPS);
	chip->mute_slots = opx_parse_mask(getenv("OPX_MUTESLOTS"), OPX_SLOTS);

	for (i = 0; i < OPX_DBG_MAXCHIPS; i++)
	{
		if (opx_dbg_chips[i] == NULL)
		{
			opx_dbg_chips[i] = chip;
			break;
		}
	}
}

static void opx_dbg_stop(OPX_CHIP *chip)
{
	int i;

	for (i = 0; i < OPX_DBG_MAXCHIPS; i++)
		if (opx_dbg_chips[i] == chip)
			opx_dbg_chips[i] = NULL;
	if (chip->dump != NULL)
		fclose(chip->dump);
}
#endif

static UINT8 device_start_ymf271(const DEV_GEN_CFG* cfg, DEV_INFO* retDevInf)
{
	OPX_CHIP *chip;

	chip = (OPX_CHIP *)calloc(1, sizeof(OPX_CHIP));
	if (chip == NULL)
		return 0xFF;

	chip->clock = cfg->clock;
	chip->rate = chip->clock / 384;
	chip->mem_base = NULL;
	chip->mem_size = 0;

	opx_init_tables();
#ifdef YMF271_OPX_DEBUG
	opx_dbg_start(chip);
#endif

	ymf271_set_mute_mask(chip, 0x000);

	chip->_devData.chipInf = chip;
	INIT_DEVINF(retDevInf, &chip->_devData, chip->rate, &devDef);
	return 0x00;
}

static void device_stop_ymf271(void *info)
{
	OPX_CHIP *chip = (OPX_CHIP *)info;

#ifdef YMF271_OPX_DEBUG
	opx_dbg_stop(chip);
#endif
	free(chip->mem_base);
	free(chip);
}

static void device_reset_ymf271(void *info)
{
	OPX_CHIP *chip = (OPX_CHIP *)info;
	int i;

	for (i = 0; i < OPX_SLOTS; i++)
	{
		memset(&chip->slots[i], 0, sizeof(OPX_SLOT));
		chip->slots[i].eg_state = EG_OFF;
		chip->slots[i].eg_att = 0x3FF;
		chip->slots[i].c_fbtarget = -1;
	}
	for (i = 0; i < OPX_GROUPS; i++)
	{
		chip->groups[i].sync = 0;
		chip->groups[i].pfm = 0;
		chip->groups[i].dirty = 1;
	}
	chip->eg_cnt = 0;
	chip->eg_phase = 0;
	memset(chip->regs_main, 0, sizeof(chip->regs_main));

	/* reset timers and IRQ */
	chip->timerA = 0;
	chip->timerB = 0;
	chip->timer_ctrl = 0;
	chip->timerA_run = 0;
	chip->timerB_run = 0;
	chip->timerA_cnt = 0;
	chip->timerB_cnt = 0;
	chip->status = 0;
	chip->end_status = 0;
	chip->irqstate = 0;
	chip->ext_address = 0;
	chip->ext_rw = 0;
	chip->ext_readlatch = 0;
#ifdef YMF271_OPX_DEBUG
	chip->sample_count = 0;
#endif
}

static void ymf271_alloc_rom(void* info, UINT32 memsize)
{
	OPX_CHIP *chip = (OPX_CHIP *)info;

	if (chip->mem_size == memsize)
		return;
	chip->mem_base = (UINT8*)realloc(chip->mem_base, memsize);
	chip->mem_size = memsize;
	memset(chip->mem_base, 0xFF, memsize);
}

static void ymf271_write_rom(void *info, UINT32 offset, UINT32 length, const UINT8* data)
{
	OPX_CHIP *chip = (OPX_CHIP *)info;

	if (offset > chip->mem_size)
		return;
	if (offset + length > chip->mem_size)
		length = chip->mem_size - offset;
	memcpy(chip->mem_base + offset, data, length);
}

static void ymf271_set_mute_mask(void *info, UINT32 MuteMask)
{
	OPX_CHIP *chip = (OPX_CHIP *)info;
	UINT8 i;

	for (i = 0; i < OPX_GROUPS; i++)
		chip->groups[i].muted = (MuteMask >> i) & 0x01;
}

static void ymf271_set_log_cb(void *info, DEVCB_LOG func, void* param)
{
	OPX_CHIP *chip = (OPX_CHIP *)info;
	dev_logger_set(&chip->logger, chip, func, param);
}


/* ------------------------------------------------------------------------ */
/*  debugging interface (ymf271_opx.h)                                      */
/* ------------------------------------------------------------------------ */

#ifdef YMF271_OPX_DEBUG
void *opx_dbg_chip(int idx)
{
	int i, n = 0;
	for (i = 0; i < OPX_DBG_MAXCHIPS; i++)
	{
		if (opx_dbg_chips[i] == NULL)
			continue;
		if (n == idx)
			return opx_dbg_chips[i];
		n++;
	}
	return NULL;
}

int opx_dbg_snapshot(const void *info, OPX_DBG_STATE *out)
{
	const OPX_CHIP *chip = (const OPX_CHIP *)info;
	int i;

	if (chip == NULL || out == NULL)
		return 0;
	memset(out, 0, sizeof(*out));
	for (i = 0; i < OPX_SLOTS; i++)
	{
		const OPX_SLOT *s = &chip->slots[i];
		OPX_DBG_SLOT *d = &out->slots[i];
		d->kon = s->kon;  d->ext_en = s->ext_en;  d->ext_out = s->ext_out;
		d->lfo_freq = s->lfo_freq;  d->ams = s->ams;  d->pms = s->pms;  d->lfo_wave = s->lfo_wave;
		d->dt = s->dt;  d->mul = s->mul;  d->tl = s->tl;  d->ks = s->ks;  d->ar = s->ar;
		d->d1r = s->d1r;  d->d2r = s->d2r;  d->d1l = s->d1l;  d->rr = s->rr;
		d->fnum = s->fnum;  d->block = s->block_s;
		d->accon = s->accon;  d->fb = s->fb;  d->wave = s->wave;  d->alg = s->alg;
		memcpy(d->ch_level, s->ch_level, 4);
		d->pcm_start = s->pcm_start;  d->pcm_end = s->pcm_end;  d->pcm_loop = s->pcm_loop;
		d->pcm_altloop = s->pcm_altloop;  d->pcm_fs = s->pcm_fs;  d->pcm_12bit = s->pcm_12bit;
		d->pcm_srcnote = s->pcm_srcnote;  d->pcm_srcb = s->pcm_srcb;
		d->keycode = s->keycode;  d->eg_state = s->eg_state;  d->eg_att = s->eg_att;
		d->phase = s->phase;  d->out = s->out;  d->lfo_pos = s->lfo_pos;  d->pcm_pos = s->pcm_pos;
		d->c_carrier = s->c_carrier;  d->c_nmod = s->c_nmod;  d->c_fbhead = s->c_fbhead;
		memcpy(d->c_mod, s->c_mod, 3);  d->c_fbtarget = s->c_fbtarget;
	}
	for (i = 0; i < OPX_GROUPS; i++)
	{
		out->groups[i].sync = chip->groups[i].sync;
		out->groups[i].pfm = chip->groups[i].pfm;
		out->groups[i].muted = chip->groups[i].muted;
	}
	out->timerA = chip->timerA;  out->timerB = chip->timerB;
	out->timer_ctrl = chip->timer_ctrl;  out->status = chip->status;
	out->irqstate = chip->irqstate;  out->end_status = chip->end_status;
	out->ext_address = chip->ext_address;
	out->clock = chip->clock;  out->rate = chip->rate;  out->mem_size = chip->mem_size;
	out->sample_count = chip->sample_count;
	out->solo_mask = chip->solo_mask;  out->mute_slots = chip->mute_slots;
	return 1;
}

void opx_dbg_set_solo(void *info, UINT16 group_mask)
{
	OPX_CHIP *chip = (OPX_CHIP *)info;
	if (chip != NULL)
		chip->solo_mask = group_mask & 0x0FFF;
}

void opx_dbg_set_mute_slots(void *info, UINT64 slot_mask)
{
	OPX_CHIP *chip = (OPX_CHIP *)info;
	if (chip != NULL)
		chip->mute_slots = slot_mask;
}
#endif	/* YMF271_OPX_DEBUG */
