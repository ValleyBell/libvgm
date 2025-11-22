#ifndef ENGINE_GYM_H
#define ENGINE_GYM_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stddef.h>				// for size_t
#include "../stdtype.h"
#include "../emu/Resampler.h"	// for WAVE_32BS
#include "../utils/DataLoader.h"
#include "engine_base.h"


typedef struct player_engine_gym PE_GYM;
#define FCC_GYM 	0x47594D00

// GYMX header (optional, added by YMAMP WinAMP plugin)
//	Ofs	Len	Description
//	000	04	"GYMX"
//	004	20	song title
//	024	20	game name
//	044	20	publisher
//	064	20	emulator ("Dumped with")
//	084	20	file creator ("Dumped by")
//	0A4	100	comments
//	1A4	04	loop start frame (0 = no loop)
//	1A8	04	uncompressed data size (0 = data is uncompressed, 1 = data is zlib compressed)

typedef struct gym_header
{
	UINT8 hasHeader;
	UINT32 uncomprSize;
	UINT32 loopFrame;
	UINT32 dataOfs;
	UINT32 realFileSize;	// internal file size after possible decompression
} GYM_HEADER;

typedef struct gym_play_options
{
	PLR_GEN_OPTS genOpts;
} GYM_PLAY_OPTIONS;


PE_GYM* GYMEngine_Create(void);
void GYMEngine_Destroy(PE_GYM* self);
void GYMEngine_Init(PE_GYM* self);
void GYMEngine_Deinit(PE_GYM* self);
UINT8 GYMEngine_CanLoadFile(DATA_LOADER *dataLoader);
UINT8 GYMEngine_LoadFile(PE_GYM* self, DATA_LOADER *dataLoader);
UINT8 GYMEngine_UnloadFile(PE_GYM* self);
const GYM_HEADER* GYMEngine_GetFileHeader(const PE_GYM* self);
const char* const* GYMEngine_GetTags(PE_GYM* self);
UINT8 GYMEngine_GetSongInfo(PE_GYM* self, PLR_SONG_INFO* songInf);
UINT8 GYMEngine_GetSongDeviceInfo(const PE_GYM* self, size_t* retDevInfCount, PLR_DEV_INFO** retDevInfData);
UINT8 GYMEngine_SetDeviceOptions(PE_GYM* self, UINT32 id, const PLR_DEV_OPTS* devOpts);
UINT8 GYMEngine_GetDeviceOptions(const PE_GYM* self, UINT32 id, PLR_DEV_OPTS* devOpts);
UINT8 GYMEngine_SetDeviceMuting(PE_GYM* self, UINT32 id, const PLR_MUTE_OPTS* muteOpts);
UINT8 GYMEngine_GetDeviceMuting(const PE_GYM* self, UINT32 id, PLR_MUTE_OPTS* muteOpts);
UINT8 GYMEngine_SetPlayerOptions(PE_GYM* self, const GYM_PLAY_OPTIONS* playOpts);
UINT8 GYMEngine_GetPlayerOptions(const PE_GYM* self, GYM_PLAY_OPTIONS* playOpts);
UINT8 GYMEngine_SetSampleRate(PE_GYM* self, UINT32 sampleRate);
double GYMEngine_GetPlaybackSpeed(const PE_GYM* self);
UINT8 GYMEngine_SetPlaybackSpeed(PE_GYM* self, double speed);

UINT32 GYMEngine_Tick2Sample(const PE_GYM* self, UINT32 ticks);
UINT32 GYMEngine_Sample2Tick(const PE_GYM* self, UINT32 samples);
double GYMEngine_Tick2Second(const PE_GYM* self, UINT32 ticks);
UINT8 GYMEngine_GetState(const PE_GYM* self);
UINT32 GYMEngine_GetCurPos(const PE_GYM* self, UINT8 unit);
UINT32 GYMEngine_GetCurLoop(const PE_GYM* self);
UINT32 GYMEngine_GetTotalTicks(const PE_GYM* self);
UINT32 GYMEngine_GetLoopTicks(const PE_GYM* self);

UINT8 GYMEngine_Start(PE_GYM* self);
UINT8 GYMEngine_Stop(PE_GYM* self);
UINT8 GYMEngine_Reset(PE_GYM* self);
UINT8 GYMEngine_Seek(PE_GYM* self, UINT8 unit, UINT32 pos);
UINT32 GYMEngine_Render(PE_GYM* self, UINT32 smplCnt, WAVE_32BS* data);

#ifdef __cplusplus
}
#endif

#endif	// ENGINE_GYM_H
