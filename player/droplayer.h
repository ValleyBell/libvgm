#ifndef DROPLAYER_H
#define DROPLAYER_H

#include <stdtype.h>
#include <emu/Resampler.h>
#include <utils/DataLoader.h>
#include "playerbase.h"
#include "droplayerbase.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DROPlayer DROPlayer;
typedef struct DRO_PLAY_OPTIONS DRO_PLAY_OPTIONS;

/* same as PLAYER_EVENT_CB from playerbase.hpp */
typedef UINT8 (*DROPLAYER_EVENT_CB)(DROPlayer *player, void *userParam, UINT8 evtType, void *evtParam);

DLLEXPORT UINT8 DROPlayer_IsMyFile(DATA_LOADER *);

DLLEXPORT DROPlayer * DROPlayer_New(void);
DLLEXPORT void DROPlayer_Delete(DROPlayer *);

DLLEXPORT UINT32 DROPlayer_GetPlayerType(DROPlayer *);
DLLEXPORT const char * DROPlayer_GetPlayerName(DROPlayer *);

DLLEXPORT UINT8 DROPlayer_LoadFile(DROPlayer *, DATA_LOADER *);
DLLEXPORT UINT8 DROPlayer_UnloadFile(DROPlayer *);

DLLEXPORT const char * const * DROPlayer_GetTags(DROPlayer *);

DLLEXPORT UINT8 DROPlayer_GetSongInfo(DROPlayer *, PLR_SONG_INFO *songInf);

/* returns a copy of devInfList, needs to be free'd */
DLLEXPORT UINT8 DROPlayer_GetSongDeviceInfo(DROPlayer *, PLR_DEV_INFO ** devInfList, UINT32 *devInfLen);

DLLEXPORT UINT8 DROPlayer_InitDeviceOptions(PLR_DEV_OPTS *devOpts);
DLLEXPORT UINT8 DROPlayer_SetDeviceOptions(DROPlayer *, UINT32 id, const PLR_DEV_OPTS *devOpts);
DLLEXPORT UINT8 DROPlayer_GetDeviceOptions(DROPlayer *, UINT32 id, PLR_DEV_OPTS *devOpts);
DLLEXPORT UINT8 DROPlayer_SetDeviceMuting(DROPlayer *, UINT32 id, const PLR_MUTE_OPTS *muteOpts);
DLLEXPORT UINT8 DROPlayer_GetDeviceMuting(DROPlayer *, UINT32 id, PLR_MUTE_OPTS *muteOpts);
DLLEXPORT UINT8 DROPlayer_SetPlayerOptions(DROPlayer *, const DRO_PLAY_OPTIONS *);
DLLEXPORT UINT8 DROPlayer_GetPlayerOptions(DROPlayer *, DRO_PLAY_OPTIONS *);
DLLEXPORT UINT32 DROPlayer_GetSampleRate(DROPlayer *);
DLLEXPORT UINT8 DROPlayer_SetSampleRate(DROPlayer *, UINT32 sampleRate);
DLLEXPORT UINT8 DROPlayer_SetPlaybackSpeed(DROPlayer *, double speed);
DLLEXPORT void DROPlayer_SetCallback(DROPlayer *, DROPLAYER_EVENT_CB cbFunc, void *cbParam);
DLLEXPORT UINT32 DROPlayer_Tick2Sample(DROPlayer *, UINT32 ticks);
DLLEXPORT UINT32 DROPlayer_Sample2Tick(DROPlayer *, UINT32 samples);
DLLEXPORT double DROPlayer_Tick2Second(DROPlayer *player, UINT32 ticks);
DLLEXPORT double DROPlayer_Sample2Second(DROPlayer *player, UINT32 samples);

DLLEXPORT UINT8 DROPlayer_GetState(DROPlayer *player);
DLLEXPORT UINT32 DROPlayer_GetCurPos(DROPlayer *player, UINT8 unit);
DLLEXPORT UINT32 DROPlayer_GetCurLoop(DROPlayer *player);
DLLEXPORT UINT32 DROPlayer_GetTotalTicks(DROPlayer *player);
DLLEXPORT UINT32 DROPlayer_GetLoopTicks(DROPlayer *player);
DLLEXPORT UINT32 DROPlayer_GetTotalPlayTicks(DROPlayer *player, UINT32 numLoops);

DLLEXPORT UINT8 DROPlayer_Start(DROPlayer *player);
DLLEXPORT UINT8 DROPlayer_Stop(DROPlayer *player);
DLLEXPORT UINT8 DROPlayer_Reset(DROPlayer *player);
DLLEXPORT UINT8 DROPlayer_Seek(DROPlayer *player, UINT8 unit, UINT32 pos);
DLLEXPORT UINT8 DROPlayer_Render(DROPlayer *player, UINT32 smplCnt, WAVE_32BS *data);

#ifdef __cplusplus
}
#endif


#endif
