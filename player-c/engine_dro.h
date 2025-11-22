#ifndef ENGINE_DRO_H
#define ENGINE_DRO_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stddef.h>				// for size_t
#include "../stdtype.h"
#include "../emu/Resampler.h"	// for WAVE_32BS
#include "../utils/DataLoader.h"
#include "engine_base.h"


typedef struct player_engine_dro PE_DRO;
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

typedef struct dro_header
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
	UINT32 dataOfs;
} DRO_HEADER;

// DRO v2 often incorrectly specify DualOPL2 instead of OPL3
// These constants allow a configuration of how to handle DualOPL2 in DRO v2 files.
#define DRO_V2OPL3_DETECT	0x00	// scan the initialization block and use OPL3 if "OPL3 enable" if found [default]
#define DRO_V2OPL3_HEADER	0x01	// strictly follow the DRO header
#define DRO_V2OPL3_ENFORCE	0x02	// always enforce OPL3 mode when the DRO says DualOPL2
typedef struct dro_play_options
{
	PLR_GEN_OPTS genOpts;
	UINT8 v2opl3Mode;	// DRO v2 DualOPL2 -> OPL3 fixes
} DRO_PLAY_OPTIONS;


PE_DRO* DROEngine_Create(void);
void DROEngine_Destroy(PE_DRO* self);
void DROEngine_Init(PE_DRO* self);
void DROEngine_Deinit(PE_DRO* self);
UINT8 DROEngine_CanLoadFile(DATA_LOADER *dataLoader);
UINT8 DROEngine_LoadFile(PE_DRO* self, DATA_LOADER *dataLoader);
UINT8 DROEngine_UnloadFile(PE_DRO* self);
const DRO_HEADER* DROEngine_GetFileHeader(const PE_DRO* self);
const char* const* DROEngine_GetTags(PE_DRO* self);
UINT8 DROEngine_GetSongInfo(PE_DRO* self, PLR_SONG_INFO* songInf);
UINT8 DROEngine_GetSongDeviceInfo(const PE_DRO* self, size_t* retDevInfCount, PLR_DEV_INFO** retDevInfData);
UINT8 DROEngine_SetDeviceOptions(PE_DRO* self, UINT32 id, const PLR_DEV_OPTS* devOpts);
UINT8 DROEngine_GetDeviceOptions(const PE_DRO* self, UINT32 id, PLR_DEV_OPTS* devOpts);
UINT8 DROEngine_SetDeviceMuting(PE_DRO* self, UINT32 id, const PLR_MUTE_OPTS* muteOpts);
UINT8 DROEngine_GetDeviceMuting(const PE_DRO* self, UINT32 id, PLR_MUTE_OPTS* muteOpts);
UINT8 DROEngine_SetPlayerOptions(PE_DRO* self, const DRO_PLAY_OPTIONS* playOpts);
UINT8 DROEngine_GetPlayerOptions(const PE_DRO* self, DRO_PLAY_OPTIONS* playOpts);
UINT8 DROEngine_SetSampleRate(PE_DRO* self, UINT32 sampleRate);
double DROEngine_GetPlaybackSpeed(const PE_DRO* self);
UINT8 DROEngine_SetPlaybackSpeed(PE_DRO* self, double speed);

UINT32 DROEngine_Tick2Sample(const PE_DRO* self, UINT32 ticks);
UINT32 DROEngine_Sample2Tick(const PE_DRO* self, UINT32 samples);
double DROEngine_Tick2Second(const PE_DRO* self, UINT32 ticks);
UINT8 DROEngine_GetState(const PE_DRO* self);
UINT32 DROEngine_GetCurPos(const PE_DRO* self, UINT8 unit);
UINT32 DROEngine_GetCurLoop(const PE_DRO* self);
UINT32 DROEngine_GetTotalTicks(const PE_DRO* self);
UINT32 DROEngine_GetLoopTicks(const PE_DRO* self);

UINT8 DROEngine_Start(PE_DRO* self);
UINT8 DROEngine_Stop(PE_DRO* self);
UINT8 DROEngine_Reset(PE_DRO* self);
UINT8 DROEngine_Seek(PE_DRO* self, UINT8 unit, UINT32 pos);
UINT32 DROEngine_Render(PE_DRO* self, UINT32 smplCnt, WAVE_32BS* data);

#ifdef __cplusplus
}
#endif

#endif	// ENGINE_DRO_H
