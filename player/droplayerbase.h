#ifndef DROPLAYERBASE_H
#define DROPLAYERBASE_H

#include "helper.h"

/* shared defines/structs */

#define FCC_DRO 	0x44524F00

// DRO v0 header (DOSBox 0.62, SVN r1864)
//	Ofs	Len	Description
//	00	08	"DBRAWOPL"
//	04	04	data length in milliseconds
//	08	04	data length in bytes
//	0C	01	hardware type (0 = OPL2, 1 = OPL3, 2 = DualOPL2)

// DRO v1 header (DOSBox 0.63, SVN r2065)
//	Ofs	Len	Description
//	00	08	"DBRAWOPL"
//	04	04	version minor
//	08	04	version major
//	0C	04	data length in milliseconds
//	10	04	data length in bytes
//	14	04	hardware type (0 = OPL2, 1 = OPL3, 2 = DualOPL2)

// DRO v2 header (DOSBox 0.73, SVN r3178)
//	Ofs	Len	Description
//	00	08	"DBRAWOPL"
//	04	04	version major
//	08	04	version minor
//	0C	04	data length in "pairs" (1 pair = 2 bytes, a pair consists of command + data)
//	10	04	data length in milliseconds
//	14	01	hardware type (0 = OPL2, 1 = DualOPL2, 2 = OPL3)
//	15	01	data format (0 = interleaved command/data)
//	16	01	compression (0 = no compression)
//	17	01	command code for short delay
//	18	01	command code for long delay
//	19	01	size of register codemap cl
//	1A	cl	register codemap

struct DRO_HEADER
{
	UINT16 verMajor;
	UINT16 verMinor;
	UINT32 dataSize;	// in bytes
	UINT32 lengthMS;
	UINT8 hwType;
	UINT8 format;
	UINT8 compression;
	UINT8 cmdDlyShort;
	UINT8 cmdDlyLong;
	UINT8 regCmdCnt;
	UINT8 regCmdMap[0x80];
};

// DRO v2 often incorrectly specify DualOPL2 instead of OPL3
// These constants allow a configuration of how to handle DualOPL2 in DRO v2 files.
#define DRO_V2OPL3_DETECT	0x00	// scan the initialization block and use OPL3 if "OPL3 enable" if found [default]
#define DRO_V2OPL3_HEADER	0x01	// strictly follow the DRO header
#define DRO_V2OPL3_ENFORCE	0x02	// always enforce OPL3 mode when the DRO says DualOPL2
struct DRO_PLAY_OPTIONS
{
	UINT8 v2opl3Mode;	// DRO v2 DualOPL2 -> OPL3 fixes
};


typedef struct _dro_chip_device DRO_CHIPDEV;
struct _dro_chip_device
{
	VGM_BASEDEV base;
	size_t optID;
	DEVFUNC_WRITE_A8D8 write;
};

#endif
