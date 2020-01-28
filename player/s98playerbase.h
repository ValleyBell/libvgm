#ifndef S98PLAYERBASE_H
#define S98PLAYERBASE_H

#include "helper.h"

/* shared defines/structs */

#define FCC_S98 	0x53393800

struct S98_HEADER
{
	UINT8 fileVer;
	UINT32 tickMult;	// [v1] tick timing numerator
	UINT32 tickDiv;		// [v2] tick timing denumerator
	UINT32 compression;	// [v1: 0 - no compression, >0 - size of uncompressed data] [v2: ??] [v3: must be 0]
	UINT32 tagOfs;		// [v1/2: song title file offset] [v3: tag data file offset]
	UINT32 dataOfs;		// play data file offset
	UINT32 loopOfs;		// loop file offset
};
struct S98_DEVICE
{
	UINT32 devType;
	UINT32 clock;
	UINT32 pan;			// [v2: reserved] [v3: pan setting]
	UINT32 app_spec;	// [v2: application-specific] [v3: reserved]
};


#endif
