// Configuration flags (bit 31 for MSM6585)
#define MSM5205_S1	0
#define MSM5205_S2	1

#define MSM5205_ADPCM_3B	2
#define MSM5205_ADPCM_4B	0

typedef struct msm5205_config
{
	DEV_GEN_CFG _genCfg;

	UINT8 prescaler;	// prescaler, Bit 0 = S1, Bit 1 = S2
	UINT8 adpcmBits;	// bits per ADPCM sample (3, 4), 0 = default (3)
} MSM5205_CFG;

extern const DEV_DECL sndDev_MSM5205;

#endif  // __MSM5205_H__
