#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include <string>

#include <iconv.h>

#define INLINE	static inline

#include <common_def.h>
#include "vgmplayer.hpp"
#include <emu/EmuStructs.h>
#include <emu/SoundEmu.h>
#include <emu/Resampler.h>
#include <emu/SoundDevs.h>
#include <emu/EmuCores.h>
#include <emu/dac_control.h>
#include <emu/cores/sn764intf.h>	// for SN76496_CFG
#include <emu/cores/segapcm.h>		// for SEGAPCM_CFG
#include <emu/cores/ayintf.h>		// for AY8910_CFG
#include <emu/cores/okim6258.h>		// for OKIM6258_CFG
#include <emu/cores/k054539.h>
#include <emu/cores/c140.h>
#include <emu/cores/es5503.h>
#include <emu/cores/es5506.h>
#include <emu/cores/c352.h>			// for C352_CFG

#include "../vgm/dblk_compr.h"
#include "helper.h"



/*static*/ const UINT8 VGMPlayer::_DEV_LIST[_CHIP_COUNT] =
{
	DEVID_SN76496, DEVID_YM2413, DEVID_YM2612, DEVID_YM2151, DEVID_SEGAPCM, DEVID_RF5C68, DEVID_YM2203, DEVID_YM2608,
	DEVID_YM2610, DEVID_YM3812, DEVID_YM3526, DEVID_Y8950, DEVID_YMF262, DEVID_YMF278B, DEVID_YMF271, DEVID_YMZ280B,
	DEVID_RF5C68, DEVID_32X_PWM, DEVID_AY8910, DEVID_GB_DMG, DEVID_NES_APU, DEVID_YMW258, DEVID_uPD7759, DEVID_OKIM6258,
	DEVID_OKIM6295, DEVID_K051649, DEVID_K054539, DEVID_C6280, DEVID_C140, DEVID_K053260, DEVID_POKEY, DEVID_QSOUND,
	DEVID_SCSP, DEVID_WSWAN, DEVID_VBOY_VSU, DEVID_SAA1099, DEVID_ES5503, DEVID_ES5506, DEVID_X1_010, DEVID_C352,
	DEVID_GA20,
};

/*static*/ const UINT32 VGMPlayer::_CHIPCLK_OFS[_CHIP_COUNT] =
{
	0x0C, 0x10, 0x2C, 0x30, 0x38, 0x40, 0x44, 0x48,
	0x4C, 0x50, 0x54, 0x58, 0x5C, 0x60, 0x64, 0x68,
	0x6C, 0x70, 0x74, 0x80, 0x84, 0x88, 0x8C, 0x90,
	0x98, 0x9C, 0xA0, 0xA4, 0xA8, 0xAC, 0xB0, 0xB4,
	0xB8, 0xC0, 0xC4, 0xC8, 0xCC, 0xD0, 0xD8, 0xDC,
	0xE0
};


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

VGMPlayer::VGMPlayer() :
	_filePos(0),
	_fileTick(0),
	_playTick(0),
	_playSmpl(0),
	_curLoop(0),
	_playState(0x00),
	_psTrigger(0x00)
{
	_icUTF16 = iconv_open("UTF-8", "UTF-16LE");
	
	return;
}

VGMPlayer::~VGMPlayer()
{
	if (_icUTF16 != (iconv_t)-1)
		iconv_close(_icUTF16);
}

UINT32 VGMPlayer::GetPlayerType(void) const
{
	return FCC_VGM;
}

const char* VGMPlayer::GetPlayerName(void) const
{
	return "VGM";
}

UINT8 VGMPlayer::LoadFile(const char* fileName)
{
	FILE* hFile;
	char fileSig[4];
	size_t readBytes;
	size_t fileSize;
	
	hFile = fopen(fileName, "rb");
	if (hFile == NULL)
		return 0xFF;
	
	fseek(hFile, 0, SEEK_END);
	fileSize = ftell(hFile);
	rewind(hFile);
	
	readBytes = fread(fileSig, 1, 4, hFile);
	if (readBytes < 4 || memcmp(fileSig, "Vgm ", 4) || fileSize < 0x38)
	{
		fclose(hFile);
		return 0xF0;	// invalid file
	}
	/*if (! (fileSig[3] >= '0' && fileSig[3] <= '3'))
	{
		fclose(hFile);
		return 0xF1;	// unsupported version
	}*/
	
	_fileData.resize(fileSize);
	memcpy(&_fileData[0], fileSig, 4);
	
	readBytes = 4 + fread(&_fileData[4], 1, fileSize - 4, hFile);
	if (readBytes < _fileData.size())
		_fileData.resize(readBytes);	// file was not completely read
	
	fclose(hFile);
	
	ParseHeader(&_fileHdr);
	
	LoadTags();
	
	return 0x00;
}

UINT8 VGMPlayer::ParseHeader(VGM_HEADER* vgmHdr)
{
	memset(vgmHdr, 0x00, sizeof(VGM_HEADER));
	
	vgmHdr->fileVer = ReadLE32(&_fileData[0x08]);
	
	vgmHdr->dataOfs = (vgmHdr->fileVer >= 0x150) ? ReadRelOfs(&_fileData[0], 0x34) : 0x00;
	if (! vgmHdr->dataOfs)
		vgmHdr->dataOfs = 0x40;
	_hdrLenFile = vgmHdr->dataOfs;
	
	vgmHdr->extraHdrOfs = (_hdrLenFile >= 0xC0) ? ReadRelOfs(&_fileData[0], 0xBC) : 0x00;
	if (vgmHdr->extraHdrOfs && _hdrLenFile > vgmHdr->extraHdrOfs)
		_hdrLenFile = vgmHdr->extraHdrOfs;	// the main header ends where the extra header begins
	
	if (_hdrLenFile > _HDR_BUF_SIZE)
		_hdrLenFile = _HDR_BUF_SIZE;
	memset(_hdrBuffer, 0x00, _HDR_BUF_SIZE);
	memcpy(_hdrBuffer, &_fileData[0x00], _hdrLenFile);
	
	vgmHdr->eofOfs = ReadRelOfs(_hdrBuffer, 0x04);
	vgmHdr->gd3Ofs = ReadRelOfs(_hdrBuffer, 0x14);
	vgmHdr->numTicks = ReadLE32(&_hdrBuffer[0x18]);
	vgmHdr->loopOfs = ReadRelOfs(_hdrBuffer, 0x1C);
	vgmHdr->loopTicks = ReadLE32(&_hdrBuffer[0x20]);
	
	if (vgmHdr->extraHdrOfs)
	{
		UINT32 xhLen;
		
		xhLen = ReadLE32(&_fileData[vgmHdr->extraHdrOfs]);
		if (xhLen >= 0x08)
			vgmHdr->xhChpClkOfs = ReadRelOfs(&_fileData[0], vgmHdr->extraHdrOfs + 0x04);
		if (xhLen >= 0x0C)
			vgmHdr->xhChpVolOfs = ReadRelOfs(&_fileData[0], vgmHdr->extraHdrOfs + 0x08);
	}
	
	if (! vgmHdr->eofOfs || vgmHdr->eofOfs > _fileData.size())
		vgmHdr->eofOfs = _fileData.size();	// catch invalid EOF values
	vgmHdr->dataEnd = vgmHdr->eofOfs;
	// command data ends at the GD3 offset if:
	//	GD3 is used && GD3 offset < EOF (just to be sure) && GD3 offset > dataOfs (catch files with GD3 between header and data)
	if (vgmHdr->gd3Ofs && (vgmHdr->gd3Ofs < vgmHdr->dataEnd && vgmHdr->gd3Ofs >= vgmHdr->dataOfs))
		vgmHdr->dataEnd = vgmHdr->gd3Ofs;
	
	return 0x00;
}

UINT8 VGMPlayer::LoadTags(void)
{
	_tagData.clear();
	if (! _fileHdr.gd3Ofs)
		return 0x00;
	
	size_t tagCount;
	UINT32 curPos;
	UINT32 eotPos;
	
	if (memcmp(&_fileData[_fileHdr.gd3Ofs + 0x00], "Gd3 ", 4))
		return 0xF0;	// bad tag
	
	_tagVer = ReadLE32(&_fileData[_fileHdr.gd3Ofs + 0x04]);
	if (_tagVer < 0x100 || _tagVer >= 0x200)
		return 0xF1;	// unsupported tag version
	tagCount = 11;
	
	_tagData.resize(tagCount);
	eotPos = ReadLE32(&_fileData[_fileHdr.gd3Ofs + 0x08]);
	if (eotPos > _fileHdr.eofOfs)
		eotPos = _fileHdr.eofOfs;
	
	curPos = _fileHdr.gd3Ofs + 0x0C;
	eotPos += curPos;
	for (size_t curTag = 0; curTag < tagCount; curTag ++)
	{
		UINT32 startPos = curPos;
		
		// search for UTF-16 L'\0' character
		while(curPos < eotPos && *(UINT16*)&_fileData[curPos] != 0x0000)
			curPos += 0x02;
		_tagData[curTag] = GetUTF8String(&_fileData[startPos], &_fileData[curPos]);
		curPos += 0x02;	// skip '\0'
	}
	
	return 0x00;
}

std::string VGMPlayer::GetUTF8String(const UINT8* startPtr, const UINT8* endPtr)
{
	size_t convSize = 0;
	char* convData = NULL;
	std::string result;
	UINT8 retVal;
	
	retVal = StrCharsetConv(_icUTF16, &convSize, &convData, endPtr - startPtr, (const char*)startPtr);
	
	result.assign(convData, convData + convSize);
	free(convData);
	return result;
}

UINT8 VGMPlayer::UnloadFile(void)
{
	if (_playState & PLAYSTATE_PLAY)
		return 0xFF;
	
	_playState = 0x00;
	_fileData.clear();
	_fileHdr.fileVer = 0xFF;
	_fileHdr.dataOfs = 0x00;
	_devices.clear();
	_tagData.clear();
	
	return 0x00;
}

const VGM_HEADER* VGMPlayer::GetFileHeader(void) const
{
	return &_fileHdr;
}

const char* VGMPlayer::GetSongTitle(void)
{
	if (_tagData.size() >= 1)
		return _tagData[0].c_str();
	else
		return NULL;
}

UINT8 VGMPlayer::SetSampleRate(UINT32 sampleRate)
{
	if (_playState & PLAYSTATE_PLAY)
		return 0x01;	// can't set during playback
	
	_outSmplRate = sampleRate;
	return 0x00;
}

/*UINT8 VGMPlayer::SetPlaybackSpeed(double speed)
{
	return 0xFF;	// not yet supported
}*/


void VGMPlayer::RefreshTSRates(void)
{
	_tsMult = _outSmplRate * 1;
	_tsDiv = 44100;
	
	return;
}

UINT32 VGMPlayer::Tick2Sample(UINT32 ticks) const
{
	return (UINT32)(ticks * _tsMult / _tsDiv);
}

UINT32 VGMPlayer::Sample2Tick(UINT32 samples) const
{
	return (UINT32)(samples * _tsDiv / _tsMult);
}

double VGMPlayer::Tick2Second(UINT32 ticks) const
{
	return ticks * 1 / 44100.0;
}

UINT8 VGMPlayer::GetState(void) const
{
	return _playState;
}

UINT32 VGMPlayer::GetCurFileOfs(void) const
{
	return _filePos;
}

UINT32 VGMPlayer::GetCurTick(void) const
{
	return _playTick;
}

UINT32 VGMPlayer::GetCurSample(void) const
{
	return _playSmpl;
}

UINT32 VGMPlayer::GetTotalTicks(void) const
{
	return _fileHdr.numTicks;
}

UINT32 VGMPlayer::GetLoopTicks(void) const
{
	if (! _fileHdr.loopOfs)
		return 0;
	else
		return _fileHdr.loopTicks;
}

UINT32 VGMPlayer::GetCurrentLoop(void) const
{
	return _curLoop;
}


UINT8 VGMPlayer::Start(void)
{
	size_t curStrm;
	
	InitDevices();
	
	_dacStreams.clear();
	for (curStrm = 0; curStrm < 0x100; curStrm ++)
		_dacStrmMap[curStrm] = (size_t)-1;
	//memset(&_pcmBank, 0x00, sizeof(PCM_BANK) * _PCM_BANK_COUNT);
	memset(&_pcmComprTbl, 0x00, sizeof(PCM_COMPR_TBL));
	
	_playState |= PLAYSTATE_PLAY;
	Reset();
	if (_eventCbFunc != NULL)
		_eventCbFunc(this, _eventCbParam, PLREVT_START, NULL);
	
	return 0x00;
}

UINT8 VGMPlayer::Stop(void)
{
	size_t curDev;
	size_t curBank;
	DEV_INFO* devInf;
	PCM_BANK* pcmBnk;
	
	_playState &= ~PLAYSTATE_PLAY;
	
	for (curDev = 0; curDev < _dacStreams.size(); curDev ++)
	{
		devInf = &_dacStreams[curDev].defInf;
		devInf->devDef->Stop(devInf->dataPtr);
		devInf->dataPtr = NULL;
	}
	_dacStreams.clear();
	
	for (curBank = 0x00; curBank < _PCM_BANK_COUNT; curBank ++)
	{
		pcmBnk = &_pcmBank[curBank];
		pcmBnk->bankOfs.clear();
		pcmBnk->bankSize.clear();
		pcmBnk->data.clear();
	}
	free(_pcmComprTbl.values.d8);	_pcmComprTbl.values.d8 = NULL;
	
	for (curDev = 0; curDev < _devices.size(); curDev ++)
		FreeDeviceTree(&_devices[curDev].base, 0);
	_devices.clear();
	if (_eventCbFunc != NULL)
		_eventCbFunc(this, _eventCbParam, PLREVT_STOP, NULL);
	
	return 0x00;
}

UINT8 VGMPlayer::Reset(void)
{
	size_t curDev;
	
	_filePos = _fileHdr.dataOfs;
	_fileTick = 0;
	_playTick = 0;
	_playSmpl = 0;
	_playState &= ~PLAYSTATE_END;
	_psTrigger = 0x00;
	_curLoop = 0;
	
	RefreshTSRates();
	
	for (curDev = 0; curDev < _devices.size(); curDev ++)
	{
		VGM_BASEDEV* clDev;
		
		clDev = &_devices[curDev].base;
		clDev->defInf.devDef->Reset(clDev->defInf.dataPtr);
		for (; clDev != NULL; clDev = clDev->linkDev)
		{
			// TODO: Resmpl_Reset(&clDev->resmpl);
		}
	}
	
	return 0x00;
}

UINT32 VGMPlayer::GetHeaderChipClock(UINT8 chipID)
{
	if (chipID >= _CHIP_COUNT)
		return 0;
	
	return ReadLE32(&_hdrBuffer[_CHIPCLK_OFS[chipID]]);
}

void VGMPlayer::InitDevices(void)
{
	UINT8 vgmChip;
	UINT8 chipType;
	UINT8 chipID;
	size_t curChip;
	
	UINT32 hdrClock;
	DEV_GEN_CFG devCfg;
	CHIP_DEVICE chipDev;
	VGM_BASEDEV* clDev;
	DEV_INFO* devInf;
	UINT8 retVal;
	
	_devices.clear();
	for (vgmChip = 0x00; vgmChip < _CHIP_COUNT; vgmChip ++)
	{
		for (chipID = 0; chipID < 2; chipID ++)
			_devMap[vgmChip][chipID] = (size_t)-1;
	}
	
	for (vgmChip = 0x00; vgmChip < _CHIP_COUNT; vgmChip ++)
	{
		hdrClock = GetHeaderChipClock(vgmChip);
		if (! hdrClock)
			continue;
		for (chipID = 0; chipID < 2; chipID ++)
		{
			if (chipID && ! (hdrClock & 0x40000000))
				continue;
			memset(&chipDev, 0x00, sizeof(CHIP_DEVICE));
			devInf = &chipDev.base.defInf;
			
			chipType = _DEV_LIST[vgmChip];
			chipDev.vgmChipType = vgmChip;
			chipDev.chipID = chipID;
			chipDev.base.defInf.dataPtr = NULL;
			chipDev.base.linkDev = NULL;
			devCfg.emuCore = 0x00;
			devCfg.srMode = DEVRI_SRMODE_NATIVE;
			devCfg.flags = (hdrClock & 0x80000000) >> 31;
			devCfg.clock = hdrClock & ~0xC0000000;
			devCfg.smplRate = _outSmplRate;
			switch(chipType)
			{
			case DEVID_SN76496:
				{
					SN76496_CFG snCfg;
					
					devCfg.emuCore = FCC_MAXM;
					devCfg.flags = 0x00;
					snCfg._genCfg = devCfg;
					snCfg.shiftRegWidth = _hdrBuffer[0x2A];
					if (! snCfg.shiftRegWidth)
						snCfg.shiftRegWidth = 0x10;
					snCfg.noiseTaps = ReadLE16(&_hdrBuffer[0x28]);
					if (! snCfg.noiseTaps)
						snCfg.noiseTaps = 0x09;
					snCfg.segaPSG = (_hdrBuffer[0x2B] & 0x01) ? 0 : 1;
					snCfg.negate = (_hdrBuffer[0x2B] & 0x02) ? 1 : 0;
					snCfg.stereo = (_hdrBuffer[0x2B] & 0x04) ? 0 : 1;
					snCfg.clkDiv = (_hdrBuffer[0x2B] & 0x08) ? 1 : 8;
					snCfg.t6w28_tone = NULL;
					
					if ((chipID & 0x01) && (hdrClock & 0x80000000))	// must be 2nd chip + T6W28 mode
					{
						CHIP_DEVICE* otherDev = GetDevicePtr(vgmChip, chipID ^ 0x01);
						if (otherDev != NULL)
							snCfg.t6w28_tone = otherDev->base.defInf.dataPtr;
					}
					
					retVal = SndEmu_Start(chipType, (DEV_GEN_CFG*)&snCfg, devInf);
					if (retVal)
						break;
					SndEmu_GetDeviceFunc(devInf->devDef, RWF_REGISTER | RWF_WRITE, DEVRW_A8D8, 0, (void**)&chipDev.write8);
					
					if (devInf->devDef->SetPanning != NULL)
					{
						INT16 panPos[4] = {0x00, -0x80, +0x80, 0x00};
						devInf->devDef->SetPanning(devInf->dataPtr, panPos);
					}
				}
				break;
			case DEVID_SEGAPCM:
				{
					SEGAPCM_CFG spCfg;
					
					spCfg._genCfg = devCfg;
					spCfg.bnkshift = _hdrBuffer[0x3C];
					spCfg.bnkmask = _hdrBuffer[0x3E];
					retVal = SndEmu_Start(chipType, (DEV_GEN_CFG*)&spCfg, devInf);
					if (retVal)
						break;
					SndEmu_GetDeviceFunc(devInf->devDef, RWF_REGISTER | RWF_WRITE, DEVRW_A16D8, 0, (void**)&chipDev.writeM8);
					SndEmu_GetDeviceFunc(devInf->devDef, RWF_MEMORY | RWF_WRITE, DEVRW_MEMSIZE, 0, (void**)&chipDev.romSize);
					SndEmu_GetDeviceFunc(devInf->devDef, RWF_MEMORY | RWF_WRITE, DEVRW_BLOCK, 0, (void**)&chipDev.romWrite);
				}
				break;
			case DEVID_RF5C68:
				if (vgmChip == 0x05)	// RF5C68
					devCfg.emuCore = FCC_MAME;
				else //if (vgmChip == 0x10)	// RF5C164
					devCfg.emuCore = FCC_GENS;
				retVal = SndEmu_Start(DEVID_RF5C68, &devCfg, devInf);
				if (retVal)
					break;
				SndEmu_GetDeviceFunc(devInf->devDef, RWF_REGISTER | RWF_WRITE, DEVRW_A8D8, 0, (void**)&chipDev.write8);
				SndEmu_GetDeviceFunc(devInf->devDef, RWF_MEMORY | RWF_WRITE, DEVRW_A16D8, 0, (void**)&chipDev.writeM8);
				SndEmu_GetDeviceFunc(devInf->devDef, RWF_MEMORY | RWF_WRITE, DEVRW_BLOCK, 0, (void**)&chipDev.romWrite);
				break;
			case DEVID_YM2610:
				retVal = SndEmu_Start(chipType, &devCfg, devInf);
				if (retVal)
					break;
				SndEmu_GetDeviceFunc(devInf->devDef, RWF_REGISTER | RWF_WRITE, DEVRW_A8D8, 0, (void**)&chipDev.write8);
				SndEmu_GetDeviceFunc(devInf->devDef, RWF_MEMORY | RWF_WRITE, DEVRW_MEMSIZE, 'A', (void**)&chipDev.romSize);
				SndEmu_GetDeviceFunc(devInf->devDef, RWF_MEMORY | RWF_WRITE, DEVRW_BLOCK, 'A', (void**)&chipDev.romWrite);
				SndEmu_GetDeviceFunc(devInf->devDef, RWF_MEMORY | RWF_WRITE, DEVRW_MEMSIZE, 'B', (void**)&chipDev.romSizeB);
				SndEmu_GetDeviceFunc(devInf->devDef, RWF_MEMORY | RWF_WRITE, DEVRW_BLOCK, 'B', (void**)&chipDev.romWriteB);
				break;
			case DEVID_YMF278B:
				retVal = SndEmu_Start(chipType, &devCfg, devInf);
				if (retVal)
					break;
				SndEmu_GetDeviceFunc(devInf->devDef, RWF_REGISTER | RWF_WRITE, DEVRW_A8D8, 0, (void**)&chipDev.write8);
				SndEmu_GetDeviceFunc(devInf->devDef, RWF_MEMORY | RWF_WRITE, DEVRW_MEMSIZE, 0x524F, (void**)&chipDev.romSize);
				SndEmu_GetDeviceFunc(devInf->devDef, RWF_MEMORY | RWF_WRITE, DEVRW_BLOCK, 0x524F, (void**)&chipDev.romWrite);
				SndEmu_GetDeviceFunc(devInf->devDef, RWF_MEMORY | RWF_WRITE, DEVRW_MEMSIZE, 0x5241, (void**)&chipDev.romSizeB);
				SndEmu_GetDeviceFunc(devInf->devDef, RWF_MEMORY | RWF_WRITE, DEVRW_BLOCK, 0x5241, (void**)&chipDev.romWriteB);
				LoadOPL4ROM(&chipDev);
				break;
			case DEVID_AY8910:
				{
					AY8910_CFG ayCfg;
					
					devCfg.emuCore = FCC_EMU_;
					ayCfg._genCfg = devCfg;
					ayCfg.chipType = _hdrBuffer[0x78];
					ayCfg.chipFlags = _hdrBuffer[0x79];
					
					retVal = SndEmu_Start(chipType, (DEV_GEN_CFG*)&ayCfg, devInf);
					if (retVal)
						break;
					SndEmu_GetDeviceFunc(devInf->devDef, RWF_REGISTER | RWF_WRITE, DEVRW_A8D8, 0, (void**)&chipDev.write8);
					
					if (devInf->devDef->SetPanning != NULL)
					{
						INT16 panPos[3] = {-0x80, +0x80, 0x00};
						devInf->devDef->SetPanning(devInf->dataPtr, panPos);
					}
				}
				break;
			case DEVID_32X_PWM:
				retVal = SndEmu_Start(chipType, &devCfg, devInf);
				if (retVal)
					break;
				SndEmu_GetDeviceFunc(devInf->devDef, RWF_REGISTER | RWF_WRITE, DEVRW_A8D16, 0, (void**)&chipDev.writeD16);
				break;
			case DEVID_YMW258:
				retVal = SndEmu_Start(chipType, &devCfg, devInf);
				if (retVal)
					break;
				SndEmu_GetDeviceFunc(devInf->devDef, RWF_REGISTER | RWF_WRITE, DEVRW_A8D8, 0, (void**)&chipDev.write8);
				SndEmu_GetDeviceFunc(devInf->devDef, RWF_REGISTER | RWF_WRITE, DEVRW_A8D16, 0, (void**)&chipDev.writeD16);
				SndEmu_GetDeviceFunc(devInf->devDef, RWF_MEMORY | RWF_WRITE, DEVRW_MEMSIZE, 0, (void**)&chipDev.romSize);
				SndEmu_GetDeviceFunc(devInf->devDef, RWF_MEMORY | RWF_WRITE, DEVRW_BLOCK, 0, (void**)&chipDev.romWrite);
				break;
			case DEVID_OKIM6258:
				{
					OKIM6258_CFG okiCfg;
					
					okiCfg._genCfg = devCfg;
					okiCfg.divider = (_hdrBuffer[0x94] & 0x03) >> 0;
					okiCfg.adpcmBits = (_hdrBuffer[0x94] & 0x04) ? OKIM6258_ADPCM_4B : OKIM6258_ADPCM_3B;
					okiCfg.outputBits = (_hdrBuffer[0x94] & 0x08) ? OKIM6258_OUT_12B : OKIM6258_OUT_10B;
					
					retVal = SndEmu_Start(chipType, (DEV_GEN_CFG*)&okiCfg, devInf);
					if (retVal)
						break;
					SndEmu_GetDeviceFunc(devInf->devDef, RWF_REGISTER | RWF_WRITE, DEVRW_A8D8, 0, (void**)&chipDev.write8);
				}
				break;
			case DEVID_K054539:
				{
					if (devCfg.clock < 1000000)	// if < 1 MHz, then it's the sample rate, not the clock
						devCfg.clock *= 384;	// (for backwards compatibility with old VGM logs from 2012/13)
					devCfg.flags = _hdrBuffer[0x95];
					
					retVal = SndEmu_Start(chipType, &devCfg, devInf);
					if (retVal)
						break;
					SndEmu_GetDeviceFunc(devInf->devDef, RWF_REGISTER | RWF_WRITE, DEVRW_A16D8, 0, (void**)&chipDev.writeM8);
					SndEmu_GetDeviceFunc(devInf->devDef, RWF_MEMORY | RWF_WRITE, DEVRW_MEMSIZE, 0, (void**)&chipDev.romSize);
					SndEmu_GetDeviceFunc(devInf->devDef, RWF_MEMORY | RWF_WRITE, DEVRW_BLOCK, 0, (void**)&chipDev.romWrite);
				}
				break;
			case DEVID_C140:
				{
					if (devCfg.clock < 1000000)	// if < 1 MHz, then it's the sample rate, not the clock
						devCfg.clock *= 384;	// (for backwards compatibility with old VGM logs from 2013/14)
					devCfg.flags = _hdrBuffer[0x96];	// banking type
					
					retVal = SndEmu_Start(chipType, &devCfg, devInf);
					if (retVal)
						break;
					SndEmu_GetDeviceFunc(devInf->devDef, RWF_REGISTER | RWF_WRITE, DEVRW_A16D8, 0, (void**)&chipDev.writeM8);
					SndEmu_GetDeviceFunc(devInf->devDef, RWF_MEMORY | RWF_WRITE, DEVRW_MEMSIZE, 0, (void**)&chipDev.romSize);
					SndEmu_GetDeviceFunc(devInf->devDef, RWF_MEMORY | RWF_WRITE, DEVRW_BLOCK, 0, (void**)&chipDev.romWrite);
				}
				break;
			case DEVID_C352:
				{
					C352_CFG cCfg;
					
					cCfg._genCfg = devCfg;
					cCfg.clk_divider = (UINT16)_hdrBuffer[0xD6] * 4;
					
					retVal = SndEmu_Start(chipType, (DEV_GEN_CFG*)&cCfg, devInf);
					if (retVal)
						break;
					SndEmu_GetDeviceFunc(devInf->devDef, RWF_REGISTER | RWF_WRITE, DEVRW_A16D16, 0, (void**)&chipDev.writeM16);
					SndEmu_GetDeviceFunc(devInf->devDef, RWF_MEMORY | RWF_WRITE, DEVRW_MEMSIZE, 0, (void**)&chipDev.romSize);
					SndEmu_GetDeviceFunc(devInf->devDef, RWF_MEMORY | RWF_WRITE, DEVRW_BLOCK, 0, (void**)&chipDev.romWrite);
				}
				break;
			case DEVID_QSOUND:
				if (devCfg.clock < 5000000)	// QSound clock used to be 4 MHz
					devCfg.clock = devCfg.clock * 15;	// 4 MHz -> 60 MHz
				devCfg.emuCore = FCC_CTR_;
				retVal = SndEmu_Start(chipType, &devCfg, devInf);
				if (retVal)
					break;
				SndEmu_GetDeviceFunc(devInf->devDef, RWF_REGISTER | RWF_WRITE, DEVRW_A8D8, 0, (void**)&chipDev.write8);
				SndEmu_GetDeviceFunc(devInf->devDef, RWF_REGISTER | RWF_QUICKWRITE, DEVRW_A8D16, 0, (void**)&chipDev.writeD16);
				SndEmu_GetDeviceFunc(devInf->devDef, RWF_MEMORY | RWF_WRITE, DEVRW_MEMSIZE, 0, (void**)&chipDev.romSize);
				SndEmu_GetDeviceFunc(devInf->devDef, RWF_MEMORY | RWF_WRITE, DEVRW_BLOCK, 0, (void**)&chipDev.romWrite);
				break;
			case DEVID_WSWAN:
				retVal = SndEmu_Start(chipType, &devCfg, devInf);
				if (retVal)
					break;
				SndEmu_GetDeviceFunc(devInf->devDef, RWF_REGISTER | RWF_WRITE, DEVRW_A8D8, 0, (void**)&chipDev.write8);
				SndEmu_GetDeviceFunc(devInf->devDef, RWF_MEMORY | RWF_WRITE, DEVRW_A16D8, 0, (void**)&chipDev.writeM8);
				break;
			case DEVID_ES5503:
				{
					devCfg.flags = _hdrBuffer[0xD4];	// output channels
					
					retVal = SndEmu_Start(chipType, &devCfg, devInf);
					if (retVal)
						break;
					SndEmu_GetDeviceFunc(devInf->devDef, RWF_REGISTER | RWF_WRITE, DEVRW_A8D8, 0, (void**)&chipDev.write8);
					SndEmu_GetDeviceFunc(devInf->devDef, RWF_MEMORY | RWF_WRITE, DEVRW_BLOCK, 0, (void**)&chipDev.romWrite);
				}
				break;
			case DEVID_ES5506:
				{
					devCfg.flags = _hdrBuffer[0xD5];	// output channels
					
					retVal = SndEmu_Start(chipType, &devCfg, devInf);
					if (retVal)
						break;
					SndEmu_GetDeviceFunc(devInf->devDef, RWF_REGISTER | RWF_WRITE, DEVRW_A8D8, 0, (void**)&chipDev.write8);
					SndEmu_GetDeviceFunc(devInf->devDef, RWF_REGISTER | RWF_WRITE, DEVRW_A8D16, 0, (void**)&chipDev.writeD16);
					SndEmu_GetDeviceFunc(devInf->devDef, RWF_MEMORY | RWF_WRITE, DEVRW_BLOCK, 0, (void**)&chipDev.romWrite);
				}
				break;
			case DEVID_SCSP:
				if (devCfg.clock < 1000000)	// if < 1 MHz, then it's the sample rate, not the clock
					devCfg.clock *= 512;	// (for backwards compatibility with old VGM logs from 2012-14)
				retVal = SndEmu_Start(chipType, &devCfg, devInf);
				if (retVal)
					break;
				SndEmu_GetDeviceFunc(devInf->devDef, RWF_REGISTER | RWF_WRITE, DEVRW_A16D8, 0, (void**)&chipDev.writeM8);
				SndEmu_GetDeviceFunc(devInf->devDef, RWF_REGISTER | RWF_WRITE, DEVRW_A16D16, 0, (void**)&chipDev.writeM16);
				SndEmu_GetDeviceFunc(devInf->devDef, RWF_MEMORY | RWF_WRITE, DEVRW_BLOCK, 0, (void**)&chipDev.romWrite);
				break;
			default:
				if (chipType == DEVID_YM2612)
					devCfg.emuCore = FCC_GPGX;
				else if (chipType == DEVID_YM3812)
					devCfg.emuCore = FCC_ADLE;
				else if (chipType == DEVID_YMF262)
					devCfg.emuCore = FCC_NUKE;
				else if (chipType == DEVID_NES_APU)
					devCfg.emuCore = FCC_NSFP;
				else if (chipType == DEVID_C6280)
					devCfg.emuCore = FCC_OOTK;
				else if (chipType == DEVID_SAA1099)
					devCfg.emuCore = FCC_MAME;
				
				retVal = SndEmu_Start(chipType, &devCfg, devInf);
				if (retVal)
					break;
				SndEmu_GetDeviceFunc(devInf->devDef, RWF_REGISTER | RWF_WRITE, DEVRW_A8D8, 0, (void**)&chipDev.write8);
				SndEmu_GetDeviceFunc(devInf->devDef, RWF_REGISTER | RWF_WRITE, DEVRW_A16D8, 0, (void**)&chipDev.writeM8);
				SndEmu_GetDeviceFunc(devInf->devDef, RWF_MEMORY | RWF_WRITE, DEVRW_MEMSIZE, 0, (void**)&chipDev.romSize);
				SndEmu_GetDeviceFunc(devInf->devDef, RWF_MEMORY | RWF_WRITE, DEVRW_BLOCK, 0, (void**)&chipDev.romWrite);
				if (chipType == DEVID_YM2612)
					devInf->devDef->SetOptionBits(devInf->dataPtr, 0x80);	// enable legacy mode
				else if (chipType == DEVID_GB_DMG)
					devInf->devDef->SetOptionBits(devInf->dataPtr, 0x80);	// enable legacy mode
				break;
			}
			if (retVal)
			{
				devInf->dataPtr = NULL;
				devInf->devDef = NULL;
				continue;
			}
			
			SetupLinkedDevices(&chipDev.base, &DeviceLinkCallback, this);
			// already done by SndEmu_Start()
			//devInf->devDef->Reset(devInf->dataPtr);
			
			_devMap[vgmChip][chipID] = _devices.size();
			_devices.push_back(chipDev);
		}	// for (chipID)
	}	// end for (vgmChip)
	
	// Initializing the resampler has to be done separately due to reallocations happening above
	// and the memory address of the RESMPL_STATE mustn't change in order to allow callbacks from the devices.
	for (curChip = 0; curChip < _devices.size(); curChip ++)
	{
		for (clDev = &_devices[curChip].base; clDev != NULL; clDev = clDev->linkDev)
		{
			Resmpl_SetVals(&clDev->resmpl, 0xFF, 0x100, _outSmplRate);
			Resmpl_DevConnect(&clDev->resmpl, &clDev->defInf);
			Resmpl_Init(&clDev->resmpl);
		}
	}
	
	return;
}

/*static*/ void VGMPlayer::DeviceLinkCallback(void* userParam, VGM_BASEDEV* cDev, DEVLINK_INFO* dLink)
{
	DEVLINK_CB_DATA* cbData = (DEVLINK_CB_DATA*)userParam;
	VGMPlayer* oThis = cbData->object;
	
	if (dLink->devID == DEVID_AY8910)
		dLink->cfg->emuCore = FCC_EMU_;
	else if (dLink->devID == DEVID_YMF262)
		dLink->cfg->emuCore = FCC_ADLE;
	
	if (dLink->devID == DEVID_AY8910)
	{
		AY8910_CFG* ayCfg = (AY8910_CFG*)dLink->cfg;
		if (cbData->chipType == DEVID_YM2203)
			ayCfg->chipFlags = oThis->_hdrBuffer[0x7A];	// YM2203 SSG flags
		else if (cbData->chipType == DEVID_YM2608)
			ayCfg->chipFlags = oThis->_hdrBuffer[0x7B];	// YM2608 SSG flags
	}
	
	return;
}

VGMPlayer::CHIP_DEVICE* VGMPlayer::GetDevicePtr(UINT8 chipType, UINT8 chipID)
{
	if (chipType >= _CHIP_COUNT || chipID >= 2)
		return NULL;
	
	size_t devID = _devMap[chipType][chipID];
	if (devID == (size_t)-1)
		return NULL;
	return &_devices[devID];
}

void VGMPlayer::LoadOPL4ROM(CHIP_DEVICE* chipDev)
{
	if (chipDev->romWrite == NULL)
		return;
	
	const char* romFile = "yrw801.rom";
	FILE* hFile;
	std::vector<UINT8> yrwData;
	
	hFile = fopen(romFile, "rb");
	if (hFile == NULL)
	{
		printf("Warning: Couldn't load %s!\n", romFile);
		return;
	}
	
	fseek(hFile, 0, SEEK_END);
	yrwData.resize(ftell(hFile));
	rewind(hFile);
	fread(&yrwData[0], 1, yrwData.size(), hFile);
	fclose(hFile);
	
	chipDev->romSize(chipDev->base.defInf.dataPtr, yrwData.size());
	chipDev->romWrite(chipDev->base.defInf.dataPtr, 0x00, yrwData.size(), &yrwData[0]);
	yrwData.clear();
	
	return;
}

UINT32 VGMPlayer::Render(UINT32 smplCnt, WAVE_32BS* data)
{
	UINT32 curSmpl;
	UINT32 smplFileTick;
	size_t curDev;
	
	for (curSmpl = 0; curSmpl < smplCnt; curSmpl ++)
	{
		smplFileTick = Sample2Tick(_playSmpl);
		ParseFile(smplFileTick - _playTick);
		_playSmpl ++;
		
		for (curDev = 0; curDev < _devices.size(); curDev ++)
		{
			VGM_BASEDEV* clDev;
			
			for (clDev = &_devices[curDev].base; clDev != NULL; clDev = clDev->linkDev)
			{
				if (clDev->defInf.dataPtr != NULL)
					Resmpl_Execute(&clDev->resmpl, 1, &data[curSmpl]);
			}
		}
		for (curDev = 0; curDev < _dacStreams.size(); curDev ++)
		{
			DEV_INFO* dacDInf = &_dacStreams[curDev].defInf;
			dacDInf->devDef->Update(dacDInf->dataPtr, 1, NULL);
		}
		
		if (_psTrigger & PLAYSTATE_END)
		{
			_psTrigger &= ~PLAYSTATE_END;
			return curSmpl + 1;
		}
	}
	
	return curSmpl;
}

void VGMPlayer::ParseFile(UINT32 ticks)
{
	_playTick += ticks;
	if (_playState & PLAYSTATE_END)
		return;
	
	while(_fileTick <= _playTick && ! (_playState & PLAYSTATE_END))
	{
		UINT8 curCmd = _fileData[_filePos];
		COMMAND_FUNC func = _CMD_INFO[curCmd].func;
		(this->*func)();
		_filePos += _CMD_INFO[curCmd].cmdLen;
	}
	
	return;
}