#ifndef S98PLAYER_H
#define S98PLAYER_H

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

typedef struct S98Player S98Player;

/* same as PLAYER_EVENT_CB from playerbase.hpp */
typedef UINT8 (*S98PLAYER_EVENT_CB)(S98Player *player, void *userParam, UINT8 evtType, void *evtParam);

DLLEXPORT UINT8 S98Player_IsMyFile(DATA_LOADER *);

DLLEXPORT S98Player * S98Player_New(void);
DLLEXPORT void S98Player_Delete(S98Player *);

DLLEXPORT UINT8 S98Player_LoadFile(S98Player *, DATA_LOADER *);
DLLEXPORT UINT8 S98Player_UnloadFile(S98Player *);


DLLEXPORT const char * const * S98Player_GetTags(S98Player *);
DLLEXPORT UINT32 S98Player_GetSampleRate(S98Player *);
DLLEXPORT UINT8 S98Player_SetSampleRate(S98Player *, UINT32 sampleRate);
DLLEXPORT UINT8 S98Player_SetPlaybackSpeed(S98Player *, double speed);
DLLEXPORT void S98Player_SetCallback(S98PLAYER_EVENT_CB cbFunc, void *cbParam);
DLLEXPORT UINT32 S98Player_Tick2Sample(S98Player *, UINT32 ticks);
DLLEXPORT UINT32 S98Player_Sample2Tick(S98Player *, UINT32 samples);
DLLEXPORT double S98Player_Tick2Second(S98Player *player, UINT32 ticks);
DLLEXPORT double S98Player_Sample2Second(S98Player *player, UINT32 samples);

DLLEXPORT UINT8 S98Player_GetState(S98Player *player);
DLLEXPORT UINT32 S98Player_GetCurPos(S98Player *player, UINT8 unit);
DLLEXPORT UINT32 S98Player_GetCurLoop(S98Player *player);
DLLEXPORT UINT32 S98Player_GetTotalTicks(S98Player *player);
DLLEXPORT UINT32 S98Player_GetLoopTicks(S98Player *player);
DLLEXPORT UINT32 S98Player_GetTotalPlayTicks(S98Player *player, UINT32 numLoops);

DLLEXPORT UINT8 S98Player_Start(S98Player *player);
DLLEXPORT UINT8 S98Player_Stop(S98Player *player);
DLLEXPORT UINT8 S98Player_Reset(S98Player *player);
DLLEXPORT UINT8 S98Player_Seek(S98Player *player, UINT8 unit, UINT32 pos);
DLLEXPORT UINT8 S98Player_Render(S98Player *player, UINT32 smplCnt, WAVE_32BS *data);

#ifdef __cplusplus
}
#endif


#endif
