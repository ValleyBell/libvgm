#ifndef VGMPLAYERBASE_H
#define VGMPLAYERBASE_H

#include "helper.h"

/* shared structs/defines between c++ and c */

#define FCC_VGM 	0x56474D00

// This structure contains only some basic information about the VGM file,
// not the full header.
struct VGM_HEADER
{
	UINT32 fileVer;
	UINT32 eofOfs;
	UINT32 extraHdrOfs;
	UINT32 dataOfs;		// command data start offset
	UINT32 loopOfs;		// loop offset
	UINT32 dataEnd;		// command data end offset
	UINT32 gd3Ofs;		// GD3 tag offset
	
	UINT32 xhChpClkOfs;	// extra header offset: chip clocks
	UINT32 xhChpVolOfs;	// extra header offset: chip volume
	
	UINT32 numTicks;	// total number of samples
	UINT32 loopTicks;	// number of samples for the looping part
	UINT32 recordHz;	// rate of the recording in Hz (60 for NTSC, 50 for PAL, 0 disables rate scaling)
	
	INT8 loopBase;
	UINT8 loopModifier;	// 4.4 fixed point
	INT16 volumeGain;	// 8.8 fixed point, +0x100 = +6 db
};

struct VGM_PLAY_OPTIONS
{
	UINT32 playbackHz;	// set to 60 (NTSC) or 50 (PAL) for region-specific song speed adjustment
						// Note: requires VGM_HEADER.recordHz to be non-zero to work.
	UINT8 hardStopOld;	// enforce silence at end of old VGMs (<1.50), fixes Key Off events being trimmed off
};



#endif
