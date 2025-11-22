#ifndef __PLAYERA_HPP__
#define __PLAYERA_HPP__

#ifdef __cplusplus
extern "C"
{
#endif

#include "../stdtype.h"
#include "../utils/DataLoader.h"
#include "engine_base.h"


#define PLAYSTATE_FADE	0x10	// is fading
#define PLAYSTATE_FIN	0x20	// finished playing (file end + fading + trailing silence)

#define PLAYTIME_LOOP_EXCL	0x00	// excluding loops, jumps back in time when the file loops
#define PLAYTIME_LOOP_INCL	0x01	// including loops, no jumping back
#define PLAYTIME_TIME_FILE	0x00	// file time, progresses slower/faster when playback speed is adjusted
#define PLAYTIME_TIME_PBK	0x02	// playback time, file duration will be longer/shorter when playback speed is adjusted
#define PLAYTIME_WITH_FADE	0x10	// include fade out time (looping songs only)
#define PLAYTIME_WITH_SLNC	0x20	// include silence after songs

typedef struct player_config
{
	INT32 masterVol;	// master volume (16.16 fixed point, negative value = phase inversion)
	UINT8 ignoreVolGain;	// [bool] ignore track-specific volume gain
	UINT8 chnInvert;	// channel phase inversion (bit 0 - left, bit 1 - right)
	UINT32 loopCount;
	UINT32 fadeSmpls;
	UINT32 endSilenceSmpls;
	double pbSpeed;
} PLAYER_CFG;

// TODO: find a proper name for this class
typedef struct player PLAYERA;

PLAYERA* PlayerA_Create(void);
void PlayerA_Destroy(PLAYERA* self);
void PlayerA_Init(PLAYERA* self);
void PlayerA_Deinit(PLAYERA* self);
void PlayerA_RegisterPlayerEngine(PLAYERA* self, PEBASE* engine);
void PlayerA_UnregisterAllPlayers(PLAYERA* self);
void PlayerA_GetRegisteredPlayers(const PLAYERA* self, size_t* retEnCount, PEBASE* const* * retEngines);

UINT8 PlayerA_SetOutputSettings(PLAYERA* self, UINT32 smplRate, UINT8 channels, UINT8 smplBits, UINT32 smplBufferLen);
UINT32 PlayerA_GetSampleRate(const PLAYERA* self);
void PlayerA_SetSampleRate(PLAYERA* self, UINT32 sampleRate);
double PlayerA_GetPlaybackSpeed(const PLAYERA* self);
void PlayerA_SetPlaybackSpeed(PLAYERA* self, double speed);
UINT32 PlayerA_GetLoopCount(const PLAYERA* self);
void PlayerA_SetLoopCount(PLAYERA* self, UINT32 loops);
INT32 PlayerA_GetMasterVolume(const PLAYERA* self);
void PlayerA_SetMasterVolume(PLAYERA* self, INT32 volume);
INT32 PlayerA_GetSongVolume(const PLAYERA* self);
UINT32 PlayerA_GetFadeSamples(const PLAYERA* self);
void PlayerA_SetFadeSamples(PLAYERA* self, UINT32 smplCnt);
UINT32 PlayerA_GetEndSilenceSamples(const PLAYERA* self);
void PlayerA_SetEndSilenceSamples(PLAYERA* self, UINT32 smplCnt);
const PLAYER_CFG* PlayerA_GetConfiguration(const PLAYERA* self);
void PlayerA_SetConfiguration(PLAYERA* self, const PLAYER_CFG* config);

void PlayerA_SetEventCallback(PLAYERA* self, PLAYER_EVENT_CB cbFunc, void* cbParam);
void PlayerA_SetFileReqCallback(PLAYERA* self, PLAYER_FILEREQ_CB cbFunc, void* cbParam);
void PlayerA_SetLogCallback(PLAYERA* self, PLAYER_LOG_CB cbFunc, void* cbParam);
UINT8 PlayerA_GetState(const PLAYERA* self);
UINT32 PlayerA_GetCurPos(const PLAYERA* self, UINT8 unit);
double PlayerA_GetCurTime(const PLAYERA* self, UINT8 flags);	// TODO: add GetCurSample()
double PlayerA_GetTotalTime(const PLAYERA* self, UINT8 flags);	// TODO: add GetTotalSamples()
UINT32 PlayerA_GetCurLoop(const PLAYERA* self);
double PlayerA_GetLoopTime(const PLAYERA* self);	// TODO: add GetLoopSamples()
PEBASE* PlayerA_GetPlayer(PLAYERA* self);
const PEBASE* PlayerA_GetPlayerC(const PLAYERA* self);

UINT8 PlayerA_LoadFile(PLAYERA* self, DATA_LOADER* dLoad);
UINT8 PlayerA_UnloadFile(PLAYERA* self);
UINT32 PlayerA_GetFileSize(PLAYERA* self);
UINT8 PlayerA_Start(PLAYERA* self);
UINT8 PlayerA_Stop(PLAYERA* self);
UINT8 PlayerA_Reset(PLAYERA* self);
UINT8 PlayerA_FadeOut(PLAYERA* self);
UINT8 PlayerA_Seek(PLAYERA* self, UINT8 unit, UINT32 pos);
UINT32 PlayerA_Render(PLAYERA* self, UINT32 bufSize, void* data);

#ifdef __cplusplus
}
#endif

#endif	// __PLAYERA_HPP__
