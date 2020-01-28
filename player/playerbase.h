#ifndef PLAYERBASE_H
#define PLAYERBASE_H

/* defines and structs shared between c and c++ APIs */

#ifndef DLLEXPORT
#ifdef _MSC_VER
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT
#endif
#endif

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

#define PLREVT_NONE		0x00
#define PLREVT_START	0x01	// playback started
#define PLREVT_STOP		0x02	// playback stopped
#define PLREVT_LOOP		0x03	// starting next loop [evtParam: UINT32* loopNumber, ret == 0x01 -> stop processing]
#define PLREVT_END		0x04	// reached the end of the song

typedef struct PLR_SONG_INFO PLR_SONG_INFO;
typedef struct PLR_DEV_INFO PLR_DEV_INFO;
typedef struct PLR_MUTE_OPTS PLR_MUTE_OPTS;
typedef struct PLR_DEV_OPTS PLR_DEV_OPTS;

struct PLR_SONG_INFO
{
	UINT32 format;		// four-character-code for file format
	UINT16 fileVerMaj;	// file version (major, encoded as BCD)
	UINT16 fileVerMin;	// file version (minor, encoded as BCD)
	UINT32 tickRateMul;	// internal ticks per second: numerator
	UINT32 tickRateDiv;	// internal ticks per second: denumerator
	// 1 second = 1 tick * tickMult / tickDiv
	UINT32 songLen;		// song length in ticks
	UINT32 loopTick;	// tick position where the loop begins
	UINT32 deviceCnt;	// number of used sound devices
};

struct PLR_DEV_INFO
{
	UINT32 id;		// device ID
	UINT8 type;		// device type
	UINT8 instance;	// instance ID of this device type (0xFF -> N/A for this format)
	UINT16 volume;	// output volume (0x100 = 100%)
	UINT32 core;	// FCC for device emulation core
	UINT32 clock;	// chip clock
	UINT32 cParams;	// additional device configuration parameters (SN76489 params, AY8910 type, ...)
	UINT32 smplRate;	// current sample rate (0 if not running)
};

struct PLR_MUTE_OPTS
{
	UINT8 disable;		// suspend emulation (0x01 = main device, 0x02 = linked, 0xFF = all)
	UINT32 chnMute[2];	// channel muting mask ([1] is used for linked devices)
};

#define PLR_DEV_ID(chip, instance)	(0x80000000 | (instance << 16) | (chip << 0))

struct PLR_DEV_OPTS
{
	UINT32 emuCore;		// enforce a certain sound core (0 = use default)
	UINT8 srMode;		// sample rate mode (see DEVRI_SRMODE)
	UINT8 resmplMode;	// resampling mode (0 - high quality, 1 - low quality, 2 - LQ down, HQ up)
	UINT32 smplRate;	// emulaiton sample rate
	UINT32 coreOpts;
	PLR_MUTE_OPTS muteOpts;
};

#endif // PLAYERBASE_H
