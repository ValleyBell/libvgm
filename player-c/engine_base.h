#ifndef ENGINE_BASE_H
#define ENGINE_BASE_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stddef.h>				// for size_t
#include "../stdtype.h"
#include "../emu/EmuStructs.h"	// for DEV_DECL, DEV_GEN_CFG
#include "../emu/Resampler.h"	// for WAVE_32BS
#include "../utils/DataLoader.h"


typedef struct player_engine_base PEBASE;


// GetState() bit masks
#define PLAYSTATE_PLAY	0x01	// is playing
#define PLAYSTATE_END	0x02	// has reached the end of the file
#define PLAYSTATE_PAUSE	0x04	// is paused (render wave, but don't advance in the song)
#define PLAYSTATE_SEEK	0x08	// is seeking

// GetCurPos()/Seek() units
#define PLAYPOS_FILEOFS	0x00	// file offset (in bytes)
#define PLAYPOS_TICK	0x01	// tick position (scale: internal tick rate)
#define PLAYPOS_SAMPLE	0x02	// sample number (scale: rendering sample rate)
#define PLAYPOS_COMMAND	0x03	// internal command ID

// callback functions and event constants
typedef UINT8 (*PLAYER_EVENT_CB)(PEBASE* pe, void* userParam, UINT8 evtType, void* evtParam);
#define PLREVT_NONE		0x00
#define PLREVT_START	0x01	// playback started
#define PLREVT_STOP		0x02	// playback stopped
#define PLREVT_LOOP		0x03	// starting next loop [evtParam: UINT32* loopNumber, ret == 0x01 -> stop processing]
#define PLREVT_END		0x04	// reached the end of the song

typedef DATA_LOADER* (*PLAYER_FILEREQ_CB)(void* userParam, PEBASE* pe, const char* fileName);

typedef void (*PLAYER_LOG_CB)(void* userParam, PEBASE* player, UINT8 level, UINT8 srcType, const char* srcTag, const char* message);
// log levels
#define PLRLOG_OFF		DEVLOG_OFF
#define PLRLOG_ERROR	DEVLOG_ERROR
#define PLRLOG_WARN		DEVLOG_WARN
#define PLRLOG_INFO		DEVLOG_INFO
#define PLRLOG_DEBUG	DEVLOG_DEBUG
#define PLRLOG_TRACE	DEVLOG_TRACE
// log source types
#define PLRLOGSRC_PLR	0x00	// player
#define PLRLOGSRC_EMU	0x01	// sound emulation


typedef struct player_song_info
{
	UINT32 format;		// four-character-code for file format
	UINT16 fileVerMaj;	// file version (major, encoded as BCD)
	UINT16 fileVerMin;	// file version (minor, encoded as BCD)
	UINT32 tickRateMul;	// internal ticks per second: numerator
	UINT32 tickRateDiv;	// internal ticks per second: denumerator
	// 1 second = 1 tick * tickMult / tickDiv
	UINT32 songLen;		// song length in ticks
	UINT32 loopTick;	// tick position where the loop begins (-1 = no loop)
	INT32 volGain;		// song-specific volume gain, 16.16 fixed point factor (0x10000 = 100%)
	UINT32 deviceCnt;	// number of used sound devices
} PLR_SONG_INFO;

typedef struct player_device_info
{
	UINT32 id;		// device ID
	DEV_ID type;	// device type
	UINT8 instance;	// instance ID of this device type (0xFF -> N/A for this format)
	UINT16 volume;	// output volume (0x100 = 100%)
	UINT32 core;	// FCC of device emulation core
	UINT32 smplRate;	// current sample rate (0 if not running)
	const DEV_DECL* devDecl;	// device declaration
	const DEV_GEN_CFG* devCfg;	// device configuration parameters
} PLR_DEV_INFO;

typedef struct player_mute_options
{
	UINT8 disable;		// suspend emulation (0x01 = main device, 0x02 = linked, 0xFF = all)
	UINT32 chnMute[2];	// channel muting mask ([1] is used for linked devices)
} PLR_MUTE_OPTS;
typedef struct player_pan_options
{
	INT16 chnPan[2][32];	// channel panning [TODO: rethink how this should be really configured]
} PLR_PAN_OPTS;

#define PLR_DEV_ID(chip, instance)	(0x80000000U | (instance << 16) | (chip << 0))

typedef struct player_device_options
{
	UINT32 emuCore[2];	// enforce a certain sound core (0 = use default, [1] is used for linked devices)
	UINT8 srMode;		// sample rate mode (see DEVRI_SRMODE)
	UINT8 resmplMode;	// resampling mode (0 - high quality, 1 - low quality, 2 - LQ down, HQ up)
	UINT32 smplRate;	// emulaiton sample rate
	UINT32 coreOpts;
	PLR_MUTE_OPTS muteOpts;
	PLR_PAN_OPTS panOpts;
} PLR_DEV_OPTS;

typedef struct player_generic_options
{
	UINT32 pbSpeed; // playback speed (16.16 fixed point scale, 0x10000 = 100%)
} PLR_GEN_OPTS;


// TODO: move these into "helper.h"
#define DEFINE_PE_ARRAY(T, name)	\
	typedef struct {	\
		size_t size;	\
		T* data;		\
	} name;
#define PE_ARRAY_MALLOC(var, T, count)	{ \
	(var).data = (T*)malloc(sizeof(T) * (count));	\
	(var).size = count; }
#define PE_ARRAY_CALLOC(var, T, count)	{ \
	(var).data = (T*)calloc(count, sizeof(T));	\
	(var).size = count; }
#define PE_ARRAY_FREE(var)	{ \
	free((var).data);	(var).data = NULL;	\
	(var).size = 0; }

#define DEFINE_PE_VECTOR(T, name)	\
	typedef struct {	\
		size_t alloc;	\
		size_t size;	\
		T* data;		\
	} name;
#define PE_VECTOR_ALLOC(var, T, count)	{ \
	(var).data = (T*)malloc(sizeof(T) * (count));	\
	(var).alloc = count; \
	(var).size = 0; }
#define PE_VECTOR_REALLOC(var, T, count)	{ \
	(var).data = (T*)realloc((var).data, sizeof(T) * (count));	\
	(var).alloc = count; }
#define PE_VECTOR_FREE(var)	{ \
	free((var).data);	(var).data = NULL;	\
	(var).alloc = 0; \
	(var).size = 0; }


//	--- concept ---
//	- Player class does file rendering at fixed volume (but changeable speed)
//	- host program handles master volume + fading + stopping after X loops (notified via callback)

struct player_engine_vtable
{
	UINT32 playerType;
	const char* playerName;
	
	void (*Init)(PEBASE* self);
	void (*Deinit)(PEBASE* self);
	
	UINT8 (*CanLoadFile)(DATA_LOADER *dataLoader);
	UINT8 (*LoadFile)(PEBASE* self, DATA_LOADER *dataLoader);
	UINT8 (*UnloadFile)(PEBASE* self);
	
	const char* const* (*GetTags)(PEBASE* self);
	UINT8 (*GetSongInfo)(PEBASE* self, PLR_SONG_INFO* songInf);
	UINT8 (*GetSongDeviceInfo)(const PEBASE* self, size_t* retDevInfCount, PLR_DEV_INFO** retDevInfData);
	//static UINT8 InitDeviceOptions(PLR_DEV_OPTS* devOpts);
	UINT8 (*SetDeviceOptions)(PEBASE* self, UINT32 id, const PLR_DEV_OPTS* devOpts);
	UINT8 (*GetDeviceOptions)(const PEBASE* self, UINT32 id, PLR_DEV_OPTS* devOpts);
	UINT8 (*SetDeviceMuting)(PEBASE* self, UINT32 id, const PLR_MUTE_OPTS* muteOpts);
	UINT8 (*GetDeviceMuting)(const PEBASE* self, UINT32 id, PLR_MUTE_OPTS* muteOpts);
	// player-specific options
	UINT8 (*SetPlayerOptions)(PEBASE* self, const PLR_GEN_OPTS* playOpts);
	UINT8 (*GetPlayerOptions)(const PEBASE* self, PLR_GEN_OPTS* playOpts);
	
	UINT32 (*GetSampleRate)(const PEBASE* self);
	UINT8 (*SetSampleRate)(PEBASE* self, UINT32 sampleRate);
	double (*GetPlaybackSpeed)(const PEBASE* self);
	UINT8 (*SetPlaybackSpeed)(PEBASE* self, double speed);
	void (*SetUserDevices)(PEBASE* self, const DEV_DECL** userDevList, UINT8 devStartOpts);
	void (*SetEventCallback)(PEBASE* self, PLAYER_EVENT_CB cbFunc, void* cbParam);
	void (*SetFileReqCallback)(PEBASE* self, PLAYER_FILEREQ_CB cbFunc, void* cbParam);
	void (*SetLogCallback)(PEBASE* self, PLAYER_LOG_CB cbFunc, void* cbParam);
	UINT32 (*Tick2Sample)(const PEBASE* self, UINT32 ticks);
	UINT32 (*Sample2Tick)(const PEBASE* self, UINT32 samples);
	double (*Tick2Second)(const PEBASE* self, UINT32 ticks);
	double (*Sample2Second)(const PEBASE* self, UINT32 samples);
	
	UINT8 (*GetState)(const PEBASE* self);			// get playback state (playing / paused / ...)
	UINT32 (*GetCurPos)(const PEBASE* self, UINT8 unit);	// get current playback position
	UINT32 (*GetCurLoop)(const PEBASE* self);		// get current loop index (0 = 1st loop, 1 = 2nd loop, ...)
	UINT32 (*GetTotalTicks)(const PEBASE* self);	// get time for playing once in ticks
	UINT32 (*GetLoopTicks)(const PEBASE* self);	// get time for one loop in ticks
	UINT32 (*GetTotalPlayTicks)(const PEBASE* self, UINT32 numLoops);	// get time for playing + looping (without fading)
	
	UINT8 (*Start)(PEBASE* self);
	UINT8 (*Stop)(PEBASE* self);
	UINT8 (*Reset)(PEBASE* self);
	UINT8 (*Seek)(PEBASE* self, UINT8 unit, UINT32 pos); // seek to playback position
	UINT32 (*Render)(PEBASE* self, UINT32 smplCnt, WAVE_32BS* data);
};

struct player_engine_base	// TODO: call it LVPE_BASE? (lvpe = LibVgmPlayerEngine)
{
	struct player_engine_vtable vtbl;
	
	// private members
	UINT32 outSmplRate;
	const DEV_DECL** userDevList;
	UINT8 devStartOpts;
	PLAYER_EVENT_CB eventCbFunc;
	void* eventCbParam;
	PLAYER_FILEREQ_CB fileReqCbFunc;
	void* fileReqCbParam;
	PLAYER_LOG_CB logCbFunc;
	void* logCbParam;
};

PEBASE* PBaseEngine_Create(void);
void PBaseEngine_Destroy(PEBASE* self);
void PBaseEngine_Init(PEBASE* self);
void PBaseEngine_Deinit(PEBASE* self);
UINT8 PBaseEngine_InitDeviceOptions(PLR_DEV_OPTS* devOpts);
UINT32 PBaseEngine_GetSampleRate(const PEBASE* self);
UINT8 PBaseEngine_SetSampleRate(PEBASE* self, UINT32 sampleRate);
double PBaseEngine_GetPlaybackSpeed(const PEBASE* self);
UINT8 PBaseEngine_SetPlaybackSpeed(PEBASE* self, double speed);
void PBaseEngine_SetUserDevices(PEBASE* self, const DEV_DECL** userDevList, UINT8 devStartOpts);
void PBaseEngine_SetEventCallback(PEBASE* self, PLAYER_EVENT_CB cbFunc, void* cbParam);
void PBaseEngine_SetFileReqCallback(PEBASE* self, PLAYER_FILEREQ_CB cbFunc, void* cbParam);
void PBaseEngine_SetLogCallback(PEBASE* self, PLAYER_LOG_CB cbFunc, void* cbParam);
double PBaseEngine_Sample2Second(const PEBASE* self, UINT32 samples);
UINT32 PBaseEngine_GetTotalPlayTicks(const PEBASE* self, UINT32 numLoops);

#ifdef __cplusplus
}
#endif

#endif	// ENGINE_BASE_H
