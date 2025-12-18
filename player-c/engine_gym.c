#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "../common_def.h"
#include "engine_gym.h"
#include "../emu/EmuStructs.h"
#include "../emu/SoundEmu.h"
#include "../emu/Resampler.h"
#include "../emu/SoundDevs.h"
#include "../emu/EmuCores.h"
#include "../emu/cores/sn764intf.h"	// for SN76496_CFG
#include "../utils/StrUtils.h"
#include "../player/helper.h"
#include "../emu/logging.h"


DEFINE_PE_ARRAY(UINT8, ARR_UINT8)
typedef struct gym_device_config
{
	DEV_ID type;
	UINT16 volume;
	ARR_UINT8 data;
} GYM_DEVCFG;
typedef struct device_log_callback_data
{
	PE_GYM* player;
	size_t chipDevID;
} DEVLOG_CB_DATA;
typedef struct gym_chip_device
{
	VGM_BASEDEV base;
	size_t optID;
	DEVFUNC_WRITE_A8D8 write;
	DEVLOG_CB_DATA logCbData;
} GYM_CHIPDEV;

DEFINE_PE_ARRAY(GYM_DEVCFG, ARR_DEVCFG)
DEFINE_PE_ARRAY(GYM_CHIPDEV, ARR_CHIPDEV)
DEFINE_PE_ARRAY(const char*, ARR_CSTR)
DEFINE_PE_VECTOR(char*, VEC_STR)
DEFINE_PE_VECTOR(const char*, VEC_CSTR)
#define selfcall	self->pe.vtbl


struct player_engine_gym
{
	PEBASE pe;
	
	CPCONV* cpc1252;	// CP1252 -> UTF-8 codepage conversion
	DEV_LOGGER logger;
	DATA_LOADER* dLoad;
	UINT32 fileLen;
	const UINT8* fileData;	// data pointer for quick access, equals dLoad->GetFileData().data()
	ARR_UINT8 decFData;
	
	GYM_HEADER fileHdr;
	ARR_DEVCFG devCfgs;
	UINT32 tickFreq;
	UINT32 totalTicks;
	UINT32 loopOfs;
	VEC_STR tagData;	// strings for tags (stores allocated pointers)
	VEC_CSTR tagList;	// tag list that only stores references
	
	ARR_UINT8 pcmBuffer;
	UINT32 pcmBaseTick;
	UINT32 pcmInPos;	// input position (GYM -> buffer)
	UINT32 pcmOutPos;	// output position (buffer -> YM2612)
	UINT8 ymFreqRegs[0x20];	// cache of 0x0A0..0x0AF and 0x1A0..0x1AF frequency registers
	UINT8 ymLatch[2];	// current latch value ([0] = normal channels, [1] = CH3 multi-freq mode registers]
	
	// tick/sample conversion rates
	UINT64 tsMult;
	UINT64 tsDiv;
	UINT64 ttMult;
	UINT64 lastTsMult;
	UINT64 lastTsDiv;
	
	GYM_PLAY_OPTIONS playOpts;
	PLR_DEV_OPTS devOpts[2];	// 0 = YM2612, 1 = SEGA PSG
	ARR_CHIPDEV devices;
	ARR_CSTR devNames;
	size_t optDevMap[2];	// maps devOpts vector index to devices vector
	
	UINT32 filePos;
	UINT32 fileTick;
	UINT32 playTick;
	UINT32 playSmpl;
	UINT32 curLoop;
	UINT32 lastLoopTick;
	
	UINT8 playState;
	UINT8 psTrigger;	// used to temporarily trigger special commands
};


static struct player_engine_vtable GYMEngine_vtbl;

INLINE UINT32 ReadLE32(const UINT8* data);
INLINE void SaveDeviceConfig(ARR_UINT8* dst, const void* srcData, size_t srcLen);
static void FreeStringVectorData(VEC_STR* vec);

PE_GYM* GYMEngine_Create(void);
void GYMEngine_Destroy(PE_GYM* self);
void GYMEngine_Init(PE_GYM* self);
void GYMEngine_Deinit(PE_GYM* self);

UINT8 GYMEngine_CanLoadFile(DATA_LOADER *dataLoader);
static UINT8 GYMEngine_CheckRawGYMFile(UINT32 dataLen, const UINT8* data);
UINT8 GYMEngine_LoadFile(PE_GYM* self, DATA_LOADER *dataLoader);
static UINT8 GYMEngine_DecompressZlibData(PE_GYM* self);
static void GYMEngine_CalcSongLength(PE_GYM* self);
static UINT8 GYMEngine_LoadTags(PE_GYM* self);
static void GYMEngine_LoadTag(PE_GYM* self, const char* tagName, const void* data, size_t maxlen);
static char* GYMEngine_GetUTF8String(PE_GYM* self, const char* startPtr, const char* endPtr);
UINT8 GYMEngine_UnloadFile(PE_GYM* self);
const GYM_HEADER* GYMEngine_GetFileHeader(const PE_GYM* self);

const char* const* GYMEngine_GetTags(PE_GYM* self);
UINT8 GYMEngine_GetSongInfo(PE_GYM* self, PLR_SONG_INFO* songInf);
UINT8 GYMEngine_GetSongDeviceInfo(const PE_GYM* self, size_t* retDevInfCount, PLR_DEV_INFO** retDevInfData);
static size_t GYMEngine_DeviceID2OptionID(const PE_GYM* self, UINT32 id);
static void GYMEngine_RefreshMuting(PE_GYM* self, GYM_CHIPDEV* chipDev, const PLR_MUTE_OPTS* muteOpts);
static void GYMEngine_RefreshPanning(PE_GYM* self, GYM_CHIPDEV* chipDev, const PLR_PAN_OPTS* panOpts);
UINT8 GYMEngine_SetDeviceOptions(PE_GYM* self, UINT32 id, const PLR_DEV_OPTS* devOpts);
UINT8 GYMEngine_GetDeviceOptions(const PE_GYM* self, UINT32 id, PLR_DEV_OPTS* devOpts);
UINT8 GYMEngine_SetDeviceMuting(PE_GYM* self, UINT32 id, const PLR_MUTE_OPTS* muteOpts);
UINT8 GYMEngine_GetDeviceMuting(const PE_GYM* self, UINT32 id, PLR_MUTE_OPTS* muteOpts);
UINT8 GYMEngine_SetPlayerOptions(PE_GYM* self, const GYM_PLAY_OPTIONS* playOpts);
UINT8 GYMEngine_GetPlayerOptions(const PE_GYM* self, GYM_PLAY_OPTIONS* playOpts);

UINT8 GYMEngine_SetSampleRate(PE_GYM* self, UINT32 sampleRate);
double GYMEngine_GetPlaybackSpeed(const PE_GYM* self);
UINT8 GYMEngine_SetPlaybackSpeed(PE_GYM* self, double speed);
static void GYMEngine_RefreshTSRates(PE_GYM* self);
UINT32 GYMEngine_Tick2Sample(const PE_GYM* self, UINT32 ticks);
UINT32 GYMEngine_Sample2Tick(const PE_GYM* self, UINT32 samples);
double GYMEngine_Tick2Second(const PE_GYM* self, UINT32 ticks);

UINT8 GYMEngine_GetState(const PE_GYM* self);
UINT32 GYMEngine_GetCurPos(const PE_GYM* self, UINT8 unit);
UINT32 GYMEngine_GetCurLoop(const PE_GYM* self);
UINT32 GYMEngine_GetTotalTicks(const PE_GYM* self);
UINT32 GYMEngine_GetLoopTicks(const PE_GYM* self);

static void GYMEngine_PlayerLogCB(void* userParam, void* source, UINT8 level, const char* message);
static void GYMEngine_SndEmuLogCB(void* userParam, void* source, UINT8 level, const char* message);

static void GYMEngine_GenerateDeviceConfig(PE_GYM* self);
UINT8 GYMEngine_Start(PE_GYM* self);
UINT8 GYMEngine_Stop(PE_GYM* self);
UINT8 GYMEngine_Reset(PE_GYM* self);
UINT8 GYMEngine_Seek(PE_GYM* self, UINT8 unit, UINT32 pos);
static UINT8 GYMEngine_SeekToTick(PE_GYM* self, UINT32 tick);
static UINT8 GYMEngine_SeekToFilePos(PE_GYM* self, UINT32 pos);
UINT32 GYMEngine_Render(PE_GYM* self, UINT32 smplCnt, WAVE_32BS* data);
static void GYMEngine_ParseFile(PE_GYM* self, UINT32 ticks);
static void GYMEngine_DoCommand(PE_GYM* self);
static void GYMEngine_DoFileEnd(PE_GYM* self);


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

PE_GYM* GYMEngine_Create(void)
{
	PE_GYM* self = malloc(sizeof(PE_GYM));
	if (self == NULL)
		return NULL;

	self->pe.vtbl = GYMEngine_vtbl;
	selfcall.Init(&self->pe);
	return self;
}

void GYMEngine_Destroy(PE_GYM* self)
{
	selfcall.Deinit(&self->pe);
	free(self);
	return;
}

void GYMEngine_Init(PE_GYM* self)
{
	size_t curDev;
	UINT8 retVal;
	
	PBaseEngine_Init(&self->pe);
	
	self->decFData.data = NULL;
	self->devCfgs.data = NULL;
	self->devices.data = NULL;
	self->devNames.data = NULL;
	self->pcmBuffer.data = NULL;

	self->tickFreq = 60;
	self->filePos = 0;
	self->fileTick = 0;
	self->playTick = 0;
	self->playSmpl = 0;
	self->curLoop = 0;
	self->playState = 0x00;
	self->psTrigger = 0x00;

	dev_logger_set(&self->logger, self, GYMEngine_PlayerLogCB, NULL);

	self->playOpts.genOpts.pbSpeed = 0x10000;

	self->lastTsMult = 0;
	self->lastTsDiv = 0;
	
	for (curDev = 0; curDev < 2; curDev ++)
		PBaseEngine_InitDeviceOptions(&self->devOpts[curDev]);
	GYMEngine_GenerateDeviceConfig(self);
	
	retVal = CPConv_Init(&self->cpc1252, "CP1252", "UTF-8");
	if (retVal)
		self->cpc1252 = NULL;
	PE_VECTOR_ALLOC(self->tagData, char*, 8);
	PE_VECTOR_ALLOC(self->tagList, const char*, 16);
	self->tagList.data[self->tagList.size] = NULL;
	self->tagList.size ++;

	return;
}

void GYMEngine_Deinit(PE_GYM* self)
{
	self->pe.eventCbFunc = NULL;	// prevent any callbacks during destruction
	
	if (self->playState & PLAYSTATE_PLAY)
		selfcall.Stop(&self->pe);
	selfcall.UnloadFile(&self->pe);
	
	if (self->cpc1252 != NULL)
		CPConv_Deinit(self->cpc1252);
	PE_ARRAY_FREE(self->devCfgs);
	PE_ARRAY_FREE(self->devNames);

	PE_ARRAY_FREE(self->pcmBuffer);

	FreeStringVectorData(&self->tagData);
	PE_VECTOR_FREE(self->tagData);
	PE_VECTOR_FREE(self->tagList);

	PBaseEngine_Deinit(&self->pe);
	return;
}

UINT8 GYMEngine_CanLoadFile(DATA_LOADER *dataLoader)
{
	DataLoader_ReadUntil(dataLoader, 0x04);
	if (DataLoader_GetSize(dataLoader) < 0x04)
		return 0xF1;	// file too small
	if (! memcmp(&DataLoader_GetData(dataLoader)[0x00], "GYMX", 4))
		return 0x00;	// valid GYMX header
	DataLoader_ReadUntil(dataLoader, 0x200);
	if (GYMEngine_CheckRawGYMFile(DataLoader_GetSize(dataLoader), DataLoader_GetData(dataLoader)))
		return 0x00;	// The heuristic for detection raw GYM files found no issues - assume good file
	return 0xF0;	// invalid file
}

static UINT8 GYMEngine_CheckRawGYMFile(UINT32 dataLen, const UINT8* data)
{
	UINT32 filePos;
	UINT8 curCmd;
	UINT8 curReg;
	UINT8 doPSGFreq2nd;
	
	filePos = 0x00;
	
	while(filePos < dataLen && data[filePos] == 0x00)
		filePos ++;
	if (filePos >= dataLen)
		return 0;	// only 00s - assume invalid file
	
	doPSGFreq2nd = false;
	while(filePos < dataLen)
	{
		curCmd = data[filePos];
		filePos ++;
		switch(curCmd)
		{
		case 0x00:	// wait 1 frame
			doPSGFreq2nd = false;
			break;
		case 0x01:	// YM2612 port 0
		case 0x02:	// YM2612 port 1
			if (filePos + 0x02 > dataLen)
				break;
			curReg = data[filePos + 0x00];
			// valid YM2612 registers are:
			//	port 0: 21..B7
			//	port 1: 30..B7
			if (curReg >= 0xB8)
				return 0;
			if (curCmd == 0x01 && curReg < 0x21)
				return 0;
			if (curCmd == 0x02 && curReg < 0x30)
				return 0;
			filePos += 0x02;
			break;
		case 0x03:	// SEGA PSG
			if (filePos + 0x01 > dataLen)
				break;
			curReg = data[filePos];
			if (curReg & 0x80)
			{
				// bit 7 set = command byte
				if ((curReg & 0x10) == 0x00 && (curReg < 0xE0))
					doPSGFreq2nd = true;	// frequency write - usually followed by a command 00..3F
				else
					doPSGFreq2nd = false;	// single command write (volume or noise type)
			}
			else if (doPSGFreq2nd && curReg < 0x40)
			{
				// this is valid
				doPSGFreq2nd = false;	// expect no other byte
			}
			else
			{
				return 0;	// invalid PSG values
			}
			filePos += 0x01;
			break;
		default:
			return 0;	// assume invalid file
		}
	}
	
	return 1;
}

UINT8 GYMEngine_LoadFile(PE_GYM* self, DATA_LOADER *dataLoader)
{
	self->dLoad = NULL;
	DataLoader_ReadUntil(dataLoader,0x1AC);	// try to read the full GYMX header
	self->fileData = DataLoader_GetData(dataLoader);
	if (DataLoader_GetSize(dataLoader) < 0x04)
		return 0xF0;	// invalid file
	
	self->fileHdr.hasHeader = ! memcmp(&self->fileData[0x00], "GYMX", 4);
	if (! self->fileHdr.hasHeader)
	{
		self->fileHdr.uncomprSize = 0;
		self->fileHdr.loopFrame = 0;
		self->fileHdr.dataOfs = 0x00;
	}
	else
	{
		if (DataLoader_GetSize(dataLoader) < 0x1AC)
			return 0xF1;	// file too small
		self->fileHdr.loopFrame = ReadLE32(&self->fileData[0x1A4]);
		self->fileHdr.uncomprSize = ReadLE32(&self->fileData[0x1A8]);
		self->fileHdr.dataOfs = 0x1AC;
	}
	
	self->dLoad = dataLoader;
	DataLoader_ReadAll(self->dLoad);
	self->fileData = DataLoader_GetData(self->dLoad);
	self->fileLen = DataLoader_GetSize(self->dLoad);
	
	GYMEngine_LoadTags(self);
	
	if (self->fileHdr.uncomprSize > 0)
	{
		UINT8 retVal = GYMEngine_DecompressZlibData(self);
		if (retVal & 0x80)
			return 0xFF;	// decompression error
	}
	self->fileHdr.realFileSize = self->fileLen;
	
	GYMEngine_CalcSongLength(self);
	
	return 0x00;
}

static UINT8 GYMEngine_DecompressZlibData(PE_GYM* self)
{
	z_stream zStream;
	int ret;
	
	PE_ARRAY_FREE(self->decFData)
	PE_ARRAY_MALLOC(self->decFData, UINT8, self->fileHdr.dataOfs + self->fileHdr.uncomprSize)
	memcpy(&self->decFData.data[0], self->fileData, self->fileHdr.dataOfs);	// copy file header
	
	zStream.zalloc = Z_NULL;
	zStream.zfree = Z_NULL;
	zStream.opaque = Z_NULL;
	zStream.avail_in = DataLoader_GetSize(self->dLoad) - self->fileHdr.dataOfs;
	zStream.next_in = (z_const Bytef*)&self->fileData[self->fileHdr.dataOfs];
	ret = inflateInit2(&zStream, 0x20 | 15);
	if (ret != Z_OK)
		return 0xFF;
	zStream.avail_out = (uInt)(self->decFData.size - self->fileHdr.dataOfs);
	zStream.next_out = (Bytef*)&self->decFData.data[self->fileHdr.dataOfs];
	
	ret = inflate(&zStream, Z_SYNC_FLUSH);
	if (! (ret == Z_OK || ret == Z_STREAM_END))
	{
		emu_logf(&self->logger, PLRLOG_ERROR, "GYM decompression error %d after decompressing %lu bytes.\n",
			ret, zStream.total_out);
	}
	self->decFData.size = self->fileHdr.dataOfs + zStream.total_out;
	
	inflateEnd(&zStream);
	
	self->fileData = self->decFData.data;
	self->fileLen = (UINT32)self->decFData.size;
	return (ret == Z_OK || ret == Z_STREAM_END) ? 0x00 : 0x01;
}

static void GYMEngine_CalcSongLength(PE_GYM* self)
{
	UINT32 filePos;
	UINT8 curCmd;
	
	self->totalTicks = 0;
	self->loopOfs = 0;
	
	filePos = self->fileHdr.dataOfs;
	while(filePos < self->fileLen)
	{
		if (self->totalTicks == self->fileHdr.loopFrame && self->fileHdr.loopFrame != 0)
			self->loopOfs = filePos;
		
		curCmd = self->fileData[filePos];
		filePos ++;
		switch(curCmd)
		{
		case 0x00:	// wait 1 frame
			self->totalTicks ++;
			break;
		case 0x01:
		case 0x02:
			filePos += 0x02;
			break;
		case 0x03:
			filePos += 0x01;
			break;
		default:
			// just ignore unknown commands
			break;
		}
	}
	
	return;
}

static UINT8 GYMEngine_LoadTags(PE_GYM* self)
{
	FreeStringVectorData(&self->tagData);
	self->tagList.size = 0;
	if (! self->fileHdr.hasHeader)
	{
		self->tagList.data[self->tagList.size] = NULL;
		self->tagList.size ++;
		return 0x00;
	}
	
	GYMEngine_LoadTag(self, "TITLE",        &self->fileData[0x04], 0x20);
	GYMEngine_LoadTag(self, "GAME",         &self->fileData[0x24], 0x20);
	// no "ARTIST" tag in GYMX files
	GYMEngine_LoadTag(self, "PUBLISHER",    &self->fileData[0x44], 0x20);
	GYMEngine_LoadTag(self, "EMULATOR",     &self->fileData[0x64], 0x20);
	GYMEngine_LoadTag(self, "ENCODED_BY",   &self->fileData[0x84], 0x20);
	GYMEngine_LoadTag(self, "COMMENT",      &self->fileData[0xA4], 0x100);
	
	self->tagList.data[self->tagList.size] = NULL;	// add list terminator
	self->tagList.size ++;
	return 0x00;
}

static void GYMEngine_LoadTag(PE_GYM* self, const char* tagName, const void* data, size_t maxlen)
{
	const char* startPtr = (const char*)data;
	const char* endPtr = (const char*)memchr(startPtr, '\0', maxlen);
	char* utf8Str;

	if (endPtr == NULL)
		endPtr = startPtr + maxlen;
	utf8Str = GYMEngine_GetUTF8String(self, startPtr, endPtr);

	self->tagData.data[self->tagData.size] = utf8Str;
	self->tagData.size ++;
	
	self->tagList.data[self->tagList.size + 0] = tagName;
	self->tagList.data[self->tagList.size + 1] = utf8Str;
	self->tagList.size += 2;

	return;
}

static char* GYMEngine_GetUTF8String(PE_GYM* self, const char* startPtr, const char* endPtr)
{
	char* result = NULL;

	if (startPtr == endPtr)
		return NULL;
	
	if (self->cpc1252 != NULL)
	{
		size_t convSize = 0;
		char* convData = NULL;
		UINT8 retVal = CPConv_StrConvert(self->cpc1252, &convSize, &convData, endPtr - startPtr, startPtr);
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

UINT8 GYMEngine_UnloadFile(PE_GYM* self)
{
	if (self->playState & PLAYSTATE_PLAY)
		return 0xFF;
	
	self->playState = 0x00;
	self->dLoad = NULL;
	self->fileData = NULL;
	PE_ARRAY_FREE(self->decFData)	// free allocated memory
	self->fileHdr.hasHeader = 0;
	self->fileHdr.dataOfs = 0x00;
	PE_ARRAY_FREE(self->devices);
	
	return 0x00;
}

const GYM_HEADER* GYMEngine_GetFileHeader(const PE_GYM* self)
{
	return &self->fileHdr;
}

const char* const* GYMEngine_GetTags(PE_GYM* self)
{
	return self->tagList.data;
}

UINT8 GYMEngine_GetSongInfo(PE_GYM* self, PLR_SONG_INFO* songInf)
{
	if (self->dLoad == NULL)
		return 0xFF;
	
	songInf->format = FCC_GYM;
	songInf->fileVerMaj = 0;
	songInf->fileVerMin = 0;
	songInf->tickRateMul = 1;
	songInf->tickRateDiv = self->tickFreq;
	songInf->songLen = selfcall.GetTotalTicks(&self->pe);
	songInf->loopTick = self->loopOfs ? selfcall.GetLoopTicks(&self->pe) : (UINT32)-1;
	songInf->volGain = 0x10000;
	songInf->deviceCnt = (UINT32)self->devCfgs.size;
	
	return 0x00;
}

UINT8 GYMEngine_GetSongDeviceInfo(const PE_GYM* self, size_t* retDevInfCount, PLR_DEV_INFO** retDevInfData)
{
	if (self->dLoad == NULL)
		return 0xFF;
	
	size_t curDev;
	
	*retDevInfCount = self->devCfgs.size;
	*retDevInfData = (PLR_DEV_INFO*)calloc(*retDevInfCount, sizeof(PLR_DEV_INFO));
	for (curDev = 0; curDev < self->devCfgs.size; curDev ++)
	{
		const DEV_GEN_CFG* devCfg = (DEV_GEN_CFG*)self->devCfgs.data[curDev].data.data;
		PLR_DEV_INFO* devInf = &(*retDevInfData)[curDev];
		memset(devInf, 0x00, sizeof(PLR_DEV_INFO));
		
		devInf->id = (UINT32)curDev;
		devInf->type = self->devCfgs.data[curDev].type;
		devInf->instance = 0;
		devInf->devCfg = devCfg;
		if (self->devices.size > 0)
		{
			const VGM_BASEDEV* cDev = &self->devices.data[curDev].base;
			devInf->devDecl = cDev->defInf.devDecl;
			devInf->core = (cDev->defInf.devDef != NULL) ? cDev->defInf.devDef->coreID : 0x00;
			devInf->volume = (cDev->resmpl.volumeL + cDev->resmpl.volumeR) / 2;
			devInf->smplRate = cDev->defInf.sampleRate;
		}
		else
		{
			devInf->devDecl = SndEmu_GetDevDecl(devInf->type, self->pe.userDevList, self->pe.devStartOpts);
			devInf->core = 0x00;
			devInf->volume = self->devCfgs.data[curDev].volume;
			devInf->smplRate = 0;
		}
	}
	if (self->devices.size > 0)
		return 0x01;	// returned "live" data
	else
		return 0x00;	// returned data based on file header
}

static size_t GYMEngine_DeviceID2OptionID(const PE_GYM* self, UINT32 id)
{
	DEV_ID type;
	UINT8 instance;
	
	if (id & 0x80000000)
	{
		type = (id >> 0) & 0xFF;
		instance = (id >> 16) & 0xFF;
	}
	else if (id < self->devCfgs.size)
	{
		type = self->devCfgs.data[id].type;
		instance = 0;
	}
	else
	{
		return (size_t)-1;
	}
	
	if (instance == 0)
	{
		if (type == DEVID_YM2612)
			return 0;
		else if (type == DEVID_SN76496)
			return 1;
	}
	return (size_t)-1;
}

static void GYMEngine_RefreshMuting(PE_GYM* self, GYM_CHIPDEV* chipDev, const PLR_MUTE_OPTS* muteOpts)
{
	DEV_INFO* devInf = &chipDev->base.defInf;
	if (devInf->dataPtr != NULL && devInf->devDef->SetMuteMask != NULL)
		devInf->devDef->SetMuteMask(devInf->dataPtr, muteOpts->chnMute[0]);
	
	return;
}

// TODO: This is unused?
static void GYMEngine_RefreshPanning(PE_GYM* self, GYM_CHIPDEV* chipDev, const PLR_PAN_OPTS* panOpts)
{
	DEV_INFO* devInf = &chipDev->base.defInf;
	if (devInf->dataPtr == NULL)
		return;
	DEVFUNC_PANALL funcPan = NULL;
	UINT8 retVal = SndEmu_GetDeviceFunc(devInf->devDef, RWF_CHN_PAN | RWF_WRITE, DEVRW_ALL, 0, (void**)&funcPan);
	if (retVal != EERR_NOT_FOUND && funcPan != NULL)
		funcPan(devInf->dataPtr, &panOpts->chnPan[0][0]);
	
	return;
}

UINT8 GYMEngine_SetDeviceOptions(PE_GYM* self, UINT32 id, const PLR_DEV_OPTS* devOpts)
{
	size_t optID = GYMEngine_DeviceID2OptionID(self, id);
	if (optID == (size_t)-1)
		return 0x80;	// bad device ID
	
	self->devOpts[optID] = *devOpts;
	
	size_t devID = self->optDevMap[optID];
	if (devID < self->devices.size)
		GYMEngine_RefreshMuting(self, &self->devices.data[devID], &self->devOpts[optID].muteOpts);
		// TODO: RefreshPanning
	return 0x00;
}

UINT8 GYMEngine_GetDeviceOptions(const PE_GYM* self, UINT32 id, PLR_DEV_OPTS* devOpts)
{
	size_t optID = GYMEngine_DeviceID2OptionID(self, id);
	if (optID == (size_t)-1)
		return 0x80;	// bad device ID
	
	*devOpts = self->devOpts[optID];
	return 0x00;
}

UINT8 GYMEngine_SetDeviceMuting(PE_GYM* self, UINT32 id, const PLR_MUTE_OPTS* muteOpts)
{
	size_t optID = GYMEngine_DeviceID2OptionID(self, id);
	if (optID == (size_t)-1)
		return 0x80;	// bad device ID
	
	self->devOpts[optID].muteOpts = *muteOpts;
	
	size_t devID = self->optDevMap[optID];
	if (devID < self->devices.size)
		GYMEngine_RefreshMuting(self, &self->devices.data[devID], &self->devOpts[optID].muteOpts);
	return 0x00;
}

UINT8 GYMEngine_GetDeviceMuting(const PE_GYM* self, UINT32 id, PLR_MUTE_OPTS* muteOpts)
{
	size_t optID = GYMEngine_DeviceID2OptionID(self, id);
	if (optID == (size_t)-1)
		return 0x80;	// bad device ID
	
	*muteOpts = self->devOpts[optID].muteOpts;
	return 0x00;
}

UINT8 GYMEngine_SetPlayerOptions(PE_GYM* self, const GYM_PLAY_OPTIONS* playOpts)
{
	self->playOpts = *playOpts;
	return 0x00;
}

UINT8 GYMEngine_GetPlayerOptions(const PE_GYM* self, GYM_PLAY_OPTIONS* playOpts)
{
	*playOpts = self->playOpts;
	return 0x00;
}

UINT8 GYMEngine_SetSampleRate(PE_GYM* self, UINT32 sampleRate)
{
	if (self->playState & PLAYSTATE_PLAY)
		return 0x01;	// can't set during playback
	
	self->pe.outSmplRate = sampleRate;
	return 0x00;
}

double GYMEngine_GetPlaybackSpeed(const PE_GYM* self)
{
	return self->playOpts.genOpts.pbSpeed / (double)0x10000;
}

UINT8 GYMEngine_SetPlaybackSpeed(PE_GYM* self, double speed)
{
	self->playOpts.genOpts.pbSpeed = (UINT32)(0x10000 * speed);
	GYMEngine_RefreshTSRates(self);
	return 0x00;
}


static void GYMEngine_RefreshTSRates(PE_GYM* self)
{
	self->ttMult = 1;
	self->tsDiv = self->tickFreq;
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

UINT32 GYMEngine_Tick2Sample(const PE_GYM* self, UINT32 ticks)
{
	if (ticks == (UINT32)-1)
		return -1;
	return (UINT32)(ticks * self->tsMult / self->tsDiv);
}

UINT32 GYMEngine_Sample2Tick(const PE_GYM* self, UINT32 samples)
{
	if (samples == (UINT32)-1)
		return -1;
	return (UINT32)(samples * self->tsDiv / self->tsMult);
}

double GYMEngine_Tick2Second(const PE_GYM* self, UINT32 ticks)
{
	if (ticks == (UINT32)-1)
		return -1.0;
	return (INT64)(ticks * self->ttMult) / (double)(INT64)self->tsDiv;
}

UINT8 GYMEngine_GetState(const PE_GYM* self)
{
	return self->playState;
}

UINT32 GYMEngine_GetCurPos(const PE_GYM* self, UINT8 unit)
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

UINT32 GYMEngine_GetCurLoop(const PE_GYM* self)
{
	return self->curLoop;
}

UINT32 GYMEngine_GetTotalTicks(const PE_GYM* self)
{
	return self->totalTicks;
}

UINT32 GYMEngine_GetLoopTicks(const PE_GYM* self)
{
	if (! self->loopOfs)
		return 0;
	else
		return self->totalTicks - self->fileHdr.loopFrame;
}

static void GYMEngine_PlayerLogCB(void* userParam, void* source, UINT8 level, const char* message)
{
	PE_GYM* player = (PE_GYM*)source;
	if (player->pe.logCbFunc == NULL)
		return;
	player->pe.logCbFunc(player->pe.logCbParam, &player->pe, level, PLRLOGSRC_PLR, NULL, message);
	return;
}

static void GYMEngine_SndEmuLogCB(void* userParam, void* source, UINT8 level, const char* message)
{
	DEVLOG_CB_DATA* cbData = (DEVLOG_CB_DATA*)userParam;
	PE_GYM* player = cbData->player;
	if (player->pe.logCbFunc == NULL)
		return;
	if ((player->playState & PLAYSTATE_SEEK) && level > PLRLOG_ERROR)
		return;	// prevent message spam while seeking
	player->pe.logCbFunc(player->pe.logCbParam, &player->pe, level, PLRLOGSRC_EMU,
		player->devNames.data[cbData->chipDevID], message);
	return;
}


static void GYMEngine_GenerateDeviceConfig(PE_GYM* self)
{
	PE_ARRAY_FREE(self->devCfgs);
	PE_ARRAY_CALLOC(self->devCfgs, GYM_DEVCFG, 2);
	PE_ARRAY_FREE(self->devNames);
	PE_ARRAY_MALLOC(self->devNames, const char*, 2);
	{
		DEV_GEN_CFG devCfg;
		memset(&devCfg, 0x00, sizeof(DEV_GEN_CFG));
		devCfg.clock = 7670453;	// YMAMP clock: 7670442
		self->devCfgs.data[0].type = DEVID_YM2612;
		self->devCfgs.data[0].volume = 0x100;
		SaveDeviceConfig(&self->devCfgs.data[0].data, &devCfg, sizeof(DEV_GEN_CFG));
		self->devNames.data[0] = "FM";
	}
	{
		SN76496_CFG snCfg;
		memset(&snCfg, 0x00, sizeof(SN76496_CFG));
		snCfg._genCfg.clock = 3579545;	// YMAMP clock: 3579580
		snCfg.shiftRegWidth = 0x10;
		snCfg.noiseTaps = 0x09;
		snCfg.segaPSG = 1;
		snCfg.negate = 0;
		snCfg.stereo = 1;
		snCfg.clkDiv = 8;
		snCfg.t6w28_tone = NULL;
		self->devCfgs.data[1].type = DEVID_SN76496;
		self->devCfgs.data[1].volume = 0x80;
		SaveDeviceConfig(&self->devCfgs.data[1].data, &snCfg, sizeof(SN76496_CFG));
		self->devNames.data[1] = "PSG";
	}
	
	return;
}

UINT8 GYMEngine_Start(PE_GYM* self)
{
	size_t pcmBufSize;
	size_t curDev;
	UINT8 retVal;
	
	pcmBufSize = self->pe.outSmplRate / 30;	// that's the buffer size in YMAMP
	if (self->pcmBuffer.size != pcmBufSize)
	{
		PE_ARRAY_FREE(self->pcmBuffer);
		PE_ARRAY_MALLOC(self->pcmBuffer, UINT8, pcmBufSize);
	}

	for (curDev = 0; curDev < 2; curDev ++)
		self->optDevMap[curDev] = (size_t)-1;
	
	PE_ARRAY_FREE(self->devices);
	PE_ARRAY_MALLOC(self->devices, GYM_CHIPDEV, self->devCfgs.size);
	for (curDev = 0; curDev < self->devCfgs.size; curDev ++)
	{
		GYM_CHIPDEV* cDev = &self->devices.data[curDev];
		PLR_DEV_OPTS* devOpts;
		DEV_GEN_CFG* devCfg = (DEV_GEN_CFG*)self->devCfgs.data[curDev].data.data;
		VGM_BASEDEV* clDev;
		
		cDev->base.defInf.dataPtr = NULL;
		cDev->base.linkDev = NULL;
		cDev->optID = GYMEngine_DeviceID2OptionID(self, (UINT32)curDev);
		
		devOpts = (cDev->optID != (size_t)-1) ? &self->devOpts[cDev->optID] : NULL;
		devCfg->emuCore = (devOpts != NULL) ? devOpts->emuCore[0] : 0x00;
		devCfg->srMode = (devOpts != NULL) ? devOpts->srMode : DEVRI_SRMODE_NATIVE;
		if (devOpts != NULL && devOpts->smplRate)
			devCfg->smplRate = devOpts->smplRate;
		else
			devCfg->smplRate = self->pe.outSmplRate;
		
		retVal = SndEmu_Start2(self->devCfgs.data[curDev].type, devCfg, &cDev->base.defInf, self->pe.userDevList, self->pe.devStartOpts);
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
			cDev->base.defInf.devDef->SetLogCB(cDev->base.defInf.dataPtr, GYMEngine_SndEmuLogCB, &cDev->logCbData);
		SetupLinkedDevices(&cDev->base, NULL, NULL);
		
		if (devOpts != NULL)
		{
			if (cDev->base.defInf.devDef->SetOptionBits != NULL)
				cDev->base.defInf.devDef->SetOptionBits(cDev->base.defInf.dataPtr, devOpts->coreOpts);
			// TODO: RefreshMuting
			// TODO: RefreshPanning
			
			self->optDevMap[cDev->optID] = curDev;
		}
		
		for (clDev = &cDev->base; clDev != NULL; clDev = clDev->linkDev)
		{
			UINT8 resmplMode = (devOpts != NULL) ? devOpts->resmplMode : RSMODE_LINEAR;
			Resmpl_SetVals(&clDev->resmpl, resmplMode, self->devCfgs.data[curDev].volume, self->pe.outSmplRate);
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

UINT8 GYMEngine_Stop(PE_GYM* self)
{
	size_t curDev;
	
	self->playState &= ~PLAYSTATE_PLAY;
	
	for (curDev = 0; curDev < self->devices.size; curDev ++)
	{
		GYM_CHIPDEV* cDev = &self->devices.data[curDev];
		FreeDeviceTree(&cDev->base, 0);
	}
	PE_ARRAY_FREE(self->devices);
	if (self->pe.eventCbFunc != NULL)
		self->pe.eventCbFunc(&self->pe, self->pe.eventCbParam, PLREVT_STOP, NULL);
	
	return 0x00;
}

UINT8 GYMEngine_Reset(PE_GYM* self)
{
	size_t curDev;
	
	self->filePos = self->fileHdr.dataOfs;
	self->fileTick = 0;
	self->playTick = 0;
	self->playSmpl = 0;
	self->playState &= ~PLAYSTATE_END;
	self->psTrigger = 0x00;
	self->curLoop = 0;
	self->lastLoopTick = 0;
	
	self->pcmBaseTick = (UINT32)-1;
	self->pcmInPos = 0x00;
	self->pcmOutPos = (UINT32)-1;

	GYMEngine_RefreshTSRates(self);	

	for (curDev = 0; curDev < self->devices.size; curDev ++)
	{
		GYM_CHIPDEV* cDev = &self->devices.data[curDev];
		VGM_BASEDEV* clDev;
		if (cDev->base.defInf.dataPtr == NULL)
			continue;
		
		cDev->base.defInf.devDef->Reset(cDev->base.defInf.dataPtr);
		for (clDev = &cDev->base; clDev != NULL; clDev = clDev->linkDev)
		{
			// TODO: Resmpl_Reset(&clDev->resmpl);
		}
	}
	
	return 0x00;
}

UINT8 GYMEngine_Seek(PE_GYM* self, UINT8 unit, UINT32 pos)
{
	switch(unit)
	{
	case PLAYPOS_FILEOFS:
		self->playState |= PLAYSTATE_SEEK;
		if (pos < self->filePos)
			selfcall.Reset(&self->pe);
		return GYMEngine_SeekToFilePos(self, pos);
	case PLAYPOS_SAMPLE:
		pos = selfcall.Sample2Tick(&self->pe, pos);
		// fall through
	case PLAYPOS_TICK:
		self->playState |= PLAYSTATE_SEEK;
		if (pos < self->playTick)
			selfcall.Reset(&self->pe);
		return GYMEngine_SeekToTick(self, pos);
	case PLAYPOS_COMMAND:
	default:
		return 0xFF;
	}
}

static UINT8 GYMEngine_SeekToTick(PE_GYM* self, UINT32 tick)
{
	self->playState |= PLAYSTATE_SEEK;
	if (tick > self->playTick)
		GYMEngine_ParseFile(self, tick - self->playTick);
	self->playSmpl = selfcall.Tick2Sample(&self->pe, self->playTick);
	self->playState &= ~PLAYSTATE_SEEK;
	return 0x00;
}

static UINT8 GYMEngine_SeekToFilePos(PE_GYM* self, UINT32 pos)
{
	self->playState |= PLAYSTATE_SEEK;
	while(self->filePos <= pos && ! (self->playState & PLAYSTATE_END))
		GYMEngine_DoCommand(self);
	self->playTick = self->fileTick;
	self->playSmpl = selfcall.Tick2Sample(&self->pe, self->playTick);
	self->playState &= ~PLAYSTATE_SEEK;
	
	return 0x00;
}

UINT32 GYMEngine_Render(PE_GYM* self, UINT32 smplCnt, WAVE_32BS* data)
{
	UINT32 curSmpl;
	UINT32 smplFileTick;
	UINT32 maxSmpl;
	INT32 smplStep;	// might be negative due to rounding errors in Tick2Sample
	size_t curDev;
	UINT32 pcmLastBase = (UINT32)-1;
	UINT32 pcmSmplStart = 0;
	UINT32 pcmSmplLen = 1;
	
	// Note: use do {} while(), so that "smplCnt == 0" can be used to process until reaching the next sample.
	curSmpl = 0;
	do
	{
		smplFileTick = GYMEngine_Sample2Tick(self, self->playSmpl);
		GYMEngine_ParseFile(self, smplFileTick - self->playTick);
		
		// render as many samples at once as possible (for better performance)
		maxSmpl = GYMEngine_Tick2Sample(self, self->fileTick);
		smplStep = maxSmpl - self->playSmpl;
		// reduce sample steps to 1 when PCM is active
		if (smplStep < 1 || self->pcmInPos > 0)
			smplStep = 1;	// must render at least 1 sample in order to advance
		if ((UINT32)smplStep > smplCnt - curSmpl)
			smplStep = smplCnt - curSmpl;
		
		if (self->pcmInPos > 0)
		{
			// PCM buffer handling
			UINT32 pcmIdx;

			// Stream all buffered writes to the YM2612, evenly distributed over the current frame.
			if (pcmLastBase != self->pcmBaseTick)
			{
				pcmLastBase = self->pcmBaseTick;
				pcmSmplStart = GYMEngine_Tick2Sample(self, self->pcmBaseTick);
				pcmSmplLen = GYMEngine_Tick2Sample(self, self->pcmBaseTick + 1) - pcmSmplStart;
			}
			pcmIdx = (self->playSmpl - pcmSmplStart) * self->pcmInPos / pcmSmplLen;
			if (pcmIdx != self->pcmOutPos)
			{
				GYM_CHIPDEV* cDev = &self->devices.data[0];
				DEV_DATA* dataPtr = cDev->base.defInf.dataPtr;
				self->pcmOutPos = pcmIdx;
				if (! (dataPtr == NULL || cDev->write == NULL) && self->pcmOutPos < self->pcmInPos)
				{
					cDev->write(dataPtr, 0, 0x2A);
					cDev->write(dataPtr, 1, self->pcmBuffer.data[pcmIdx]);
				}
				if (self->pcmOutPos == self->pcmInPos - 1)
					self->pcmInPos = 0;	// reached the end of the buffer - disable further PCM streaming
			}
		}
		
		for (curDev = 0; curDev < self->devices.size; curDev ++)
		{
			GYM_CHIPDEV* cDev = &self->devices.data[curDev];
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

static void GYMEngine_ParseFile(PE_GYM* self, UINT32 ticks)
{
	self->playTick += ticks;
	if (self->playState & PLAYSTATE_END)
		return;
	
	while(self->fileTick <= self->playTick && ! (self->playState & PLAYSTATE_END))
		GYMEngine_DoCommand(self);
	
	return;
}

static void GYMEngine_DoCommand(PE_GYM* self)
{
	UINT8 curCmd;
	
	if (self->filePos >= self->fileLen)
	{
		GYMEngine_DoFileEnd(self);
		return;
	}
	
	curCmd = self->fileData[self->filePos];
	self->filePos ++;
	switch(curCmd)
	{
	case 0x00:	// wait 1 frame
		self->fileTick ++;
		return;
	case 0x01:	// write to YM2612 port 0
	case 0x02:	// write to YM2612 port 1
		{
			UINT8 port = curCmd - 0x01;
			UINT8 reg = self->fileData[self->filePos + 0x00];
			UINT8 data = self->fileData[self->filePos + 0x01];
			self->filePos += 0x02;
			
			if (port == 0 && reg == 0x2A)
			{
				if (self->playState & PLAYSTATE_SEEK)
					return;	// ignore PCM buffer while seeking
				// special handling for PCM writes: put them into a separate buffer
				if (self->pcmBaseTick != self->fileTick)
				{
					self->pcmBaseTick = self->fileTick;
					self->pcmInPos = 0;
					self->pcmOutPos = (UINT32)-1;
				}
				if (self->pcmInPos < self->pcmBuffer.size)
				{
					self->pcmBuffer.data[self->pcmInPos] = data;
					self->pcmInPos ++;
				}
				return;
			}
			
			GYM_CHIPDEV* cDev = &self->devices.data[0];
			DEV_DATA* dataPtr = cDev->base.defInf.dataPtr;
			if (dataPtr == NULL || cDev->write == NULL)
				return;
			
			if ((reg & 0xF0) == 0xA0)
			{
				// Note: The OPN series has a particular behaviour with frequency registers (Ax) that
				// wasn't known when GYM files were created.
				// In short: reg A4..A6 must be followed by reg A0..A2 of the same channel to have the intended effect.
				// (same for reg AC..AE and A8..AA)
				//
				// Thus we apply the following patches, in order to be able to play back GYMs without glitches:
				// - "reg A4" only -> send "reg A4", then "reg A0" (with data from cache)
				// - "reg A0" only -> send "reg A4" (with data from cache), then "reg A0"
				UINT8 cacheReg = (port << 4) | (reg & 0x0F);
				UINT8 isLatch = (reg & 0x04);
				UINT8 latchID = (reg & 0x08) >> 3;
				self->ymFreqRegs[cacheReg] = data;
				
				if (isLatch)
				{
					UINT8 needPatch = 1;
					if (self->filePos + 0x01 < self->fileLen &&
						self->fileData[self->filePos + 0x00] == curCmd && self->fileData[self->filePos + 0x01] == (reg ^ 0x04))
						needPatch = 0;	// the next write is the 2nd part - no patch needed
					
					cDev->write(dataPtr, (port << 1) | 0, reg);
					cDev->write(dataPtr, (port << 1) | 1, data);
					self->ymLatch[latchID] = data;
					if (needPatch)
					{
						//emu_logf(&self->logger, PLRLOG_TRACE, "Fixing missing freq p2: %03X=%02X + [%03X=%02X]\n",
						//	(port << 8) | reg, data, (port << 8) | (reg ^ 0x04), self->ymFreqRegs[cacheReg ^ 0x04]);
						// complete the 2-part write by sending the command for the 2nd part
						cDev->write(dataPtr, (port << 1) | 0, reg ^ 0x04);
						cDev->write(dataPtr, (port << 1) | 1, self->ymFreqRegs[cacheReg ^ 0x04]);
					}
				}
				else
				{
					if (self->ymLatch[latchID] != self->ymFreqRegs[cacheReg ^ 0x04])
					{
						//emu_logf(&self->logger, PLRLOG_TRACE, "Fixing missing freq p1: [%03X=%02X] + %03X=%02X\n",
						//	(port << 8) | (reg ^ 0x04), self->ymFreqRegs[cacheReg ^ 0x04], (port << 8) | reg, data);
						// make sure to set the latch to the correct value
						cDev->write(dataPtr, (port << 1) | 0, reg ^ 0x04);
						cDev->write(dataPtr, (port << 1) | 1, self->ymFreqRegs[cacheReg ^ 0x04]);
						self->ymLatch[latchID] = self->ymFreqRegs[cacheReg ^ 0x04];
					}
					cDev->write(dataPtr, (port << 1) | 0, reg);
					cDev->write(dataPtr, (port << 1) | 1, data);
				}
			}
			else
			{
				cDev->write(dataPtr, (port << 1) | 0, reg);
				cDev->write(dataPtr, (port << 1) | 1, data);
			}
		}
		return;
	case 0x03:	// write to PSG
		{
			UINT8 data = self->fileData[self->filePos + 0x00];
			self->filePos += 0x01;
			
			GYM_CHIPDEV* cDev = &self->devices.data[1];
			DEV_DATA* dataPtr = cDev->base.defInf.dataPtr;
			if (dataPtr == NULL || cDev->write == NULL)
				return;
			
			cDev->write(dataPtr, SN76496_W_REG, data);
		}
		return;
	default:
		emu_logf(&self->logger, PLRLOG_WARN, "Unknown GYM command %02X found! (filePos 0x%06X)\n", curCmd, self->filePos - 0x01);
		return;
	}
	
	return;
}

static void GYMEngine_DoFileEnd(PE_GYM* self)
{
	UINT8 doLoop = (self->loopOfs != 0);
	
	if (self->playState & PLAYSTATE_SEEK)	// recalculate playSmpl to fix state when triggering callbacks
		self->playSmpl = selfcall.Tick2Sample(&self->pe, self->fileTick);	// Note: fileTick results in more accurate position
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
		if (self->pe.eventCbFunc != NULL)
		{
			UINT8 retVal;
			
			retVal = self->pe.eventCbFunc(&self->pe, self->pe.eventCbParam, PLREVT_LOOP, &self->curLoop);
			if (retVal == 0x01)	// "stop" signal?
			{
				self->playState |= PLAYSTATE_END;
				self->psTrigger |= PLAYSTATE_END;
				if (self->pe.eventCbFunc != NULL)
					self->pe.eventCbFunc(&self->pe, self->pe.eventCbParam, PLREVT_END, NULL);
				return;
			}
		}
		self->filePos = self->loopOfs;
		return;
	}
	
	self->playState |= PLAYSTATE_END;
	self->psTrigger |= PLAYSTATE_END;
	if (self->pe.eventCbFunc != NULL)
		self->pe.eventCbFunc(&self->pe, self->pe.eventCbParam, PLREVT_END, NULL);
	
	return;
}


static struct player_engine_vtable GYMEngine_vtbl =
{
	FCC_GYM,	// playerType
	"GYM",	// playerName
	
	(void*)GYMEngine_Init,
	(void*)GYMEngine_Deinit,
	
	(void*)GYMEngine_CanLoadFile,
	(void*)GYMEngine_LoadFile,
	(void*)GYMEngine_UnloadFile,
	
	(void*)GYMEngine_GetTags,
	(void*)GYMEngine_GetSongInfo,
	(void*)GYMEngine_GetSongDeviceInfo,

	(void*)GYMEngine_SetDeviceOptions,
	(void*)GYMEngine_GetDeviceOptions,
	(void*)GYMEngine_SetDeviceMuting,
	(void*)GYMEngine_GetDeviceMuting,

	(void*)GYMEngine_SetPlayerOptions,
	(void*)GYMEngine_GetPlayerOptions,
	
	PBaseEngine_GetSampleRate,
	(void*)GYMEngine_SetSampleRate,
	(void*)GYMEngine_GetPlaybackSpeed,
	(void*)GYMEngine_SetPlaybackSpeed,
	PBaseEngine_SetUserDevices,
	PBaseEngine_SetEventCallback,
	PBaseEngine_SetFileReqCallback,
	PBaseEngine_SetLogCallback,
	(void*)GYMEngine_Tick2Sample,
	(void*)GYMEngine_Sample2Tick,
	(void*)GYMEngine_Tick2Second,
	PBaseEngine_Sample2Second,
	
	(void*)GYMEngine_GetState,
	(void*)GYMEngine_GetCurPos,
	(void*)GYMEngine_GetCurLoop,
	(void*)GYMEngine_GetTotalTicks,
	(void*)GYMEngine_GetLoopTicks,
	PBaseEngine_GetTotalPlayTicks,
	
	(void*)GYMEngine_Start,
	(void*)GYMEngine_Stop,
	(void*)GYMEngine_Reset,
	(void*)GYMEngine_Seek,
	(void*)GYMEngine_Render,
};
