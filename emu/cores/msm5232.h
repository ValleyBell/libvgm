#ifndef __MSM5232_H__
#define __MSM5232_H__

#include "../EmuStructs.h"

typedef struct {
	DEV_GEN_CFG _genCfg;
	double capacitors[8]; // external capacitor values for each channel (in Farads)
} MSM5232_CFG;

extern const DEV_DECL sndDev_MSM5232;

#endif  // __MSM5232_H__