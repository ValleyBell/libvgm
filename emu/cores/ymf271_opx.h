/* YMF271 (OPX) core: debugging interface, compiled in with YMF271_OPX_DEBUG.

   Not part of the libvgm device API.  Everything here reads the state of a
   running chip or mutes parts of it; callers must serialise these calls
   with the emulation (hold the same lock as the audio thread). */
#ifndef __YMF271_OPX_H__
#define __YMF271_OPX_H__

#include "../../stdtype.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OPX_DBG_SLOTS	48
#define OPX_DBG_GROUPS	12

/* Snapshot of one slot: decoded function registers + runtime state. */
typedef struct
{
	/* function registers (decoded) */
	UINT8 kon, ext_en, ext_out;
	UINT8 lfo_freq, ams, pms, lfo_wave;
	UINT8 dt, mul, tl, ks, ar, d1r, d2r, d1l, rr;
	UINT16 fnum;
	INT8 block;
	UINT8 accon, fb, wave, alg;
	UINT8 ch_level[4];
	/* PCM attributes (meaningful on slots 0,4,..,44 in PCM sync modes) */
	UINT32 pcm_start, pcm_end, pcm_loop;
	UINT8 pcm_altloop, pcm_fs, pcm_12bit, pcm_srcnote, pcm_srcb;
	/* runtime */
	UINT8 keycode;
	UINT8 eg_state;		/* 0 attack, 1 decay1, 2 decay2, 3 release, 4 off */
	INT32 eg_att;		/* 10-bit attenuation, 0.09375 dB units */
	UINT32 phase;
	INT32 out;			/* last 14-bit operator output */
	UINT8 lfo_pos;
	UINT32 pcm_pos;
	/* connection cache */
	UINT8 c_carrier, c_nmod, c_fbhead;
	UINT8 c_mod[3];
	INT8 c_fbtarget;
} OPX_DBG_SLOT;

typedef struct
{
	UINT8 sync, pfm, muted;
} OPX_DBG_GROUP;

typedef struct
{
	OPX_DBG_SLOT slots[OPX_DBG_SLOTS];
	OPX_DBG_GROUP groups[OPX_DBG_GROUPS];
	UINT16 timerA;
	UINT8 timerB, timer_ctrl, status, irqstate;
	UINT16 end_status;
	UINT32 ext_address;
	UINT32 clock, rate, mem_size;
	UINT32 sample_count;
	UINT16 solo_mask;
	UINT64 mute_slots;
} OPX_DBG_STATE;

/* Chip instance registry: chips register themselves on start and unregister
   on stop.  idx 0 = first started instance still alive.  Never cache the
   pointer across device start/stop. */
void *opx_dbg_chip(int idx);
int   opx_dbg_snapshot(const void *chip, OPX_DBG_STATE *out);	/* 1 = ok */
void  opx_dbg_set_solo(void *chip, UINT16 group_mask);			/* 0 = all audible */
void  opx_dbg_set_mute_slots(void *chip, UINT64 slot_mask);

#ifdef __cplusplus
}
#endif

#endif	/* __YMF271_OPX_H__ */
