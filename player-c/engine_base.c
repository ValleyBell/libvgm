#include <stddef.h>
#include "engine_base.h"

#include <stdlib.h>
#include <string.h>	// for memset()

static struct player_engine_vtable PBaseEngine_vtbl;

PEBASE* PBaseEngine_Create(void)
{
	PEBASE* self = malloc(sizeof(PEBASE));
	if (self == NULL)
		return NULL;
	
	self->vtbl = PBaseEngine_vtbl;
	self->vtbl.Init(self);
	return self;
}

void PBaseEngine_Destroy(PEBASE* self)
{
	self->vtbl.Deinit(self);
	free(self);
	return;
}

void PBaseEngine_Init(PEBASE* self)
{
	self->outSmplRate = 0;
	self->userDevList = NULL;
	self->devStartOpts = 0;
	self->eventCbFunc = NULL;
	self->eventCbParam = NULL;
	self->fileReqCbFunc = NULL;
	self->fileReqCbParam = NULL;
	self->logCbFunc = NULL;
	self->logCbParam = NULL;
	return;
}

void PBaseEngine_Deinit(PEBASE* self)
{
	return;
}

UINT8 PBaseEngine_InitDeviceOptions(PLR_DEV_OPTS* devOpts)
{
	devOpts->emuCore[0] = 0x00;
	devOpts->emuCore[1] = 0x00;
	devOpts->srMode = DEVRI_SRMODE_NATIVE;
	devOpts->resmplMode = 0x00;
	devOpts->smplRate = 0;
	devOpts->coreOpts = 0x00;
	devOpts->muteOpts.disable = 0x00;
	devOpts->muteOpts.chnMute[0] = 0x00;
	devOpts->muteOpts.chnMute[1] = 0x00;
	memset(devOpts->panOpts.chnPan, 0x00, sizeof(devOpts->panOpts.chnPan));
	return 0x00;
}

UINT32 PBaseEngine_GetSampleRate(const PEBASE* self)
{
	return self->outSmplRate;
}

UINT8 PBaseEngine_SetSampleRate(PEBASE* self, UINT32 sampleRate)
{
	self->outSmplRate = sampleRate;
	return 0x00;
}

double PBaseEngine_GetPlaybackSpeed(const PEBASE* self)
{
	return -1;	// not implemented
}

UINT8 PBaseEngine_SetPlaybackSpeed(PEBASE* self, double speed)
{
	return 0xFF;	// not implemented
}

void PBaseEngine_SetUserDevices(PEBASE* self, const DEV_DECL** userDevList, UINT8 devStartOpts)
{
	self->userDevList = userDevList;
	self->devStartOpts = devStartOpts;
	
	return;
}

void PBaseEngine_SetEventCallback(PEBASE* self, PLAYER_EVENT_CB cbFunc, void* cbParam)
{
	self->eventCbFunc = cbFunc;
	self->eventCbParam = cbParam;
	
	return;
}

void PBaseEngine_SetFileReqCallback(PEBASE* self, PLAYER_FILEREQ_CB cbFunc, void* cbParam)
{
	self->fileReqCbFunc = cbFunc;
	self->fileReqCbParam = cbParam;
	
	return;
}

void PBaseEngine_SetLogCallback(PEBASE* self, PLAYER_LOG_CB cbFunc, void* cbParam)
{
	self->logCbFunc = cbFunc;
	self->logCbParam = cbParam;
	
	return;
}

double PBaseEngine_Sample2Second(const PEBASE* self, UINT32 samples)
{
	if (samples == (UINT32)-1)
		return -1.0;
	return samples / (double)self->outSmplRate;
}

UINT32 PBaseEngine_GetTotalPlayTicks(const PEBASE* self, UINT32 numLoops)
{
	if (numLoops == 0 && self->vtbl.GetLoopTicks(self) > 0)
		return (UINT32)-1;
	return self->vtbl.GetTotalTicks(self) + self->vtbl.GetLoopTicks(self) * (numLoops - 1);
}

static struct player_engine_vtable PBaseEngine_vtbl =
{
	0x00000000,	// playerType
	"",	// playerName
	
	PBaseEngine_Init,	// Init
	PBaseEngine_Deinit,	// Deinit
	
	NULL,	// CanLoadFile
	NULL,	// LoadFile
	NULL,	// UnloadFile
	
	NULL,	// GetTags
	NULL,	// GetSongInfo
	NULL,	// GetSongDeviceInfo
	//static UINT8 InitDeviceOptions(PLR_DEV_OPTS* devOpts);
	NULL,	// SetDeviceOptions
	NULL,	// GetDeviceOptions
	NULL,	// SetDeviceMuting
	NULL,	// GetDeviceMuting
	// player-specific options
	NULL,	// SetPlayerOptions
	NULL,	// GetPlayerOptions
	
	PBaseEngine_GetSampleRate,	// GetSampleRate
	PBaseEngine_SetSampleRate,	// SetSampleRate
	PBaseEngine_GetPlaybackSpeed,	// GetPlaybackSpeed
	PBaseEngine_SetPlaybackSpeed,	// SetPlaybackSpeed
	PBaseEngine_SetUserDevices,	// SetUserDevices
	PBaseEngine_SetEventCallback,	// SetEventCallback
	PBaseEngine_SetFileReqCallback,	// SetFileReqCallback
	PBaseEngine_SetLogCallback,	// SetLogCallback
	NULL,	// Tick2Sample
	NULL,	// Sample2Tick
	NULL,	// Tick2Second
	PBaseEngine_Sample2Second,	// Sample2Second
	
	NULL,	// GetState
	NULL,	// GetCurPos
	NULL,	// GetCurLoop
	NULL,	// GetTotalTicks
	NULL,	// GetLoopTicks
	PBaseEngine_GetTotalPlayTicks,	// GetTotalPlayTicks
	
	NULL,	// Start
	NULL,	// Stop
	NULL,	// Reset
	NULL,	// Seek
	NULL,	// Render
};
