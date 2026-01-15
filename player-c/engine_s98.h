#ifndef __S98PLAYER_HPP__
#define __S98PLAYER_HPP__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stddef.h>				// for size_t
#include "../stdtype.h"
#include "../emu/Resampler.h"	// for WAVE_32BS
#include "../utils/DataLoader.h"
#include "engine_base.h"


typedef struct player_engine_s98 PE_S98;
#define FCC_S98 	0x53393800

typedef struct s98_header
{
	UINT8 fileVer;
	UINT32 tickMult;	// [v1] tick timing numerator
	UINT32 tickDiv;		// [v2] tick timing denumerator
	UINT32 compression;	// [v1: 0 - no compression, >0 - size of uncompressed data] [v2: ??] [v3: must be 0]
	UINT32 tagOfs;		// [v1/2: song title file offset] [v3: tag data file offset]
	UINT32 dataOfs;		// play data file offset
	UINT32 loopOfs;		// loop file offset
} S98_HEADER;
typedef struct s98_header_device
{
	UINT32 devType;
	UINT32 clock;
	UINT32 pan;			// [v2: reserved] [v3: pan setting]
	UINT32 app_spec;	// [v2: application-specific] [v3: reserved]
} S98_HDR_DEVICE;
typedef struct s98_play_options
{
	PLR_GEN_OPTS genOpts;
} S98_PLAY_OPTIONS;


PE_S98* S98Engine_Create(void);
void S98Engine_Destroy(PE_S98* self);
void S98Engine_Init(PE_S98* self);
void S98Engine_Deinit(PE_S98* self);
UINT8 S98Engine_CanLoadFile(DATA_LOADER *dataLoader);
UINT8 S98Engine_LoadFile(PE_S98* self, DATA_LOADER *dataLoader);
UINT8 S98Engine_UnloadFile(PE_S98* self);
const S98_HEADER* S98Engine_GetFileHeader(const PE_S98* self);
const char* const* S98Engine_GetTags(PE_S98* self);
UINT8 S98Engine_GetSongInfo(PE_S98* self, PLR_SONG_INFO* songInf);
UINT8 S98Engine_GetSongDeviceInfo(const PE_S98* self, size_t* retDevInfCount, PLR_DEV_INFO** retDevInfData);
UINT8 S98Engine_SetDeviceOptions(PE_S98* self, UINT32 id, const PLR_DEV_OPTS* devOpts);
UINT8 S98Engine_GetDeviceOptions(const PE_S98* self, UINT32 id, PLR_DEV_OPTS* devOpts);
UINT8 S98Engine_SetDeviceMuting(PE_S98* self, UINT32 id, const PLR_MUTE_OPTS* muteOpts);
UINT8 S98Engine_GetDeviceMuting(const PE_S98* self, UINT32 id, PLR_MUTE_OPTS* muteOpts);
UINT8 S98Engine_SetPlayerOptions(PE_S98* self, const S98_PLAY_OPTIONS* playOpts);
UINT8 S98Engine_GetPlayerOptions(const PE_S98* self, S98_PLAY_OPTIONS* playOpts);
UINT8 S98Engine_SetSampleRate(PE_S98* self, UINT32 sampleRate);
double S98Engine_GetPlaybackSpeed(const PE_S98* self);
UINT8 S98Engine_SetPlaybackSpeed(PE_S98* self, double speed);

UINT32 S98Engine_Tick2Sample(const PE_S98* self, UINT32 ticks);
UINT32 S98Engine_Sample2Tick(const PE_S98* self, UINT32 samples);
double S98Engine_Tick2Second(const PE_S98* self, UINT32 ticks);
UINT8 S98Engine_GetState(const PE_S98* self);
UINT32 S98Engine_GetCurPos(const PE_S98* self, UINT8 unit);
UINT32 S98Engine_GetCurLoop(const PE_S98* self);
UINT32 S98Engine_GetTotalTicks(const PE_S98* self);
UINT32 S98Engine_GetLoopTicks(const PE_S98* self);

UINT8 S98Engine_Start(PE_S98* self);
UINT8 S98Engine_Stop(PE_S98* self);
UINT8 S98Engine_Reset(PE_S98* self);
UINT8 S98Engine_Seek(PE_S98* self, UINT8 unit, UINT32 pos);
UINT32 S98Engine_Render(PE_S98* self, UINT32 smplCnt, WAVE_32BS* data);


#ifdef __cplusplus
}
#endif

#endif	// __S98PLAYER_HPP__
