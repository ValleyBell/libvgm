#include <stdlib.h>
#include <string.h>
#include <stdio.h>	// for snprintf()
#include <math.h>	// for pow()

#include "../common_def.h"
#include "engine_vgm.h"
#include "../emu/EmuStructs.h"
#include "../emu/SoundEmu.h"
#include "../emu/Resampler.h"
#include "../emu/SoundDevs.h"
#include "../emu/EmuCores.h"
#include "../emu/cores/sn764intf.h"	// for SN76496_CFG
#include "../emu/cores/2612intf.h"
#include "../emu/cores/segapcm.h"		// for SEGAPCM_CFG
#include "../emu/cores/ayintf.h"		// for AY8910_CFG
#include "../emu/cores/gb.h"
#include "../emu/cores/okim6258.h"		// for MSM6258_CFG
#include "../emu/cores/k054539.h"
#include "../emu/cores/c140.h"
#include "../emu/cores/qsoundintf.h"
#include "../emu/cores/scsp.h"
#include "../emu/cores/msm5205.h"		// for MSM5205_CFG
#include "../emu/cores/msm5232.h"		// for MSM5232_CFG

#include "../player/dblk_compr.h"
#include "../utils/StrUtils.h"
#include "../player/helper.h"
#include "../emu/logging.h"

#ifdef _MSC_VER
#define snprintf	_snprintf
#endif


#include "engine_vgm-internal.h"
typedef struct devlink_cb_data
{
	PE_VGM* player;
	VGM_DEVCFG* sdCfg;
	VGM_CHIPDEV* chipDev;
} DEVLINK_CB_DATA;

static const DEV_ID OPT_DEV_LIST[OPT_DEV_COUNT] =
{
	DEVID_SN76496, DEVID_YM2413, DEVID_YM2612, DEVID_YM2151, DEVID_SEGAPCM, DEVID_RF5C68, DEVID_YM2203, DEVID_YM2608,
	DEVID_YM2610, DEVID_YM3812, DEVID_YM3526, DEVID_Y8950, DEVID_YMF262, DEVID_YMF278B, DEVID_YMF271, DEVID_YMZ280B,
	DEVID_32X_PWM, DEVID_AY8910, DEVID_GB_DMG, DEVID_NES_APU, DEVID_YMW258, DEVID_uPD7759, DEVID_MSM6258, DEVID_MSM6295,
	DEVID_K051649, DEVID_K054539, DEVID_C6280, DEVID_C140, DEVID_C219, DEVID_K053260, DEVID_POKEY, DEVID_QSOUND,
	DEVID_SCSP, DEVID_WSWAN, DEVID_VBOY_VSU, DEVID_SAA1099, DEVID_ES5503, DEVID_ES5506, DEVID_X1_010, DEVID_C352,
	DEVID_GA20, DEVID_MIKEY, DEVID_K007232, DEVID_K005289, DEVID_MSM5205, DEVID_MSM5232, DEVID_BSMT2000, DEVID_ICS2115,
};

static const DEV_ID DEV_LIST[CHIP_COUNT] =
{
	DEVID_SN76496, DEVID_YM2413, DEVID_YM2612, DEVID_YM2151, DEVID_SEGAPCM, DEVID_RF5C68, DEVID_YM2203, DEVID_YM2608,
	DEVID_YM2610, DEVID_YM3812, DEVID_YM3526, DEVID_Y8950, DEVID_YMF262, DEVID_YMF278B, DEVID_YMF271, DEVID_YMZ280B,
	DEVID_RF5C68, DEVID_32X_PWM, DEVID_AY8910, DEVID_GB_DMG, DEVID_NES_APU, DEVID_YMW258, DEVID_uPD7759, DEVID_MSM6258,
	DEVID_MSM6295, DEVID_K051649, DEVID_K054539, DEVID_C6280, DEVID_C140, DEVID_K053260, DEVID_POKEY, DEVID_QSOUND,
	DEVID_SCSP, DEVID_WSWAN, DEVID_VBOY_VSU, DEVID_SAA1099, DEVID_ES5503, DEVID_ES5506, DEVID_X1_010, DEVID_C352,
	DEVID_GA20, DEVID_MIKEY, DEVID_K007232, DEVID_K005289, DEVID_MSM5205, DEVID_MSM5232, DEVID_BSMT2000, DEVID_ICS2115, 
};

static const UINT32 CHIPCLK_OFS[CHIP_COUNT] =
{
	0x0C, 0x10, 0x2C, 0x30, 0x38, 0x40, 0x44, 0x48,
	0x4C, 0x50, 0x54, 0x58, 0x5C, 0x60, 0x64, 0x68,
	0x6C, 0x70, 0x74, 0x80, 0x84, 0x88, 0x8C, 0x90,
	0x98, 0x9C, 0xA0, 0xA4, 0xA8, 0xAC, 0xB0, 0xB4,
	0xB8, 0xC0, 0xC4, 0xC8, 0xCC, 0xD0, 0xD8, 0xDC,
	0xE0, 0xE4, 0xE8, 0xEC, 0xF0, 0xF4, 0xF8, 0xFC,
};
static const UINT16 CHIP_VOLUME[CHIP_COUNT] =
{
	0x80, 0x200, 0x100, 0x100, 0x180, 0xB0, 0x100, 0x80,
	0x80, 0x100, 0x100, 0x100, 0x100, 0x100, 0x100, 0x98,
	0x80, 0xE0, 0x100, 0xC0, 0x100, 0x40, 0x11E, 0x1C0,
	0x100, 0xA0, 0x100, 0x100, 0x100, 0x100, 0x100, 0x100,
	0x20, 0x100, 0x100, 0x100, 0x40, 0x20, 0x100, 0x40,
	0x280, 0x100, 0x100, 0x100, 0x100, 0x100, 0x200, 0x800,
};
static const UINT16 PB_VOL_AMNT[CHIP_COUNT] =
{
	0x100, 0x80, 0x100, 0x100, 0x100, 0x100, 0x100, 0x100,
	0x100, 0x200, 0x200, 0x200, 0x200, 0x100, 0x100, 0x1AF,
	0x200, 0x100, 0x200, 0x400, 0x200, 0x400, 0x100, 0x200,
	0x200, 0x100, 0x100, 0x100, 0x180, 0x100, 0x100, 0x100,
	0x800, 0x100, 0x100, 0x100, 0x800, 0x1000, 0x100, 0x800,
	0x100, 0x200, 0x100, 0x100, 0x200, 0x100, 0x100, 0x40,
};

enum
{
	// TODO: rename to TAGID_*
	TAG_TRACK_NAME_EN, TAG_TRACK_NAME_JP,
	TAG_GAME_NAME_EN, TAG_GAME_NAME_JP,
	TAG_SYSTEM_NAME_EN, TAG_SYSTEM_NAME_JP,
	TAG_ARTIST_EN, TAG_ARTIST_JP,
	TAG_GAME_RELEASE_DATE,
	TAG_VGM_CREATOR,
	TAG_NOTES,
	TAG_COUNT
};
static const char* const TAG_TYPE_LIST[TAG_COUNT] =
{
	"TITLE", "TITLE-JPN",
	"GAME", "GAME-JPN",
	"SYSTEM", "SYSTEM-JPN",
	"ARTIST", "ARTIST-JPN",
	"DATE",
	"ENCODED_BY",
	"COMMENT",
};

#define P2612FIX_ACTIVE	0x01	// set when YM2612 "legacy mode" is active (should be only at sample 0)
#define P2612FIX_ENABLE	0x80	// the VGM needs a special workaround due to VGMTool2 YM2612 trimming

#define selfcall	self->pe.vtbl



static struct player_engine_vtable VGMEngine_vtbl;

INLINE UINT32 ReadLE32(const UINT8* data);
INLINE void SaveDeviceConfig(ARR_UINT8* dst, const void* srcData, size_t srcLen);
static void FreeStringArrayData(ARR_STR* vec);

PE_VGM* VGMEngine_Create(void);
void VGMEngine_Destroy(PE_VGM* self);
void VGMEngine_Init(PE_VGM* self);
void VGMEngine_Deinit(PE_VGM* self);

UINT8 VGMEngine_CanLoadFile(DATA_LOADER *dataLoader);
UINT8 VGMEngine_LoadFile(PE_VGM* self, DATA_LOADER *dataLoader);
static UINT8 VGMEngine_ParseHeader(PE_VGM* self);
static void VGMEngine_ParseXHdr_Data32(PE_VGM* self, UINT32 fileOfs, ARR_XHDR_D32* xData);
static void VGMEngine_ParseXHdr_Data16(PE_VGM* self, UINT32 fileOfs, ARR_XHDR_D16* xData);
static UINT8 VGMEngine_LoadTags(PE_VGM* self);
static char* VGMEngine_GetUTF8String(PE_VGM* self, const char* startPtr, const char* endPtr);
UINT8 VGMEngine_UnloadFile(PE_VGM* self);
const VGM_HEADER* VGMEngine_GetFileHeader(const PE_VGM* self);

const char* const* VGMEngine_GetTags(PE_VGM* self);
UINT8 VGMEngine_GetSongInfo(PE_VGM* self, PLR_SONG_INFO* songInf);
UINT8 VGMEngine_GetSongDeviceInfo(const PE_VGM* self, size_t* retDevInfCount, PLR_DEV_INFO** retDevInfData);
static size_t VGMEngine_DeviceID2OptionID(const PE_VGM* self, UINT32 id);
static void VGMEngine_RefreshDevOptions(PE_VGM* self, VGM_CHIPDEV* chipDev, const PLR_DEV_OPTS* devOpts);
static void VGMEngine_RefreshMuting(PE_VGM* self, VGM_CHIPDEV* chipDev, const PLR_MUTE_OPTS* muteOpts);
static void VGMEngine_RefreshPanning(PE_VGM* self, VGM_CHIPDEV* chipDev, const PLR_PAN_OPTS* panOpts);
UINT8 VGMEngine_SetDeviceOptions(PE_VGM* self, UINT32 id, const PLR_DEV_OPTS* devOpts);
UINT8 VGMEngine_GetDeviceOptions(const PE_VGM* self, UINT32 id, PLR_DEV_OPTS* devOpts);
UINT8 VGMEngine_SetDeviceMuting(PE_VGM* self, UINT32 id, const PLR_MUTE_OPTS* muteOpts);
UINT8 VGMEngine_GetDeviceMuting(const PE_VGM* self, UINT32 id, PLR_MUTE_OPTS* muteOpts);
UINT8 VGMEngine_SetPlayerOptions(PE_VGM* self, const VGM_PLAY_OPTIONS* playOpts);
UINT8 VGMEngine_GetPlayerOptions(const PE_VGM* self, VGM_PLAY_OPTIONS* playOpts);

UINT8 VGMEngine_SetSampleRate(PE_VGM* self, UINT32 sampleRate);
double VGMEngine_GetPlaybackSpeed(const PE_VGM* self);
UINT8 VGMEngine_SetPlaybackSpeed(PE_VGM* self, double speed);
static void VGMEngine_RefreshTSRates(PE_VGM* self);
UINT32 VGMEngine_Tick2Sample(const PE_VGM* self, UINT32 ticks);
UINT32 VGMEngine_Sample2Tick(const PE_VGM* self, UINT32 samples);
double VGMEngine_Tick2Second(const PE_VGM* self, UINT32 ticks);

UINT8 VGMEngine_GetState(const PE_VGM* self);
UINT32 VGMEngine_GetCurPos(const PE_VGM* self, UINT8 unit);
UINT32 VGMEngine_GetCurLoop(const PE_VGM* self);
UINT32 VGMEngine_GetTotalTicks(const PE_VGM* self);	// get time for playing once in ticks
UINT32 VGMEngine_GetLoopTicks(const PE_VGM* self);	// get time for one loop in ticks
UINT32 VGMEngine_GetModifiedLoopCount(const PE_VGM* self, UINT32 defaultLoops);
UINT8 VGMEngine_GetStreamDevInfo(const PE_VGM* self, size_t* retDevInfCount, VGM_PCMSTRM_DEV** retDevInfData);

static void VGMEngine_PlayerLogCB(void* userParam, void* source, UINT8 level, const char* message);
static void VGMEngine_SndEmuLogCB(void* userParam, void* source, UINT8 level, const char* message);

UINT8 VGMEngine_Start(PE_VGM* self);
UINT8 VGMEngine_Stop(PE_VGM* self);
UINT8 VGMEngine_Reset(PE_VGM* self);
static UINT32 VGMEngine_GetHeaderChipClock(const PE_VGM* self, UINT8 chipType);
INLINE UINT32 VGMEngine_GetChipCount(const PE_VGM* self, UINT8 chipType);
static UINT32 VGMEngine_GetChipClock(const PE_VGM* self, UINT8 chipType, UINT8 chipID);
static UINT16 VGMEngine_GetChipVolume(const PE_VGM* self, UINT8 chipType, UINT8 chipID, UINT8 isLinked);
static UINT16 VGMEngine_EstimateOverallVolume(const PE_VGM* self);
static void VGMEngine_NormalizeOverallVolume(PE_VGM* self, UINT16 overallVol);
static void VGMEngine_GenerateDeviceConfig(PE_VGM* self);
static void VGMEngine_InitDevices(PE_VGM* self);

static void VGMEngine_DeviceLinkCallback(void* userParam, VGM_BASEDEV* cDev, DEVLINK_INFO* dLink);
VGM_CHIPDEV* VGMEngine_GetDevicePtr(PE_VGM* self, UINT8 chipType, UINT8 chipID);
static void VGMEngine_LoadOPL4ROM(PE_VGM* self, VGM_CHIPDEV* chipDev);

UINT8 VGMEngine_Seek(PE_VGM* self, UINT8 unit, UINT32 pos);
static UINT8 VGMEngine_SeekToTick(PE_VGM* self, UINT32 tick);
static UINT8 VGMEngine_SeekToFilePos(PE_VGM* self, UINT32 pos);
UINT32 VGMEngine_Render(PE_VGM* self, UINT32 smplCnt, WAVE_32BS* data);
static void VGMEngine_ParseFile(PE_VGM* self, UINT32 ticks);

static void VGMEngine_ParseFileForFMClocks(PE_VGM* self);


INLINE UINT16 ReadLE16(const UINT8* data)
{
	// read 16-Bit Word (Little Endian/Intel Byte Order)
#ifdef VGM_LITTLE_ENDIAN
	return *(UINT16*)data;
#else
	return (data[0x01] << 8) | (data[0x00] << 0);
#endif
}

INLINE UINT32 ReadLE32(const UINT8* data)
{
	// read 32-Bit Word (Little Endian/Intel Byte Order)
#ifdef VGM_LITTLE_ENDIAN
	return	*(UINT32*)data;
#else
	return	(data[0x03] << 24) | (data[0x02] << 16) |
			(data[0x01] <<  8) | (data[0x00] <<  0);
#endif
}

INLINE UINT32 ReadRelOfs(const UINT8* data, UINT32 fileOfs)
{
	// read a VGM-style (relative) offset
	UINT32 ofs = ReadLE32(&data[fileOfs]);
	return ofs ? (fileOfs + ofs) : ofs;	// add base to offset, if offset != 0
}

INLINE UINT16 MulFixed8x8(UINT16 a, UINT16 b)	// 8.8 fixed point multiplication
{
	UINT32 res16;	// 16.16 fixed point result
	
	res16 = (UINT32)a * b;
	return (res16 + 0x80) >> 8;	// round to nearest neighbour + scale back to 8.8 fixed point
}

INLINE void SaveDeviceConfig(ARR_UINT8* dst, const void* srcData, size_t srcLen)
{
	PE_ARRAY_FREE(*dst)
	PE_ARRAY_MALLOC(*dst, UINT8, srcLen)
	memcpy(dst->data, srcData, dst->size);
	return;
}

static void FreeStringArrayData(ARR_STR* arr)
{
	size_t idx;

	for (idx = 0; idx < arr->size; idx ++)
	{
		free(arr->data[idx]);
		arr->data[idx] = NULL;
	}

	return;
}

PE_VGM* VGMEngine_Create(void)
{
	PE_VGM* self = malloc(sizeof(PE_VGM));
	if (self == NULL)
		return NULL;

	self->pe.vtbl = VGMEngine_vtbl;
	selfcall.Init(&self->pe);
	return self;
}

void VGMEngine_Destroy(PE_VGM* self)
{
	selfcall.Deinit(&self->pe);
	free(self);
	return;
}

void VGMEngine_Init(PE_VGM* self)
{
	UINT8 retVal;
	size_t curBank;
	UINT16 optChip;
	UINT8 chipID;
	
	PBaseEngine_Init(&self->pe);
	
	self->yrwRom.data = NULL;	self->yrwRom.size = 0;
	self->xHdrChipClk.data = NULL;
	self->xHdrChipVol.data = NULL;

	self->devCfgs.data = NULL;	self->devCfgs.size = 0;
	self->devices.data = NULL;
	self->devNames.data = NULL;
	self->devNameBuffer.data = NULL;

	self->dacStreams.data = NULL;	self->dacStreams.size = 0;
	for (curBank = 0x00; curBank < PCM_BANK_COUNT; curBank ++)
	{
		PCM_BANK* pcmBnk = &self->pcmBank[curBank];
		pcmBnk->bankOfs.data = NULL;
		pcmBnk->bankSize.data = NULL;
		pcmBnk->data.data = NULL;
	}
	
	memset(&self->fileHdr, 0x00, sizeof(self->fileHdr));
	self->filePos = 0;
	self->fileTick = 0;
	self->playTick = 0;
	self->playSmpl = 0;
	self->curLoop = 0;
	self->playState = 0x00;
	self->psTrigger = 0x00;
	
	dev_logger_set(&self->logger, self, VGMEngine_PlayerLogCB, NULL);
	
	self->playOpts.playbackHz = 0;
	self->playOpts.hardStopOld = 0;
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
			DEV_ID devID = OPT_DEV_LIST[optChip];
			PLR_DEV_OPTS* devOpts = &self->devOpts[optID];
			
			PBaseEngine_InitDeviceOptions(&self->devOpts[optID]);
			if (devID == DEVID_AY8910)
				devOpts->coreOpts = OPT_AY8910_PCM3CH_DETECT;
			else if (devID == DEVID_NES_APU)
				devOpts->coreOpts = 0x01B7;
			else if (devID == DEVID_SCSP)
				devOpts->coreOpts = OPT_SCSP_BYPASS_DSP;
			self->devOptMap[devID][chipID] = optID;
			self->optDevMap[optID] = (size_t)-1;
		}
	}
	{
		UINT8 vgmChip;
		for (vgmChip = 0x00; vgmChip < CHIP_COUNT; vgmChip ++)
		{
			for (chipID = 0; chipID < 2; chipID ++)
				self->vdDevMap[vgmChip][chipID] = (size_t)-1;
		}
	}
	
	retVal = CPConv_Init(&self->cpcUTF16, "UTF-16LE", "UTF-8");
	if (retVal)
		self->cpcUTF16 = NULL;
	memset(&self->pcmComprTbl, 0x00, sizeof(PCM_COMPR_TBL));
	PE_ARRAY_CALLOC(self->tagData, char*, TAG_COUNT);
	PE_VECTOR_ALLOC(self->tagList, const char*, TAG_COUNT * 2 + 1);
	self->tagList.data[self->tagList.size] = NULL;
	self->tagList.size ++;
	return;
}

void VGMEngine_Deinit(PE_VGM* self)
{
	self->pe.eventCbFunc = NULL;	// prevent any callbacks during destruction
	
	if (self->playState & PLAYSTATE_PLAY)
		selfcall.Stop(&self->pe);
	selfcall.UnloadFile(&self->pe);
	
	if (self->cpcUTF16 != NULL)
		CPConv_Deinit(self->cpcUTF16);

	PE_ARRAY_FREE(self->yrwRom);

	FreeStringArrayData(&self->tagData);
	PE_ARRAY_FREE(self->tagData);
	PE_VECTOR_FREE(self->tagList);

	PBaseEngine_Deinit(&self->pe);
	return;
}

UINT8 VGMEngine_CanLoadFile(DATA_LOADER *dataLoader)
{
	DataLoader_ReadUntil(dataLoader,0x38);
	if (DataLoader_GetSize(dataLoader) < 0x38)
		return 0xF1;	// file too small
	if (memcmp(&DataLoader_GetData(dataLoader)[0x00], "Vgm ", 4))
		return 0xF0;	// invalid signature
	return 0x00;
}

UINT8 VGMEngine_LoadFile(PE_VGM* self, DATA_LOADER *dataLoader)
{
	self->dLoad = NULL;
	DataLoader_ReadUntil(dataLoader,0x38);
	self->fileData = DataLoader_GetData(dataLoader);
	if (DataLoader_GetSize(dataLoader) < 0x38 || memcmp(&self->fileData[0x00], "Vgm ", 4))
		return 0xF0;	// invalid file
	
	self->dLoad = dataLoader;
	DataLoader_ReadAll(self->dLoad);
	self->fileData = DataLoader_GetData(self->dLoad);
	
	// parse main header
	VGMEngine_ParseHeader(self);
	
	// parse extra headers
	VGMEngine_ParseXHdr_Data32(self, self->fileHdr.xhChpClkOfs, &self->xHdrChipClk);
	VGMEngine_ParseXHdr_Data16(self, self->fileHdr.xhChpVolOfs, &self->xHdrChipVol);
	
	VGMEngine_GenerateDeviceConfig(self);
	
	// parse tags
	VGMEngine_LoadTags(self);
	
	VGMEngine_RefreshTSRates(self);	// make Tick2Sample etc. work
	
	return 0x00;
}

static UINT8 VGMEngine_ParseHeader(PE_VGM* self)
{
	memset(&self->fileHdr, 0x00, sizeof(VGM_HEADER));
	
	self->fileHdr.fileVer = ReadLE32(&self->fileData[0x08]);
	
	self->fileHdr.dataOfs = (self->fileHdr.fileVer >= 0x150) ? ReadRelOfs(self->fileData, 0x34) : 0x00;
	if (! self->fileHdr.dataOfs)
		self->fileHdr.dataOfs = 0x40;	// offset not set - assume v1.00 header size
	if (self->fileHdr.dataOfs < 0x38)
	{
		emu_logf(&self->logger, PLRLOG_WARN, "Invalid Data Offset 0x%02X!\n", self->fileHdr.dataOfs);
		self->fileHdr.dataOfs = 0x38;
	}
	self->hdrLenFile = self->fileHdr.dataOfs;
	
	self->fileHdr.extraHdrOfs = (self->hdrLenFile >= 0xC0) ? ReadRelOfs(self->fileData, 0xBC) : 0x00;
	if (self->fileHdr.extraHdrOfs && self->hdrLenFile > self->fileHdr.extraHdrOfs)
		self->hdrLenFile = self->fileHdr.extraHdrOfs;	// the main header ends where the extra header begins
	
	if (self->hdrLenFile > HDR_BUF_SIZE)
		self->hdrLenFile = HDR_BUF_SIZE;
	memset(self->hdrBuffer, 0x00, HDR_BUF_SIZE);
	memcpy(self->hdrBuffer, self->fileData, self->hdrLenFile);
	
	self->fileHdr.eofOfs = ReadRelOfs(self->hdrBuffer, 0x04);
	self->fileHdr.gd3Ofs = ReadRelOfs(self->hdrBuffer, 0x14);
	self->fileHdr.numTicks = ReadLE32(&self->hdrBuffer[0x18]);
	self->fileHdr.loopOfs = ReadRelOfs(self->hdrBuffer, 0x1C);
	self->fileHdr.loopTicks = ReadLE32(&self->hdrBuffer[0x20]);
	self->fileHdr.recordHz = ReadLE32(&self->hdrBuffer[0x24]);
	
	self->fileHdr.loopBase = (INT8)self->hdrBuffer[0x7E];
	self->fileHdr.loopModifier = self->hdrBuffer[0x7F];
	if (self->hdrBuffer[0x7C] <= 0xC0)
		self->fileHdr.volumeGain = self->hdrBuffer[0x7C];
	else if (self->hdrBuffer[0x7C] == 0xC1)
		self->fileHdr.volumeGain = -0x40;
	else
		self->fileHdr.volumeGain = self->hdrBuffer[0x7C] - 0x100;
	self->fileHdr.volumeGain <<= 3;	// 3.5 fixed point -> 8.8 fixed point
	
	if (! self->fileHdr.eofOfs || self->fileHdr.eofOfs > DataLoader_GetSize(self->dLoad))
	{
		emu_logf(&self->logger, PLRLOG_WARN, "Invalid EOF Offset 0x%06X! (should be: 0x%06X)\n",
				self->fileHdr.eofOfs, DataLoader_GetSize(self->dLoad));
		self->fileHdr.eofOfs = DataLoader_GetSize(self->dLoad);	// catch invalid EOF values
	}
	self->fileHdr.dataEnd = self->fileHdr.eofOfs;
	// command data ends at the GD3 offset if:
	//	GD3 is used && GD3 offset < EOF (just to be sure) && GD3 offset > dataOfs (catch files with GD3 between header and data)
	if (self->fileHdr.gd3Ofs && (self->fileHdr.gd3Ofs < self->fileHdr.dataEnd && self->fileHdr.gd3Ofs >= self->fileHdr.dataOfs))
		self->fileHdr.dataEnd = self->fileHdr.gd3Ofs;
	
	if (self->fileHdr.extraHdrOfs && self->fileHdr.extraHdrOfs < self->fileHdr.eofOfs)
	{
		UINT32 xhLen = ReadLE32(&self->fileData[self->fileHdr.extraHdrOfs]);
		if (xhLen >= 0x08)
			self->fileHdr.xhChpClkOfs = ReadRelOfs(self->fileData, self->fileHdr.extraHdrOfs + 0x04);
		if (xhLen >= 0x0C)
			self->fileHdr.xhChpVolOfs = ReadRelOfs(self->fileData, self->fileHdr.extraHdrOfs + 0x08);
	}

	if (self->fileHdr.loopOfs)
	{
		if (self->fileHdr.loopOfs < self->fileHdr.dataOfs || self->fileHdr.loopOfs >= self->fileHdr.dataEnd)
		{
			emu_logf(&self->logger, PLRLOG_WARN, "Invalid loop offset 0x%06X - ignoring!\n", self->fileHdr.loopOfs);
			self->fileHdr.loopOfs = 0x00;
		}
		if (self->fileHdr.loopOfs && self->fileHdr.loopTicks == 0)
		{
			// 0-Sample-Loops causes the program to hang in the playback routine
			emu_logf(&self->logger, PLRLOG_WARN, "Ignored Zero-Sample-Loop!\n");
			self->fileHdr.loopOfs = 0x00;
		}
	}
	
	self->p2612Fix = 0x00;
	self->v101Fix = 0x00;
	if (self->fileHdr.fileVer <= 0x150)
	{
		if (VGMEngine_GetChipCount(self, 0x02) == 1)	// there must be exactly 1x YM2612 present
			self->p2612Fix = P2612FIX_ENABLE;	// enable fix for Project2612 VGMs
	}

	if (self->fileHdr.fileVer < 0x110)
	{
		if (VGMEngine_GetChipCount(self, 0x01))        // There must be an FM clock
		{
			VGMEngine_ParseFileForFMClocks(self);
			self->v101Fix = 1;
		}
	}
	
	return 0x00;
}

static void VGMEngine_ParseXHdr_Data32(PE_VGM* self, UINT32 fileOfs, ARR_XHDR_D32* xData)
{
	PE_ARRAY_FREE(*xData);
	if (! fileOfs || fileOfs >= DataLoader_GetSize(self->dLoad))
		return;
	
	UINT32 curPos = fileOfs;
	size_t curChip;
	
	PE_ARRAY_MALLOC(*xData, XHDR_DATA32, self->fileData[curPos]);
	curPos ++;
	for (curChip = 0; curChip < xData->size; curChip ++, curPos += 0x05)
	{
		if (curPos + 0x05 > DataLoader_GetSize(self->dLoad))
		{
			xData->size = curChip;
			break;
		}
		
		XHDR_DATA32* cData = &xData->data[curChip];
		cData->type = self->fileData[curPos + 0x00];
		cData->data = ReadLE32(&self->fileData[curPos + 0x01]);
	}
	
	return;
}

static void VGMEngine_ParseXHdr_Data16(PE_VGM* self, UINT32 fileOfs, ARR_XHDR_D16* xData)
{
	PE_ARRAY_FREE(*xData);
	if (! fileOfs || fileOfs >= DataLoader_GetSize(self->dLoad))
		return;
	
	UINT32 curPos = fileOfs;
	size_t curChip;
	
	PE_ARRAY_MALLOC(*xData, XHDR_DATA16, self->fileData[curPos]);
	curPos ++;
	for (curChip = 0; curChip < xData->size; curChip ++, curPos += 0x04)
	{
		if (curPos + 0x04 > DataLoader_GetSize(self->dLoad))
		{
			xData->size = curChip;
			break;
		}
		
		XHDR_DATA16* cData = &xData->data[curChip];
		cData->type = self->fileData[curPos + 0x00];
		cData->flags = self->fileData[curPos + 0x01];
		cData->data = ReadLE16(&self->fileData[curPos + 0x02]);
	}
	
	return;
}

UINT8 VGMEngine_LoadTags(PE_VGM* self)
{
	size_t curTag;
	
	FreeStringArrayData(&self->tagData);
	self->tagList.size = 0;
	self->tagList.data[self->tagList.size] = NULL;	// default to "no tags"
	self->tagList.size ++;

	if (! self->fileHdr.gd3Ofs)
		return 0x00;	// no GD3 tag present
	if (self->fileHdr.gd3Ofs >= self->fileHdr.eofOfs)
		return 0xF3;	// tag error (offset out-of-range)
	
	UINT32 curPos;
	UINT32 eotPos;
	
	if (self->fileHdr.gd3Ofs + 0x0C > self->fileHdr.eofOfs)	// separate check to catch overflows
		return 0xF3;	// tag error (GD3 header incomplete)
	if (memcmp(&self->fileData[self->fileHdr.gd3Ofs + 0x00], "Gd3 ", 4))
		return 0xF0;	// bad tag
	
	self->tagVer = ReadLE32(&self->fileData[self->fileHdr.gd3Ofs + 0x04]);
	if (self->tagVer < 0x100 || self->tagVer >= 0x200)
		return 0xF1;	// unsupported tag version
	
	eotPos = ReadLE32(&self->fileData[self->fileHdr.gd3Ofs + 0x08]);
	curPos = self->fileHdr.gd3Ofs + 0x0C;
	eotPos += curPos;
	if (eotPos > self->fileHdr.eofOfs)
		eotPos = self->fileHdr.eofOfs;
	
	self->tagList.size = 0;	// revert list terminator
	for (curTag = 0; curTag < TAG_COUNT; curTag ++)
	{
		UINT32 startPos = curPos;
		if (curPos >= eotPos)
			break;
		
		// search for UTF-16 L'\0' character
		while(curPos < eotPos && ReadLE16(&self->fileData[curPos]) != L'\0')
			curPos += 0x02;
		self->tagData.data[curTag] = VGMEngine_GetUTF8String(self, (char*)&self->fileData[startPos], (char*)&self->fileData[curPos]);
		curPos += 0x02;	// skip '\0'
		
		if (self->tagData.data[curTag] != NULL && self->tagData.data[curTag][0] != '\0')	// skip empty tags
		{
			self->tagList.data[self->tagList.size + 0] = TAG_TYPE_LIST[curTag];
			self->tagList.data[self->tagList.size + 1] = self->tagData.data[curTag];
			self->tagList.size += 2;
		}
	}
	
	self->tagList.data[self->tagList.size] = NULL;	// add list terminator
	self->tagList.size ++;
	return 0x00;
}

static char* VGMEngine_GetUTF8String(PE_VGM* self, const char* startPtr, const char* endPtr)
{
	char* result = NULL;
	size_t convSize = 0;
	char* convData = NULL;
	UINT8 retVal;
	
	if (self->cpcUTF16 == NULL || startPtr == endPtr)
		return NULL;
	
	retVal = CPConv_StrConvert(self->cpcUTF16, &convSize, &convData, endPtr - startPtr, startPtr);
	if (retVal < 0x80)
	{
		result = (char*)malloc(convSize + 1);
		memcpy(result, convData, convSize);
		result[convSize] = '\0';
	}
	free(convData);
	return result;
}

UINT8 VGMEngine_UnloadFile(PE_VGM* self)
{
	size_t curDev;

	if (self->playState & PLAYSTATE_PLAY)
		return 0xFF;
	
	self->playState = 0x00;
	self->dLoad = NULL;
	self->fileData = NULL;
	self->fileHdr.fileVer = 0xFFFFFFFF;
	self->fileHdr.dataOfs = 0x00;

	PE_ARRAY_FREE(self->xHdrChipClk);
	PE_ARRAY_FREE(self->xHdrChipVol);

	PE_VECTOR_FREE(self->dacStreams);

	for (curDev = 0; curDev < self->devCfgs.size; curDev ++)
		PE_ARRAY_FREE(self->devCfgs.data[curDev].cfgData);
	PE_ARRAY_FREE(self->devCfgs);
	PE_ARRAY_FREE(self->devices);
	PE_ARRAY_FREE(self->devNames);
	PE_ARRAY_FREE(self->devNameBuffer);

	FreeStringArrayData(&self->tagData);
	self->tagList.size = 0;
	
	return 0x00;
}

const VGM_HEADER* VGMEngine_GetFileHeader(const PE_VGM* self)
{
	return &self->fileHdr;
}

const char* const* VGMEngine_GetTags(PE_VGM* self)
{
	if (self->tagList.size == 0)
		return NULL;
	else
		return &self->tagList.data[0];
}

UINT8 VGMEngine_GetSongInfo(PE_VGM* self, PLR_SONG_INFO* songInf)
{
	UINT8 vgmChip;
	
	if (self->dLoad == NULL)
		return 0xFF;
	
	songInf->format = FCC_VGM;
	songInf->fileVerMaj = (self->fileHdr.fileVer >> 8) & 0xFFFFFF;
	songInf->fileVerMin = (self->fileHdr.fileVer >> 0) & 0xFF;
	songInf->tickRateMul = 1;
	songInf->tickRateDiv = 44100;
	songInf->songLen = selfcall.GetTotalTicks(&self->pe);
	songInf->loopTick = self->fileHdr.loopOfs ? selfcall.GetLoopTicks(&self->pe) : (UINT32)-1;
	songInf->volGain = (INT32)(0x10000 * pow(2.0, self->fileHdr.volumeGain / (double)0x100) + 0.5);
	songInf->deviceCnt = 0;
	for (vgmChip = 0x00; vgmChip < CHIP_COUNT; vgmChip ++)
		songInf->deviceCnt += VGMEngine_GetChipCount(self, vgmChip);
	
	return 0x00;
}

UINT8 VGMEngine_GetSongDeviceInfo(const PE_VGM* self, size_t* retDevInfCount, PLR_DEV_INFO** retDevInfData)
{
	size_t curDev;
	size_t diIdx;
	
	if (self->dLoad == NULL)
		return 0xFF;
	
	diIdx = self->devCfgs.size;
	for (curDev = 0; curDev < self->devCfgs.size; curDev++)
	{
		const VGM_DEVCFG* sdCfg = &self->devCfgs.data[curDev];
		const VGM_CHIPDEV* cDev = (sdCfg->deviceID < self->devices.size) ? &self->devices.data[sdCfg->deviceID] : NULL;
		DEV_ID devType = sdCfg->type;
		if (cDev != NULL)
		{
			diIdx += cDev->base.defInf.linkDevCount;
		}
		else
		{
			const DEV_DECL* devDecl = SndEmu_GetDevDecl(devType, self->pe.userDevList, self->pe.devStartOpts);
			const DEVLINK_IDS* dlIds = devDecl->linkDevIDs((const DEV_GEN_CFG*)sdCfg->cfgData.data);
			if (dlIds != NULL && dlIds->devCount > 0)
				diIdx += dlIds->devCount;
		}
	}
	
	*retDevInfCount = diIdx;
	*retDevInfData = (PLR_DEV_INFO*)calloc(*retDevInfCount, sizeof(PLR_DEV_INFO));
	for (curDev = 0, diIdx = 0; curDev < self->devCfgs.size; curDev ++)
	{
		const VGM_DEVCFG* sdCfg = &self->devCfgs.data[curDev];
		const DEV_GEN_CFG* dCfg = (const DEV_GEN_CFG*)sdCfg->cfgData.data;
		const VGM_CHIPDEV* cDev = (sdCfg->deviceID < self->devices.size) ? &self->devices.data[sdCfg->deviceID] : NULL;
		size_t diIdxParent = diIdx;
		PLR_DEV_INFO* devInf = &(*retDevInfData)[curDev];
		diIdx ++;
		
		// chip configuration from VGM header
		memset(devInf, 0x00, sizeof(PLR_DEV_INFO));
		devInf->type = sdCfg->type;
		devInf->id = (UINT32)sdCfg->deviceID;
		devInf->parentIdx = (UINT32)-1;
		devInf->instance = (UINT8)sdCfg->instance;
		devInf->devLogName = (sdCfg->deviceID < self->devNames.size) ? self->devNames.data[sdCfg->deviceID] : NULL;
		devInf->devCfg = dCfg;
		if (cDev != NULL)
		{
			// when playing, get information from device structures (may feature modified volume levels)
			const VGM_BASEDEV* clDev = &cDev->base;
			UINT32 curLDev;
			
			devInf->devDecl = clDev->defInf.devDecl;
			devInf->core = (clDev->defInf.devDef != NULL) ? clDev->defInf.devDef->coreID : 0x00;
			devInf->volume = (clDev->resmpl.volumeL + clDev->resmpl.volumeR) / 2;
			devInf->smplRate = clDev->defInf.sampleRate;
			
			for (curLDev = 0, clDev = clDev->linkDev; curLDev < cDev->base.defInf.linkDevCount && clDev != NULL; curLDev ++, clDev = clDev->linkDev)
			{
				const DEVLINK_INFO* dLink = &cDev->base.defInf.linkDevs[curLDev];
				PLR_DEV_INFO* lDevInf = &(*retDevInfData)[diIdx];
				diIdx ++;
				
				memset(lDevInf, 0x00, sizeof(PLR_DEV_INFO));
				lDevInf->type = dLink->devID;
				lDevInf->id = (UINT32)sdCfg->deviceID;
				lDevInf->parentIdx = diIdxParent;
				lDevInf->instance = (UINT16)curLDev;
				lDevInf->devLogName = devInf->devLogName;
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
			devInf->volume = VGMEngine_GetChipVolume(self, sdCfg->vgmChipType, sdCfg->instance, 0);
			devInf->smplRate = 0;
			
			dlIds = devInf->devDecl->linkDevIDs(dCfg);
			if (dlIds != NULL && dlIds->devCount > 0)
			{
				size_t curLDev;
				for (curLDev = 0; curLDev < dlIds->devCount; curLDev ++)
				{
					PLR_DEV_INFO* lDevInf = &(*retDevInfData)[diIdx];
					diIdx ++;
					
					memset(lDevInf, 0x00, sizeof(PLR_DEV_INFO));
					lDevInf->type = dlIds->devIDs[curLDev];
					lDevInf->id = (UINT32)sdCfg->deviceID;
					lDevInf->parentIdx = diIdxParent;
					lDevInf->instance = (UINT16)curLDev;
					lDevInf->devDecl = SndEmu_GetDevDecl(lDevInf->type, self->pe.userDevList, self->pe.devStartOpts);
					lDevInf->devLogName = devInf->devLogName;
					lDevInf->devCfg = NULL;
					lDevInf->core = 0x00;
					lDevInf->volume = VGMEngine_GetChipVolume(self, sdCfg->vgmChipType, sdCfg->instance, 1);
					lDevInf->smplRate = 0;
				}
			}
		}
	}
	if (self->playState & PLAYSTATE_PLAY)
		return 0x01;	// returned "live" data
	else
		return 0x00;	// returned data based on file header
}

static size_t VGMEngine_DeviceID2OptionID(const PE_VGM* self, UINT32 id)
{
	DEV_ID type;
	UINT8 instance;
	
	if (id & 0x80000000)
	{
		type = (id >> 0) & 0xFF;
		instance = (id >> 16) & 0xFF;
	}
	else if (id < self->devices.size)
	{
		type = self->devices.data[id].chipType;
		instance = self->devices.data[id].chipID;
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

static void VGMEngine_RefreshDevOptions(PE_VGM* self, VGM_CHIPDEV* chipDev, const PLR_DEV_OPTS* devOpts)
{
	DEV_ID chipType = chipDev->chipType;
	DEV_INFO* devInf = &chipDev->base.defInf;
	if (devInf->devDef->SetOptionBits == NULL)
		return;
	
	UINT32 coreOpts = devOpts->coreOpts;
	if (chipType == DEVID_YM2612)
	{
		if (chipDev->flags)
			coreOpts = (coreOpts & ~0x30) | OPT_YM2612_TYPE_OPN2C_ASIC;	// enforce YM3438 mode
		if (self->p2612Fix & P2612FIX_ACTIVE)
			coreOpts |= OPT_YM2612_LEGACY_MODE;	// enable legacy mode
	}
	else if (chipType == DEVID_GB_DMG)
		coreOpts |= OPT_GB_DMG_LEGACY_MODE;	// enable legacy mode (fix playback of old VGMs)
	else if (chipType == DEVID_QSOUND)
		coreOpts |= OPT_QSOUND_NOWAIT;	// make sure seeking works
	
	devInf->devDef->SetOptionBits(devInf->dataPtr, coreOpts);
	return;
}

static void VGMEngine_RefreshMuting(PE_VGM* self, VGM_CHIPDEV* chipDev, const PLR_MUTE_OPTS* muteOpts)
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

static void VGMEngine_RefreshPanning(PE_VGM* self, VGM_CHIPDEV* chipDev, const PLR_PAN_OPTS* panOpts)
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

UINT8 VGMEngine_SetDeviceOptions(PE_VGM* self, UINT32 id, const PLR_DEV_OPTS* devOpts)
{
	size_t optID = VGMEngine_DeviceID2OptionID(self, id);
	if (optID == (size_t)-1)
		return 0x80;	// bad device ID
	
	self->devOpts[optID] = *devOpts;
	
	size_t devID = self->optDevMap[optID];
	if (devID < self->devices.size)
	{
		VGMEngine_RefreshDevOptions(self, &self->devices.data[devID], &self->devOpts[optID]);
		VGMEngine_RefreshMuting(self, &self->devices.data[devID], &self->devOpts[optID].muteOpts);
		VGMEngine_RefreshPanning(self, &self->devices.data[devID], &self->devOpts[optID].panOpts);
	}
	return 0x00;
}

UINT8 VGMEngine_GetDeviceOptions(const PE_VGM* self, UINT32 id, PLR_DEV_OPTS* devOpts)
{
	size_t optID = VGMEngine_DeviceID2OptionID(self, id);
	if (optID == (size_t)-1)
		return 0x80;	// bad device ID
	
	*devOpts = self->devOpts[optID];
	return 0x00;
}

UINT8 VGMEngine_SetDeviceMuting(PE_VGM* self, UINT32 id, const PLR_MUTE_OPTS* muteOpts)
{
	size_t optID = VGMEngine_DeviceID2OptionID(self, id);
	if (optID == (size_t)-1)
		return 0x80;	// bad device ID
	
	self->devOpts[optID].muteOpts = *muteOpts;
	
	size_t devID = self->optDevMap[optID];
	if (devID < self->devices.size)
		VGMEngine_RefreshMuting(self, &self->devices.data[devID], &self->devOpts[optID].muteOpts);
	return 0x00;
}

UINT8 VGMEngine_GetDeviceMuting(const PE_VGM* self, UINT32 id, PLR_MUTE_OPTS* muteOpts)
{
	size_t optID = VGMEngine_DeviceID2OptionID(self, id);
	if (optID == (size_t)-1)
		return 0x80;	// bad device ID
	
	*muteOpts = self->devOpts[optID].muteOpts;
	return 0x00;
}

UINT8 VGMEngine_SetPlayerOptions(PE_VGM* self, const VGM_PLAY_OPTIONS* playOpts)
{
	self->playOpts = *playOpts;
	VGMEngine_RefreshTSRates(self);	// refresh, in case self->playOpts.playbackHz changed
	return 0x00;
}

UINT8 VGMEngine_GetPlayerOptions(const PE_VGM* self, VGM_PLAY_OPTIONS* playOpts)
{
	*playOpts = self->playOpts;
	return 0x00;
}

UINT8 VGMEngine_SetSampleRate(PE_VGM* self, UINT32 sampleRate)
{
	if (self->playState & PLAYSTATE_PLAY)
		return 0x01;	// can't set during playback
	
	self->pe.outSmplRate = sampleRate;
	return 0x00;
}

double VGMEngine_GetPlaybackSpeed(const PE_VGM* self)
{
	return self->playOpts.genOpts.pbSpeed / (double)0x10000;
}

UINT8 VGMEngine_SetPlaybackSpeed(PE_VGM* self, double speed)
{
	self->playOpts.genOpts.pbSpeed = (UINT32)(0x10000 * speed);
	VGMEngine_RefreshTSRates(self);
	return 0x00;
}


void VGMEngine_RefreshTSRates(PE_VGM* self)
{
	self->ttMult = 1;
	self->tsDiv = 44100;
	if (self->playOpts.playbackHz && self->fileHdr.recordHz)
	{
		self->ttMult *= self->fileHdr.recordHz;
		self->tsDiv *= self->playOpts.playbackHz;
	}
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

UINT32 VGMEngine_Tick2Sample(const PE_VGM* self, UINT32 ticks)
{
	if (ticks == (UINT32)-1)
		return -1;
	return (UINT32)(ticks * self->tsMult / self->tsDiv);
}

UINT32 VGMEngine_Sample2Tick(const PE_VGM* self, UINT32 samples)
{
	if (samples == (UINT32)-1)
		return -1;
	return (UINT32)(samples * self->tsDiv / self->tsMult);
}

double VGMEngine_Tick2Second(const PE_VGM* self, UINT32 ticks)
{
	if (ticks == (UINT32)-1)
		return -1.0;
	return (INT64)(ticks * self->ttMult) / (double)(INT64)self->tsDiv;
}

UINT8 VGMEngine_GetState(const PE_VGM* self)
{
	return self->playState;
}

UINT32 VGMEngine_GetCurPos(const PE_VGM* self, UINT8 unit)
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

UINT32 VGMEngine_GetCurLoop(const PE_VGM* self)
{
	return self->curLoop;
}

UINT32 VGMEngine_GetTotalTicks(const PE_VGM* self)
{
	return self->fileHdr.numTicks;
}

UINT32 VGMEngine_GetLoopTicks(const PE_VGM* self)
{
	if (! self->fileHdr.loopOfs)
		return 0;
	else
		return self->fileHdr.loopTicks;
}

UINT32 VGMEngine_GetModifiedLoopCount(const PE_VGM* self, UINT32 defaultLoops)
{
	if (defaultLoops == 0)
		return 0;
	UINT32 loopCntModified;
	if (self->fileHdr.loopModifier)
		loopCntModified = (defaultLoops * self->fileHdr.loopModifier + 0x08) / 0x10;
	else
		loopCntModified = defaultLoops;
	if ((INT32)loopCntModified <= self->fileHdr.loopBase)
		return 1;
	else
		return loopCntModified - self->fileHdr.loopBase;
}

UINT8 VGMEngine_GetStreamDevInfo(const PE_VGM* self, size_t* retDevInfCount, VGM_PCMSTRM_DEV** retDevInfData)
{
	*retDevInfCount = self->dacStreams.size;
	*retDevInfData = self->dacStreams.data;
	return 0x00;
}

static void VGMEngine_PlayerLogCB(void* userParam, void* source, UINT8 level, const char* message)
{
	PE_VGM* player = (PE_VGM*)source;
	if (player->pe.logCbFunc == NULL)
		return;
	player->pe.logCbFunc(player->pe.logCbParam, &player->pe, level, PLRLOGSRC_PLR, NULL, message);
	return;
}

static void VGMEngine_SndEmuLogCB(void* userParam, void* source, UINT8 level, const char* message)
{
	DEVLOG_CB_DATA* cbData = (DEVLOG_CB_DATA*)userParam;
	PE_VGM* player = cbData->player;
	if (player->pe.logCbFunc == NULL)
		return;
	if ((player->playState & PLAYSTATE_SEEK) && level > PLRLOG_ERROR)
		return;	// prevent message spam while seeking
	player->pe.logCbFunc(player->pe.logCbParam, &player->pe, level, PLRLOGSRC_EMU,
		player->devNames.data[cbData->chipDevID], message);
	return;
}


UINT8 VGMEngine_Start(PE_VGM* self)
{
	VGMEngine_InitDevices(self);
	
	self->playState |= PLAYSTATE_PLAY;
	selfcall.Reset(&self->pe);
	if (self->pe.eventCbFunc != NULL)
		self->pe.eventCbFunc(&self->pe, self->pe.eventCbParam, PLREVT_START, NULL);
	
	return 0x00;
}

UINT8 VGMEngine_Stop(PE_VGM* self)
{
	size_t curDev;
	size_t curBank;
	
	self->playState &= ~PLAYSTATE_PLAY;
	
	for (curDev = 0; curDev < self->dacStreams.size; curDev ++)
	{
		DEV_INFO* devInf = &self->dacStreams.data[curDev].defInf;
		devInf->devDef->Stop(devInf->dataPtr);
	}
	self->dacStreams.size = 0;
	
	for (curBank = 0x00; curBank < PCM_BANK_COUNT; curBank ++)
	{
		PCM_BANK* pcmBnk = &self->pcmBank[curBank];
		PE_VECTOR_FREE(pcmBnk->bankOfs);
		PE_VECTOR_FREE(pcmBnk->bankSize);
		PE_VECTOR_FREE(pcmBnk->data);
	}
	free(self->pcmComprTbl.values.d8);	self->pcmComprTbl.values.d8 = NULL;
	
	for (curDev = 0; curDev < self->devices.size; curDev ++)
	{
		VGM_CHIPDEV* cDev = &self->devices.data[curDev];
		FreeDeviceTree(&cDev->base, 0);
	}
	PE_ARRAY_FREE(self->devNames);
	PE_ARRAY_FREE(self->devices);
	if (self->pe.eventCbFunc != NULL)
		self->pe.eventCbFunc(&self->pe, self->pe.eventCbParam, PLREVT_STOP, NULL);
	
	return 0x00;
}

UINT8 VGMEngine_Reset(PE_VGM* self)
{
	size_t curDev;
	size_t curStrm;
	UINT8 chipID;
	size_t curBank;
	
	self->filePos = self->fileHdr.dataOfs;
	self->fileTick = 0;
	self->playTick = 0;
	self->playSmpl = 0;
	self->playState &= ~PLAYSTATE_END;
	self->psTrigger = 0x00;
	self->curLoop = 0;
	self->lastLoopTick = 0;
	
	VGMEngine_RefreshTSRates(self);
	
	// TODO (optimization): keep self->dacStreams vector and just reset devices
	for (curDev = 0; curDev < self->dacStreams.size; curDev++)
	{
		DEV_INFO* devInf = &self->dacStreams.data[curDev].defInf;
		devInf->devDef->Stop(devInf->dataPtr);
	}
	self->dacStreams.size = 0;
	for (curStrm = 0; curStrm < 0x100; curStrm ++)
		self->dacStrmMap[curStrm] = (size_t)-1;
	
	// TODO (optimization): don't reset self->pcmBank and instead skip data that was already loaded
	for (curBank = 0x00; curBank < PCM_BANK_COUNT; curBank++)
	{
		PCM_BANK* pcmBnk = &self->pcmBank[curBank];
		PE_VECTOR_FREE(pcmBnk->bankOfs);
		PE_VECTOR_FREE(pcmBnk->bankSize);
		PE_VECTOR_FREE(pcmBnk->data);
	}
	free(self->pcmComprTbl.values.d8);	self->pcmComprTbl.values.d8 = NULL;
	memset(&self->pcmComprTbl, 0x00, sizeof(PCM_COMPR_TBL));
	
	self->ym2612pcm_bnkPos = 0x00;
	memset(self->rf5cBank, 0x00, sizeof(self->rf5cBank));
	for (chipID = 0; chipID < 2; chipID ++)
	{
		memset(self->qsWork[chipID].startAddrCache, 0x00, sizeof(self->qsWork[0].startAddrCache));
		memset(self->qsWork[chipID].pitchCache, 0x00, sizeof(self->qsWork[0].pitchCache));
	}
	
	for (curDev = 0; curDev < self->devices.size; curDev ++)
	{
		VGM_BASEDEV* clDev = &self->devices.data[curDev].base;
		clDev->defInf.devDef->Reset(clDev->defInf.dataPtr);
		for (; clDev != NULL; clDev = clDev->linkDev)
		{
			// TODO: Resmpl_Reset(&clDev->resmpl);
		}
	}
	
	if ((self->p2612Fix & P2612FIX_ENABLE) && ! (self->p2612Fix & P2612FIX_ACTIVE))
	{
		self->p2612Fix |= P2612FIX_ACTIVE;	// enable Project2612 fix (YM2612 "legacy" mode)
		
		size_t optID = self->devOptMap[DEVID_YM2612][0];
		size_t devID = (optID == (size_t)-1) ? (size_t)-1 : self->optDevMap[optID];
		// refresh options, adding OPT_YM2612_LEGACY_MODE
		if (devID < self->devices.size)
			VGMEngine_RefreshDevOptions(self, &self->devices.data[devID], &self->devOpts[optID]);
	}
	
	return 0x00;
}

static UINT32 VGMEngine_GetHeaderChipClock(const PE_VGM* self, UINT8 chipType)
{
	if (chipType >= CHIP_COUNT)
		return 0;

	// Fix for 1.00/1.01 "FM" clock
	if (self->v101Fix)
	{
		switch (chipType)
		{
		case 1:
			return self->v101ym2413clock;
		case 2:
			return self->v101ym2612clock;
		case 3:
			return self->v101ym2151clock;
		default:
			break;
		}
	}
	
	return ReadLE32(&self->hdrBuffer[CHIPCLK_OFS[chipType]]);
}

INLINE UINT32 VGMEngine_GetChipCount(const PE_VGM* self, UINT8 chipType)
{
	UINT32 clock = VGMEngine_GetHeaderChipClock(self, chipType);
	if (! clock)
		return 0;
	return (clock & 0x40000000) ? 2 : 1;
}

static UINT32 VGMEngine_GetChipClock(const PE_VGM* self, UINT8 chipType, UINT8 chipID)
{
	size_t curChip;
	UINT32 clock = VGMEngine_GetHeaderChipClock(self, chipType);
	
	if (chipID == 0)
		return clock & ~0x40000000;	// return clock without dual-chip bit
	if (! (clock & 0x40000000))
		return 0;	// dual-chip bit not set - no second chip used
	
	for (curChip = 0; curChip < self->xHdrChipClk.size; curChip ++)
	{
		const XHDR_DATA32* cData = &self->xHdrChipClk.data[curChip];
		if (cData->type == chipType)
			return cData->data;
	}
	
	return clock & ~0x40000000;	// return clock without dual-chip bit
}

static UINT16 VGMEngine_GetChipVolume(const PE_VGM* self, UINT8 chipType, UINT8 chipID, UINT8 isLinked)
{
	size_t curChip;
	UINT16 numChips;
	UINT16 vol;
	
	if (chipType >= CHIP_COUNT)
		return 0;
	
	vol = CHIP_VOLUME[chipType];
	numChips = VGMEngine_GetChipCount(self, chipType);
	if (chipType == 0x00)
	{
		// The T6W28 consists of 2 "half" chips, so we need to treat it as 1.
		if (VGMEngine_GetHeaderChipClock(self, chipType) & 0x80000000)
			numChips = 1;
	}
	
	if (isLinked)
	{
		if (chipType == 0x06)
			vol /= 2;	// the YM2203's SSG should be half as loud as the FM part
	}
	if (numChips > 1)
		vol /= numChips;
	
	chipType = (isLinked << 7) | (chipType & 0x7F);
	for (curChip = 0; curChip < self->xHdrChipVol.size; curChip ++)
	{
		const XHDR_DATA16* cData = &self->xHdrChipVol.data[curChip];
		if (cData->type == chipType && (cData->flags & 0x01) == chipID)
		{
			// Bit 15 - absolute/relative volume
			//	0 - absolute
			//	1 - relative (0x0100 = 1.0, 0x80 = 0.5, etc.)
			if (cData->data & 0x8000)
				vol = MulFixed8x8(vol, cData->data & 0x7FFF);
			else
				vol = cData->data;
			break;
		}
	}
	
	// additional patches for adjusted volume scale in sound cores
	if (chipType == 0x19)	// K051649
		vol = vol * 8 / 5;
	else if (chipType == 0x1C)	// C140/C219
		vol = (vol * 2 + 1) / 3;
	return vol;
}

static UINT16 VGMEngine_EstimateOverallVolume(const PE_VGM* self)
{
	size_t curChip;
	const VGM_BASEDEV* clDev;
	UINT16 absVol;
	
	absVol = 0x00;
	for (curChip = 0; curChip < self->devices.size; curChip ++)
	{
		const VGM_CHIPDEV* chipDev = &self->devices.data[curChip];
		for (clDev = &chipDev->base; clDev != NULL; clDev = clDev->linkDev)
		{
			absVol += MulFixed8x8(clDev->resmpl.volumeL + clDev->resmpl.volumeR,
									PB_VOL_AMNT[chipDev->vgmChipType]) / 2;
		}
	}
	
	return absVol;
}

static void VGMEngine_NormalizeOverallVolume(PE_VGM* self, UINT16 overallVol)
{
	if (! overallVol)
		return;
	
	UINT16 volFactor;
	size_t curChip;
	VGM_BASEDEV* clDev;
	
	if (overallVol <= 0x180)
	{
		volFactor = 1;
		while(overallVol <= 0x180)
		{
			volFactor *= 2;
			overallVol *= 2;
		}
		
		for (curChip = 0; curChip < self->devices.size; curChip ++)
		{
			VGM_CHIPDEV* chipDev = &self->devices.data[curChip];
			for (clDev = &chipDev->base; clDev != NULL; clDev = clDev->linkDev)
			{
				clDev->resmpl.volumeL *= volFactor;
				clDev->resmpl.volumeR *= volFactor;
			}
		}
	}
	else if (overallVol > 0x300)
	{
		volFactor = 1;
		while(overallVol > 0x300)
		{
			volFactor *= 2;
			overallVol /= 2;
		}
		
		for (curChip = 0; curChip < self->devices.size; curChip ++)
		{
			VGM_CHIPDEV* chipDev = &self->devices.data[curChip];
			for (clDev = &chipDev->base; clDev != NULL; clDev = clDev->linkDev)
			{
				clDev->resmpl.volumeL /= volFactor;
				clDev->resmpl.volumeR /= volFactor;
			}
		}
	}
	
	return;
}

static void VGMEngine_GenerateDeviceConfig(PE_VGM* self)
{
	size_t cfgIdx;
	UINT8 vgmChip;
	UINT8 chipID;
	
	cfgIdx = 0;
	for (vgmChip = 0x00; vgmChip < CHIP_COUNT; vgmChip ++)
		cfgIdx += VGMEngine_GetChipCount(self, vgmChip);
	PE_ARRAY_FREE(self->devCfgs);
	PE_ARRAY_CALLOC(self->devCfgs, VGM_DEVCFG, cfgIdx);
	
	cfgIdx = 0;
	for (vgmChip = 0x00; vgmChip < CHIP_COUNT; vgmChip ++)
	{
		for (chipID = 0; chipID < VGMEngine_GetChipCount(self, vgmChip); chipID ++)
		{
			DEV_GEN_CFG devCfg;
			VGM_DEVCFG* sdCfg = &self->devCfgs.data[cfgIdx];
			DEV_ID chipType = DEV_LIST[vgmChip];
			UINT32 hdrClock = VGMEngine_GetChipClock(self, vgmChip, chipID);
			
			memset(&devCfg, 0x00, sizeof(DEV_GEN_CFG));
			devCfg.clock = hdrClock & ~0xC0000000;
			devCfg.flags = (hdrClock & 0x80000000) >> 31;
			switch(chipType)
			{
			case DEVID_SN76496:
				{
					SN76496_CFG snCfg;
					
					snCfg._genCfg = devCfg;
					snCfg.shiftRegWidth = self->hdrBuffer[0x2A];
					if (! snCfg.shiftRegWidth)
						snCfg.shiftRegWidth = 0x10;
					snCfg.noiseTaps = ReadLE16(&self->hdrBuffer[0x28]);
					if (! snCfg.noiseTaps)
						snCfg.noiseTaps = 0x09;
					snCfg.segaPSG = (self->hdrBuffer[0x2B] & 0x01) ? 0 : 1;
					snCfg.negate = (self->hdrBuffer[0x2B] & 0x02) ? 1 : 0;
					snCfg.stereo = (self->hdrBuffer[0x2B] & 0x04) ? 0 : 1;
					snCfg.clkDiv = (self->hdrBuffer[0x2B] & 0x08) ? 1 : 8;
					snCfg.ncrPSG = (self->hdrBuffer[0x2B] & 0x10) ? 1 : 0;
					snCfg.t6w28_tone = NULL;
					SaveDeviceConfig(&sdCfg->cfgData, &snCfg, sizeof(SN76496_CFG));
				}
				break;
			case DEVID_SEGAPCM:
				{
					SEGAPCM_CFG spCfg;
					
					spCfg._genCfg = devCfg;
					spCfg.bnkshift = self->hdrBuffer[0x3C];
					spCfg.bnkmask = self->hdrBuffer[0x3E];
					SaveDeviceConfig(&sdCfg->cfgData, &spCfg, sizeof(SEGAPCM_CFG));
				}
				break;
			case DEVID_RF5C68:
				if (vgmChip == 0x05)	// RF5C68
					devCfg.flags = 0;
				else //if (vgmChip == 0x10)	// RF5C164
					devCfg.flags = 1;
				SaveDeviceConfig(&sdCfg->cfgData, &devCfg, sizeof(DEV_GEN_CFG));
				break;
			case DEVID_AY8910:
				{
					AY8910_CFG ayCfg;
					
					ayCfg._genCfg = devCfg;
					ayCfg.chipType = self->hdrBuffer[0x78];
					ayCfg.chipFlags = self->hdrBuffer[0x79];
					SaveDeviceConfig(&sdCfg->cfgData, &ayCfg, sizeof(AY8910_CFG));
				}
				break;
			case DEVID_YMW258:
				devCfg.clock = devCfg.clock * 224 / 180;	// fix VGM clock, which is based on the old /180 clock divider
				SaveDeviceConfig(&sdCfg->cfgData, &devCfg, sizeof(DEV_GEN_CFG));
				break;
			case DEVID_MSM6258:
				{
					MSM6258_CFG okiCfg;
					
					okiCfg._genCfg = devCfg;
					okiCfg.divider = (self->hdrBuffer[0x94] & 0x03) >> 0;
					okiCfg.adpcmBits = (self->hdrBuffer[0x94] & 0x04) ? MSM6258_ADPCM_4B : MSM6258_ADPCM_3B;
					okiCfg.outputBits = (self->hdrBuffer[0x94] & 0x08) ? MSM6258_OUT_12B : MSM6258_OUT_10B;
					
					SaveDeviceConfig(&sdCfg->cfgData, &okiCfg, sizeof(MSM6258_CFG));
				}
				break;
			case DEVID_K054539:
				if (devCfg.clock < 1000000)	// if < 1 MHz, then it's the sample rate, not the clock
					devCfg.clock *= 384;	// (for backwards compatibility with old VGM logs from 2012/13)
				devCfg.flags = self->hdrBuffer[0x95];
				SaveDeviceConfig(&sdCfg->cfgData, &devCfg, sizeof(DEV_GEN_CFG));
				break;
			case DEVID_C140:
				if (self->hdrBuffer[0x96] == 2)	// Namco ASIC 219
				{
					if (devCfg.clock == 44100)
						devCfg.clock = 25056500;
					else if (devCfg.clock < 1000000)	// if < 1 MHz, then it's the (incorrect) sample rate, not the clock
						devCfg.clock *= 576;	// (for backwards compatibility with old VGM logs from 2013/14)
					chipType = DEVID_C219;
				}
				else
				{
					if (devCfg.clock == 21390)
						devCfg.clock = 12288000;
					else if (devCfg.clock < 1000000)	// if < 1 MHz, then it's the (incorrect) sample rate, not the clock
						devCfg.clock *= 576;	// (for backwards compatibility with old VGM logs from 2013/14)
					devCfg.flags = self->hdrBuffer[0x96];	// banking type
					chipType = DEVID_C140;
				}
				SaveDeviceConfig(&sdCfg->cfgData, &devCfg, sizeof(DEV_GEN_CFG));
				break;
			case DEVID_MSM5205:
				{
					MSM5205_CFG okiCfg;

					okiCfg._genCfg = devCfg;
					okiCfg.prescaler = (self->hdrBuffer[0xD7] & 0x03) >> 0;
					okiCfg.adpcmBits = (self->hdrBuffer[0xD7] & 0x04) ? MSM5205_ADPCM_4B : MSM5205_ADPCM_3B;

					SaveDeviceConfig(&sdCfg->cfgData, &okiCfg, sizeof(MSM5205_CFG));
				}
				break;
			case DEVID_C352:
				devCfg.clock = devCfg.clock * 72 / self->hdrBuffer[0xD6];	// real clock = VGM clock / (VGM clkDiv * 4) * 288
				SaveDeviceConfig(&sdCfg->cfgData, &devCfg, sizeof(DEV_GEN_CFG));
				break;
			case DEVID_QSOUND:
				if (devCfg.clock < 5000000)	// QSound clock used to be 4 MHz
					devCfg.clock = devCfg.clock * 15;	// 4 MHz -> 60 MHz
				SaveDeviceConfig(&sdCfg->cfgData, &devCfg, sizeof(DEV_GEN_CFG));
				break;
			case DEVID_ES5503:
				devCfg.flags = self->hdrBuffer[0xD4];	// output channels
				SaveDeviceConfig(&sdCfg->cfgData, &devCfg, sizeof(DEV_GEN_CFG));
				break;
			case DEVID_ES5506:
				devCfg.flags = self->hdrBuffer[0xD5];	// output channels
				SaveDeviceConfig(&sdCfg->cfgData, &devCfg, sizeof(DEV_GEN_CFG));
				break;
			case DEVID_SCSP:
				if (devCfg.clock < 1000000)	// if < 1 MHz, then it's the sample rate, not the clock
					devCfg.clock *= 512;	// (for backwards compatibility with old VGM logs from 2012-14)
				SaveDeviceConfig(&sdCfg->cfgData, &devCfg, sizeof(DEV_GEN_CFG));
				break;
			case DEVID_MSM5232:
			{
				MSM5232_CFG okiCfg;

				okiCfg._genCfg = devCfg;
				// default value for now
				okiCfg.capacitors[0] = (double)(1e-6);
				okiCfg.capacitors[1] = (double)(1e-6);
				okiCfg.capacitors[2] = (double)(1e-6);
				okiCfg.capacitors[3] = (double)(1e-6);
				okiCfg.capacitors[4] = (double)(1e-6);
				okiCfg.capacitors[5] = (double)(1e-6);
				okiCfg.capacitors[6] = (double)(1e-6);
				okiCfg.capacitors[7] = (double)(1e-6);
				SaveDeviceConfig(&sdCfg->cfgData, &okiCfg, sizeof(MSM5232_CFG));
				break;
			}
			default:
				SaveDeviceConfig(&sdCfg->cfgData, &devCfg, sizeof(DEV_GEN_CFG));
				break;
			}
			
			sdCfg->deviceID = (size_t)-1;
			sdCfg->vgmChipType = vgmChip;
			sdCfg->type = chipType;
			sdCfg->instance = chipID;
			cfgIdx ++;
		}	// for (chipID)
	}	// end for (vgmChip)
	self->devCfgs.size = cfgIdx;
	
	return;
}

static void VGMEngine_InitDevices(PE_VGM* self)
{
	size_t curChip;
	size_t devIdx;
	size_t dnBufSize;
	char* dnBufPtr;
	size_t dnBufPos;
	
	memset(self->shownCmdWarnings, 0, 0x100);
	
	{
		UINT8 vgmChip;
		UINT8 chipID;
		for (vgmChip = 0x00; vgmChip < CHIP_COUNT; vgmChip ++)
		{
			for (chipID = 0; chipID < 2; chipID ++)
				self->vdDevMap[vgmChip][chipID] = (size_t)-1;
		}
	}
	for (curChip = 0; curChip < OPT_DEV_COUNT * 2; curChip ++)
		self->optDevMap[curChip] = (size_t)-1;
	
	// When the Project2612 fix is enabled [bit 7], enable it during chip init [bit 0].
	if (self->p2612Fix & P2612FIX_ENABLE)
		self->p2612Fix |= P2612FIX_ACTIVE;
	else
		self->p2612Fix &= ~P2612FIX_ACTIVE;
	
	PE_ARRAY_FREE(self->devices);
	PE_ARRAY_MALLOC(self->devices, VGM_CHIPDEV, self->devCfgs.size);
	PE_ARRAY_FREE(self->devNames);
	PE_ARRAY_MALLOC(self->devNames, const char*, self->devCfgs.size);
	PE_VECTOR_FREE(self->devNameBuffer);
	PE_VECTOR_ALLOC(self->devNameBuffer, char, self->devCfgs.size * 0x10);
	dnBufSize = self->devNameBuffer.alloc;
	dnBufPtr = self->devNameBuffer.data;
	dnBufPos = self->devNameBuffer.size;

	devIdx = 0;
	for (curChip = 0; curChip < self->devCfgs.size; curChip ++)
	{
		VGM_DEVCFG* sdCfg = &self->devCfgs.data[curChip];
		VGM_CHIPDEV* cDev = &self->devices.data[devIdx];
		DEV_ID chipType = sdCfg->type;
		UINT8 chipID = sdCfg->instance;
		DEV_GEN_CFG* devCfg = (DEV_GEN_CFG*)sdCfg->cfgData.data;
		DEV_INFO* devInf;
		const PLR_DEV_OPTS* devOpts;
		UINT8 retVal;
		
		memset(cDev, 0x00, sizeof(VGM_CHIPDEV));
		devInf = &cDev->base.defInf;
		
		sdCfg->deviceID = (size_t)-1;
		cDev->vgmChipType = sdCfg->vgmChipType;
		cDev->chipType = sdCfg->type;
		cDev->chipID = chipID;
		cDev->optID = self->devOptMap[chipType][chipID];
		cDev->cfgID = curChip;
		cDev->base.defInf.dataPtr = NULL;
		cDev->base.linkDev = NULL;
		
		devOpts = (cDev->optID != (size_t)-1) ? &self->devOpts[cDev->optID] : NULL;
		devCfg->emuCore = (devOpts != NULL) ? devOpts->emuCore[0] : 0x00;
		devCfg->srMode = (devOpts != NULL) ? devOpts->srMode : DEVRI_SRMODE_NATIVE;
		if (devOpts != NULL && devOpts->smplRate)
			devCfg->smplRate = devOpts->smplRate;
		else
			devCfg->smplRate = self->pe.outSmplRate;
		
		switch(chipType)
		{
		case DEVID_SN76496:
			if ((chipID & 0x01) && devCfg->flags)	// must be 2nd chip + T6W28 mode
			{
				VGM_CHIPDEV* otherDev = VGMEngine_GetDevicePtr(self, sdCfg->vgmChipType, chipID ^ 0x01);
				if (otherDev != NULL)
				{
					SN76496_CFG* snCfg = (SN76496_CFG*)devCfg;
					// set pointer to other instance, for connecting both
					snCfg->t6w28_tone = otherDev->base.defInf.dataPtr;
					// ensure that both instances use the same core, as they are going to cross-reference each other
					snCfg->_genCfg.emuCore = otherDev->base.defInf.devDef->coreID;
				}
			}
			
			if (! devCfg->emuCore)
				devCfg->emuCore = FCC_MAME;
			retVal = SndEmu_Start2(chipType, devCfg, devInf, self->pe.userDevList, self->pe.devStartOpts);
			if (retVal)
				break;
			SndEmu_GetDeviceFunc(devInf->devDef, RWF_REGISTER | RWF_WRITE, DEVRW_A8D8, 0, (void**)&cDev->write8);
			break;
		case DEVID_RF5C68:
			if (! devCfg->emuCore)
			{
				if (devCfg->flags == 1)	// RF5C164
					devCfg->emuCore = FCC_GENS;
				else //if (devCfg->flags == 0)	// RF5C68
					devCfg->emuCore = FCC_MAME;
			}
			retVal = SndEmu_Start2(chipType, devCfg, devInf, self->pe.userDevList, self->pe.devStartOpts);
			if (retVal)
				break;
			SndEmu_GetDeviceFunc(devInf->devDef, RWF_REGISTER | RWF_WRITE, DEVRW_A8D8, 0, (void**)&cDev->write8);
			SndEmu_GetDeviceFunc(devInf->devDef, RWF_MEMORY | RWF_WRITE, DEVRW_A16D8, 0, (void**)&cDev->writeM8);
			SndEmu_GetDeviceFunc(devInf->devDef, RWF_MEMORY | RWF_WRITE, DEVRW_BLOCK, 0, (void**)&cDev->romWrite);
			break;
		case DEVID_YM2610:
			retVal = SndEmu_Start2(chipType, devCfg, devInf, self->pe.userDevList, self->pe.devStartOpts);
			if (retVal)
				break;
			SndEmu_GetDeviceFunc(devInf->devDef, RWF_REGISTER | RWF_WRITE, DEVRW_A8D8, 0, (void**)&cDev->write8);
			SndEmu_GetDeviceFunc(devInf->devDef, RWF_MEMORY | RWF_WRITE, DEVRW_MEMSIZE, 'A', (void**)&cDev->romSize);
			SndEmu_GetDeviceFunc(devInf->devDef, RWF_MEMORY | RWF_WRITE, DEVRW_BLOCK, 'A', (void**)&cDev->romWrite);
			SndEmu_GetDeviceFunc(devInf->devDef, RWF_MEMORY | RWF_WRITE, DEVRW_MEMSIZE, 'B', (void**)&cDev->romSizeB);
			SndEmu_GetDeviceFunc(devInf->devDef, RWF_MEMORY | RWF_WRITE, DEVRW_BLOCK, 'B', (void**)&cDev->romWriteB);
			break;
		case DEVID_YMF278B:
			retVal = SndEmu_Start2(chipType, devCfg, devInf, self->pe.userDevList, self->pe.devStartOpts);
			if (retVal)
				break;
			SndEmu_GetDeviceFunc(devInf->devDef, RWF_REGISTER | RWF_WRITE, DEVRW_A8D8, 0, (void**)&cDev->write8);
			SndEmu_GetDeviceFunc(devInf->devDef, RWF_MEMORY | RWF_WRITE, DEVRW_MEMSIZE, 0x524F, (void**)&cDev->romSize);
			SndEmu_GetDeviceFunc(devInf->devDef, RWF_MEMORY | RWF_WRITE, DEVRW_BLOCK, 0x524F, (void**)&cDev->romWrite);
			SndEmu_GetDeviceFunc(devInf->devDef, RWF_MEMORY | RWF_WRITE, DEVRW_MEMSIZE, 0x5241, (void**)&cDev->romSizeB);
			SndEmu_GetDeviceFunc(devInf->devDef, RWF_MEMORY | RWF_WRITE, DEVRW_BLOCK, 0x5241, (void**)&cDev->romWriteB);
			VGMEngine_LoadOPL4ROM(self, cDev);
			break;
		case DEVID_32X_PWM:
			retVal = SndEmu_Start2(chipType, devCfg, devInf, self->pe.userDevList, self->pe.devStartOpts);
			if (retVal)
				break;
			SndEmu_GetDeviceFunc(devInf->devDef, RWF_REGISTER | RWF_WRITE, DEVRW_A8D16, 0, (void**)&cDev->writeD16);
			break;
		case DEVID_YMW258:
			retVal = SndEmu_Start2(chipType, devCfg, devInf, self->pe.userDevList, self->pe.devStartOpts);
			if (retVal)
				break;
			SndEmu_GetDeviceFunc(devInf->devDef, RWF_REGISTER | RWF_WRITE, DEVRW_A8D8, 0, (void**)&cDev->write8);
			SndEmu_GetDeviceFunc(devInf->devDef, RWF_REGISTER | RWF_WRITE, DEVRW_A8D16, 0, (void**)&cDev->writeD16);
			SndEmu_GetDeviceFunc(devInf->devDef, RWF_MEMORY | RWF_WRITE, DEVRW_MEMSIZE, 0, (void**)&cDev->romSize);
			SndEmu_GetDeviceFunc(devInf->devDef, RWF_MEMORY | RWF_WRITE, DEVRW_BLOCK, 0, (void**)&cDev->romWrite);
			break;
		case DEVID_C352:
			retVal = SndEmu_Start2(chipType, devCfg, devInf, self->pe.userDevList, self->pe.devStartOpts);
			if (retVal)
				break;
			SndEmu_GetDeviceFunc(devInf->devDef, RWF_REGISTER | RWF_WRITE, DEVRW_A16D16, 0, (void**)&cDev->writeM16);
			SndEmu_GetDeviceFunc(devInf->devDef, RWF_MEMORY | RWF_WRITE, DEVRW_MEMSIZE, 0, (void**)&cDev->romSize);
			SndEmu_GetDeviceFunc(devInf->devDef, RWF_MEMORY | RWF_WRITE, DEVRW_BLOCK, 0, (void**)&cDev->romWrite);
			break;
		case DEVID_QSOUND:
			cDev->flags = 0x00;
			{
				UINT32 hdrClock = VGMEngine_GetChipClock(self, sdCfg->vgmChipType, chipID) & ~0xC0000000;
				if (hdrClock < devCfg->clock)	// QSound VGMs with old (4 MHz) clock
					cDev->flags |= 0x01;	// enable QSound hacks (required for proper playback of old VGMs)
			}
			if (! devCfg->emuCore)
				devCfg->emuCore = FCC_CTR_;
			retVal = SndEmu_Start2(chipType, devCfg, devInf, self->pe.userDevList, self->pe.devStartOpts);
			if (retVal)
				break;
			SndEmu_GetDeviceFunc(devInf->devDef, RWF_REGISTER | RWF_WRITE, DEVRW_A8D8, 0, (void**)&cDev->write8);
			SndEmu_GetDeviceFunc(devInf->devDef, RWF_REGISTER | RWF_QUICKWRITE, DEVRW_A8D16, 0, (void**)&cDev->writeD16);
			SndEmu_GetDeviceFunc(devInf->devDef, RWF_MEMORY | RWF_WRITE, DEVRW_MEMSIZE, 0, (void**)&cDev->romSize);
			SndEmu_GetDeviceFunc(devInf->devDef, RWF_MEMORY | RWF_WRITE, DEVRW_BLOCK, 0, (void**)&cDev->romWrite);
			
			memset(&self->qsWork[chipID], 0x00, sizeof(QSOUND_WORK));
			if (devInf->devDef->coreID == FCC_MAME)
				cDev->flags &= ~0x01;	// MAME's old HLE doesn't need those hacks
			if (cDev->writeD16 != NULL)
				self->qsWork[chipID].write = VGMEngine_WriteQSound_A;
			else if (cDev->write8 != NULL)
				self->qsWork[chipID].write = VGMEngine_WriteQSound_B;
			else
				self->qsWork[chipID].write = NULL;
			break;
		case DEVID_WSWAN:
			retVal = SndEmu_Start2(chipType, devCfg, devInf, self->pe.userDevList, self->pe.devStartOpts);
			if (retVal)
				break;
			SndEmu_GetDeviceFunc(devInf->devDef, RWF_REGISTER | RWF_WRITE, DEVRW_A8D8, 0, (void**)&cDev->write8);
			SndEmu_GetDeviceFunc(devInf->devDef, RWF_MEMORY | RWF_WRITE, DEVRW_A16D8, 0, (void**)&cDev->writeM8);
			break;
		case DEVID_ES5506:
			retVal = SndEmu_Start2(chipType, devCfg, devInf, self->pe.userDevList, self->pe.devStartOpts);
			if (retVal)
				break;
			SndEmu_GetDeviceFunc(devInf->devDef, RWF_REGISTER | RWF_WRITE, DEVRW_A8D8, 0, (void**)&cDev->write8);
			SndEmu_GetDeviceFunc(devInf->devDef, RWF_REGISTER | RWF_WRITE, DEVRW_A8D16, 0, (void**)&cDev->writeD16);
			SndEmu_GetDeviceFunc(devInf->devDef, RWF_MEMORY | RWF_WRITE, DEVRW_BLOCK, 0, (void**)&cDev->romWrite);
			break;
		case DEVID_SCSP:
			retVal = SndEmu_Start2(chipType, devCfg, devInf, self->pe.userDevList, self->pe.devStartOpts);
			if (retVal)
				break;
			SndEmu_GetDeviceFunc(devInf->devDef, RWF_REGISTER | RWF_WRITE, DEVRW_A16D8, 0, (void**)&cDev->writeM8);
			SndEmu_GetDeviceFunc(devInf->devDef, RWF_REGISTER | RWF_WRITE, DEVRW_A16D16, 0, (void**)&cDev->writeM16);
			SndEmu_GetDeviceFunc(devInf->devDef, RWF_MEMORY | RWF_WRITE, DEVRW_BLOCK, 0, (void**)&cDev->romWrite);
			break;
		case DEVID_K005289:
			retVal = SndEmu_Start2(chipType, devCfg, devInf, self->pe.userDevList, self->pe.devStartOpts);
			if (retVal)
				break;
			SndEmu_GetDeviceFunc(devInf->devDef, RWF_REGISTER | RWF_WRITE, DEVRW_A8D16, 0, (void**)&cDev->writeD16);
			SndEmu_GetDeviceFunc(devInf->devDef, RWF_MEMORY | RWF_WRITE, DEVRW_BLOCK, 0, (void**)&cDev->romWrite);
			break;
		case DEVID_BSMT2000:
			retVal = SndEmu_Start2(chipType, devCfg, devInf, self->pe.userDevList, self->pe.devStartOpts);
			if (retVal)
				break;
			SndEmu_GetDeviceFunc(devInf->devDef, RWF_REGISTER | RWF_WRITE, DEVRW_A8D8, 0, (void**)&cDev->write8);
			SndEmu_GetDeviceFunc(devInf->devDef, RWF_REGISTER | RWF_QUICKWRITE, DEVRW_A8D16, 0, (void**)&cDev->writeD16);
			SndEmu_GetDeviceFunc(devInf->devDef, RWF_MEMORY | RWF_WRITE, DEVRW_MEMSIZE, 0, (void**)&cDev->romSize);
			SndEmu_GetDeviceFunc(devInf->devDef, RWF_MEMORY | RWF_WRITE, DEVRW_BLOCK, 0, (void**)&cDev->romWrite);
			break;
		default:
			if (chipType == DEVID_YM2612)
				cDev->flags |= devCfg->flags;
			else if (chipType == DEVID_C219)
				cDev->flags |= 0x01;	// enable 16-bit byteswap patch on all ROM data
			
			retVal = SndEmu_Start2(chipType, devCfg, devInf, self->pe.userDevList, self->pe.devStartOpts);
			if (retVal)
				break;
			SndEmu_GetDeviceFunc(devInf->devDef, RWF_REGISTER | RWF_READ, DEVRW_A8D8, 0, (void**)&cDev->read8);
			SndEmu_GetDeviceFunc(devInf->devDef, RWF_REGISTER | RWF_WRITE, DEVRW_A8D8, 0, (void**)&cDev->write8);
			SndEmu_GetDeviceFunc(devInf->devDef, RWF_REGISTER | RWF_WRITE, DEVRW_A16D8, 0, (void**)&cDev->writeM8);
			SndEmu_GetDeviceFunc(devInf->devDef, RWF_MEMORY | RWF_WRITE, DEVRW_MEMSIZE, 0, (void**)&cDev->romSize);
			SndEmu_GetDeviceFunc(devInf->devDef, RWF_MEMORY | RWF_WRITE, DEVRW_BLOCK, 0, (void**)&cDev->romWrite);
			break;
		}
		if (retVal)
		{
			devInf->dataPtr = NULL;
			devInf->devDef = NULL;
			continue;
		}
		sdCfg->deviceID = devIdx;
		
		// set name here, so that we can have logs during SetupLinkedDevices()
		self->devNames.data[devIdx] = SndEmu_GetDevName(chipType, 0x00, devCfg);	// use short name for now
		if (VGMEngine_GetChipCount(self, sdCfg->vgmChipType) > 1)
		{
			const char* devName = self->devNames.data[devIdx];
			if (dnBufPos + 1 >= dnBufSize)
			{
				self->devNames.data[devIdx] = &dnBufPtr[dnBufSize - 1];	// point to last byte of the buffer (null-terminator)
			}
			else
			{
				size_t maxLen = dnBufSize - dnBufPos;
				int printedCount = snprintf(&dnBufPtr[dnBufPos], maxLen, "%s #%u", devName, 1 + chipID);
				self->devNames.data[devIdx] = &dnBufPtr[dnBufPos];
				if (printedCount >= 0 && (size_t)printedCount <= maxLen)
					dnBufPos += printedCount + 1;
				else
					dnBufPos = dnBufSize;
			}
		}
		cDev->logCbData.player = self;
		cDev->logCbData.chipDevID = devIdx;
		
		{
			DEVLINK_CB_DATA dlCbData;
			dlCbData.player = self;
			dlCbData.sdCfg = sdCfg;
			dlCbData.chipDev = cDev;
			if (devInf->devDef->SetLogCB != NULL)	// allow for device link warnings
				devInf->devDef->SetLogCB(devInf->dataPtr, VGMEngine_SndEmuLogCB, &cDev->logCbData);
			SetupLinkedDevices(&cDev->base, &VGMEngine_DeviceLinkCallback, &dlCbData);
		}
		// already done by SndEmu_Start()
		//devInf->devDef->Reset(devInf->dataPtr);
		
		if (devOpts != NULL)
		{
			VGMEngine_RefreshDevOptions(self, cDev, devOpts);
			VGMEngine_RefreshMuting(self, cDev, &devOpts->muteOpts);
			VGMEngine_RefreshPanning(self, cDev, &devOpts->panOpts);
		}
		if (devInf->linkDevCount > 0 && devInf->linkDevs[0].devID == DEVID_AY8910)
		{
			VGM_BASEDEV* clDev = cDev->base.linkDev;
			size_t optID = VGMEngine_DeviceID2OptionID(self, PLR_DEV_ID(DEVID_AY8910, chipID));
			if (optID != (size_t)-1 && clDev != NULL && clDev->defInf.devDef->SetOptionBits != NULL)
				clDev->defInf.devDef->SetOptionBits(devInf->dataPtr, self->devOpts[optID].coreOpts);
		}

		self->vdDevMap[sdCfg->vgmChipType][chipID] = devIdx;
		if (cDev->optID != (size_t)-1)
			self->optDevMap[cDev->optID] = devIdx;
		devIdx ++;
	}	// end for (curChip)
	self->devices.size = devIdx;
	self->devNameBuffer.size = dnBufPos;
	
	// Initializing the resampler has to be done separately. (just in case we want to reallocations above)
	// The memory address of the RESMPL_STATE must not change in order to allow callbacks from the devices.
	for (curChip = 0; curChip < self->devices.size; curChip ++)
	{
		VGM_CHIPDEV* cDev = &self->devices.data[curChip];
		DEV_INFO* devInf = &cDev->base.defInf;
		const PLR_DEV_OPTS* devOpts = (cDev->optID != (size_t)-1) ? &self->devOpts[cDev->optID] : NULL;
		VGM_BASEDEV* clDev;
		UINT8 linkCntr = 0;
		
		if (devInf->devDef->SetLogCB != NULL)
			devInf->devDef->SetLogCB(devInf->dataPtr, VGMEngine_SndEmuLogCB, &cDev->logCbData);
		
		for (clDev = &cDev->base; clDev != NULL; clDev = clDev->linkDev, linkCntr ++)
		{
			UINT16 chipVol = VGMEngine_GetChipVolume(self, cDev->vgmChipType, cDev->chipID, linkCntr);
			UINT8 resmplMode = (devOpts != NULL) ? devOpts->resmplMode : RSMODE_LINEAR;
			
			Resmpl_SetVals(&clDev->resmpl, resmplMode, chipVol, self->pe.outSmplRate);
			Resmpl_DevConnect(&clDev->resmpl, &clDev->defInf);
			Resmpl_Init(&clDev->resmpl);
		}
		
		if (cDev->chipType == DEVID_YM3812)
		{
			if (VGMEngine_GetChipClock(self, cDev->vgmChipType, cDev->chipID) & 0x80000000)
			{
				// Dual-OPL with Stereo - 1st chip is panned to the left, 2nd chip is panned to the right
				for (clDev = &cDev->base; clDev != NULL; clDev = clDev->linkDev, linkCntr ++)
				{
					if (cDev->chipID & 0x01)
					{
						clDev->resmpl.volumeL = 0x00;
						clDev->resmpl.volumeR *= 2;
					}
					else
					{
						clDev->resmpl.volumeL *= 2;
						clDev->resmpl.volumeR = 0x00;
					}
				}
			}
		}
	}
	
	VGMEngine_NormalizeOverallVolume(self, VGMEngine_EstimateOverallVolume(self));
	
	return;
}

static void VGMEngine_DeviceLinkCallback(void* userParam, VGM_BASEDEV* cDev, DEVLINK_INFO* dLink)
{
	DEVLINK_CB_DATA* cbData = (DEVLINK_CB_DATA*)userParam;
	PE_VGM* self = cbData->player;
	//const VGM_DEVCFG* sdCfg = cbData->sdCfg;
	const VGM_CHIPDEV* chipDev = cbData->chipDev;
	const PLR_DEV_OPTS* devOpts = (chipDev->optID != (size_t)-1) ? &self->devOpts[chipDev->optID] : NULL;
	
	if (devOpts != NULL && devOpts->emuCore[1])
	{
		// set emulation core of linked device (OPN(A) SSG / OPL4 FM)
		dLink->cfg->emuCore = devOpts->emuCore[1];
	}
	else
	{
		if (dLink->devID == DEVID_AY8910)
			dLink->cfg->emuCore = FCC_EMU_;
		else if (dLink->devID == DEVID_YMF262)
			dLink->cfg->emuCore = FCC_ADLE;
	}
	
	if (dLink->devID == DEVID_AY8910)
	{
		AY8910_CFG* ayCfg = (AY8910_CFG*)dLink->cfg;
		if (chipDev->chipType == DEVID_YM2203)
			ayCfg->chipFlags = self->hdrBuffer[0x7A];	// YM2203 SSG flags
		else if (chipDev->chipType == DEVID_YM2608)
			ayCfg->chipFlags = self->hdrBuffer[0x7B];	// YM2608 SSG flags
	}
	
	return;
}

VGM_CHIPDEV* VGMEngine_GetDevicePtr(PE_VGM* self, UINT8 chipType, UINT8 chipID)
{
	if (chipType >= CHIP_COUNT || chipID >= 2)
		return NULL;
	
	size_t devID = self->vdDevMap[chipType][chipID];
	if (devID == (size_t)-1)
		return NULL;
	return &self->devices.data[devID];
}

static void VGMEngine_LoadOPL4ROM(PE_VGM* self, VGM_CHIPDEV* chipDev)
{
	static const char* romFile = "yrw801.rom";
	
	if (chipDev->romWrite == NULL)
		return;
	
	if (self->yrwRom.size == 0 || self->yrwRom.data == NULL)
	{
		if (self->pe.fileReqCbFunc == NULL)
			return;
		DATA_LOADER* romDLoad = self->pe.fileReqCbFunc(self->pe.fileReqCbParam, &self->pe, romFile);
		if (romDLoad == NULL)
			return;
		DataLoader_ReadAll(romDLoad);
		
		UINT32 yrwSize = DataLoader_GetSize(romDLoad);
		const UINT8* yrwData = DataLoader_GetData(romDLoad);
		PE_ARRAY_FREE(self->yrwRom);
		if (yrwSize > 0 && yrwData != NULL)
		{
			PE_ARRAY_MALLOC(self->yrwRom, UINT8, yrwSize)
			memcpy(self->yrwRom.data, yrwData, yrwSize);
		}
		DataLoader_Deinit(romDLoad);
	}
	if (self->yrwRom.size == 0 || self->yrwRom.data == NULL)
		return;
	
	if (chipDev->romSize != NULL)
		chipDev->romSize(chipDev->base.defInf.dataPtr, (UINT32)self->yrwRom.size);
	chipDev->romWrite(chipDev->base.defInf.dataPtr, 0x00, (UINT32)self->yrwRom.size, self->yrwRom.data);
	
	return;
}

UINT8 VGMEngine_Seek(PE_VGM* self, UINT8 unit, UINT32 pos)
{
	switch(unit)
	{
	case PLAYPOS_FILEOFS:
		self->playState |= PLAYSTATE_SEEK;
		if (pos < self->filePos)
			VGMEngine_Reset(self);
		return VGMEngine_SeekToFilePos(self, pos);
	case PLAYPOS_SAMPLE:
		pos = VGMEngine_Sample2Tick(self, pos);
		// fall through
	case PLAYPOS_TICK:
		self->playState |= PLAYSTATE_SEEK;
		if (pos < self->playTick)
			VGMEngine_Reset(self);
		return VGMEngine_SeekToTick(self, pos);
	case PLAYPOS_COMMAND:
	default:
		return 0xFF;
	}
}

UINT8 VGMEngine_SeekToTick(PE_VGM* self, UINT32 tick)
{
	self->playState |= PLAYSTATE_SEEK;
	if (tick > self->playTick)
		VGMEngine_ParseFile(self, tick - self->playTick);
	self->playSmpl = VGMEngine_Tick2Sample(self, self->playTick);
	self->playState &= ~PLAYSTATE_SEEK;
	return 0x00;
}

UINT8 VGMEngine_SeekToFilePos(PE_VGM* self, UINT32 pos)
{
	self->playState |= PLAYSTATE_SEEK;
	while(self->filePos < self->fileHdr.dataEnd && self->filePos <= pos && ! (self->playState & PLAYSTATE_END))
	{
		UINT8 curCmd = self->fileData[self->filePos];
		const COMMAND_INFO* cmdInfo = &VGMEngine_CMD_INFO[curCmd];
		cmdInfo->func(self, cmdInfo);
		self->filePos += cmdInfo->cmdLen;
	}
	self->playTick = self->fileTick;
	self->playSmpl = VGMEngine_Tick2Sample(self, self->playTick);
	
	if (self->filePos >= self->fileHdr.dataEnd)
	{
		self->playState |= PLAYSTATE_END;
		self->psTrigger |= PLAYSTATE_END;
		if (self->pe.eventCbFunc != NULL)
			self->pe.eventCbFunc(&self->pe, self->pe.eventCbParam, PLREVT_END, NULL);
		emu_logf(&self->logger, PLRLOG_WARN, "VGM file ends early! (filePos 0x%06X, end at 0x%06X)\n", self->filePos, self->fileHdr.dataEnd);
	}
	self->playState &= ~PLAYSTATE_SEEK;
	
	return 0x00;
}

UINT32 VGMEngine_Render(PE_VGM* self, UINT32 smplCnt, WAVE_32BS* data)
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
		smplFileTick = VGMEngine_Sample2Tick(self, self->playSmpl);
		VGMEngine_ParseFile(self, smplFileTick - self->playTick);
		
		// render as many samples at once as possible (for better performance)
		maxSmpl = VGMEngine_Tick2Sample(self, self->fileTick);
		smplStep = maxSmpl - self->playSmpl;
		// When DAC streams are active, limit step size to 1, so that DAC streams and sound chip emulation are in sync.
		if (smplStep < 1 || self->dacStreams.size != 0)
			smplStep = 1;	// must render at least 1 sample in order to advance
		if ((UINT32)smplStep > smplCnt - curSmpl)
			smplStep = smplCnt - curSmpl;
		
		for (curDev = 0; curDev < self->devices.size; curDev ++)
		{
			VGM_CHIPDEV* cDev = &self->devices.data[curDev];
			UINT8 disable = (cDev->optID != (size_t)-1) ? self->devOpts[cDev->optID].muteOpts.disable : 0x00;
			VGM_BASEDEV* clDev;
			
			for (clDev = &cDev->base; clDev != NULL; clDev = clDev->linkDev, disable >>= 1)
			{
				if (clDev->defInf.dataPtr != NULL && ! (disable & 0x01))
					Resmpl_Execute(&clDev->resmpl, smplStep, &data[curSmpl]);
			}
		}
		for (curDev = 0; curDev < self->dacStreams.size; curDev ++)
		{
			DEV_INFO* dacDInf = &self->dacStreams.data[curDev].defInf;
			dacDInf->devDef->Update(dacDInf->dataPtr, smplStep, NULL);
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

void VGMEngine_ParseFile(PE_VGM* self, UINT32 ticks)
{
	self->playTick += ticks;
	if (self->playState & PLAYSTATE_END)
		return;
	
	while(self->filePos < self->fileHdr.dataEnd && self->fileTick <= self->playTick && ! (self->playState & PLAYSTATE_END))
	{
		UINT8 curCmd = self->fileData[self->filePos];
		const COMMAND_INFO* cmdInfo = &VGMEngine_CMD_INFO[curCmd];
		cmdInfo->func(self, cmdInfo);
		self->filePos += cmdInfo->cmdLen;
	}
	
	if (self->p2612Fix & P2612FIX_ACTIVE)
	{
		self->p2612Fix &= ~P2612FIX_ACTIVE;	// disable Project2612 fix
		// Note: Due to the way the Legacy Mode is implemented in YM2612 GPGX right now,
		//       it should be no problem to keep it enabled during the whole song.
		//       But let's just turn it off for safety.
		
		size_t optID = self->devOptMap[DEVID_YM2612][0];
		size_t devID = (optID == (size_t)-1) ? (size_t)-1 : self->optDevMap[optID];
		// refresh options, removing OPT_YM2612_LEGACY_MODE
		if (devID < self->devices.size)
			VGMEngine_RefreshDevOptions(self, &self->devices.data[devID], &self->devOpts[optID]);
	}
	
	if (self->filePos >= self->fileHdr.dataEnd)
	{
		if (self->playState & PLAYSTATE_SEEK)	// recalculate playSmpl to fix state when triggering callbacks
			self->playSmpl = VGMEngine_Tick2Sample(self, self->fileTick);	// Note: fileTick results in more accurate position
		self->playState |= PLAYSTATE_END;
		self->psTrigger |= PLAYSTATE_END;
		if (self->pe.eventCbFunc != NULL)
			self->pe.eventCbFunc(&self->pe, self->pe.eventCbParam, PLREVT_END, NULL);
		emu_logf(&self->logger, PLRLOG_WARN, "VGM file ends early! (filePos 0x%06X, end at 0x%06X)\n", self->filePos, self->fileHdr.dataEnd);
	}
	
	return;
}

static void VGMEngine_ParseFileForFMClocks(PE_VGM* self)
{
	UINT32 filePos = self->fileHdr.dataOfs;

	self->v101ym2413clock = VGMEngine_GetHeaderChipClock(self, 0x01);
	self->v101ym2612clock = 0;
	self->v101ym2151clock = 0;

	while(filePos < self->fileHdr.dataEnd)
	{
		UINT8 curCmd = self->fileData[filePos];
		switch (curCmd)
		{
		case 0x66: // end of command data
			return;
		case 0x67: // data block
			filePos += 7 + (ReadLE32(&self->fileData[filePos + 0x03]) & 0x7FFFFFFF);
			break;
		case 0x51: // YM2413 register write
			return;
		case 0x52: // YM2612 register write, port 0
		case 0x53: // YM2612 register write, port 1
			self->v101ym2612clock = self->v101ym2413clock;
			self->v101ym2413clock = 0;
			return;
		case 0x54: // YM2151 register write
			self->v101ym2151clock = self->v101ym2413clock;
			self->v101ym2413clock = 0;
			return;
		default:
			if (VGMEngine_CMD_INFO[curCmd].cmdLen == 0)
				return;
			filePos += VGMEngine_CMD_INFO[curCmd].cmdLen;
			break;
		}
	}
}

static struct player_engine_vtable VGMEngine_vtbl =
{
	FCC_VGM,	// playerType
	"VGM",	// playerName
	
	(void*)VGMEngine_Init,
	(void*)VGMEngine_Deinit,
	
	(void*)VGMEngine_CanLoadFile,
	(void*)VGMEngine_LoadFile,
	(void*)VGMEngine_UnloadFile,
	
	(void*)VGMEngine_GetTags,
	(void*)VGMEngine_GetSongInfo,
	(void*)VGMEngine_GetSongDeviceInfo,

	(void*)VGMEngine_SetDeviceOptions,
	(void*)VGMEngine_GetDeviceOptions,
	(void*)VGMEngine_SetDeviceMuting,
	(void*)VGMEngine_GetDeviceMuting,

	(void*)VGMEngine_SetPlayerOptions,
	(void*)VGMEngine_GetPlayerOptions,
	
	PBaseEngine_GetSampleRate,
	(void*)VGMEngine_SetSampleRate,
	(void*)VGMEngine_GetPlaybackSpeed,
	(void*)VGMEngine_SetPlaybackSpeed,
	PBaseEngine_SetUserDevices,
	PBaseEngine_SetEventCallback,
	PBaseEngine_SetFileReqCallback,
	PBaseEngine_SetLogCallback,
	(void*)VGMEngine_Tick2Sample,
	(void*)VGMEngine_Sample2Tick,
	(void*)VGMEngine_Tick2Second,
	PBaseEngine_Sample2Second,
	
	(void*)VGMEngine_GetState,
	(void*)VGMEngine_GetCurPos,
	(void*)VGMEngine_GetCurLoop,
	(void*)VGMEngine_GetTotalTicks,
	(void*)VGMEngine_GetLoopTicks,
	PBaseEngine_GetTotalPlayTicks,
	
	(void*)VGMEngine_Start,
	(void*)VGMEngine_Stop,
	(void*)VGMEngine_Reset,
	(void*)VGMEngine_Seek,
	(void*)VGMEngine_Render,
};
