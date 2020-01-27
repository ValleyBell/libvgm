#ifndef VGMPLAYER_H
#define VGMPLAYER_H

#include <stdtype.h>
#include <emu/Resampler.h>
#include <utils/DataLoader.h>

#ifndef DLLEXPORT
#ifdef _MSC_VER
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct VGMPlayer VGMPlayer;

/* same as PLAYER_EVENT_CB from playerbase.hpp */
typedef UINT8 (*VGMPLAYER_EVENT_CB)(VGMPlayer *player, void *userParam, UINT8 evtType, void *evtParam);

DLLEXPORT UINT8 VGMPlayer_IsMyFile(DATA_LOADER *);

DLLEXPORT VGMPlayer * VGMPlayer_New(void);
DLLEXPORT void VGMPlayer_Delete(VGMPlayer *);

DLLEXPORT UINT8 VGMPlayer_LoadFile(VGMPlayer *, DATA_LOADER *);
DLLEXPORT UINT8 VGMPlayer_UnloadFile(VGMPlayer *);

DLLEXPORT const char * const * VGMPlayer_GetTags(VGMPlayer *);
DLLEXPORT UINT32 VGMPlayer_GetSampleRate(VGMPlayer *);
DLLEXPORT UINT8 VGMPlayer_SetSampleRate(VGMPlayer *, UINT32 sampleRate);
DLLEXPORT UINT8 VGMPlayer_SetPlaybackSpeed(VGMPlayer *, double speed);
DLLEXPORT void VGMPlayer_SetCallback(VGMPLAYER_EVENT_CB cbFunc, void *cbParam);
DLLEXPORT UINT32 VGMPlayer_Tick2Sample(VGMPlayer *, UINT32 ticks);
DLLEXPORT UINT32 VGMPlayer_Sample2Tick(VGMPlayer *, UINT32 samples);
DLLEXPORT double VGMPlayer_Tick2Second(VGMPlayer *player, UINT32 ticks);
DLLEXPORT double VGMPlayer_Sample2Second(VGMPlayer *player, UINT32 samples);

DLLEXPORT UINT8 VGMPlayer_GetState(VGMPlayer *player);
DLLEXPORT UINT32 VGMPlayer_GetCurPos(VGMPlayer *player, UINT8 unit);
DLLEXPORT UINT32 VGMPlayer_GetCurLoop(VGMPlayer *player);
DLLEXPORT UINT32 VGMPlayer_GetTotalTicks(VGMPlayer *player);
DLLEXPORT UINT32 VGMPlayer_GetLoopTicks(VGMPlayer *player);
DLLEXPORT UINT32 VGMPlayer_GetTotalPlayTicks(VGMPlayer *player, UINT32 numLoops);

DLLEXPORT UINT8 VGMPlayer_Start(VGMPlayer *player);
DLLEXPORT UINT8 VGMPlayer_Stop(VGMPlayer *player);
DLLEXPORT UINT8 VGMPlayer_Reset(VGMPlayer *player);
DLLEXPORT UINT8 VGMPlayer_Seek(VGMPlayer *player, UINT8 unit, UINT32 pos);
DLLEXPORT UINT8 VGMPlayer_Render(VGMPlayer *player, UINT32 smplCnt, WAVE_32BS *data);

#ifdef __cplusplus
}
#endif


#endif
