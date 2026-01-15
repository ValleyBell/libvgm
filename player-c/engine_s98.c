#include <stdlib.h>
#include <string.h>
#include <stdio.h>	// for snprintf()
#include <ctype.h>

#ifdef _MSC_VER
#define stricmp		_stricmp
#define strdup		_strdup
#else
#define stricmp		strcasecmp
//#define strdup		strdup
#endif

#include "../common_def.h"
#include "engine_s98.h"
#include "../emu/EmuStructs.h"
#include "../emu/SoundEmu.h"
#include "../emu/Resampler.h"
#include "../emu/SoundDevs.h"
#include "../emu/EmuCores.h"
#include "../emu/cores/sn764intf.h"	// for SN76496_CFG
#include "../emu/cores/ayintf.h"	// for AY8910_CFG
#include "../utils/StrUtils.h"
#include "../player/helper.h"
#include "../emu/logging.h"

#ifdef _MSC_VER
#define snprintf	_snprintf
#endif


DEFINE_PE_ARRAY(UINT8, ARR_UINT8)
typedef struct s98_device_config
{
	ARR_UINT8 data;
} S98_DEVCFG;
typedef struct device_log_callback_data
{
	PE_S98* player;
	size_t chipDevID;
} DEVLOG_CB_DATA;
typedef struct s98_chip_device
{
	VGM_BASEDEV base;
	size_t optID;
	ARR_UINT8 cfg;
	DEVFUNC_WRITE_A8D8 write;
	DEVLOG_CB_DATA logCbData;
} S98_CHIPDEV;
typedef struct devlink_cb_data
{
	PE_S98* player;
	S98_CHIPDEV* chipDev;
} DEVLINK_CB_DATA;

enum S98_DEVTYPES
{
	S98DEV_NONE = 0,	// S98 v2 End-Of-Device marker
	S98DEV_PSGYM = 1,	// YM2149
	S98DEV_OPN = 2,		// YM2203
	S98DEV_OPN2 = 3,	// YM2612
	S98DEV_OPNA = 4,	// YM2608
	S98DEV_OPM = 5,		// YM2151
	// S98 v3 device types
	S98DEV_OPLL = 6,	// YM2413
	S98DEV_OPL = 7,		// YM3526
	S98DEV_OPL2 = 8,	// YM3812
	S98DEV_OPL3 = 9,	// YMF262
	S98DEV_PSGAY = 15,	// AY-3-8910
	S98DEV_DCSG = 16,	// SN76489
	S98DEV_END
};
static const DEV_ID S98_DEV_LIST[S98DEV_END] = {
	0xFF,
	DEVID_AY8910, DEVID_YM2203, DEVID_YM2612, DEVID_YM2608,
	DEVID_YM2151, DEVID_YM2413, DEVID_YM3526, DEVID_YM3812,
	DEVID_YMF262, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, DEVID_AY8910, DEVID_SN76496,
};

static const char* const S98_TAG_MAPPING[] =
{
	"TITLE", "TITLE",
	"ARTIST", "ARTIST",
	"GAME", "GAME",
	"YEAR", "DATE",
	"GENRE", "GENRE",
	"COMMENT", "COMMENT",
	"COPYRIGHT", "COPYRIGHT",
	"S98BY", "ENCODED_BY",
	"SYSTEM", "SYSTEM",
	NULL,
};

#define OPT_DEV_COUNT	0x0A

static const DEV_ID OPT_DEV_LIST[OPT_DEV_COUNT] =	// list of configurable libvgm devices
{
	DEVID_AY8910, DEVID_YM2203, DEVID_YM2612, DEVID_YM2608,
	DEVID_YM2151, DEVID_YM2413, DEVID_YM3526, DEVID_YM3812,
	DEVID_YMF262, DEVID_SN76496,
};

DEFINE_PE_ARRAY(S98_HDR_DEVICE, ARR_DEVHDR)
DEFINE_PE_ARRAY(S98_DEVCFG, ARR_DEVCFG)
DEFINE_PE_ARRAY(S98_CHIPDEV, ARR_CHIPDEV)
DEFINE_PE_ARRAY(const char*, ARR_CSTR)
DEFINE_PE_VECTOR(char*, VEC_STR)	// vector of strings
DEFINE_PE_VECTOR(const char*, VEC_CSTR)
DEFINE_PE_VECTOR(char, VSTR)	// string represented using the VECTOR struct
#define selfcall	self->pe.vtbl


struct player_engine_s98
{
	PEBASE pe;
	
	CPCONV* cpcSJIS;	// ShiftJIS -> UTF-8 codepage conversion
	DEV_LOGGER logger;
	DATA_LOADER *dLoad;
	const UINT8* fileData;	// data pointer for quick access, equals self->dLoad->GetFileData().data()

	S98_HEADER fileHdr;
	ARR_DEVHDR devHdrs;
	ARR_DEVCFG devCfgs;
	UINT32 totalTicks;
	UINT32 loopTick;
	//VEC_STR tagData;	// strings for tags (stores allocated pointers) [-> replaced by tagMap]
	VEC_STR tagMap;	// key-value pairs of tags (stores allocated pointers)
	VEC_CSTR tagList;	// tag list that only stores references

	// tick/sample conversion rates
	UINT64 tsMult;
	UINT64 tsDiv;
	UINT64 ttMult;
	UINT64 lastTsMult;
	UINT64 lastTsDiv;

	S98_PLAY_OPTIONS playOpts;
	PLR_DEV_OPTS devOpts[OPT_DEV_COUNT * 2];	// space for 2 instances per chip
	size_t devOptMap[0x100][2];	// maps libvgm device ID to self->devOpts vector
	ARR_CHIPDEV devices;
	ARR_CSTR devNames;
	size_t optDevMap[OPT_DEV_COUNT * 2];	// maps self->devOpts vector index to self->devices vector

	UINT32 filePos;
	UINT32 fileTick;
	UINT32 playTick;
	UINT32 playSmpl;
	UINT32 curLoop;
	UINT32 lastLoopTick;

	UINT8 playState;
	UINT8 psTrigger;	// used to temporarily trigger special commands
};


static struct player_engine_vtable S98Engine_vtbl;

INLINE UINT32 ReadLE32(const UINT8* data);
INLINE void SaveDeviceConfig(ARR_UINT8* dst, const void* srcData, size_t srcLen);
static void FreeStringVectorData(VEC_STR* vec);

PE_S98* S98Engine_Create(void);
void S98Engine_Destroy(PE_S98* self);
void S98Engine_Init(PE_S98* self);
void S98Engine_Deinit(PE_S98* self);

UINT8 S98Engine_CanLoadFile(DATA_LOADER *dataLoader);
UINT8 S98Engine_LoadFile(PE_S98* self, DATA_LOADER *dataLoader);
static void S98Engine_CalcSongLength(PE_S98* self);
static UINT8 S98Engine_LoadTags(PE_S98* self);
static char* S98Engine_GetUTF8String(PE_S98* self, const char* startPtr, const char* endPtr);
static char* TrimPSFTagWhitespace(char* data);
static UINT8 ExtractKeyValue(char* line, char** retKey, char** retValue);
static UINT8 S98Engine_ParsePSFTags(PE_S98* self, const char* tagData);
UINT8 S98Engine_UnloadFile(PE_S98* self);
const S98_HEADER* S98Engine_GetFileHeader(const PE_S98* self);

const char* const* S98Engine_GetTags(PE_S98* self);
UINT8 S98Engine_GetSongInfo(PE_S98* self, PLR_SONG_INFO* songInf);
UINT8 S98Engine_GetSongDeviceInfo(const PE_S98* self, size_t* retDevInfCount, PLR_DEV_INFO** retDevInfData);
static UINT8 S98Engine_GetDeviceInstance(const PE_S98* self, size_t id);
static size_t S98Engine_DeviceID2OptionID(const PE_S98* self, UINT32 id);
static void S98Engine_RefreshMuting(PE_S98* self, S98_CHIPDEV* chipDev, const PLR_MUTE_OPTS* muteOpts);
static void S98Engine_RefreshPanning(PE_S98* self, S98_CHIPDEV* chipDev, const PLR_PAN_OPTS* panOpts);
UINT8 S98Engine_SetDeviceOptions(PE_S98* self, UINT32 id, const PLR_DEV_OPTS* devOpts);
UINT8 S98Engine_GetDeviceOptions(const PE_S98* self, UINT32 id, PLR_DEV_OPTS* devOpts);
UINT8 S98Engine_SetDeviceMuting(PE_S98* self, UINT32 id, const PLR_MUTE_OPTS* muteOpts);
UINT8 S98Engine_GetDeviceMuting(const PE_S98* self, UINT32 id, PLR_MUTE_OPTS* muteOpts);
UINT8 S98Engine_SetPlayerOptions(PE_S98* self, const S98_PLAY_OPTIONS* playOpts);
UINT8 S98Engine_GetPlayerOptions(const PE_S98* self, S98_PLAY_OPTIONS* playOpts);

UINT8 S98Engine_SetSampleRate(PE_S98* self, UINT32 sampleRate);
double S98Engine_GetPlaybackSpeed(const PE_S98* self);
UINT8 S98Engine_SetPlaybackSpeed(PE_S98* self, double speed);
static void S98Engine_RefreshTSRates(PE_S98* self);
UINT32 S98Engine_Tick2Sample(const PE_S98* self, UINT32 ticks);
UINT32 S98Engine_Sample2Tick(const PE_S98* self, UINT32 samples);
double S98Engine_Tick2Second(const PE_S98* self, UINT32 ticks);

UINT8 S98Engine_GetState(const PE_S98* self);
UINT32 S98Engine_GetCurPos(const PE_S98* self, UINT8 unit);
UINT32 S98Engine_GetCurLoop(const PE_S98* self);
UINT32 S98Engine_GetTotalTicks(const PE_S98* self);	// get time for playing once in ticks
UINT32 S98Engine_GetLoopTicks(const PE_S98* self);	// get time for one loop in ticks

UINT8 S98Engine_Start(PE_S98* self);
UINT8 S98Engine_Stop(PE_S98* self);
UINT8 S98Engine_Reset(PE_S98* self);
UINT8 S98Engine_Seek(PE_S98* self, UINT8 unit, UINT32 pos);
UINT32 S98Engine_Render(PE_S98* self, UINT32 smplCnt, WAVE_32BS* data);

static void S98Engine_PlayerLogCB(void* userParam, void* source, UINT8 level, const char* message);
static void S98Engine_SndEmuLogCB(void* userParam, void* source, UINT8 level, const char* message);

static void S98Engine_GenerateDeviceConfig(PE_S98* self);
static void S98Engine_DeviceLinkCallback(void* userParam, VGM_BASEDEV* cDev, DEVLINK_INFO* dLink);
static UINT8 S98Engine_SeekToTick(PE_S98* self, UINT32 tick);
static UINT8 S98Engine_SeekToFilePos(PE_S98* self, UINT32 pos);
static void S98Engine_ParseFile(PE_S98* self, UINT32 ticks);
static void S98Engine_HandleEOF(PE_S98* self);
static void S98Engine_DoCommand(PE_S98* self);
static void S98Engine_DoRegWrite(PE_S98* self, UINT8 deviceID, UINT8 port, UINT8 reg, UINT8 data);
static UINT32 S98Engine_ReadVarInt(PE_S98* self, UINT32* filePos);


INLINE UINT32 ReadLE32(const UINT8* data)
{
	return	(data[0x03] << 24) | (data[0x02] << 16) |
			(data[0x01] <<  8) | (data[0x00] <<  0);
}

INLINE void SaveDeviceConfig(ARR_UINT8* dst, const void* srcData, size_t srcLen)
{
	PE_ARRAY_FREE(*dst)
	PE_ARRAY_MALLOC(*dst, UINT8, srcLen)
	memcpy(dst->data, srcData, dst->size);
	return;
}

static void FreeStringVectorData(VEC_STR* vec)
{
	size_t idx;

	for (idx = 0; idx < vec->size; idx ++)
	{
		free(vec->data[idx]);
		vec->data[idx] = NULL;
	}
	vec->size = 0;

	return;
}

PE_S98* S98Engine_Create(void)
{
	PE_S98* self = malloc(sizeof(PE_S98));
	if (self == NULL)
		return NULL;

	self->pe.vtbl = S98Engine_vtbl;
	selfcall.Init(&self->pe);
	return self;
}

void S98Engine_Destroy(PE_S98* self)
{
	selfcall.Deinit(&self->pe);
	free(self);
	return;
}

void S98Engine_Init(PE_S98* self)
{
	UINT8 retVal;
	UINT16 optChip;
	UINT8 chipID;

	PBaseEngine_Init(&self->pe);
	
	self->devCfgs.data = NULL;
	self->devices.data = NULL;
	self->devNames.data = NULL;

	memset(&self->fileHdr, 0x00, sizeof(self->fileHdr));
	self->filePos = 0;
	self->fileTick = 0;
	self->playTick = 0;
	self->playSmpl = 0;
	self->curLoop = 0;
	self->playState = 0x00;
	self->psTrigger = 0x00;

	dev_logger_set(&self->logger, self, S98Engine_PlayerLogCB, NULL);

	self->playOpts.genOpts.pbSpeed = 0x10000;

	self->lastTsMult = 0;
	self->lastTsDiv = 0;

	for (optChip = 0x00; optChip < 0x100; optChip ++)
	{
		for (chipID = 0; chipID < 2; chipID ++)
			self->devOptMap[optChip][chipID] = (size_t)-1;
	}
	for (optChip = 0; optChip < OPT_DEV_COUNT; optChip ++)
	{
		for (chipID = 0; chipID < 2; chipID ++)
		{
			size_t optID = optChip * 2 + chipID;
			PBaseEngine_InitDeviceOptions(&self->devOpts[optID]);
			self->devOptMap[OPT_DEV_LIST[optChip]][chipID] = optID;
		}
	}
	
	retVal = CPConv_Init(&self->cpcSJIS, "CP932", "UTF-8");
	if (retVal)
		self->cpcSJIS = NULL;
	PE_VECTOR_ALLOC(self->tagMap, char*, 16);
	PE_VECTOR_ALLOC(self->tagList, const char*, 16);
	self->tagList.data[self->tagList.size] = NULL;
	self->tagList.size ++;
}

void S98Engine_Deinit(PE_S98* self)
{
	self->pe.eventCbFunc = NULL;	// prevent any callbacks during destruction
	
	if (self->playState & PLAYSTATE_PLAY)
		selfcall.Stop(&self->pe);
	selfcall.UnloadFile(&self->pe);
	
	if (self->cpcSJIS != NULL)
		CPConv_Deinit(self->cpcSJIS);
	PE_ARRAY_FREE(self->devCfgs);
	PE_ARRAY_FREE(self->devNames);

	FreeStringVectorData(&self->tagMap);
	PE_VECTOR_FREE(self->tagMap);
	PE_VECTOR_FREE(self->tagList);

	PBaseEngine_Deinit(&self->pe);
	return;
}

UINT8 S98Engine_CanLoadFile(DATA_LOADER *dataLoader)
{
	DataLoader_ReadUntil(dataLoader,0x20);
	if (DataLoader_GetSize(dataLoader) < 0x20)
		return 0xF1;	// file too small
	if (memcmp(&DataLoader_GetData(dataLoader)[0x00], "S98", 3))
		return 0xF0;	// invalid signature
	return 0x00;
}

UINT8 S98Engine_LoadFile(PE_S98* self, DATA_LOADER *dataLoader)
{
	UINT32 devCount;
	UINT32 curDev;
	UINT32 curPos;
	
	self->dLoad = NULL;
	DataLoader_ReadUntil(dataLoader,0x20);
	self->fileData = DataLoader_GetData(dataLoader);
	if (DataLoader_GetSize(dataLoader) < 0x20 || memcmp(&self->fileData[0x00], "S98", 3))
		return 0xF0;	// invalid file
	if (! (self->fileData[0x03] >= '0' && self->fileData[0x03] <= '3'))
		return 0xF1;	// unsupported version
	
	self->dLoad = dataLoader;
	DataLoader_ReadAll(self->dLoad);
	self->fileData = DataLoader_GetData(self->dLoad);
	
	self->fileHdr.fileVer = self->fileData[0x03] - '0';
	self->fileHdr.tickMult = ReadLE32(&self->fileData[0x04]);
	self->fileHdr.tickDiv = ReadLE32(&self->fileData[0x08]);
	self->fileHdr.compression = ReadLE32(&self->fileData[0x0C]);
	self->fileHdr.tagOfs = ReadLE32(&self->fileData[0x10]);
	self->fileHdr.dataOfs = ReadLE32(&self->fileData[0x14]);
	self->fileHdr.loopOfs = ReadLE32(&self->fileData[0x18]);
	
	PE_ARRAY_FREE(self->devHdrs);
	switch(self->fileHdr.fileVer)
	{
	case 0:
		self->fileHdr.tickMult = 0;
		// fall through
	case 1:
		self->fileHdr.tickDiv = 0;
		// only default device available
		break;
	case 2:
		curPos = 0x20;
		for (devCount = 0; ; devCount ++, curPos += 0x10)
		{
			if (ReadLE32(&self->fileData[curPos + 0x00]) == S98DEV_NONE)
				break;	// stop at device type 0
		}
		
		curPos = 0x20;
		PE_ARRAY_MALLOC(self->devHdrs, S98_HDR_DEVICE, devCount);
		for (curDev = 0; curDev < devCount; curDev ++, curPos += 0x10)
		{
			self->devHdrs.data[curDev].devType = ReadLE32(&self->fileData[curPos + 0x00]);
			self->devHdrs.data[curDev].clock = ReadLE32(&self->fileData[curPos + 0x04]);
			self->devHdrs.data[curDev].pan = 0;
			self->devHdrs.data[curDev].app_spec = ReadLE32(&self->fileData[curPos + 0x0C]);
		}
		break;	// not supported yet
	case 3:
		devCount = ReadLE32(&self->fileData[0x1C]);
		curPos = 0x20;
		PE_ARRAY_MALLOC(self->devHdrs, S98_HDR_DEVICE, devCount);
		for (curDev = 0; curDev < devCount; curDev ++, curPos += 0x10)
		{
			self->devHdrs.data[curDev].devType = ReadLE32(&self->fileData[curPos + 0x00]);
			self->devHdrs.data[curDev].clock = ReadLE32(&self->fileData[curPos + 0x04]);
			self->devHdrs.data[curDev].pan = ReadLE32(&self->fileData[curPos + 0x08]);
			self->devHdrs.data[curDev].app_spec = 0;
		}
		break;
	}
	if (self->devHdrs.size == 0)
	{
		PE_ARRAY_MALLOC(self->devHdrs, S98_HDR_DEVICE, devCount);
		curDev = 0;
		self->devHdrs.data[curDev].devType = S98DEV_OPNA;
		self->devHdrs.data[curDev].clock = 7987200;
		self->devHdrs.data[curDev].pan = 0;
		self->devHdrs.data[curDev].app_spec = 0;
	}
	
	if (! self->fileHdr.tickMult)
		self->fileHdr.tickMult = 10;
	if (! self->fileHdr.tickDiv)
		self->fileHdr.tickDiv = 1000;
	
	S98Engine_GenerateDeviceConfig(self);
	S98Engine_CalcSongLength(self);
	
	if (self->fileHdr.loopOfs)
	{
		if (self->fileHdr.loopOfs < self->fileHdr.dataOfs || self->fileHdr.loopOfs >= DataLoader_GetSize(self->dLoad))
		{
			emu_logf(&self->logger, PLRLOG_WARN, "Invalid loop offset 0x%06X - ignoring!\n", self->fileHdr.loopOfs);
			self->fileHdr.loopOfs = 0x00;
		}
		if (self->fileHdr.loopOfs && self->loopTick == self->totalTicks)
		{
			// 0-Sample-Loops causes the program to hang in the playback routine
			emu_logf(&self->logger, PLRLOG_WARN, "Warning! Ignored Zero-Sample-Loop!\n");
			self->fileHdr.loopOfs = 0x00;
		}
	}
	
	// parse tags
	S98Engine_LoadTags(self);
	
	S98Engine_RefreshTSRates(self);	// make Tick2Sample etc. work
	
	return 0x00;
}

static void S98Engine_CalcSongLength(PE_S98* self)
{
	UINT32 filePos;
	bool fileEnd;
	UINT8 curCmd;
	
	self->totalTicks = 0;
	self->loopTick = 0;
	
	fileEnd = false;
	filePos = self->fileHdr.dataOfs;
	while(! fileEnd && filePos < DataLoader_GetSize(self->dLoad))
	{
		if (filePos == self->fileHdr.loopOfs)
			self->loopTick = self->totalTicks;
		
		curCmd = self->fileData[filePos];
		filePos ++;
		switch(curCmd)
		{
		case 0xFF:	// advance 1 tick
			self->totalTicks ++;
			break;
		case 0xFE:	// advance multiple ticks
			self->totalTicks += 2 + S98Engine_ReadVarInt(self, &filePos);
			break;
		case 0xFD:
			fileEnd = true;
			break;
		default:
			filePos += 0x02;
			break;
		}
	}
	
	return;
}

static UINT8 S98Engine_LoadTags(PE_S98* self)
{
	const char* startPtr;
	const char* endPtr;
	size_t tagStrIdx;

	FreeStringVectorData(&self->tagMap);
	self->tagList.size = 0;
	self->tagList.data[self->tagList.size] = NULL;
	self->tagList.size ++;

	if (! self->fileHdr.tagOfs)
		return 0x00;
	if (self->fileHdr.tagOfs >= DataLoader_GetSize(self->dLoad))
		return 0xF3;	// tag error (offset out-of-range)
	
	// find end of string (can be either '\0' or EOF)
	startPtr = (const char*)&self->fileData[self->fileHdr.tagOfs];
	endPtr = (const char*)memchr(startPtr, '\0', DataLoader_GetSize(self->dLoad) - self->fileHdr.tagOfs);
	if (endPtr == NULL)
		endPtr = (const char*)self->fileData + DataLoader_GetSize(self->dLoad);
	
	if (self->fileHdr.fileVer < 3)
	{
		// tag offset = song title (\0-terminated)
		self->tagMap.data[self->tagMap.size + 0] = strdup("TITLE");
		self->tagMap.data[self->tagMap.size + 1] = S98Engine_GetUTF8String(self, startPtr, endPtr);
		self->tagMap.size += 2;
	}
	else
	{
		char* tagData;
		UINT8 tagIsUTF8 = 0;
		
		// tag offset = PSF tag
		if (endPtr - startPtr < 5 || memcmp(startPtr, "[S98]", 5))
		{
			emu_logf(&self->logger, PLRLOG_ERROR, "Invalid S98 tag data!\n");
			emu_logf(&self->logger, PLRLOG_DEBUG, "tagData size: %zu, Signature: %.5s\n", endPtr - startPtr, startPtr);
			return 0xF0;
		}
		startPtr += 5;
		if (endPtr - startPtr >= 3)
		{
			if (! memcmp(&startPtr[0], "\xEF\xBB\xBF", 3))	// check for UTF-8 BOM
			{
				tagIsUTF8 = 1;
				startPtr += 3;
				emu_logf(&self->logger, PLRLOG_DEBUG, "Note: Tags are UTF-8 encoded.\n");
			}
		}
		
		if (!tagIsUTF8)
		{
			tagData = S98Engine_GetUTF8String(self, startPtr, endPtr);
		}
		else
		{
			size_t len = endPtr - startPtr;
			tagData = (char*)malloc(len + 1);
			memcpy(tagData, startPtr, len);
			tagData[len] = '\0';
			//tagData = strndup(startPtr, endPtr - startPtr);
		}
		S98Engine_ParsePSFTags(self, tagData);
		free(tagData);
	}
	
	if (self->tagMap.size + 1 < self->tagList.alloc)
	{
		PE_VECTOR_FREE(self->tagList);
		PE_VECTOR_ALLOC(self->tagList, const char*, self->tagMap.size + 1);
	}
	self->tagList.size = 0;
	
	for (tagStrIdx = 0; tagStrIdx < self->tagMap.size; tagStrIdx += 2)
	{
		const char* tagKey = self->tagMap.data[tagStrIdx + 0];
		const char* tagValue = self->tagMap.data[tagStrIdx + 1];
		const char* tagName = NULL;
		const char* const* t;
		
		for (t = S98_TAG_MAPPING; *t != NULL; t += 2)
		{
			if (! stricmp(tagKey, t[0]))
			{
				tagName = t[1];
				break;
			}
		}
		
		self->tagList.data[self->tagList.size + 0] = (tagName != NULL) ? tagName : tagKey;
		self->tagList.data[self->tagList.size + 1] = tagValue;
		self->tagList.size += 2;
	}
	
	self->tagList.data[self->tagList.size] = NULL;	// add list terminator
	self->tagList.size ++;
	return 0x00;
}

static char* S98Engine_GetUTF8String(PE_S98* self, const char* startPtr, const char* endPtr)
{
	char* result = NULL;

	if (startPtr == endPtr)
		return NULL;
	
	if (self->cpcSJIS != NULL)
	{
		size_t convSize = 0;
		char* convData = NULL;
		UINT8 retVal = CPConv_StrConvert(self->cpcSJIS, &convSize, &convData, endPtr - startPtr, startPtr);
		if (retVal < 0x80)
		{
			result = (char*)malloc(convSize + 1);
			memcpy(result, convData, convSize);
			result[convSize] = '\0';
		}
		free(convData);
	}
	if (result == NULL)
	{
		// unable to convert - fallback using the original string
		size_t len = endPtr - startPtr;
		result = (char*)malloc(len + 1);
		memcpy(result, startPtr, len);
		result[len] = '\0';
	}
	return result;
}

static char* TrimPSFTagWhitespace(char* data)
{
	size_t dLen = strlen(data);
	size_t posStart;
	size_t posEnd;
	
	// according to the PSF tag specification, all characters 0x01..0x20 are considered whitespace
	// http://wiki.neillcorlett.com/PSFTagFormat
	for (posStart = 0; posStart < dLen; posStart ++)
	{
		if ((unsigned char)data[posStart] > 0x20)
			break;
	}
	for (posEnd = dLen; posEnd > 0; posEnd --)
	{
		if ((unsigned char)data[posEnd - 1] > 0x20)
			break;
	}
	data[posEnd] = '\0';
	return &data[posStart];
}

static UINT8 ExtractKeyValue(char* line, char** retKey, char** retValue)
{
	char* equalPos = strchr(line, '=');
	if (equalPos == NULL)
		return 0xFF;	// not a "key=value" line
	
	*equalPos = '\0';	// NOTE: modifying source string here
	*retKey = line;
	*retValue = &equalPos[1];
	
	*retKey = TrimPSFTagWhitespace(*retKey);
	if ((*retKey)[0] == '\0')
		return 0x01;	// invalid key
	*retValue = TrimPSFTagWhitespace(*retValue);
	
	return 0x00;
}

static void ApplyStrTransform(char* str, int (*func)(int c))
{
	while(*str != '\0')
	{
		*str = (char)func((unsigned char)(*str));
		str ++;
	}
}

static size_t FindTagMapKey(const VEC_STR* tagMap, const char* key)
{
	size_t idx;
	for (idx = 0; idx < tagMap->size; idx += 2)
	{
		if (! strcmp(tagMap->data[idx], key))
			return idx;
	}
	return (size_t)-1;
}

static UINT8 S98Engine_ParsePSFTags(PE_S98* self, const char* tagData)
{
	const char* tagEnd = tagData + strlen(tagData);
	VSTR curLine;
	const char* lineStart;
	
	PE_VECTOR_ALLOC(curLine, char, 0x10);	// TODO: use 0x100 after testing
	
	lineStart = tagData;
	while(lineStart != NULL && *lineStart != '\0')
	{
		const char* lineEnd;
		size_t lineLen;
		char* curKey;
		char* curVal;
		UINT8 retVal;

		lineEnd = strchr(lineStart, '\n');
		lineLen = (lineEnd != NULL) ? (lineEnd - lineStart) : (tagEnd - lineStart);
		
		// Concept:
		// 1. use strndup to allocate substring -> curLine
		// 2. call ExtractKeyValue(), modifies (!) curLine, returns pointers curKey, curVal
		// 3. use strdup on curKey/curVal to put them into tagMap
		// 4. free(curLine)

		// extract line
		if (lineLen + 1 < curLine.alloc)
			PE_VECTOR_REALLOC(curLine, char, lineLen + 1);
		memcpy(curLine.data, lineStart, lineLen);
		curLine.data[lineLen] = '\0';

		retVal = ExtractKeyValue(curLine.data, &curKey, &curVal);
		if (! retVal)
		{
			size_t mapIdx;

			// keys are case insensitive, so let's make it uppercase
			ApplyStrTransform(curKey, toupper);

			mapIdx = FindTagMapKey(&self->tagMap, curKey);
			if (mapIdx != (size_t)-1)
			{
				// multiline-value
				size_t valSizeOld;
				size_t valSizeNew;

				mapIdx += 1;
				// mapValue = oldValue + '\n' + newValue;
				valSizeOld = strlen(self->tagMap.data[mapIdx]);
				valSizeNew = valSizeOld + 1 + strlen(curVal);
				self->tagMap.data[mapIdx] = (char*)realloc(self->tagMap.data[mapIdx], valSizeNew + 1);
				self->tagMap.data[mapIdx][valSizeOld] = '\n';
				strcpy(&self->tagMap.data[mapIdx][valSizeOld + 1], curKey);
			}
			else
			{
				// new value
				self->tagMap.data[self->tagMap.size + 0] = strdup(curKey);
				self->tagMap.data[self->tagMap.size + 1] = strdup(curVal);
				self->tagMap.size += 2;
			}
		}
		
		lineStart = lineEnd + 1;
	}
	PE_VECTOR_FREE(curLine);
	
	return 0x00;
}

UINT8 S98Engine_UnloadFile(PE_S98* self)
{
	if (self->playState & PLAYSTATE_PLAY)
		return 0xFF;
	
	self->playState = 0x00;
	self->dLoad = NULL;
	self->fileData = NULL;
	self->fileHdr.fileVer = 0xFF;
	self->fileHdr.dataOfs = 0x00;
	PE_ARRAY_FREE(self->devHdrs);
	PE_ARRAY_FREE(self->devices);

	FreeStringVectorData(&self->tagMap);
	PE_VECTOR_FREE(self->tagMap);
	PE_VECTOR_FREE(self->tagList);
	
	return 0x00;
}

const S98_HEADER* S98Engine_GetFileHeader(const PE_S98* self)
{
	return &self->fileHdr;
}

const char* const* S98Engine_GetTags(PE_S98* self)
{
	if (self->tagList.size == 0)
		return NULL;
	else
		return &self->tagList.data[0];
}

UINT8 S98Engine_GetSongInfo(PE_S98* self, PLR_SONG_INFO* songInf)
{
	if (self->dLoad == NULL)
		return 0xFF;
	
	songInf->format = FCC_S98;
	songInf->fileVerMaj = self->fileHdr.fileVer;
	songInf->fileVerMin = 0x00;
	songInf->tickRateMul = self->fileHdr.tickMult;
	songInf->tickRateDiv = self->fileHdr.tickDiv;
	songInf->songLen = selfcall.GetTotalTicks(&self->pe);
	songInf->loopTick = self->fileHdr.loopOfs ? selfcall.GetLoopTicks(&self->pe) : (UINT32)-1;
	songInf->volGain = 0x10000;
	songInf->deviceCnt = (UINT32)self->devHdrs.size;
	
	return 0x00;
}

UINT8 S98Engine_GetSongDeviceInfo(const PE_S98* self, size_t* retDevInfCount, PLR_DEV_INFO** retDevInfData)
{
	if (self->dLoad == NULL)
		return 0xFF;
	
	size_t curDev;
	
	*retDevInfCount = self->devHdrs.size;
	*retDevInfData = (PLR_DEV_INFO*)calloc(*retDevInfCount, sizeof(PLR_DEV_INFO));
	for (curDev = 0; curDev < self->devHdrs.size; curDev ++)
	{
		const S98_HDR_DEVICE* devHdr = &self->devHdrs.data[curDev];
		PLR_DEV_INFO* devInf = &(*retDevInfData)[curDev];
		//memset(devInf, 0x00, sizeof(PLR_DEV_INFO));
		
		devInf->id = (UINT32)curDev;
		devInf->type = S98_DEV_LIST[devHdr->devType];
		devInf->instance = S98Engine_GetDeviceInstance(self, curDev);
		devInf->devCfg = (const DEV_GEN_CFG*)self->devCfgs.data[curDev].data.data;
		if (self->devices.size > 0)
		{
			const VGM_BASEDEV* cDev = &self->devices.data[curDev].base;
			const VGM_BASEDEV* clDev;
			UINT32 curLDev;
			
			devInf->devDecl = cDev->defInf.devDecl;
			devInf->core = (cDev->defInf.devDef != NULL) ? cDev->defInf.devDef->coreID : 0x00;
			devInf->volume = (cDev->resmpl.volumeL + cDev->resmpl.volumeR) / 2;
			devInf->smplRate = cDev->defInf.sampleRate;
			
			devInf->devLCnt = cDev->defInf.linkDevCount;
			if (devInf->devLCnt > 0)
				devInf->devLink = (PLR_DEV_INFO*)calloc(devInf->devLCnt, sizeof(PLR_DEV_INFO));
			else
				devInf->devLink = NULL;
			for (curLDev = 0, clDev = cDev->linkDev; curLDev < cDev->defInf.linkDevCount && clDev != NULL; curLDev ++, clDev = clDev->linkDev)
			{
				const DEVLINK_INFO* dLink = &cDev->defInf.linkDevs[curLDev];
				PLR_DEV_INFO* lDevInf = &devInf->devLink[curLDev];
				
				//memset(&lDevInf, 0x00, sizeof(PLR_DEV_INFO));
				lDevInf->type = dLink->devID;
				lDevInf->id = (UINT32)curDev;
				lDevInf->instance = 0xFF;
				lDevInf->devCfg = dLink->cfg;
				lDevInf->devDecl = clDev->defInf.devDecl;
				lDevInf->core = (clDev->defInf.devDef != NULL) ? clDev->defInf.devDef->coreID : 0x00;
				lDevInf->volume = (clDev->resmpl.volumeL + clDev->resmpl.volumeR) / 2;
				lDevInf->smplRate = clDev->defInf.sampleRate;
			}
		}
		else
		{
			const DEVLINK_IDS* dlIds;
			devInf->devDecl = SndEmu_GetDevDecl(devInf->type, self->pe.userDevList, self->pe.devStartOpts);
			devInf->core = 0x00;
			devInf->volume = 0x100;
			devInf->smplRate = 0;
			
			devInf->devLCnt = 0;
			devInf->devLink = NULL;
			dlIds = devInf->devDecl->linkDevIDs(devInf->devCfg);
			if (dlIds != NULL && dlIds->devCount > 0)
			{
				size_t curLDev;
				devInf->devLCnt = dlIds->devCount;
				devInf->devLink = (PLR_DEV_INFO*)calloc(devInf->devLCnt, sizeof(PLR_DEV_INFO));
				for (curLDev = 0; curLDev < dlIds->devCount; curLDev ++)
				{
					PLR_DEV_INFO* lDevInf = &devInf->devLink[curLDev];
					
					//memset(&lDevInf, 0x00, sizeof(PLR_DEV_INFO));
					lDevInf->type = dlIds->devIDs[curLDev];
					lDevInf->id = (UINT32)curDev;
					lDevInf->instance = 0xFF;
					lDevInf->devDecl = SndEmu_GetDevDecl(lDevInf->type, self->pe.userDevList, self->pe.devStartOpts);
					lDevInf->devCfg = NULL;
					lDevInf->core = 0x00;
					lDevInf->volume = 0xCD;
					lDevInf->smplRate = 0;
				}
			}
		}
	}
	if (self->devices.size > 0)
		return 0x01;	// returned "live" data
	else
		return 0x00;	// returned data based on file header
}

static UINT8 S98Engine_GetDeviceInstance(const PE_S98* self, size_t id)
{
	const S98_HDR_DEVICE* mainDHdr = &self->devHdrs.data[id];
	UINT8 mainDType = (mainDHdr->devType < S98DEV_END) ? S98_DEV_LIST[mainDHdr->devType] : 0xFF;
	UINT8 instance = 0;
	size_t curDev;
	
	for (curDev = 0; curDev < id; curDev ++)
	{
		const S98_HDR_DEVICE* dHdr = &self->devHdrs.data[curDev];
		UINT8 dType = (dHdr->devType < S98DEV_END) ? S98_DEV_LIST[dHdr->devType] : 0xFF;
		if (dType == mainDType)
			instance ++;
	}
	
	return instance;
}

static size_t S98Engine_DeviceID2OptionID(const PE_S98* self, UINT32 id)
{
	DEV_ID type;
	UINT8 instance;
	
	if (id & 0x80000000)
	{
		type = (id >> 0) & 0xFF;
		instance = (id >> 16) & 0xFF;
	}
	else if (id < self->devHdrs.size)
	{
		UINT32 s98DevType;
		
		s98DevType = self->devHdrs.data[id].devType;
		type = (s98DevType < S98DEV_END) ? S98_DEV_LIST[s98DevType] : 0xFF;
		instance = S98Engine_GetDeviceInstance(self, id);
	}
	else
	{
		return (size_t)-1;
	}
	
	if (instance < 2)
		return self->devOptMap[type][instance];
	else
		return (size_t)-1;
}

static void S98Engine_RefreshMuting(PE_S98* self, S98_CHIPDEV* chipDev, const PLR_MUTE_OPTS* muteOpts)
{
	VGM_BASEDEV* clDev;
	UINT8 linkCntr = 0;
	
	for (clDev = &chipDev->base; clDev != NULL && linkCntr < 2; clDev = clDev->linkDev, linkCntr ++)
	{
		DEV_INFO* devInf = &clDev->defInf;
		if (devInf->dataPtr != NULL && devInf->devDef->SetMuteMask != NULL)
			devInf->devDef->SetMuteMask(devInf->dataPtr, muteOpts->chnMute[linkCntr]);
	}
	
	return;
}

static void S98Engine_RefreshPanning(PE_S98* self, S98_CHIPDEV* chipDev, const PLR_PAN_OPTS* panOpts)
{
	VGM_BASEDEV* clDev;
	UINT8 linkCntr = 0;
	
	for (clDev = &chipDev->base; clDev != NULL && linkCntr < 2; clDev = clDev->linkDev, linkCntr ++)
	{
		DEV_INFO* devInf = &clDev->defInf;
		if (devInf->dataPtr == NULL)
			continue;
		DEVFUNC_PANALL funcPan = NULL;
		UINT8 retVal = SndEmu_GetDeviceFunc(devInf->devDef, RWF_CHN_PAN | RWF_WRITE, DEVRW_ALL, 0, (void**)&funcPan);
		if (retVal != EERR_NOT_FOUND && funcPan != NULL)
			funcPan(devInf->dataPtr, &panOpts->chnPan[linkCntr][0]);
	}
	
	return;
}

UINT8 S98Engine_SetDeviceOptions(PE_S98* self, UINT32 id, const PLR_DEV_OPTS* devOpts)
{
	size_t optID = S98Engine_DeviceID2OptionID(self, id);
	if (optID == (size_t)-1)
		return 0x80;	// bad device ID
	
	self->devOpts[optID] = *devOpts;
	
	size_t devID = self->optDevMap[optID];
	if (devID < self->devices.size)
	{
		S98Engine_RefreshMuting(self, &self->devices.data[devID], &self->devOpts[optID].muteOpts);
		S98Engine_RefreshPanning(self, &self->devices.data[devID], &self->devOpts[optID].panOpts);
	}
	return 0x00;
}

UINT8 S98Engine_GetDeviceOptions(const PE_S98* self, UINT32 id, PLR_DEV_OPTS* devOpts)
{
	size_t optID = S98Engine_DeviceID2OptionID(self, id);
	if (optID == (size_t)-1)
		return 0x80;	// bad device ID
	
	*devOpts = self->devOpts[optID];
	return 0x00;
}

UINT8 S98Engine_SetDeviceMuting(PE_S98* self, UINT32 id, const PLR_MUTE_OPTS* muteOpts)
{
	size_t optID = S98Engine_DeviceID2OptionID(self, id);
	if (optID == (size_t)-1)
		return 0x80;	// bad device ID
	
	self->devOpts[optID].muteOpts = *muteOpts;
	
	size_t devID = self->optDevMap[optID];
	if (devID < self->devices.size)
		S98Engine_RefreshMuting(self, &self->devices.data[devID], &self->devOpts[optID].muteOpts);
	return 0x00;
}

UINT8 S98Engine_GetDeviceMuting(const PE_S98* self, UINT32 id, PLR_MUTE_OPTS* muteOpts)
{
	size_t optID = S98Engine_DeviceID2OptionID(self, id);
	if (optID == (size_t)-1)
		return 0x80;	// bad device ID
	
	*muteOpts = self->devOpts[optID].muteOpts;
	return 0x00;
}

UINT8 S98Engine_SetPlayerOptions(PE_S98* self, const S98_PLAY_OPTIONS* playOpts)
{
	self->playOpts = *playOpts;
	S98Engine_RefreshTSRates(self);
	return 0x00;
}

UINT8 S98Engine_GetPlayerOptions(const PE_S98* self, S98_PLAY_OPTIONS* playOpts)
{
	*playOpts = self->playOpts;
	return 0x00;
}

UINT8 S98Engine_SetSampleRate(PE_S98* self, UINT32 sampleRate)
{
	if (self->playState & PLAYSTATE_PLAY)
		return 0x01;	// can't set during playback
	
	self->pe.outSmplRate = sampleRate;
	return 0x00;
}

double S98Engine_GetPlaybackSpeed(const PE_S98* self)
{
	return self->playOpts.genOpts.pbSpeed / (double)0x10000;
}

UINT8 S98Engine_SetPlaybackSpeed(PE_S98* self, double speed)
{
	self->playOpts.genOpts.pbSpeed = (UINT32)(0x10000 * speed);
	S98Engine_RefreshTSRates(self);
	return 0x00;
}


static void S98Engine_RefreshTSRates(PE_S98* self)
{
	self->ttMult = self->fileHdr.tickMult;
	self->tsDiv = self->fileHdr.tickDiv;
	if (self->playOpts.genOpts.pbSpeed != 0 && self->playOpts.genOpts.pbSpeed != 0x10000)
	{
		self->ttMult *= 0x10000;
		self->tsDiv *= self->playOpts.genOpts.pbSpeed;
	}
	self->tsMult = self->ttMult * self->pe.outSmplRate;
	if (self->tsMult != self->lastTsMult ||
	    self->tsDiv != self->lastTsDiv)
	{
		if (self->lastTsMult && self->lastTsDiv)	// the order * / * / is required to avoid overflow
			self->playSmpl = (UINT32)(self->playSmpl * self->lastTsDiv / self->lastTsMult * self->tsMult / self->tsDiv);
		self->lastTsMult = self->tsMult;
		self->lastTsDiv = self->tsDiv;
	}
	return;
}

UINT32 S98Engine_Tick2Sample(const PE_S98* self, UINT32 ticks)
{
	if (ticks == (UINT32)-1)
		return -1;
	return (UINT32)(ticks * self->tsMult / self->tsDiv);
}

UINT32 S98Engine_Sample2Tick(const PE_S98* self, UINT32 samples)
{
	if (samples == (UINT32)-1)
		return -1;
	return (UINT32)(samples * self->tsDiv / self->tsMult);
}

double S98Engine_Tick2Second(const PE_S98* self, UINT32 ticks)
{
	if (ticks == (UINT32)-1)
		return -1.0;
	return (INT64)(ticks * self->ttMult) / (double)(INT64)self->tsDiv;
}

UINT8 S98Engine_GetState(const PE_S98* self)
{
	return self->playState;
}

UINT32 S98Engine_GetCurPos(const PE_S98* self, UINT8 unit)
{
	switch(unit)
	{
	case PLAYPOS_FILEOFS:
		return self->filePos;
	case PLAYPOS_TICK:
		return self->playTick;
	case PLAYPOS_SAMPLE:
		return self->playSmpl;
	case PLAYPOS_COMMAND:
	default:
		return (UINT32)-1;
	}
}

UINT32 S98Engine_GetCurLoop(const PE_S98* self)
{
	return self->curLoop;
}

UINT32 S98Engine_GetTotalTicks(const PE_S98* self)
{
	return self->totalTicks;
}

UINT32 S98Engine_GetLoopTicks(const PE_S98* self)
{
	if (! self->fileHdr.loopOfs)
		return 0;
	else
		return self->totalTicks - self->loopTick;
}

/*static*/ void S98Engine_PlayerLogCB(void* userParam, void* source, UINT8 level, const char* message)
{
	PE_S98* player = (PE_S98*)source;
	if (player->pe.logCbFunc == NULL)
		return;
	player->pe.logCbFunc(player->pe.logCbParam, &player->pe, level, PLRLOGSRC_PLR, NULL, message);
	return;
}

/*static*/ void S98Engine_SndEmuLogCB(void* userParam, void* source, UINT8 level, const char* message)
{
	DEVLOG_CB_DATA* cbData = (DEVLOG_CB_DATA*)userParam;
	PE_S98* player = cbData->player;
	if (player->pe.logCbFunc == NULL)
		return;
	if ((player->playState & PLAYSTATE_SEEK) && level > PLRLOG_ERROR)
		return;	// prevent message spam while seeking
	player->pe.logCbFunc(player->pe.logCbParam, &player->pe, level, PLRLOGSRC_EMU,
		player->devNames.data[cbData->chipDevID], message);
	return;
}


static void S98Engine_GenerateDeviceConfig(PE_S98* self)
{
	size_t curDev;
	
	PE_ARRAY_FREE(self->devCfgs);
	PE_ARRAY_MALLOC(self->devCfgs, S98_DEVCFG, self->devHdrs.size);
	self->devNames.clear();
	for (curDev = 0; curDev < self->devCfgs.size; curDev ++)
	{
		const S98_HDR_DEVICE* devHdr = &self->devHdrs.data[curDev];
		DEV_GEN_CFG devCfg;
		UINT8 deviceID;
		
		memset(&devCfg, 0x00, sizeof(DEV_GEN_CFG));
		devCfg.clock = devHdr->clock;
		devCfg.flags = 0x00;
		
		deviceID = (devHdr->devType < S98DEV_END) ? S98_DEV_LIST[devHdr->devType] : 0xFF;
		const char* devName = SndEmu_GetDevName(deviceID, 0x00, &devCfg);	// use short name for now
		switch(deviceID)
		{
		case DEVID_AY8910:
			{
				AY8910_CFG ayCfg;
				
				ayCfg._genCfg = devCfg;
				if (devHdr->devType == S98DEV_PSGYM)
				{
					ayCfg.chipType = AYTYPE_YM2149;
					ayCfg.chipFlags = YM2149_PIN26_LOW;
					devName = "YM2149";
				}
				else
				{
					ayCfg.chipType = AYTYPE_AY8910;
					ayCfg.chipFlags = 0x00;
					ayCfg._genCfg.clock /= 2;
					devName = "AY8910";
				}
				
				SaveDeviceConfig(&self->devCfgs.data[curDev].data, &ayCfg, sizeof(AY8910_CFG));
			}
			break;
		case DEVID_SN76496:
			{
				SN76496_CFG snCfg;
				
				snCfg._genCfg = devCfg;
				snCfg.shiftRegWidth = 0x10;
				snCfg.noiseTaps = 0x09;
				snCfg.segaPSG = 1;
				snCfg.negate = 0;
				snCfg.stereo = 1;
				snCfg.clkDiv = 8;
				snCfg.t6w28_tone = NULL;
				
				SaveDeviceConfig(&self->devCfgs.data[curDev].data, &snCfg, sizeof(SN76496_CFG));
			}
			break;
		default:
			SaveDeviceConfig(&self->devCfgs.data[curDev].data, &devCfg, sizeof(DEV_GEN_CFG));
			break;
		}
		if (self->devCfgs.size <= 1)
		{
			self->devNames.push_back(devName);
		}
		else
		{
			char fullName[0x10];
			snprintf(fullName, 0x10, "%u-%s", 1 + (unsigned int)curDev, devName);
			self->devNames.push_back(fullName);
		}
	}
	
	return;
}

static void S98Engine_DeviceLinkCallback(void* userParam, VGM_BASEDEV* cDev, DEVLINK_INFO* dLink)
{
	DEVLINK_CB_DATA* cbData = (DEVLINK_CB_DATA*)userParam;
	PE_S98* self = cbData->player;
	const S98_CHIPDEV* chipDev = cbData->chipDev;
	const PLR_DEV_OPTS* devOpts = (chipDev->optID != (size_t)-1) ? &self->devOpts[chipDev->optID] : NULL;
	
	if (devOpts != NULL && devOpts->emuCore[1])
	{
		// set emulation core of linked device (OPN(A) SSG)
		dLink->cfg->emuCore = devOpts->emuCore[1];
	}
	
	return;
}

UINT8 S98Engine_Start(PE_S98* self)
{
	size_t curDev;
	UINT8 retVal;
	
	for (curDev = 0; curDev < OPT_DEV_COUNT * 2; curDev ++)
		self->optDevMap[curDev] = (size_t)-1;
	
	PE_ARRAY_FREE(self->devices);
	PE_ARRAY_MALLOC(self->devices, S98_CHIPDEV, self->devHdrs.size);
	for (curDev = 0; curDev < self->devHdrs.size; curDev ++)
	{
		const S98_HDR_DEVICE* devHdr = &self->devHdrs.data[curDev];
		S98_CHIPDEV* cDev = &self->devices.data[curDev];
		DEV_GEN_CFG* devCfg = (DEV_GEN_CFG*)self->devCfgs.data[curDev].data.data;
		VGM_BASEDEV* clDev;
		PLR_DEV_OPTS* devOpts;
		DEV_ID deviceID;
		UINT8 instance;
		
		cDev->base.defInf.dataPtr = NULL;
		cDev->base.defInf.devDef = NULL;
		cDev->base.linkDev = NULL;
		deviceID = (devHdr->devType < S98DEV_END) ? S98_DEV_LIST[devHdr->devType] : 0xFF;
		if (deviceID == 0xFF)
			continue;
		
		instance = S98Engine_GetDeviceInstance(self, curDev);
		if (instance < 2)
		{
			cDev->optID = self->devOptMap[deviceID][instance];
			devOpts = &self->devOpts[cDev->optID];
		}
		else
		{
			cDev->optID = (size_t)-1;
			devOpts = NULL;
		}
		devCfg->emuCore = (devOpts != NULL) ? devOpts->emuCore[0] : 0x00;
		devCfg->srMode = (devOpts != NULL) ? devOpts->srMode : DEVRI_SRMODE_NATIVE;
		if (devOpts != NULL && devOpts->smplRate)
			devCfg->smplRate = devOpts->smplRate;
		else
			devCfg->smplRate = self->pe.outSmplRate;
		
		retVal = SndEmu_Start2(deviceID, devCfg, &cDev->base.defInf, self->pe.userDevList, self->pe.devStartOpts);
		if (retVal)
		{
			cDev->base.defInf.dataPtr = NULL;
			cDev->base.defInf.devDef = NULL;
			continue;
		}
		SndEmu_GetDeviceFunc(cDev->base.defInf.devDef, RWF_REGISTER | RWF_WRITE, DEVRW_A8D8, 0, (void**)&cDev->write);
		
		cDev->logCbData.player = self;
		cDev->logCbData.chipDevID = curDev;
		if (cDev->base.defInf.devDef->SetLogCB != NULL)
			cDev->base.defInf.devDef->SetLogCB(cDev->base.defInf.dataPtr, S98Engine_SndEmuLogCB, &cDev->logCbData);
		{
			DEVLINK_CB_DATA dlCbData;
			dlCbData.player = self;
			dlCbData.chipDev = cDev;
			SetupLinkedDevices(&cDev->base, S98Engine_DeviceLinkCallback, &dlCbData);
		}
		
		if (devOpts != NULL)
		{
			if (cDev->base.defInf.devDef->SetOptionBits != NULL)
				cDev->base.defInf.devDef->SetOptionBits(cDev->base.defInf.dataPtr, devOpts->coreOpts);
			S98Engine_RefreshMuting(self, cDev, &devOpts->muteOpts);
			S98Engine_RefreshPanning(self, cDev, &devOpts->panOpts);
			
			self->optDevMap[cDev->optID] = curDev;
		}
		if (cDev->base.defInf.linkDevCount > 0 && cDev->base.defInf.linkDevs[0].devID == DEVID_AY8910)
		{
			VGM_BASEDEV* clDev = cDev->base.linkDev;
			size_t optID = S98Engine_DeviceID2OptionID(self, PLR_DEV_ID(DEVID_AY8910, instance));
			if (optID != (size_t)-1 && clDev != NULL && clDev->defInf.devDef->SetOptionBits != NULL)
				clDev->defInf.devDef->SetOptionBits(cDev->base.defInf.dataPtr, self->devOpts[optID].coreOpts);
		}
		
		for (clDev = &cDev->base; clDev != NULL; clDev = clDev->linkDev)
		{
			UINT8 resmplMode = (devOpts != NULL) ? devOpts->resmplMode : RSMODE_LINEAR;
			Resmpl_SetVals(&clDev->resmpl, resmplMode, 0x100, self->pe.outSmplRate);
			if (deviceID == DEVID_YM2203 || deviceID == DEVID_YM2608)
			{
				// set SSG volume
				if (clDev != &cDev->base)
					clDev->resmpl.volumeL = clDev->resmpl.volumeR = 0xCD;
			}
			Resmpl_DevConnect(&clDev->resmpl, &clDev->defInf);
			Resmpl_Init(&clDev->resmpl);
		}
	}
	
	self->playState |= PLAYSTATE_PLAY;
	selfcall.Reset(&self->pe);
	if (self->pe.eventCbFunc != NULL)
		self->pe.eventCbFunc(&self->pe, self->pe.eventCbParam, PLREVT_START, NULL);
	
	return 0x00;
}

UINT8 S98Engine_Stop(PE_S98* self)
{
	size_t curDev;
	
	self->playState &= ~PLAYSTATE_PLAY;
	
	for (curDev = 0; curDev < self->devices.size; curDev ++)
	{
		S98_CHIPDEV* cDev = &self->devices.data[curDev];
		FreeDeviceTree(&cDev->base, 0);
	}
	PE_ARRAY_FREE(self->devices);
	if (self->pe.eventCbFunc != NULL)
		self->pe.eventCbFunc(&self->pe, self->pe.eventCbParam, PLREVT_STOP, NULL);
	
	return 0x00;
}

UINT8 S98Engine_Reset(PE_S98* self)
{
	size_t ramSize;
	UINT8* ramInitData;
	size_t curDev;
	
	self->filePos = self->fileHdr.dataOfs;
	self->fileTick = 0;
	self->playTick = 0;
	self->playSmpl = 0;
	self->playState &= ~PLAYSTATE_END;
	self->psTrigger = 0x00;
	self->curLoop = 0;
	self->lastLoopTick = 0;
	
	S98Engine_RefreshTSRates(self);
	
	ramSize = 0x40000;
	ramInitData = (UINT8*)malloc(ramSize);
	memset(ramInitData, 0x00, ramSize);
	for (curDev = 0; curDev < self->devices.size; curDev ++)
	{
		S98_CHIPDEV* cDev = &self->devices.data[curDev];
		DEV_INFO* defInf = &cDev->base.defInf;
		VGM_BASEDEV* clDev;
		if (defInf->dataPtr == NULL)
			continue;
		
		defInf->devDef->Reset(defInf->dataPtr);
		for (clDev = &cDev->base; clDev != NULL; clDev = clDev->linkDev)
		{
			// TODO: Resmpl_Reset(&clDev->resmpl);
		}
		
		if (self->devHdrs.data[curDev].devType == S98DEV_OPNA)
		{
			DEVFUNC_WRITE_MEMSIZE SetRamSize = NULL;
			DEVFUNC_WRITE_BLOCK SetRamData = NULL;
			
			// setup DeltaT RAM size
			SndEmu_GetDeviceFunc(defInf->devDef, RWF_MEMORY | RWF_WRITE, DEVRW_MEMSIZE, 0, (void**)&SetRamSize);
			SndEmu_GetDeviceFunc(defInf->devDef, RWF_MEMORY | RWF_WRITE, DEVRW_BLOCK, 0, (void**)&SetRamData);
			if (SetRamSize != NULL)
				SetRamSize(defInf->dataPtr, 0x40000);	// 256 KB
			// initialize DeltaT RAM with 00, because some S98 files seem to expect that (e.g. King Breeder/11.s98)
			if (SetRamData != NULL)
				SetRamData(defInf->dataPtr, 0x00, (UINT32)ramSize, ramInitData);
			
			// The YM2608 defaults to OPN mode (YM2203 fallback),
			// so put it into OPNA mode (6 channels).
			cDev->write(defInf->dataPtr, 0, 0x29);
			cDev->write(defInf->dataPtr, 1, 0x80);
		}
	}
	free(ramInitData);
	
	return 0x00;
}

UINT8 S98Engine_Seek(PE_S98* self, UINT8 unit, UINT32 pos)
{
	switch(unit)
	{
	case PLAYPOS_FILEOFS:
		self->playState |= PLAYSTATE_SEEK;
		if (pos < self->filePos)
			selfcall.Reset(&self->pe);
		return S98Engine_SeekToFilePos(self, pos);
	case PLAYPOS_SAMPLE:
		pos = selfcall.Sample2Tick(&self->pe, pos);
		// fall through
	case PLAYPOS_TICK:
		self->playState |= PLAYSTATE_SEEK;
		if (pos < self->playTick)
			selfcall.Reset(&self->pe);
		return S98Engine_SeekToTick(self, pos);
	case PLAYPOS_COMMAND:
	default:
		return 0xFF;
	}
}

static UINT8 S98Engine_SeekToTick(PE_S98* self, UINT32 tick)
{
	self->playState |= PLAYSTATE_SEEK;
	if (tick > self->playTick)
		S98Engine_ParseFile(self, tick - self->playTick);
	self->playSmpl = selfcall.Tick2Sample(&self->pe, self->playTick);
	self->playState &= ~PLAYSTATE_SEEK;
	return 0x00;
}

static UINT8 S98Engine_SeekToFilePos(PE_S98* self, UINT32 pos)
{
	self->playState |= PLAYSTATE_SEEK;
	while(self->filePos <= pos && ! (self->playState & PLAYSTATE_END))
		S98Engine_DoCommand(self);
	self->playTick = self->fileTick;
	self->playSmpl = selfcall.Tick2Sample(&self->pe, self->playTick);
	self->playState &= ~PLAYSTATE_SEEK;
	
	return 0x00;
}

UINT32 S98Engine_Render(PE_S98* self, UINT32 smplCnt, WAVE_32BS* data)
{
	UINT32 curSmpl;
	UINT32 smplFileTick;
	UINT32 maxSmpl;
	INT32 smplStep;	// might be negative due to rounding errors in Tick2Sample
	size_t curDev;
	
	// Note: use do {} while(), so that "smplCnt == 0" can be used to process until reaching the next sample.
	curSmpl = 0;
	do
	{
		smplFileTick = S98Engine_Sample2Tick(self, self->playSmpl);
		S98Engine_ParseFile(self, smplFileTick - self->playTick);
		
		// render as many samples at once as possible (for better performance)
		maxSmpl = S98Engine_Tick2Sample(self, self->fileTick);
		smplStep = maxSmpl - self->playSmpl;
		if (smplStep < 1)
			smplStep = 1;	// must render at least 1 sample in order to advance
		if ((UINT32)smplStep > smplCnt - curSmpl)
			smplStep = smplCnt - curSmpl;
		
		for (curDev = 0; curDev < self->devices.size(); curDev ++)
		{
			S98_CHIPDEV* cDev = &self->devices[curDev];
			UINT8 disable = (cDev->optID != (size_t)-1) ? self->devOpts[cDev->optID].muteOpts.disable : 0x00;
			VGM_BASEDEV* clDev;
			
			for (clDev = &cDev->base; clDev != NULL; clDev = clDev->linkDev, disable >>= 1)
			{
				if (clDev->defInf.dataPtr != NULL && ! (disable & 0x01))
					Resmpl_Execute(&clDev->resmpl, smplStep, &data[curSmpl]);
			}
		}
		curSmpl += smplStep;
		self->playSmpl += smplStep;
		if (self->psTrigger & PLAYSTATE_END)
		{
			self->psTrigger &= ~PLAYSTATE_END;
			break;
		}
	} while(curSmpl < smplCnt);
	
	return curSmpl;
}

static void S98Engine_ParseFile(PE_S98* self, UINT32 ticks)
{
	self->playTick += ticks;
	if (self->playState & PLAYSTATE_END)
		return;
	
	while(self->fileTick <= self->playTick && ! (self->playState & PLAYSTATE_END))
		DoCommand();
	
	return;
}

static void S98Engine_HandleEOF(PE_S98* self)
{
	UINT8 doLoop = (self->fileHdr.loopOfs != 0);
	
	if (self->playState & PLAYSTATE_SEEK)	// recalculate playSmpl to fix state when triggering callbacks
		self->playSmpl = Tick2Sample(self->fileTick);	// Note: fileTick results in more accurate position
	if (doLoop)
	{
		if (self->lastLoopTick == self->fileTick)
		{
			doLoop = 0;	// prevent freezing due to infinite loop
			emu_logf(&self->logger, PLRLOG_WARN, "Ignored Zero-Sample-Loop!\n");
		}
		else
		{
			self->lastLoopTick = self->fileTick;
		}
	}
	if (doLoop)
	{
		self->curLoop ++;
		if (self->eventCbFunc != NULL)
		{
			UINT8 retVal;
			
			retVal = self->eventCbFunc(this, self->eventCbParam, PLREVT_LOOP, &self->curLoop);
			if (retVal == 0x01)	// "stop" signal?
			{
				self->playState |= PLAYSTATE_END;
				self->psTrigger |= PLAYSTATE_END;
				if (self->eventCbFunc != NULL)
					self->eventCbFunc(this, self->eventCbParam, PLREVT_END, NULL);
				return;
			}
		}
		self->filePos = self->fileHdr.loopOfs;
		return;
	}
	
	self->playState |= PLAYSTATE_END;
	self->psTrigger |= PLAYSTATE_END;
	if (self->eventCbFunc != NULL)
		self->eventCbFunc(this, self->eventCbParam, PLREVT_END, NULL);
	
	return;
}

static void S98Engine_DoCommand(PE_S98* self)
{
	if (self->filePos >= DataLoader_GetSize(self->dLoad))
	{
		if (self->playState & PLAYSTATE_SEEK)	// recalculate playSmpl to fix state when triggering callbacks
			self->playSmpl = Tick2Sample(self->fileTick);	// Note: fileTick results in more accurate position
		self->playState |= PLAYSTATE_END;
		self->psTrigger |= PLAYSTATE_END;
		if (self->eventCbFunc != NULL)
			self->eventCbFunc(this, self->eventCbParam, PLREVT_END, NULL);
		emu_logf(&self->logger, PLRLOG_WARN, "S98 file ends early! (filePos 0x%06X, fileSize 0x%06X)\n", self->filePos, DataLoader_GetSize(self->dLoad));
		return;
	}
	
	UINT8 curCmd;
	
	curCmd = self->fileData[self->filePos];
	self->filePos ++;
	switch(curCmd)
	{
	case 0xFF:	// advance 1 tick
		self->fileTick ++;
		break;
	case 0xFE:	// advance multiple ticks
		self->fileTick += 2 + ReadVarInt(self->filePos);
		break;
	case 0xFD:
		HandleEOF();
		break;
	default:
		DoRegWrite(curCmd >> 1, curCmd & 0x01, self->fileData[self->filePos + 0x00], self->fileData[self->filePos + 0x01]);
		self->filePos += 0x02;
		break;
	}
	
	return;
}

static void S98Engine_DoRegWrite(PE_S98* self, UINT8 deviceID, UINT8 port, UINT8 reg, UINT8 data)
{
	if (deviceID >= self->devices.size())
		return;
	
	S98_CHIPDEV* cDev = &self->devices[deviceID];
	DEV_DATA* dataPtr = cDev->base.defInf.dataPtr;
	if (dataPtr == NULL || cDev->write == NULL)
		return;
	
	if (self->devHdrs[deviceID].devType == S98DEV_DCSG)
	{
		if (reg == 1)	// GG stereo
			cDev->write(dataPtr, SN76496_W_GGST, data);
		else
			cDev->write(dataPtr, SN76496_W_REG, data);
	}
	else
	{
		cDev->write(dataPtr, (port << 1) | 0, reg);
		cDev->write(dataPtr, (port << 1) | 1, data);
	}
	
	return;
}

static UINT32 S98Engine_ReadVarInt(PE_S98* self, UINT32* filePos)
{
	UINT32 tickVal = 0;
	UINT8 tickShift = 0;
	UINT8 moreFlag;
	
	do
	{
		moreFlag = self->fileData[*filePos] & 0x80;
		tickVal |= (self->fileData[*filePos] & 0x7F) << tickShift;
		tickShift += 7;
		(*filePos) ++;
	} while(moreFlag);
	
	return tickVal;
}


static struct player_engine_vtable S98Engine_vtbl =
{
	FCC_S98,	// playerType
	"S98",	// playerName
	
	(void*)S98Engine_Init,
	(void*)S98Engine_Deinit,
	
	(void*)S98Engine_CanLoadFile,
	(void*)S98Engine_LoadFile,
	(void*)S98Engine_UnloadFile,
	
	(void*)S98Engine_GetTags,
	(void*)S98Engine_GetSongInfo,
	(void*)S98Engine_GetSongDeviceInfo,

	(void*)S98Engine_SetDeviceOptions,
	(void*)S98Engine_GetDeviceOptions,
	(void*)S98Engine_SetDeviceMuting,
	(void*)S98Engine_GetDeviceMuting,

	(void*)S98Engine_SetPlayerOptions,
	(void*)S98Engine_GetPlayerOptions,
	
	PBaseEngine_GetSampleRate,
	(void*)S98Engine_SetSampleRate,
	(void*)S98Engine_GetPlaybackSpeed,
	(void*)S98Engine_SetPlaybackSpeed,
	PBaseEngine_SetUserDevices,
	PBaseEngine_SetEventCallback,
	PBaseEngine_SetFileReqCallback,
	PBaseEngine_SetLogCallback,
	(void*)S98Engine_Tick2Sample,
	(void*)S98Engine_Sample2Tick,
	(void*)S98Engine_Tick2Second,
	PBaseEngine_Sample2Second,
	
	(void*)S98Engine_GetState,
	(void*)S98Engine_GetCurPos,
	(void*)S98Engine_GetCurLoop,
	(void*)S98Engine_GetTotalTicks,
	(void*)S98Engine_GetLoopTicks,
	PBaseEngine_GetTotalPlayTicks,
	
	(void*)S98Engine_Start,
	(void*)S98Engine_Stop,
	(void*)S98Engine_Reset,
	(void*)S98Engine_Seek,
	(void*)S98Engine_Render,
};
