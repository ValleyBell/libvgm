#ifndef __K005289_H__
#define __K005289_H__

#include "../EmuStructs.h"

// PROM data must be written via block write (offset 0, length 0x200)
// Usage: Use DEVRW_BLOCK with offset 0 and length 0x200 to load PROM.

extern const DEV_DECL sndDev_K005289;

// K005289 write offsets:
// 0x00: control A (volume + waveform bank for channel 1)
// 0x01: control B (volume + waveform bank for channel 2)
// 0x02: ld1_w (offset)
// 0x03: ld2_w (offset)
// 0x04: tg1_w
// 0x05: tg2_w

#endif // __K005289_H__
