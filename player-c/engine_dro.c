#include <stdlib.h>
#include <string.h>
#include <stdio.h>	// for snprintf()

#include "../common_def.h"
#include "engine_base.h"
#include "engine_dro.h"
#include "../emu/EmuStructs.h"
#include "../emu/SoundEmu.h"
#include "../emu/Resampler.h"
#include "../emu/SoundDevs.h"
#include "../emu/EmuCores.h"
#include "../player/helper.h"
#include "../emu/logging.h"

#ifdef _MSC_VER
#define snprintf	_snprintf
#endif


typedef enum dro_hwtypes
{
	DROHW_OPL2 = 0,
	DROHW_DUALOPL2 = 1,
	DROHW_OPL3 = 2,
} DRO_HWTYPES;


typedef struct device_log_callback_data
{
	PE_DRO* player;
	size_t chipDevID;
} DEVLOG_CB_DATA;
typedef struct dro_chip_device
{
	VGM_BASEDEV base;
	size_t optID;
	DEVFUNC_WRITE_A8D8 write;
	DEVLOG_CB_DATA logCbData;
} DRO_CHIPDEV;

DEFINE_PE_ARRAY(DEV_GEN_CFG, ARR_DEVCFG)
DEFINE_PE_ARRAY(DRO_CHIPDEV, ARR_CHIPDEV)
DEFINE_PE_ARRAY(const char*, ARR_CSTR)
#define selfcall	self->pe.vtbl


struct player_engine_dro
{
	PEBASE pe;
	
	DEV_LOGGER logger;
	DATA_LOADER* dLoad;
	const UINT8* fileData;	// data pointer for quick access, equals dLoad->GetFileData().data()
	
	DRO_HEADER fileHdr;
	UINT8 devTypeCnt;
	DEV_ID devTypes[2];
	UINT8 devPanning[2];
	ARR_DEVCFG devCfgs;
	UINT8 realHwType;
	UINT8 portShift;	// 0 for OPL2 (1 port per chip), 1 for OPL3 (2 ports per chip)
	UINT8 portMask;	// (1 << portShift) - 1
	UINT32 tickFreq;
	UINT32 totalTicks;
	
	// information about the initialization block (ends with first delay)
	UINT32 initBlkEndOfs;	// offset of end of init. block (for special fixes)
	UINT8 initRegSet[0x200];	// registers set during init. block
	UINT8 initOPL3Enable;	// value of OPL3 enable register set during init. block
	
	// tick/sample conversion rates
	UINT64 tsMult;
	UINT64 tsDiv;
	UINT64 ttMult;
	UINT64 lastTsMult;
	UINT64 lastTsDiv;
	
	DRO_PLAY_OPTIONS playOpts;
	PLR_DEV_OPTS devOpts[3];	// 0 = 1st OPL2, 1 = 2nd OPL2, 2 = 1st OPL3
	ARR_CHIPDEV devices;
	ARR_CSTR devNames;
	size_t optDevMap[3];	// maps devOpts vector index to devices vector
	char devNameBuffer[0x20];
	
	UINT32 filePos;
	UINT32 fileTick;
	UINT32 playTick;
	UINT32 playSmpl;
	
	UINT8 playState;
	UINT8 psTrigger;	// used to temporarily trigger special commands
	UINT8 selPort;		// currently selected OPL chip (for DRO v1)
};


static struct player_engine_vtable DROEngine_vtbl;

INLINE UINT16 ReadLE16(const UINT8* data);
INLINE UINT32 ReadLE32(const UINT8* data);
PE_DRO* DROEngine_Create(void);
void DROEngine_Destroy(PE_DRO* self);
void DROEngine_Init(PE_DRO* self);
void DROEngine_Deinit(PE_DRO* self);
UINT8 DROEngine_CanLoadFile(DATA_LOADER *dataLoader);
UINT8 DROEngine_LoadFile(PE_DRO* self, DATA_LOADER *dataLoader);
static void DROEngine_ScanInitBlock(PE_DRO* self);
UINT8 DROEngine_UnloadFile(PE_DRO* self);
const DRO_HEADER* DROEngine_GetFileHeader(const PE_DRO* self);
const char* const* DROEngine_GetTags(PE_DRO* self);
UINT8 DROEngine_GetSongInfo(PE_DRO* self, PLR_SONG_INFO* songInf);
UINT8 DROEngine_GetSongDeviceInfo(const PE_DRO* self, size_t* retDevInfCount, PLR_DEV_INFO** retDevInfData);
static size_t DROEngine_DeviceID2OptionID(const PE_DRO* self, UINT32 id);
static void DROEngine_RefreshMuting(PE_DRO* self, DRO_CHIPDEV* chipDev, const PLR_MUTE_OPTS* muteOpts);
static void DROEngine_RefreshPanning(PE_DRO* self, DRO_CHIPDEV* chipDev, const PLR_PAN_OPTS* panOpts);
UINT8 DROEngine_SetDeviceOptions(PE_DRO* self, UINT32 id, const PLR_DEV_OPTS* devOpts);
UINT8 DROEngine_GetDeviceOptions(const PE_DRO* self, UINT32 id, PLR_DEV_OPTS* devOpts);
UINT8 DROEngine_SetDeviceMuting(PE_DRO* self, UINT32 id, const PLR_MUTE_OPTS* muteOpts);
UINT8 DROEngine_GetDeviceMuting(const PE_DRO* self, UINT32 id, PLR_MUTE_OPTS* muteOpts);
UINT8 DROEngine_SetPlayerOptions(PE_DRO* self, const DRO_PLAY_OPTIONS* playOpts);
UINT8 DROEngine_GetPlayerOptions(const PE_DRO* self, DRO_PLAY_OPTIONS* playOpts);
UINT8 DROEngine_SetSampleRate(PE_DRO* self, UINT32 sampleRate);
double DROEngine_GetPlaybackSpeed(const PE_DRO* self);
UINT8 DROEngine_SetPlaybackSpeed(PE_DRO* self, double speed);

static void DROEngine_RefreshTSRates(PE_DRO* self);
UINT32 DROEngine_Tick2Sample(const PE_DRO* self, UINT32 ticks);
UINT32 DROEngine_Sample2Tick(const PE_DRO* self, UINT32 samples);
double DROEngine_Tick2Second(const PE_DRO* self, UINT32 ticks);
UINT8 DROEngine_GetState(const PE_DRO* self);
UINT32 DROEngine_GetCurPos(const PE_DRO* self, UINT8 unit);
UINT32 DROEngine_GetCurLoop(const PE_DRO* self);
UINT32 DROEngine_GetTotalTicks(const PE_DRO* self);
UINT32 DROEngine_GetLoopTicks(const PE_DRO* self);
static void DROEngine_PlayerLogCB(void* userParam, void* source, UINT8 level, const char* message);
static void DROEngine_SndEmuLogCB(void* userParam, void* source, UINT8 level, const char* message);

static void DROEngine_GenerateDeviceConfig(PE_DRO* self);
UINT8 DROEngine_Start(PE_DRO* self);
UINT8 DROEngine_Stop(PE_DRO* self);
UINT8 DROEngine_Reset(PE_DRO* self);
UINT8 DROEngine_Seek(PE_DRO* self, UINT8 unit, UINT32 pos);
static UINT8 DROEngine_SeekToTick(PE_DRO* self, UINT32 tick);
static UINT8 DROEngine_SeekToFilePos(PE_DRO* self, UINT32 pos);
UINT32 DROEngine_Render(PE_DRO* self, UINT32 smplCnt, WAVE_32BS* data);
static void DROEngine_ParseFile(PE_DRO* self, UINT32 ticks);
static void DROEngine_DoCommand_v1(PE_DRO* self);
static void DROEngine_DoCommand_v2(PE_DRO* self);
static void DROEngine_DoFileEnd(PE_DRO* self);
static void DROEngine_WriteReg(PE_DRO* self, UINT8 port, UINT8 reg, UINT8 data);


INLINE UINT16 ReadLE16(const UINT8* data)
{
	return (data[0x01] << 8) | (data[0x00] << 0);
}

INLINE UINT32 ReadLE32(const UINT8* data)
{
	return	(data[0x03] << 24) | (data[0x02] << 16) |
			(data[0x01] <<  8) | (data[0x00] <<  0);
}

PE_DRO* DROEngine_Create(void)
{
	PE_DRO* self = malloc(sizeof(PE_DRO));
	if (self == NULL)
		return NULL;

	self->pe.vtbl = DROEngine_vtbl;
	selfcall.Init(&self->pe);
	return self;
}

void DROEngine_Destroy(PE_DRO* self)
{
	selfcall.Deinit(&self->pe);
	free(self);
	return;
}

void DROEngine_Init(PE_DRO* self)
{
	size_t curDev;
	
	PBaseEngine_Init(&self->pe);
	
	self->tickFreq = 1000;
	self->filePos = 0;
	self->fileTick = 0;
	self->playTick = 0;
	self->playSmpl = 0;
	self->playState = 0x00;
	self->psTrigger = 0x00;
	
	dev_logger_set(&self->logger, self, DROEngine_PlayerLogCB, NULL);
	
	self->playOpts.genOpts.pbSpeed = 0x10000;
	self->playOpts.v2opl3Mode = DRO_V2OPL3_DETECT;
	
	self->lastTsMult = 0;
	self->lastTsDiv = 0;
	
	for (curDev = 0; curDev < 3; curDev ++)
		PBaseEngine_InitDeviceOptions(&self->devOpts[curDev]);
	memset(self->initRegSet, 0x00, sizeof(self->initRegSet));
	
	self->devTypeCnt = 0;
	self->devCfgs.size = 0;	self->devCfgs.data = NULL;
	self->devices.size = 0;	self->devices.data = NULL;
	self->devNames.size = 0;	self->devNames.data = NULL;
	
	return;
}

void DROEngine_Deinit(PE_DRO* self)
{
	self->pe.eventCbFunc = NULL;	// prevent any callbacks during destruction
	
	if (self->playState & PLAYSTATE_PLAY)
		selfcall.Stop(&self->pe);
	selfcall.UnloadFile(&self->pe);

	PBaseEngine_Deinit(&self->pe);
	return;
}

UINT8 DROEngine_CanLoadFile(DATA_LOADER *dataLoader)
{
	DataLoader_ReadUntil(dataLoader, 0x10);
	if (DataLoader_GetSize(dataLoader) < 0x10)
		return 0xF1;	// file too small
	if (memcmp(&DataLoader_GetData(dataLoader)[0x00], "DBRAWOPL", 8))
		return 0xF0;	// invalid signature
	return 0x00;
}

UINT8 DROEngine_LoadFile(PE_DRO* self, DATA_LOADER *dataLoader)
{
	UINT32 tempLng;
	
	self->dLoad = NULL;
	DataLoader_ReadUntil(dataLoader,0x10);
	self->fileData = DataLoader_GetData(dataLoader);
	if (DataLoader_GetSize(dataLoader) < 0x10 || memcmp(&self->fileData[0x00], "DBRAWOPL", 8))
		return 0xF0;	// invalid file
	
	// --- try to detect the DRO version ---
	tempLng = ReadLE32(&self->fileData[0x08]);
	if (tempLng & 0xFF00FF00)
	{
		// DRO v0 - This version didn't write version bytes.
		self->fileHdr.verMajor = 0x00;
		self->fileHdr.verMinor = 0x00;
	}
	else if (! (tempLng & 0x0000FFFF))
	{
		// DRO v1 - order is: minor, major
		self->fileHdr.verMinor = ReadLE16(&self->fileData[0x08]);
		self->fileHdr.verMajor = ReadLE16(&self->fileData[0x0A]);
	}
	else
	{
		// DRO v2 - order is: major, minor
		self->fileHdr.verMajor = ReadLE16(&self->fileData[0x08]);
		self->fileHdr.verMinor = ReadLE16(&self->fileData[0x0A]);
	}
	if (self->fileHdr.verMajor > 2)
		return 0xF1;	// unsupported version
	
	self->dLoad = dataLoader;
	DataLoader_ReadAll(self->dLoad);
	self->fileData = DataLoader_GetData(self->dLoad);
	
	switch(self->fileHdr.verMajor)
	{
	case 0:	// version 0 (DOSBox 0.62)
	case 1:	// version 1 (DOSBox 0.63)
		switch(self->fileHdr.verMajor)
		{
		case 0:
			self->fileHdr.lengthMS = ReadLE32(&self->fileData[0x08]);
			self->fileHdr.dataSize = ReadLE32(&self->fileData[0x0C]);
			self->fileHdr.hwType = self->fileData[0x10];
			self->fileHdr.dataOfs = 0x11;
			break;
		case 1:
			self->fileHdr.lengthMS = ReadLE32(&self->fileData[0x0C]);
			self->fileHdr.dataSize = ReadLE32(&self->fileData[0x10]);
			tempLng = ReadLE32(&self->fileData[0x14]);
			self->fileHdr.hwType = (tempLng <= 0xFF) ? (UINT8)tempLng : 0xFF;
			self->fileHdr.dataOfs = 0x18;
			break;
		}
		// swap DualOPL2 and OPL3 values
		if (self->fileHdr.hwType == 0x01)
			self->fileHdr.hwType = DROHW_OPL3;
		else if (self->fileHdr.hwType == 0x02)
			self->fileHdr.hwType = DROHW_DUALOPL2;
		self->fileHdr.format = 0x00;
		self->fileHdr.compression = 0x00;
		self->fileHdr.cmdDlyShort = 0x00;
		self->fileHdr.cmdDlyLong = 0x01;
		self->fileHdr.regCmdCnt = 0x00;
		break;
	case 2:	// version 2 (DOSBox 0.73)
		self->fileHdr.dataSize = ReadLE32(&self->fileData[0x0C]) * 2;
		self->fileHdr.lengthMS = ReadLE32(&self->fileData[0x10]);
		self->fileHdr.hwType = self->fileData[0x14];
		self->fileHdr.format = self->fileData[0x15];
		self->fileHdr.compression = self->fileData[0x16];
		self->fileHdr.cmdDlyShort = self->fileData[0x17];
		self->fileHdr.cmdDlyLong = self->fileData[0x18];
		self->fileHdr.regCmdCnt = self->fileData[0x19];
		self->fileHdr.dataOfs = 0x1A + self->fileHdr.regCmdCnt;
		
		if (self->fileHdr.regCmdCnt > 0x80)
			self->fileHdr.regCmdCnt = 0x80;	// only 0x80 values are possible
		memcpy(self->fileHdr.regCmdMap, &self->fileData[0x1A], self->fileHdr.regCmdCnt);
		
		break;
	}
	
	DROEngine_ScanInitBlock(self);
	
	self->realHwType = self->fileHdr.hwType;
	if (self->fileHdr.verMajor >= 2)
	{
		// DOSBox puts "DualOPL2" into the header of DROs that log OPL3 data ...
		// ... unless the "4op enable" register is accessed while OPL3 mode is active.
		// This bug was introduced when DRO logging was rewritten for DOSBox 0.73.
		if (self->realHwType == DROHW_DUALOPL2)
		{
			switch(self->playOpts.v2opl3Mode)
			{
			case DRO_V2OPL3_DETECT:
				// if OPL3 enable is set, it definitely is an OPL3 file
				if (self->initRegSet[0x105] && (self->initOPL3Enable & 0x01))
					self->realHwType = DROHW_OPL3;
				break;
			case DRO_V2OPL3_HEADER:
				// keep the value from the header
				break;
			case DRO_V2OPL3_ENFORCE:
				self->realHwType = DROHW_OPL3;
				break;
			}
		}
	}
	
	self->portShift = 0;
	switch(self->realHwType)
	{
	case DROHW_OPL2:	// single OPL2
		self->devTypeCnt = 1;
		self->devTypes[0] = DEVID_YM3812;	self->devPanning[0] = 0x00;
		break;
	case DROHW_DUALOPL2:	// dual OPL2
		self->devTypeCnt = 2;
		self->devTypes[0] = DEVID_YM3812;	self->devPanning[0] = 0x01;
		self->devTypes[1] = DEVID_YM3812;	self->devPanning[1] = 0x02;
		break;
	case DROHW_OPL3:	// single OPL3
	default:
		self->devTypeCnt = 1;
		self->devTypes[0] = DEVID_YMF262;	self->devPanning[0] = 0x00;
		self->portShift = 1;
		break;
	}
	self->portMask = (1 << self->portShift) - 1;
	
	self->totalTicks = self->fileHdr.lengthMS;
	DROEngine_GenerateDeviceConfig(self);
	
	return 0x00;
}

static void DROEngine_ScanInitBlock(PE_DRO* self)
{
	// Scan initialization block of the DRO in order to be able to apply special fixes.
	// Fixes like:
	//	- DualOPL2 vs. OPL3 detection (because DOSBox sets hwType to DualOPL2 more often that it should)
	//	- [DRO v1] detect size of initialization block (for filtering out unescaped writes to register 01/04)
	UINT32 filePos;
	UINT8 curCmd;
	UINT8 selPort;
	UINT16 curReg;
	UINT16 lastReg;
	
	memset(self->initRegSet, 0x00, sizeof(self->initRegSet));
	self->initOPL3Enable = 0x00;
	
	filePos = self->fileHdr.dataOfs;
	if (self->fileHdr.verMajor < 2)
	{
		selPort = 0;
		lastReg = 0x000;
		// The file begins with a register dump with increasing register numbers.
		while(filePos < DataLoader_GetSize(self->dLoad))
		{
			curCmd = self->fileData[filePos];
			if (curCmd == 0x02 || curCmd == 0x03)
			{
				// make an exception for the chip select commands
				selPort = curCmd & 0x01;
				filePos ++;
				continue;
			}
			
			curReg = (selPort << 8) | (curCmd << 0);
			if (curReg < lastReg)
				break;
			
			self->initRegSet[curReg] = true;
			if (curReg == 0x105)
				self->initOPL3Enable = self->fileData[filePos + 0x01];
			lastReg = curReg;
			filePos += 0x02;
		}
		while(filePos < DataLoader_GetSize(self->dLoad))
		{
			curCmd = self->fileData[filePos];
			
			if (curCmd == 0x00 || curCmd == 0x01)
				break;	// delay command - stop scanning
			if (curCmd == 0x02 || curCmd == 0x03)
			{
				selPort = curCmd & 0x01;
				filePos ++;
				continue;
			}
			if (curCmd == 0x04)
			{
				if (self->fileData[filePos + 0x01] < 0x08)
					break;	// properly escaped command - stop scanning
			}
			curReg = (selPort << 8) | (curCmd << 0);
			self->initRegSet[curReg] = true;
			if (curReg == 0x105)
				self->initOPL3Enable = self->fileData[filePos + 0x01];
			filePos += 0x02;
		}
	}
	else //if (self->fileHdr.verMajor == 2)
	{
		lastReg = 0x000;
		// The file begins with a register dump with increasing register numbers.
		while(filePos < DataLoader_GetSize(self->dLoad))
		{
			curCmd = self->fileData[filePos];
			if (curCmd == self->fileHdr.cmdDlyShort || curCmd == self->fileHdr.cmdDlyLong)
				break;
			
			if ((curCmd & 0x7F) >= self->fileHdr.regCmdCnt)
				break;
			curReg = ((curCmd & 0x80) << 1) | (self->fileHdr.regCmdMap[curCmd & 0x7F] << 0);
			self->initRegSet[curReg] = true;
			if (curReg == 0x105)
				self->initOPL3Enable = self->fileData[filePos + 0x01];
			filePos += 0x02;
		}
	}
	self->initBlkEndOfs = filePos;
	
	return;
}

UINT8 DROEngine_UnloadFile(PE_DRO* self)
{
	if (self->playState & PLAYSTATE_PLAY)
		return 0xFF;
	
	self->playState = 0x00;
	self->dLoad = NULL;
	self->fileData = NULL;
	self->fileHdr.verMajor = 0xFF;
	self->fileHdr.dataOfs = 0x00;
	self->devTypeCnt = 0;
	PE_ARRAY_FREE(self->devCfgs);
	PE_ARRAY_FREE(self->devices);
	PE_ARRAY_FREE(self->devNames);
	
	return 0x00;
}

const DRO_HEADER* DROEngine_GetFileHeader(const PE_DRO* self)
{
	return &self->fileHdr;
}

const char* const* DROEngine_GetTags(PE_DRO* self)
{
	static const char* const tagList[] = { NULL };
	return tagList;
}

UINT8 DROEngine_GetSongInfo(PE_DRO* self, PLR_SONG_INFO* songInf)
{
	if (self->dLoad == NULL)
		return 0xFF;
	
	songInf->format = FCC_DRO;
	songInf->fileVerMaj = self->fileHdr.verMajor;
	songInf->fileVerMin = self->fileHdr.verMinor;
	songInf->tickRateMul = 1;
	songInf->tickRateDiv = self->tickFreq;
	songInf->songLen = selfcall.GetTotalTicks(&self->pe);
	songInf->loopTick = (UINT32)-1;
	songInf->volGain = 0x10000;
	songInf->deviceCnt = (UINT32)self->devTypeCnt;
	
	return 0x00;
}

UINT8 DROEngine_GetSongDeviceInfo(const PE_DRO* self, size_t* retDevInfCount, PLR_DEV_INFO** retDevInfData)
{
	if (self->dLoad == NULL)
		return 0xFF;
	
	size_t curDev;
	
	*retDevInfCount = self->devTypeCnt;
	*retDevInfData = (PLR_DEV_INFO*)calloc(*retDevInfCount, sizeof(PLR_DEV_INFO));
	for (curDev = 0; curDev < self->devTypeCnt; curDev ++)
	{
		const DEV_GEN_CFG* devCfg = &self->devCfgs.data[curDev];
		PLR_DEV_INFO* devInf = &(*retDevInfData)[curDev];
		memset(devInf, 0x00, sizeof(PLR_DEV_INFO));
		
		devInf->id = (UINT32)curDev;
		devInf->type = self->devTypes[curDev];
		devInf->instance = (UINT8)curDev;
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
			devInf->volume = 0x100;
			devInf->smplRate = 0;
		}
	}
	if (self->devices.size > 0)
		return 0x01;	// returned "live" data
	else
		return 0x00;	// returned data based on file header
}

static size_t DROEngine_DeviceID2OptionID(const PE_DRO* self, UINT32 id)
{
	DEV_ID type;
	UINT8 instance;
	
	if (id & 0x80000000)
	{
		type = (id >> 0) & 0xFF;
		instance = (id >> 16) & 0xFF;
	}
	else if (id < self->devTypeCnt)
	{
		type = self->devTypes[id];
		instance = (UINT8)id;
	}
	else
	{
		return (size_t)-1;
	}
	
	if (type == DEVID_YM3812)
	{
		if (instance < 2)
			return 0 + instance;
	}
	else if (type == DEVID_YMF262)
	{
		if (instance < 1)
			return 2 + instance;
	}
	return (size_t)-1;
}

static void DROEngine_RefreshMuting(PE_DRO* self, DRO_CHIPDEV* chipDev, const PLR_MUTE_OPTS* muteOpts)
{
	DEV_INFO* devInf = &chipDev->base.defInf;
	if (devInf->dataPtr != NULL && devInf->devDef->SetMuteMask != NULL)
		devInf->devDef->SetMuteMask(devInf->dataPtr, muteOpts->chnMute[0]);
	
	return;
}

// TODO: This is unused?
static void DROEngine_RefreshPanning(PE_DRO* self, DRO_CHIPDEV* chipDev, const PLR_PAN_OPTS* panOpts)
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

UINT8 DROEngine_SetDeviceOptions(PE_DRO* self, UINT32 id, const PLR_DEV_OPTS* devOpts)
{
	size_t optID = DROEngine_DeviceID2OptionID(self, id);
	if (optID == (size_t)-1)
		return 0x80;	// bad device ID
	
	self->devOpts[optID] = *devOpts;
	// no immediate changes necessary for OPL2/OPL3
	
	size_t devID = self->optDevMap[optID];
	if (devID < self->devices.size)
		DROEngine_RefreshMuting(self, &self->devices.data[devID], &self->devOpts[optID].muteOpts);
	return 0x00;
}

UINT8 DROEngine_GetDeviceOptions(const PE_DRO* self, UINT32 id, PLR_DEV_OPTS* devOpts)
{
	size_t optID = DROEngine_DeviceID2OptionID(self, id);
	if (optID == (size_t)-1)
		return 0x80;	// bad device ID
	
	*devOpts = self->devOpts[optID];
	return 0x00;
}

UINT8 DROEngine_SetDeviceMuting(PE_DRO* self, UINT32 id, const PLR_MUTE_OPTS* muteOpts)
{
	size_t optID = DROEngine_DeviceID2OptionID(self, id);
	if (optID == (size_t)-1)
		return 0x80;	// bad device ID
	
	self->devOpts[optID].muteOpts = *muteOpts;
	
	size_t devID = self->optDevMap[optID];
	if (devID < self->devices.size)
		DROEngine_RefreshMuting(self, &self->devices.data[devID], &self->devOpts[optID].muteOpts);
	return 0x00;
}

UINT8 DROEngine_GetDeviceMuting(const PE_DRO* self, UINT32 id, PLR_MUTE_OPTS* muteOpts)
{
	size_t optID = DROEngine_DeviceID2OptionID(self, id);
	if (optID == (size_t)-1)
		return 0x80;	// bad device ID
	
	*muteOpts = self->devOpts[optID].muteOpts;
	return 0x00;
}

UINT8 DROEngine_SetPlayerOptions(PE_DRO* self, const DRO_PLAY_OPTIONS* playOpts)
{
	self->playOpts = *playOpts;
	DROEngine_RefreshTSRates(self);
	return 0x00;
}

UINT8 DROEngine_GetPlayerOptions(const PE_DRO* self, DRO_PLAY_OPTIONS* playOpts)
{
	*playOpts = self->playOpts;
	return 0x00;
}

UINT8 DROEngine_SetSampleRate(PE_DRO* self, UINT32 sampleRate)
{
	if (self->playState & PLAYSTATE_PLAY)
		return 0x01;	// can't set during playback
	
	self->pe.outSmplRate = sampleRate;
	return 0x00;
}

double DROEngine_GetPlaybackSpeed(const PE_DRO* self)
{
	return self->playOpts.genOpts.pbSpeed / (double)0x10000;
}

UINT8 DROEngine_SetPlaybackSpeed(PE_DRO* self, double speed)
{
	self->playOpts.genOpts.pbSpeed = (UINT32)(0x10000 * speed);
	DROEngine_RefreshTSRates(self);
	return 0x00;
}


static void DROEngine_RefreshTSRates(PE_DRO* self)
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

UINT32 DROEngine_Tick2Sample(const PE_DRO* self, UINT32 ticks)
{
	if (ticks == (UINT32)-1)
		return -1;
	return (UINT32)(ticks * self->tsMult / self->tsDiv);
}

UINT32 DROEngine_Sample2Tick(const PE_DRO* self, UINT32 samples)
{
	if (samples == (UINT32)-1)
		return -1;
	return (UINT32)(samples * self->tsDiv / self->tsMult);
}

double DROEngine_Tick2Second(const PE_DRO* self, UINT32 ticks)
{
	if (ticks == (UINT32)-1)
		return -1.0;
	return (INT64)(ticks * self->ttMult) / (double)(INT64)self->tsDiv;
}

UINT8 DROEngine_GetState(const PE_DRO* self)
{
	return self->playState;
}

UINT32 DROEngine_GetCurPos(const PE_DRO* self, UINT8 unit)
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

UINT32 DROEngine_GetCurLoop(const PE_DRO* self)
{
	return 0;
}

UINT32 DROEngine_GetTotalTicks(const PE_DRO* self)
{
	return self->totalTicks;
}

UINT32 DROEngine_GetLoopTicks(const PE_DRO* self)
{
	return 0;
}

static void DROEngine_PlayerLogCB(void* userParam, void* source, UINT8 level, const char* message)
{
	PE_DRO* player = (PE_DRO*)source;
	if (player->pe.logCbFunc == NULL)
		return;
	player->pe.logCbFunc(player->pe.logCbParam, &player->pe, level, PLRLOGSRC_PLR, NULL, message);
	return;
}

static void DROEngine_SndEmuLogCB(void* userParam, void* source, UINT8 level, const char* message)
{
	DEVLOG_CB_DATA* cbData = (DEVLOG_CB_DATA*)userParam;
	PE_DRO* player = cbData->player;
	if (player->pe.logCbFunc == NULL)
		return;
	if ((player->playState & PLAYSTATE_SEEK) && level > PLRLOG_ERROR)
		return;	// prevent message spam while seeking
	player->pe.logCbFunc(player->pe.logCbParam, &player->pe, level, PLRLOGSRC_EMU,
		player->devNames.data[cbData->chipDevID], message);
	return;
}


static void DROEngine_GenerateDeviceConfig(PE_DRO* self)
{
	size_t curDev;
	const char* chipName;
	size_t dnBufSize;
	char* dnBufPtr;
	
	PE_ARRAY_FREE(self->devCfgs);
	PE_ARRAY_MALLOC(self->devCfgs, DEV_GEN_CFG, self->devTypeCnt);
	PE_ARRAY_FREE(self->devNames);
	PE_ARRAY_MALLOC(self->devNames, const char*, self->devTypeCnt);
	dnBufSize = sizeof(self->devNameBuffer);
	dnBufPtr = self->devNameBuffer;

	for (curDev = 0; curDev < self->devCfgs.size; curDev ++)
	{
		DEV_GEN_CFG* devCfg = &self->devCfgs.data[curDev];
		memset(devCfg, 0x00, sizeof(DEV_GEN_CFG));
		
		devCfg->clock = 3579545;
		if (self->devTypes[curDev] == DEVID_YMF262)
			devCfg->clock *= 4;	// OPL3 uses a 14 MHz clock
		devCfg->flags = 0x00;
		
		chipName = (self->devTypes[curDev] == DEVID_YMF262) ? "OPL3" : "OPL2";
		if (self->devCfgs.size <= 1)
		{
			self->devNames.data[curDev] = chipName;
		}
		else
		{
			int bytesWrt = snprintf(dnBufPtr, dnBufSize, "%s #%u", chipName, 1 + (unsigned int)curDev);
			self->devNames.data[curDev] = dnBufPtr;
			if (bytesWrt < 0)
			{
				if (dnBufSize > 0)
				{
					bytesWrt = (int)dnBufSize - 1;
					dnBufPtr[bytesWrt] = '\0';
					self->devNames.data[curDev] = dnBufPtr;
					dnBufPtr += bytesWrt;	// point to null-terminator
					dnBufSize -= bytesWrt;
				}
				else
				{
					self->devNames.data[curDev] = dnBufPtr;	// point to last byte of the buffer (null-terminator)
				}
			}
			else
			{
				if (bytesWrt > dnBufSize)
					bytesWrt = (int)dnBufSize;
				bytesWrt += 1;	// include null terminator
				self->devNames.data[curDev] = dnBufPtr;
				dnBufPtr += bytesWrt;
				dnBufSize -= bytesWrt;
			}
		}
	}
	
	return;
}

UINT8 DROEngine_Start(PE_DRO* self)
{
	size_t curDev;
	UINT8 retVal;
	
	for (curDev = 0; curDev < 3; curDev ++)
		self->optDevMap[curDev] = (size_t)-1;
	
	PE_ARRAY_FREE(self->devices);
	PE_ARRAY_MALLOC(self->devices, DRO_CHIPDEV, self->devTypeCnt);
	for (curDev = 0; curDev < self->devTypeCnt; curDev ++)
	{
		DRO_CHIPDEV* cDev = &self->devices.data[curDev];
		PLR_DEV_OPTS* devOpts;
		DEV_GEN_CFG* devCfg = &self->devCfgs.data[curDev];
		VGM_BASEDEV* clDev;
		
		cDev->base.defInf.dataPtr = NULL;
		cDev->base.linkDev = NULL;
		cDev->optID = DROEngine_DeviceID2OptionID(self, (UINT32)curDev);
		
		devOpts = (cDev->optID != (size_t)-1) ? &self->devOpts[cDev->optID] : NULL;
		devCfg->emuCore = (devOpts != NULL) ? devOpts->emuCore[0] : 0x00;
		devCfg->srMode = (devOpts != NULL) ? devOpts->srMode : DEVRI_SRMODE_NATIVE;
		if (devOpts != NULL && devOpts->smplRate)
			devCfg->smplRate = devOpts->smplRate;
		else
			devCfg->smplRate = self->pe.outSmplRate;
		
		retVal = SndEmu_Start2(self->devTypes[curDev], devCfg, &cDev->base.defInf, self->pe.userDevList, self->pe.devStartOpts);
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
			cDev->base.defInf.devDef->SetLogCB(cDev->base.defInf.dataPtr, DROEngine_SndEmuLogCB, &cDev->logCbData);
		SetupLinkedDevices(&cDev->base, NULL, NULL);
		
		if (devOpts != NULL)
		{
			if (cDev->base.defInf.devDef->SetOptionBits != NULL)
				cDev->base.defInf.devDef->SetOptionBits(cDev->base.defInf.dataPtr, devOpts->coreOpts);
			
			self->optDevMap[cDev->optID] = curDev;
		}
		
		for (clDev = &cDev->base; clDev != NULL; clDev = clDev->linkDev)
		{
			UINT8 resmplMode = (devOpts != NULL) ? devOpts->resmplMode : RSMODE_LINEAR;
			
			if (devOpts != NULL && clDev->defInf.devDef->SetMuteMask != NULL)
				clDev->defInf.devDef->SetMuteMask(clDev->defInf.dataPtr, devOpts->muteOpts.chnMute[0]);
			
			Resmpl_SetVals(&clDev->resmpl, resmplMode, 0x100, self->pe.outSmplRate);
			// do DualOPL2 hard panning by muting either the left or right speaker
			if (self->devPanning[curDev] & 0x02)
				clDev->resmpl.volumeL = 0x00;
			if (self->devPanning[curDev] & 0x01)
				clDev->resmpl.volumeR = 0x00;
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

UINT8 DROEngine_Stop(PE_DRO* self)
{
	size_t curDev;
	
	self->playState &= ~PLAYSTATE_PLAY;
	
	for (curDev = 0; curDev < self->devices.size; curDev ++)
	{
		DRO_CHIPDEV* cDev = &self->devices.data[curDev];
		FreeDeviceTree(&cDev->base, 0);
	}
	PE_ARRAY_FREE(self->devices);
	if (self->pe.eventCbFunc != NULL)
		self->pe.eventCbFunc(&self->pe, self->pe.eventCbParam, PLREVT_STOP, NULL);
	
	return 0x00;
}

UINT8 DROEngine_Reset(PE_DRO* self)
{
	size_t curDev;
	UINT8 curReg;
	UINT8 curPort;
	UINT8 devport;
	
	self->filePos = self->fileHdr.dataOfs;
	self->fileTick = 0;
	self->playTick = 0;
	self->playSmpl = 0;
	self->playState &= ~PLAYSTATE_END;
	self->psTrigger = 0x00;
	self->selPort = 0;
	
	DROEngine_RefreshTSRates(self);
	
	for (curDev = 0; curDev < self->devices.size; curDev ++)
	{
		DRO_CHIPDEV* cDev = &self->devices.data[curDev];
		VGM_BASEDEV* clDev;
		if (cDev->base.defInf.dataPtr == NULL)
			continue;
		
		cDev->base.defInf.devDef->Reset(cDev->base.defInf.dataPtr);
		for (clDev = &cDev->base; clDev != NULL; clDev = clDev->linkDev)
		{
			// TODO: Resmpl_Reset(&clDev->resmpl);
		}
	}
	
	for (curDev = 0; curDev < self->devices.size; curDev ++)
	{
		if (self->devices.data[curDev].base.defInf.dataPtr == NULL)
			continue;
		
		if (self->devTypes[curDev] == DEVID_YMF262)
			DROEngine_WriteReg(self, (UINT8)(curDev << self->portShift) | 1, 0x05, 0x01);	// temporary OPL3 enable for proper register reset
		
		for (curPort = 0; curPort <= self->portMask; curPort ++)
		{
			devport = (UINT8)(curDev << self->portShift) | curPort;
			for (curReg = 0xFF; curReg >= 0x20; curReg --)
			{
				// [optimization] only send registers that are NOT part of the initialization block
				if (! self->initRegSet[(curPort << 8) | curReg])
					DROEngine_WriteReg(self, devport, curReg, 0x00);
			}
		}
		devport = (UINT8)(curDev << self->portShift);
		DROEngine_WriteReg(self, devport | 0, 0x08, 0x00);
		DROEngine_WriteReg(self, devport | 0, 0x01, 0x00);
		
		if (self->devTypes[curDev] == DEVID_YMF262)
		{
			// send OPL3 mode from DRO file now (DOSBox dumps the registers in the wrong order)
			DROEngine_WriteReg(self, devport | 1, 0x05, self->initOPL3Enable);
			DROEngine_WriteReg(self, devport | 1, 0x04, 0x00);	// disable 4op mode
		}
	}
	
	return 0x00;
}

UINT8 DROEngine_Seek(PE_DRO* self, UINT8 unit, UINT32 pos)
{
	switch(unit)
	{
	case PLAYPOS_FILEOFS:
		self->playState |= PLAYSTATE_SEEK;
		if (pos < self->filePos)
			selfcall.Reset(&self->pe);
		return DROEngine_SeekToFilePos(self, pos);
	case PLAYPOS_SAMPLE:
		pos = selfcall.Sample2Tick(&self->pe, pos);
		// fall through
	case PLAYPOS_TICK:
		self->playState |= PLAYSTATE_SEEK;
		if (pos < self->playTick)
			selfcall.Reset(&self->pe);
		return DROEngine_SeekToTick(self, pos);
	case PLAYPOS_COMMAND:
	default:
		return 0xFF;
	}
}

static UINT8 DROEngine_SeekToTick(PE_DRO* self, UINT32 tick)
{
	self->playState |= PLAYSTATE_SEEK;
	if (tick > self->playTick)
		DROEngine_ParseFile(self, tick - self->playTick);
	self->playSmpl = selfcall.Tick2Sample(&self->pe, self->playTick);
	self->playState &= ~PLAYSTATE_SEEK;
	return 0x00;
}

static UINT8 DROEngine_SeekToFilePos(PE_DRO* self, UINT32 pos)
{
	self->playState |= PLAYSTATE_SEEK;
	if (self->fileHdr.verMajor < 2)
	{
		while(self->filePos <= pos && ! (self->playState & PLAYSTATE_END))
			DROEngine_DoCommand_v1(self);
	}
	else
	{
		while(self->filePos <= pos && ! (self->playState & PLAYSTATE_END))
			DROEngine_DoCommand_v2(self);
	}
	self->playTick = self->fileTick;
	self->playSmpl = selfcall.Tick2Sample(&self->pe, self->playTick);
	self->playState &= ~PLAYSTATE_SEEK;
	
	return 0x00;
}

UINT32 DROEngine_Render(PE_DRO* self, UINT32 smplCnt, WAVE_32BS* data)
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
		smplFileTick = DROEngine_Sample2Tick(self, self->playSmpl);
		DROEngine_ParseFile(self, smplFileTick - self->playTick);
		
		// render as many samples at once as possible (for better performance)
		maxSmpl = DROEngine_Tick2Sample(self, self->fileTick);
		smplStep = maxSmpl - self->playSmpl;
		if (smplStep < 1)
			smplStep = 1;	// must render at least 1 sample in order to advance
		if ((UINT32)smplStep > smplCnt - curSmpl)
			smplStep = smplCnt - curSmpl;
		
		for (curDev = 0; curDev < self->devices.size; curDev ++)
		{
			DRO_CHIPDEV* cDev = &self->devices.data[curDev];
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

static void DROEngine_ParseFile(PE_DRO* self, UINT32 ticks)
{
	self->playTick += ticks;
	if (self->playState & PLAYSTATE_END)
		return;
	
	if (self->fileHdr.verMajor < 2)
	{
		while(self->fileTick <= self->playTick && ! (self->playState & PLAYSTATE_END))
			DROEngine_DoCommand_v1(self);
	}
	else
	{
		while(self->fileTick <= self->playTick && ! (self->playState & PLAYSTATE_END))
			DROEngine_DoCommand_v2(self);
	}
	
	return;
}

static void DROEngine_DoCommand_v1(PE_DRO* self)
{
	if (self->filePos >= DataLoader_GetSize(self->dLoad))
	{
		DROEngine_DoFileEnd(self);
		return;
	}
	
	UINT8 curCmd;
	
	//emu_logf(&_logger, PLRLOG_TRACE, "[DRO v1] Ofs %04X, Command %02X data %02X\n", self->filePos, self->fileData[_filePos], self->fileData[_filePos+1]);
	curCmd = self->fileData[self->filePos];
	self->filePos ++;
	switch(curCmd)
	{
	case 0x00:	// 1-byte delay
		self->fileTick += 1 + self->fileData[self->filePos];
		self->filePos ++;
		return;
	case 0x01:	// 2-byte delay
		// Note: With DRO v1, the DOSBox developers wanted to change this command from 0x01 to 0x10.
		//       Too bad that they updated the documentation, but not the actual code.
		if (self->filePos < self->initBlkEndOfs)
			break;	// assume missing escape command during initialization block
		if (! (self->fileData[self->filePos + 0x00] & ~0x20) &&
			(self->fileData[self->filePos + 0x01] == 0x08 || self->fileData[self->filePos + 0x01] >= 0x20))
			break;	// This is an unescaped register write. (e.g. 01 20 08 xx or 01 00 BD C0)
		
		self->fileTick += 1 + ReadLE16(&self->fileData[self->filePos]);
		self->filePos += 0x02;
		return;
	case 0x02:	// use 1st OPL2 chip / 1st OPL3 port
	case 0x03:	// use 2nd OPL2 chip / 2nd OPL3 port
		self->selPort = curCmd & 0x01;
		if (self->selPort >= (self->devTypeCnt << self->portShift))
		{
			//emu_logf(&_logger, PLRLOG_WARN, "More chips used than defined in header!\n");
			//_shownMsgs[2] = true;
		}
		return;
	case 0x04:	// escape command
		// Note: This command is used by various tools that edit DRO files, but DOSBox itself doesn't write it.
		if (self->fileData[self->filePos] >= 0x08)
			break;	// It only makes sense to escape register 00..04, so should be a direct write to register 04.
		if (self->filePos < self->initBlkEndOfs)
			break;	// assume missing escape command during initialization block
		
		// read the next value and treat it as register value
		curCmd = self->fileData[self->filePos];
		self->filePos ++;
		break;
	}
	
	DROEngine_WriteReg(self, self->selPort, curCmd, self->fileData[self->filePos]);
	self->filePos ++;
	
	return;
}

static void DROEngine_DoCommand_v2(PE_DRO* self)
{
	if (self->filePos >= DataLoader_GetSize(self->dLoad))
	{
		DROEngine_DoFileEnd(self);
		return;
	}
	
	UINT8 port;
	UINT8 reg;
	UINT8 data;
	
	//emu_logf(&_logger, PLRLOG_TRACE, "[DRO v2] Ofs %04X, Command %02X data %02X\n", self->filePos, self->fileData[self->filePos], self->fileData[self->filePos+1]);
	reg = self->fileData[self->filePos + 0x00];
	data = self->fileData[self->filePos + 0x01];
	self->filePos += 0x02;
	if (reg == self->fileHdr.cmdDlyShort)
	{
		self->fileTick += (1 + data);
		return;
	}
	else if (reg == self->fileHdr.cmdDlyLong)
	{
		self->fileTick += (1 + data) << 8;
		return;
	}
	port = (reg & 0x80) >> 7;
	reg &= 0x7F;
	if (reg >= self->fileHdr.regCmdCnt)
		return;	// invalid register
	
	reg = self->fileHdr.regCmdMap[reg];
	DROEngine_WriteReg(self, port, reg, data);
	
	return;
}

static void DROEngine_DoFileEnd(PE_DRO* self)
{
	if (self->playState & PLAYSTATE_SEEK)	// recalculate playSmpl to fix state when triggering callbacks
		self->playSmpl = selfcall.Tick2Sample(&self->pe, self->fileTick);	// Note: fileTick results in more accurate position
	self->playState |= PLAYSTATE_END;
	self->psTrigger |= PLAYSTATE_END;
	if (self->pe.eventCbFunc != NULL)
		self->pe.eventCbFunc(&self->pe, self->pe.eventCbParam, PLREVT_END, NULL);
	
	return;
}

static void DROEngine_WriteReg(PE_DRO* self, UINT8 port, UINT8 reg, UINT8 data)
{
	UINT8 devID = port >> self->portShift;
	
	if (devID >= self->devices.size)
		return;
	DRO_CHIPDEV* cDev = &self->devices.data[devID];
	DEV_DATA* dataPtr = cDev->base.defInf.dataPtr;
	if (dataPtr == NULL || cDev->write == NULL)
		return;
	
	port &= self->portMask;
	cDev->write(dataPtr, (port << 1) | 0, reg);
	cDev->write(dataPtr, (port << 1) | 1, data);
	
	return;
}


static struct player_engine_vtable DROEngine_vtbl =
{
	FCC_DRO,	// playerType
	"DRO",	// playerName
	
	(void*)DROEngine_Init,
	(void*)DROEngine_Deinit,
	
	(void*)DROEngine_CanLoadFile,
	(void*)DROEngine_LoadFile,
	(void*)DROEngine_UnloadFile,
	
	(void*)DROEngine_GetTags,
	(void*)DROEngine_GetSongInfo,
	(void*)DROEngine_GetSongDeviceInfo,

	(void*)DROEngine_SetDeviceOptions,
	(void*)DROEngine_GetDeviceOptions,
	(void*)DROEngine_SetDeviceMuting,
	(void*)DROEngine_GetDeviceMuting,

	(void*)DROEngine_SetPlayerOptions,
	(void*)DROEngine_GetPlayerOptions,
	
	PBaseEngine_GetSampleRate,
	(void*)DROEngine_SetSampleRate,
	(void*)DROEngine_GetPlaybackSpeed,
	(void*)DROEngine_SetPlaybackSpeed,
	PBaseEngine_SetUserDevices,
	PBaseEngine_SetEventCallback,
	PBaseEngine_SetFileReqCallback,
	PBaseEngine_SetLogCallback,
	(void*)DROEngine_Tick2Sample,
	(void*)DROEngine_Sample2Tick,
	(void*)DROEngine_Tick2Second,
	PBaseEngine_Sample2Second,
	
	(void*)DROEngine_GetState,
	(void*)DROEngine_GetCurPos,
	(void*)DROEngine_GetCurLoop,
	(void*)DROEngine_GetTotalTicks,
	(void*)DROEngine_GetLoopTicks,
	PBaseEngine_GetTotalPlayTicks,
	
	(void*)DROEngine_Start,
	(void*)DROEngine_Stop,
	(void*)DROEngine_Reset,
	(void*)DROEngine_Seek,
	(void*)DROEngine_Render,
};
