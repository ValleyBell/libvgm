#ifndef __MSM5205_H__
#define __MSM5205_H__
#include "../EmuStructs.h"

// cfg.flags: 0 = MSM5205, 1 = MSM6585

#define MSM5205_ADPCM_3B	3
#define MSM5205_ADPCM_4B	4

typedef struct msm5205_config
{
	DEV_GEN_CFG _genCfg;

	UINT8 prescaler;	// prescaler, bit 0 = S1, bit 1 = S2
	UINT8 adpcmBits;	// bits per ADPCM sample (3, 4), 0 = default (4)
} MSM5205_CFG;

extern const DEV_DECL sndDev_MSM5205;

#endif  // __MSM5205_H__
