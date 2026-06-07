#ifndef __NAMCOC30_H__
#define __NAMCOC30_H__

#include "../EmuStructs.h"

// PROM data must be written via block write (offset 0, length 0x100)
// Usage: Use DEVRW_BLOCK with offset 0 and length 0x100 to load PROM.

extern const DEV_DECL sndDev_NAMCOC30;

#endif // __NAMCOC30_H__
