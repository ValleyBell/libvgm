#include <stdlib.h>
#include <string.h>

#include "../stdtype.h"
#include "../utils/DataLoader.h"
#include "engine_base.h"
#include "../emu/Resampler.h"

#include "playera.h"

typedef void (*PLR_SMPL_PACK)(void* buffer, INT32 value);

DEFINE_PE_ARRAY(WAVE_32BS, ARR_W32S)
DEFINE_PE_VECTOR(PEBASE*, VEC_PEBASEPTR)

typedef struct player
{
	VEC_PEBASEPTR avbPlrs;	// available players
	UINT32 smplRate;
	PLAYER_CFG config;
	PLAYER_EVENT_CB plrCbFunc;
	void* plrCbParam;
	UINT8 myPlayState;
	
	UINT8 outSmplChns;
	UINT8 outSmplBits;
	UINT32 outSmplSize1;	// for 1 channel
	UINT32 outSmplSizeA;	// for all channels
	PLR_SMPL_PACK outSmplPack;
	ARR_W32S smplBuf;
	PEBASE* player;
	DATA_LOADER* dLoad;
	INT32 songVolume;
	UINT32 fadeSmplStart;
	UINT32 endSilenceStart;
} PLAYERA;


void PlayerA_FindPlayerEngine(PLAYERA* self);
INT32 PlayerA_CalcSongVolume(PLAYERA* self);
INT32 PlayerA_CalcCurrentVolume(PLAYERA* self, UINT32 playbackSmpl);
UINT8 PlayerA_PlayCallbackS(PEBASE* player, void* userParam, UINT8 evtType, void* evtParam);

static void SampleConv_toU8(void* buffer, INT32 value)
{
	value >>= 16;	// 24 bit -> 8 bit
	if (value < -0x80)
		value = -0x80;
	else if (value > +0x7F)
		value = +0x7F;
	*(UINT8*)buffer = (UINT8)(0x80 + value);
	return;
}

static void SampleConv_toS16(void* buffer, INT32 value)
{
	INT16 v;
	value >>= 8;	// 24 bit -> 16 bit
	if (value < -0x8000)
		value = -0x8000;
	else if (value > +0x7FFF)
		value = +0x7FFF;
	v = (INT16)value;
	memcpy(buffer, &v, sizeof(v));
	return;
}

static void SampleConv_toS24(void* buffer, INT32 value)
{
	UINT8 tmp[3];
	if (value < -0x800000)
		value = -0x800000;
	else if (value > +0x7FFFFF)
		value = +0x7FFFFF;
#if defined(VGM_LITTLE_ENDIAN)
	tmp[0] = ( value       ) & 0xFF;
	tmp[1] = ( value >> 8  ) & 0xFF;
	tmp[2] = ( value >> 16 ) & 0xFF;
#elif defined(VGM_BIG_ENDIAN)
	tmp[0] = ( value >> 16 ) & 0xFF;
	tmp[1] = ( value >> 8  ) & 0xFF;
	tmp[2] = ( value       ) & 0xFF;
#else
#error unknown endianness
#endif
	memcpy(buffer, tmp, sizeof(tmp));
	return;
}

static void SampleConv_toS32(void* buffer, INT32 value)
{
	// internal scale is 24-bit, so limit to that
	if (value < -0x800000)
		value = -0x800000;
	else if (value > +0x7FFFFF)
		value = +0x7FFFFF;
	value *= (1 << 8);	// 24 bit -> 32 bit
	memcpy(buffer, &value, sizeof(value));
	return;
}

static void SampleConv_toF32(void* buffer, INT32 value)
{
	// limiting not required here
	float v = value / (float)0x800000;
	memcpy(buffer, &v, sizeof(v));
	return;
}

static PLR_SMPL_PACK GetSampleConvFunc(UINT8 bits)
{
	if (bits == 8)
		return SampleConv_toU8;
	else if (bits == 16)
		return SampleConv_toS16;
	else if (bits == 24)
		return SampleConv_toS24;
	else if (bits == 32)
		return SampleConv_toS32;
	else
		return NULL;
}

PLAYERA* PlayerA_Create(void)
{
	PLAYERA* self = malloc(sizeof(PLAYERA));
	if (self == NULL)
		return NULL;
	
	PlayerA_Init(self);
	return self;
}

void PlayerA_Destroy(PLAYERA* self)
{
	PlayerA_Deinit(self);
	free(self);
	return;
}

void PlayerA_Init(PLAYERA* self)
{
	PE_VECTOR_ALLOC(self->avbPlrs, PEBASE*, 4)

	self->config.masterVol = 0x10000;	// fixed point 16.16
	self->config.ignoreVolGain = 0;
	self->config.chnInvert = 0x00;
	self->config.loopCount = 2;
	self->config.fadeSmpls = 0;
	self->config.endSilenceSmpls = 0;
	self->config.pbSpeed = 1.0;
	
	self->outSmplChns = 2;
	self->outSmplBits = 16;
	self->outSmplPack = GetSampleConvFunc(self->outSmplBits);
	self->smplRate = 44100;
	self->outSmplSize1 = self->outSmplBits / 8;
	self->outSmplSizeA = self->outSmplSize1 * self->outSmplChns;
	self->smplBuf.size = 0;
	self->smplBuf.data = NULL;
	
	self->plrCbFunc = NULL;
	self->plrCbParam = NULL;
	self->myPlayState = 0x00;
	self->player = NULL;
	self->dLoad = NULL;
	self->songVolume = PlayerA_CalcSongVolume(self);
	self->fadeSmplStart = (UINT32)-1;
	self->endSilenceStart = (UINT32)-1;
	
	return;
}

void PlayerA_Deinit(PLAYERA* self)
{
	PlayerA_Stop(self);
	PlayerA_UnloadFile(self);
	PlayerA_UnregisterAllPlayers(self);
	return;
}

void PlayerA_RegisterPlayerEngine(PLAYERA* self, PEBASE* engine)
{
	engine->vtbl.SetEventCallback(engine, PlayerA_PlayCallbackS, self);
	//engine->SetFileReqCallback(engine, self->frCbFunc, self->frCbParam);
	engine->vtbl.SetSampleRate(engine, self->smplRate);
	engine->vtbl.SetPlaybackSpeed(engine, self->config.pbSpeed);

	if (self->avbPlrs.size >= self->avbPlrs.alloc)
		PE_VECTOR_REALLOC(self->avbPlrs, PEBASE*, self->avbPlrs.alloc * 2);
	self->avbPlrs.data[self->avbPlrs.size] = engine;	self->avbPlrs.size ++;
	return;
}

void PlayerA_UnregisterAllPlayers(PLAYERA* self)
{
	size_t curPlr;
	for (curPlr = 0; curPlr < self->avbPlrs.size; curPlr ++)
		PBaseEngine_Destroy(self->avbPlrs.data[curPlr]);
	PE_VECTOR_FREE(self->avbPlrs);
	return;
}

void PlayerA_GetRegisteredPlayers(const PLAYERA* self, size_t* retEnCount, PEBASE* const* * retEngines)
{
	*retEnCount = self->avbPlrs.size;
	*retEngines = self->avbPlrs.data;
	return;
}


UINT8 PlayerA_SetOutputSettings(PLAYERA* self, UINT32 smplRate, UINT8 channels, UINT8 smplBits, UINT32 smplBufferLen)
{
	if (channels != 2)
		return 0xF0;	// TODO: support channels = 1
	PLR_SMPL_PACK smplPackFunc = GetSampleConvFunc(smplBits);
	if (smplPackFunc == NULL)
		return 0xF1;	// unsupported sample format
	
	self->outSmplChns = channels;
	self->outSmplBits = smplBits;
	self->outSmplPack = smplPackFunc;
	PlayerA_SetSampleRate(self, smplRate);
	self->outSmplSize1 = self->outSmplBits / 8;
	self->outSmplSizeA = self->outSmplSize1 * self->outSmplChns;
	PE_ARRAY_FREE(self->smplBuf);
	PE_ARRAY_MALLOC(self->smplBuf, WAVE_32BS, smplBufferLen);
	return 0x00;
}

UINT32 PlayerA_GetSampleRate(const PLAYERA* self)
{
	return self->smplRate;
}

void PlayerA_SetSampleRate(PLAYERA* self, UINT32 sampleRate)
{
	size_t curPlr;
	self->smplRate = sampleRate;
	for (curPlr = 0; curPlr < self->avbPlrs.size; curPlr++)
	{
		PEBASE* engine = self->avbPlrs.data[curPlr];
		if (engine == self->player && (engine->vtbl.GetState(engine) & PLAYSTATE_PLAY))
			continue;	// skip when currently playing
		engine->vtbl.SetSampleRate(engine, self->smplRate);
	}
	return;
}

double PlayerA_GetPlaybackSpeed(const PLAYERA* self)
{
	return self->config.pbSpeed;
}

void PlayerA_SetPlaybackSpeed(PLAYERA* self, double speed)
{
	size_t curPlr;
	self->config.pbSpeed = speed;
	for (curPlr = 0; curPlr < self->avbPlrs.size; curPlr++)
	{
		PEBASE* engine = self->avbPlrs.data[curPlr];
		engine->vtbl.SetPlaybackSpeed(engine, self->config.pbSpeed);
	}
	return;
}

UINT32 PlayerA_GetLoopCount(const PLAYERA* self)
{
	return self->config.loopCount;
}

void PlayerA_SetLoopCount(PLAYERA* self, UINT32 loops)
{
	self->config.loopCount = loops;
	return;
}

UINT32 PlayerA_GetFadeSamples(const PLAYERA* self)
{
	return self->config.fadeSmpls;
}

INT32 PlayerA_GetMasterVolume(const PLAYERA* self)
{
	return self->config.masterVol;
}

void PlayerA_SetMasterVolume(PLAYERA* self, INT32 volume)
{
	self->config.masterVol = volume;
	self->songVolume = PlayerA_CalcSongVolume(self);
	return;
}

INT32 PlayerA_GetSongVolume(const PLAYERA* self)
{
	return self->songVolume;
}

void PlayerA_SetFadeSamples(PLAYERA* self, UINT32 smplCnt)
{
	self->config.fadeSmpls = smplCnt;
	return;
}

UINT32 PlayerA_GetEndSilenceSamples(const PLAYERA* self)
{
	return self->config.endSilenceSmpls;
}

void PlayerA_SetEndSilenceSamples(PLAYERA* self, UINT32 smplCnt)
{
	self->config.endSilenceSmpls = smplCnt;
	return;
}

const PLAYER_CFG* PlayerA_GetConfiguration(const PLAYERA* self)
{
	return &self->config;
}

void PlayerA_SetConfiguration(PLAYERA* self, const PLAYER_CFG* config)
{
	double oldPbSpeed = self->config.pbSpeed;
	
	self->config = *config;
	PlayerA_SetMasterVolume(self, self->config.masterVol);
	if (oldPbSpeed != self->config.pbSpeed)
		PlayerA_SetPlaybackSpeed(self, self->config.pbSpeed);	// refresh in all players
	return;
}

void PlayerA_SetEventCallback(PLAYERA* self, PLAYER_EVENT_CB cbFunc, void* cbParam)
{
	self->plrCbFunc = cbFunc;
	self->plrCbParam = cbParam;
	return;
}

void PlayerA_SetFileReqCallback(PLAYERA* self, PLAYER_FILEREQ_CB cbFunc, void* cbParam)
{
	size_t curPlr;
	for (curPlr = 0; curPlr < self->avbPlrs.size; curPlr ++)
	{
		PEBASE* engine = self->avbPlrs.data[curPlr];
		engine->vtbl.SetFileReqCallback(engine, cbFunc, cbParam);
	}
	return;
}

void PlayerA_SetLogCallback(PLAYERA* self, PLAYER_LOG_CB cbFunc, void* cbParam)
{
	size_t curPlr;
	for (curPlr = 0; curPlr < self->avbPlrs.size; curPlr ++)
	{
		PEBASE* engine = self->avbPlrs.data[curPlr];
		engine->vtbl.SetLogCallback(engine, cbFunc, cbParam);
	}
	return;
}

UINT8 PlayerA_GetState(const PLAYERA* self)
{
	if (self->player == NULL)
		return 0x00;
	UINT8 finalState = self->myPlayState;
	if (self->fadeSmplStart != (UINT32)-1)
		finalState |= PLAYSTATE_FADE;
	return finalState;
}

UINT32 PlayerA_GetCurPos(const PLAYERA* self, UINT8 unit)
{
	if (self->player == NULL)
		return (UINT32)-1;
	return self->player->vtbl.GetCurPos(self->player, unit);
}

double PlayerA_GetCurTime(const PLAYERA* self, UINT8 flags)
{
	if (self->player == NULL)
		return -1.0;
	
	// using samples here, as it may be more accurate than the (possibly low-resolution) ticks
	double secs = self->player->vtbl.Sample2Second(self->player, self->player->vtbl.GetCurPos(self->player, PLAYPOS_SAMPLE));
	UINT32 curLoop = self->player->vtbl.GetCurLoop(self->player);
	if (! (flags & PLAYTIME_LOOP_INCL) && curLoop > 0)
		secs -= self->player->vtbl.Tick2Second(self->player, self->player->vtbl.GetLoopTicks(self->player) * curLoop);
	
	if (! (flags & PLAYTIME_TIME_PBK))
		secs *= self->player->vtbl.GetPlaybackSpeed(self->player);
	return secs;
}

double PlayerA_GetTotalTime(const PLAYERA* self, UINT8 flags)
{
	if (self->player == NULL)
		return -1.0;
	
	double secs;
	if (flags & PLAYTIME_LOOP_INCL)
		secs = self->player->vtbl.Tick2Second(self->player, self->player->vtbl.GetTotalPlayTicks(self->player, self->config.loopCount));
	else
		secs = self->player->vtbl.Tick2Second(self->player, self->player->vtbl.GetTotalPlayTicks(self->player, 1));
	if (secs < 0.0)	// indicates infinite runtime
		return secs;
	
	// Fade and silence time are unaffected by playback speed and thus must be applied before speed scaling.
	if ((flags & PLAYTIME_WITH_FADE) && self->player->vtbl.GetLoopTicks(self->player) > 0)
		secs += self->player->vtbl.Sample2Second(self->player, PlayerA_GetFadeSamples(self));
	if (flags & PLAYTIME_WITH_SLNC)
		secs += self->player->vtbl.Sample2Second(self->player, PlayerA_GetEndSilenceSamples(self));
	
	if (! (flags & PLAYTIME_TIME_PBK))
		secs *= self->player->vtbl.GetPlaybackSpeed(self->player);
	return secs;
}

UINT32 PlayerA_GetCurLoop(const PLAYERA* self)
{
	if (self->player == NULL)
		return (UINT32)-1;
	return self->player->vtbl.GetCurLoop(self->player);
}

double PlayerA_GetLoopTime(const PLAYERA* self)
{
	if (self->player == NULL)
		return -1.0;
	return self->player->vtbl.Tick2Second(self->player, self->player->vtbl.GetLoopTicks(self->player));
}

PEBASE* PlayerA_GetPlayer(PLAYERA* self)
{
	return self->player;
}

const PEBASE* PlayerA_GetPlayerC(const PLAYERA* self)
{
	return self->player;
}

void PlayerA_FindPlayerEngine(PLAYERA* self)
{
	size_t curPlr;
	
	self->player = NULL;
	for (curPlr = 0; curPlr < self->avbPlrs.size; curPlr ++)
	{
		PEBASE* engine = self->avbPlrs.data[curPlr];
		UINT8 retVal = engine->vtbl.CanLoadFile(self->dLoad);
		if (! retVal)
		{
			self->player = engine;
			return;
		}
	}
	
	return;
}

UINT8 PlayerA_LoadFile(PLAYERA* self, DATA_LOADER* dLoad)
{
	self->dLoad = dLoad;
	PlayerA_FindPlayerEngine(self);
	if (self->player == NULL)
		return 0xFF;
	
	// set the configuration so that the configuration is loaded properly
	self->player->vtbl.SetSampleRate(self->player, self->smplRate);
	self->player->vtbl.SetPlaybackSpeed(self->player, self->config.pbSpeed);
	
	UINT8 retVal = self->player->vtbl.LoadFile(self->player, dLoad);
	if (retVal >= 0x80)
		return retVal;
	return retVal;
}

UINT8 PlayerA_UnloadFile(PLAYERA* self)
{
	if (self->player == NULL)
		return 0xFF;
	
	self->player->vtbl.Stop(self->player);
	UINT8 retVal = self->player->vtbl.UnloadFile(self->player);
	self->player = NULL;
	self->dLoad = NULL;
	return retVal;
}

UINT32 PlayerA_GetFileSize(PLAYERA* self)
{
	if (self->dLoad == NULL)
		return 0x00;
	
	UINT32 result = DataLoader_GetTotalSize(self->dLoad);
	if (result != (UINT32)-1)
		return result;
	else
		return DataLoader_GetSize(self->dLoad);
}

UINT8 PlayerA_Start(PLAYERA* self)
{
	if (self->player == NULL)
		return 0xFF;
	self->player->vtbl.SetSampleRate(self->player, self->smplRate);
	self->player->vtbl.SetPlaybackSpeed(self->player, self->config.pbSpeed);
	self->songVolume = PlayerA_CalcSongVolume(self);
	self->fadeSmplStart = (UINT32)-1;
	self->endSilenceStart = (UINT32)-1;
	
	UINT8 retVal = self->player->vtbl.Start(self->player);
	self->myPlayState = self->player->vtbl.GetState(self->player) & (PLAYSTATE_PLAY | PLAYSTATE_END);
	return retVal;
}

UINT8 PlayerA_Stop(PLAYERA* self)
{
	if (self->player == NULL)
		return 0xFF;
	UINT8 retVal = self->player->vtbl.Stop(self->player);
	self->myPlayState = self->player->vtbl.GetState(self->player) & (PLAYSTATE_PLAY | PLAYSTATE_END);
	self->myPlayState |= PLAYSTATE_FIN;
	return retVal;
}

UINT8 PlayerA_Reset(PLAYERA* self)
{
	if (self->player == NULL)
		return 0xFF;
	self->fadeSmplStart = (UINT32)-1;
	self->endSilenceStart = (UINT32)-1;
	UINT8 retVal = self->player->vtbl.Reset(self->player);
	self->myPlayState = self->player->vtbl.GetState(self->player) & (PLAYSTATE_PLAY | PLAYSTATE_END);
	return retVal;
}

UINT8 PlayerA_FadeOut(PLAYERA* self)
{
	if (self->player == NULL)
		return 0xFF;
	if (self->fadeSmplStart == (UINT32)-1)
		self->fadeSmplStart = self->player->vtbl.GetCurPos(self->player, PLAYPOS_SAMPLE);
	return 0x00;
}

UINT8 PlayerA_Seek(PLAYERA* self, UINT8 unit, UINT32 pos)
{
	if (self->player == NULL)
		return 0xFF;
	UINT8 retVal = self->player->vtbl.Seek(self->player, unit, pos);
	self->myPlayState = self->player->vtbl.GetState(self->player) & (PLAYSTATE_PLAY | PLAYSTATE_END);
	
	UINT32 pbSmpl = self->player->vtbl.GetCurPos(self->player, PLAYPOS_SAMPLE);
	if (pbSmpl < self->fadeSmplStart)
		self->fadeSmplStart = (UINT32)-1;
	if (pbSmpl < self->endSilenceStart)
		self->endSilenceStart = (UINT32)-1;
	return retVal;
}

#if 1
#define VOLCALC64
#define VOL_BITS	16	// use .X fixed point for working volume
#else
#define VOL_BITS	8	// use .X fixed point for working volume
#endif
#define VOL_SHIFT	(16 - VOL_BITS)	// shift for master volume -> working volume

// Pre- and post-shifts are used to make the calculations as accurate as possible
// without causing the sample data (likely 24 bits) to overflow while applying the volume gain.
// Smaller values for VOL_PRESH are more accurate, but have a higher risk of overflows during calculations.
// (24 + VOL_POSTSH) must NOT be larger than 31
#define VOL_PRESH	4	// sample data pre-shift
#define VOL_POSTSH	(VOL_BITS - VOL_PRESH)	// post-shift after volume multiplication

// 16.16 fixed point multiplication
#define MUL16X16_FIXED(a, b)	(INT32)(((INT64)a * b) >> 16)

INT32 PlayerA_CalcSongVolume(PLAYERA* self)
{
	INT32 volume = self->config.masterVol;
	
	if (! self->config.ignoreVolGain && self->player != NULL)
	{
		PLR_SONG_INFO songInfo;
		UINT8 retVal = self->player->vtbl.GetSongInfo(self->player, &songInfo);
		if (! retVal)
			volume = MUL16X16_FIXED(volume, songInfo.volGain);
	}
	
	return volume;
}

INT32 PlayerA_CalcCurrentVolume(PLAYERA* self, UINT32 playbackSmpl)
{
	INT32 curVol;	// 16.16 fixed point
	
	// 1. master volume
	curVol = self->songVolume;
	
	// 2. apply fade-out factor
	if (playbackSmpl >= self->fadeSmplStart)
	{
		UINT32 fadeSmpls;
		UINT64 fadeVol;	// 64 bit for less type casts when doing multiplications with .16 fixed point
		
		fadeSmpls = playbackSmpl - self->fadeSmplStart;
		if (fadeSmpls >= self->config.fadeSmpls)
			return 0x0000;	// going beyond fade time -> volume 0
		
		fadeVol = (UINT64)fadeSmpls * 0x10000 / self->config.fadeSmpls;
		fadeVol = 0x10000 - fadeVol;	// fade from full volume to silence
		fadeVol = fadeVol * fadeVol;	// logarithmic fading sounds nicer
		curVol = (INT32)(((INT64)fadeVol * curVol) >> 32);
	}
	
	return curVol;
}

UINT32 PlayerA_Render(PLAYERA* self, UINT32 bufSize, void* data)
{
	UINT8* bData = (UINT8*)data;
	UINT32 basePbSmpl;
	UINT32 smplCount;
	UINT32 smplRendered;
	UINT32 curSmpl;
	WAVE_32BS fnlSmpl;	// final sample value
	INT32 curVolume;
	
	smplCount = bufSize / self->outSmplSizeA;
	if (self->player == NULL)
	{
		memset(data, 0x00, smplCount * self->outSmplSizeA);
		return smplCount * self->outSmplSizeA;
	}
	if (! (self->player->vtbl.GetState(self->player) & PLAYSTATE_PLAY))
	{
		//fprintf(stderr, "Player Warning: calling Render while not playing! playState = 0x%02X\n", self->player->vtbl.GetState(self->player));
		memset(data, 0x00, smplCount * self->outSmplSizeA);
		return smplCount * self->outSmplSizeA;
	}
	
	if (! smplCount)
	{
		self->player->vtbl.Render(self->player, 0, NULL);	// dummy-rendering
		return 0;
	}
	
	if (smplCount > (UINT32)self->smplBuf.size)
		smplCount = (UINT32)self->smplBuf.size;
	memset(self->smplBuf.data, 0, smplCount * sizeof(WAVE_32BS));
	basePbSmpl = self->player->vtbl.GetCurPos(self->player, PLAYPOS_SAMPLE);
	smplRendered = self->player->vtbl.Render(self->player, smplCount, self->smplBuf.data);
	smplCount = smplRendered;
	
	curVolume = PlayerA_CalcCurrentVolume(self, basePbSmpl) >> VOL_SHIFT;
	for (curSmpl = 0; curSmpl < smplCount; curSmpl ++, basePbSmpl ++)
	{
		if (basePbSmpl >= self->fadeSmplStart)
		{
			UINT32 fadeSmpls = basePbSmpl - self->fadeSmplStart;
			if (fadeSmpls >= self->config.fadeSmpls && ! (self->myPlayState & PLAYSTATE_END))
			{
				if (self->endSilenceStart == (UINT32)-1)
					self->endSilenceStart = basePbSmpl;
				self->myPlayState |= PLAYSTATE_END;
			}
			
			curVolume = PlayerA_CalcCurrentVolume(self, basePbSmpl) >> VOL_SHIFT;
		}
		if (basePbSmpl >= self->endSilenceStart)
		{
			UINT32 silenceSmpls = basePbSmpl - self->endSilenceStart;
			if (silenceSmpls >= self->config.endSilenceSmpls && ! (self->myPlayState & PLAYSTATE_FIN))
			{
				self->myPlayState |= PLAYSTATE_FIN;
				if (self->plrCbFunc != NULL)
					self->plrCbFunc(self->player, self->plrCbParam, PLREVT_END, NULL);
				// NOTE: We are effectively discarding rendered samples here!
				// We can get away with that for now, as the application is supposed to
				// stop playback at this point, but we shouldn't really do this.
				break;
			}
		}
		
		// Input is about 24 bits (some cores might output a bit more)
		fnlSmpl = self->smplBuf.data[curSmpl];
		
#ifdef VOLCALC64
		fnlSmpl.L = (INT32)( ((INT64)fnlSmpl.L * curVolume) >> VOL_BITS );
		fnlSmpl.R = (INT32)( ((INT64)fnlSmpl.R * curVolume) >> VOL_BITS );
#else
		fnlSmpl.L = ((fnlSmpl.L >> VOL_PRESH) * curVolume) >> VOL_POSTSH;
		fnlSmpl.R = ((fnlSmpl.R >> VOL_PRESH) * curVolume) >> VOL_POSTSH;
#endif
		
		if (self->config.chnInvert & 0x01)
			fnlSmpl.L = -fnlSmpl.L;
		if (self->config.chnInvert & 0x02)
			fnlSmpl.R = -fnlSmpl.R;
		
		self->outSmplPack(&bData[(curSmpl * 2 + 0) * self->outSmplSize1], fnlSmpl.L);
		self->outSmplPack(&bData[(curSmpl * 2 + 1) * self->outSmplSize1], fnlSmpl.R);
	}
	
	return curSmpl * self->outSmplSizeA;
}

UINT8 PlayerA_PlayCallbackS(PEBASE* player, void* userParam, UINT8 evtType, void* evtParam)
{
	PLAYERA* self = (PLAYERA*)userParam;
	UINT8 retVal = 0x00;
	
	if (evtType != PLREVT_END)	// We will generate our own PLREVT_END event depending on fading/endSilence.
	{
		if (self->plrCbFunc != NULL)
			retVal = self->plrCbFunc(player, self->plrCbParam, evtType, evtParam);
		if (retVal)
			return retVal;
	}
	
	switch(evtType)
	{
	case PLREVT_START:
	case PLREVT_STOP:
		break;
	case PLREVT_LOOP:
		{
			UINT32* curLoop = (UINT32*)evtParam;
			if (self->config.loopCount > 0 && *curLoop >= self->config.loopCount)
			{
				//if (self->config.fadeSmpls == 0)
				//	return 0x01;	// send "stop" signal to player engine
				PlayerA_FadeOut(self);
			}
		}
		break;
	case PLREVT_END:
		self->myPlayState |= PLAYSTATE_END;
		self->endSilenceStart = player->vtbl.GetCurPos(player, PLAYPOS_SAMPLE);
		break;
	}
	return 0x00;
}
