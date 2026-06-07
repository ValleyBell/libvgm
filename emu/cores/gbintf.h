#ifndef __GBINTF_H__
#define __GBINTF_H__

#include "../EmuStructs.h"

#ifndef SNDDEV_SELECT
// undefine one of the variables to disable the cores
#define EC_GB_MAME		// enable GameBoy DMG core from MAME
#define EC_GB_SAMEBOY	// enable GameBoy DMG core from SameBoy
#endif


#define OPT_GB_DMG_NO_WAVE_CORRUPT	0x02	// disable WaveRAM corruption
											// Non-GBC models overwrite parts of the WaveRAM when triggered
											// while reading a sample. (hardware bug, fixed in GBC)
#define OPT_GB_DMG_LEGACY_MODE	0x80		// simulate behaviour of old MAME core
											// required for playing older VGM files optimized with vgm_cmp
											// (default: disabled)

// default option bitmask: 0x00


extern const DEV_DECL sndDev_GB_DMG;

#endif	// __GBINTF_H__
