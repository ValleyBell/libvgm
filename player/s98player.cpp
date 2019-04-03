#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <vector>
#include <string>
#include <algorithm>

#define INLINE	static inline

#include <common_def.h>
#include "s98player.hpp"
#include <emu/EmuStructs.h>
#include <emu/SoundEmu.h>
#include <emu/Resampler.h>
#include <emu/SoundDevs.h>
#include <emu/EmuCores.h>
#include <emu/cores/sn764intf.h>	// for SN76496_CFG
#include <emu/cores/ayintf.h>		// for AY8910_CFG
#include "../utils/StrUtils.h"
#include "helper.h"


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
static const UINT8 S98_DEV_LIST[S98DEV_END] = {
	0xFF,
	DEVID_AY8910, DEVID_YM2203, DEVID_YM2612, DEVID_YM2608,
	DEVID_YM2151, DEVID_YM2413, DEVID_YM3526, DEVID_YM3812,
	DEVID_YMF262, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, DEVID_AY8910, DEVID_SN76496,
};


INLINE UINT16 ReadLE16(const UINT8* data)
{
	return (data[0x01] << 8) | (data[0x00] << 0);
}

INLINE UINT32 ReadLE32(const UINT8* data)
{
	return	(data[0x03] << 24) | (data[0x02] << 16) |
			(data[0x01] <<  8) | (data[0x00] <<  0);
}

S98Player::S98Player() :
	_filePos(0),
	_fileTick(0),
	_playTick(0),
	_playSmpl(0),
	_curLoop(0),
	_playState(0x00),
	_psTrigger(0x00)
{
	UINT8 retVal = CPConv_Init(&_cpcSJIS, "CP932", "UTF-8");
	if (retVal)
		_cpcSJIS = NULL;
}

S98Player::~S98Player()
{
	if (_cpcSJIS != NULL)
		CPConv_Deinit(_cpcSJIS);
}

UINT32 S98Player::GetPlayerType(void) const
{
	return FCC_S98;
}

const char* S98Player::GetPlayerName(void) const
{
	return "S98";
}

/*static*/ UINT8 S98Player::IsMyFile(FileLoader& fileLoader)
{
	fileLoader.ReadUntil(0x20);
	if (fileLoader.GetFileSize() < 0x20)
		return 0xF1;	// file too small
	if (memcmp(&fileLoader.GetFileData()[0x00], "S98", 3))
		return 0xF0;	// invalid signature
	return 0x00;
}

UINT8 S98Player::LoadFile(FileLoader& fileLoader)
{
	UINT32 devCount;
	UINT32 curDev;
	UINT32 curPos;
	
	_fLoad = NULL;
	fileLoader.ReadUntil(0x20);
	_fileData = &fileLoader.GetFileData()[0];
	if (fileLoader.GetFileSize() < 0x20 || memcmp(&_fileData[0x00], "S98", 3))
		return 0xF0;	// invalid file
	if (! (_fileData[0x03] >= '0' && _fileData[0x03] <= '3'))
		return 0xF1;	// unsupported version
	
	_fLoad = &fileLoader;
	_fLoad->ReadFullFile();
	_fileData = &fileLoader.GetFileData()[0];
	
	_fileHdr.fileVer = _fileData[0x03] - '0';
	_fileHdr.tickMult = ReadLE32(&_fileData[0x04]);
	_fileHdr.tickDiv = ReadLE32(&_fileData[0x08]);
	_fileHdr.compression = ReadLE32(&_fileData[0x0C]);
	_fileHdr.tagOfs = ReadLE32(&_fileData[0x10]);
	_fileHdr.dataOfs = ReadLE32(&_fileData[0x14]);
	_fileHdr.loopOfs = ReadLE32(&_fileData[0x18]);
	
	_devHdrs.clear();
	switch(_fileHdr.fileVer)
	{
	case 0:
		_fileHdr.tickMult = 0;
		// fall through
	case 1:
		_fileHdr.tickDiv = 0;
		// only default device available
		break;
	case 2:
		curPos = 0x20;
		for (devCount = 0; ; devCount ++, curPos += 0x10)
		{
			if (ReadLE32(&_fileData[curPos + 0x00]) == S98DEV_NONE)
				break;	// stop at device type 0
		}
		
		curPos = 0x20;
		_devHdrs.resize(devCount);
		for (curDev = 0; curDev < devCount; curDev ++, curPos += 0x10)
		{
			_devHdrs[curDev].devType = ReadLE32(&_fileData[curPos + 0x00]);
			_devHdrs[curDev].clock = ReadLE32(&_fileData[curPos + 0x04]);
			_devHdrs[curDev].pan = 0;
			_devHdrs[curDev].app_spec = ReadLE32(&_fileData[curPos + 0x0C]);
		}
		break;	// not supported yet
	case 3:
		devCount = ReadLE32(&_fileData[0x1C]);
		curPos = 0x20;
		_devHdrs.resize(devCount);
		for (curDev = 0; curDev < devCount; curDev ++, curPos += 0x10)
		{
			_devHdrs[curDev].devType = ReadLE32(&_fileData[curPos + 0x00]);
			_devHdrs[curDev].clock = ReadLE32(&_fileData[curPos + 0x04]);
			_devHdrs[curDev].pan = ReadLE32(&_fileData[curPos + 0x08]);
			_devHdrs[curDev].app_spec = 0;
		}
		break;
	}
	if (_devHdrs.empty())
	{
		_devHdrs.resize(1);
		curDev = 0;
		_devHdrs[curDev].devType = S98DEV_OPNA;
		_devHdrs[curDev].clock = 7987200;
		_devHdrs[curDev].pan = 0;
		_devHdrs[curDev].app_spec = 0;
	}
	
	if (! _fileHdr.tickMult)
		_fileHdr.tickMult = 10;
	if (! _fileHdr.tickDiv)
		_fileHdr.tickDiv = 1000;
	
	CalcSongLength();
	LoadTags();
	
	return 0x00;
}

void S98Player::CalcSongLength(void)
{
	UINT32 filePos;
	bool fileEnd;
	UINT8 curCmd;
	
	_totalTicks = 0;
	_loopTick = 0;
	
	fileEnd = false;
	filePos = _fileHdr.dataOfs;
	while(! fileEnd && filePos < _fLoad->GetFileSize())
	{
		if (filePos == _fileHdr.loopOfs)
			_loopTick = _totalTicks;
		
		curCmd = _fileData[filePos];
		filePos ++;
		switch(curCmd)
		{
		case 0xFF:	// advance 1 tick
			_totalTicks ++;
			break;
		case 0xFE:	// advance multiple ticks
			_totalTicks += 2 + ReadVarInt(filePos);
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

UINT8 S98Player::LoadTags(void)
{
	_tagData.clear();
	if (! _fileHdr.tagOfs)
		return 0x00;
	
	const char* startPtr;
	const char* endPtr;
	
	// find end of string (can be either '\0' or EOF)
	startPtr = (const char*)&_fileData[_fileHdr.tagOfs];
	endPtr = (const char*)memchr(startPtr, '\0', _fLoad->GetFileSize() - _fileHdr.tagOfs);
	if (endPtr == NULL)
		endPtr = (const char*)_fileData + _fLoad->GetFileSize();
	
	if (_fileHdr.fileVer < 3)
	{
		// tag offset = song title (\0-terminated)
		_tagData["TITLE"] = GetUTF8String(startPtr, endPtr);
	}
	else
	{
		std::string tagData;
		bool tagIsUTF8 = false;
		
		// tag offset = PSF tag
		if (endPtr - startPtr < 5 || memcmp(startPtr, "[S98]", 5))
		{
			fprintf(stderr, "Invalid S98 tag data!\n");
			fprintf(stderr, "tagData size: %zu, Signature: %.5s\n", endPtr - startPtr, startPtr);
			return 0xF0;
		}
		startPtr += 5;
		if (endPtr - startPtr >= 3)
		{
			if (startPtr[0] == 0xEF && startPtr[1] == 0xBB && startPtr[2] == 0xBF)	// check for UTF-8 BOM
			{
				tagIsUTF8 = true;
				startPtr += 3;
				fprintf(stderr, "Info: Tags are UTF-8 encoded.");
			}
		}
		
		if (! tagIsUTF8)
			tagData = GetUTF8String(startPtr, endPtr);
		else
			tagData.assign(startPtr, endPtr);
		ParsePSFTags(tagData);
	}
	
	return 0x00;
}

const char* S98Player::GetTagWithName(const std::string& name)
{
	std::map<std::string, std::string>::const_iterator mapIt;
	
	mapIt = _tagData.find(name);
	if (mapIt != _tagData.end())
		return mapIt->second.c_str();
	else
		return NULL;
}

std::string S98Player::GetUTF8String(const char* startPtr, const char* endPtr)
{
	if (_cpcSJIS != NULL)
	{
		size_t convSize = 0;
		char* convData = NULL;
		std::string result;
		UINT8 retVal;
		
		retVal = CPConv_StrConvert(_cpcSJIS, &convSize, &convData, endPtr - startPtr, startPtr);
		
		result.assign(convData, convData + convSize);
		free(convData);
		if (retVal < 0x80)
			return result;
	}
	// unable to convert - fallback using the original string
	return std::string(startPtr, endPtr);
}

static std::string TrimPSFTagWhitespace(const std::string& data)
{
	size_t posStart;
	size_t posEnd;
	
	// according to the PSF tag specification, all characters 0x01..0x20 are considered whitespace
	// http://wiki.neillcorlett.com/PSFTagFormat
	for (posStart = 0; posStart < data.length(); posStart ++)
	{
		if ((unsigned char)data[posStart] > 0x20)
			break;
	}
	for (posEnd = data.length(); posEnd > 0; posEnd --)
	{
		if ((unsigned char)data[posEnd - 1] > 0x20)
			break;
	}
	return data.substr(posStart, posEnd - posStart);
}

static UINT8 ExtractKeyValue(const std::string& line, std::string& retKey, std::string& retValue)
{
	size_t equalPos;
	
	equalPos = line.find('=');
	if (equalPos == std::string::npos)
		return 0xFF;	// not a "key=value" line
	
	retKey = line.substr(0, equalPos);
	retValue = line.substr(equalPos + 1);
	
	retKey = TrimPSFTagWhitespace(retKey);
	if (retKey.empty())
		return 0x01;	// invalid key
	retValue = TrimPSFTagWhitespace(retValue);
	
	return 0x00;
}

UINT8 S98Player::ParsePSFTags(const std::string& tagData)
{
	size_t lineStart;
	size_t lineEnd;
	std::string curLine;
	std::string curKey;
	std::string curVal;
	UINT8 retVal;
	
	lineStart = 0;
	while(lineStart < tagData.length())
	{
		lineEnd = tagData.find('\n', lineStart);
		if (lineEnd == std::string::npos)
			lineEnd = tagData.length();
		
		curLine = tagData.substr(lineStart, lineEnd - lineStart);
		retVal = ExtractKeyValue(curLine, curKey, curVal);
		if (! retVal)
		{
			std::map<std::string, std::string>::iterator mapIt;
			
			// keys are case insensitive, so let's make it uppercase
			std::transform(curKey.begin(), curKey.end(), curKey.begin(), ::toupper);
			mapIt = _tagData.find(curKey);
			if (mapIt == _tagData.end())
				_tagData[curKey] = curVal;	// new value
			else
				mapIt->second = mapIt->second + '\n' + curVal;	// multiline-value
		}
		
		lineStart = lineEnd + 1;
	}
	
	return 0x00;
}

UINT8 S98Player::UnloadFile(void)
{
	if (_playState & PLAYSTATE_PLAY)
		return 0xFF;
	
	_playState = 0x00;
	_fLoad = NULL;
	_fileData = NULL;
	_fileHdr.fileVer = 0xFF;
	_fileHdr.dataOfs = 0x00;
	_devHdrs.clear();
	_devices.clear();
	_tagData.clear();
	
	return 0x00;
}

const S98_HEADER* S98Player::GetFileHeader(void) const
{
	return &_fileHdr;
}

const char* S98Player::GetSongTitle(void)
{
	return GetTagWithName("TITLE");
}

const char* S98Player::GetSongAuthor(void)
{
	return GetTagWithName("ARTIST");
}

const char* S98Player::GetSongGame(void)
{
	return GetTagWithName("GAME");
}

const char* S98Player::GetSongSystem(void)
{
	return GetTagWithName("SYSTEM");
}

const char* S98Player::GetSongDate(void)
{
	return GetTagWithName("YEAR");
}

const char* S98Player::GetSongComment(void)
{
	return GetTagWithName("COMMENT");
}

UINT8 S98Player::SetSampleRate(UINT32 sampleRate)
{
	if (_playState & PLAYSTATE_PLAY)
		return 0x01;	// can't set during playback
	
	_outSmplRate = sampleRate;
	return 0x00;
}

/*UINT8 S98Player::SetPlaybackSpeed(double speed)
{
	return 0xFF;	// not yet supported
}*/


void S98Player::RefreshTSRates(void)
{
	_tsMult = _outSmplRate * _fileHdr.tickMult;
	_tsDiv = _fileHdr.tickDiv;
	
	return;
}

UINT32 S98Player::Tick2Sample(UINT32 ticks) const
{
	return (UINT32)(ticks * _tsMult / _tsDiv);
}

UINT32 S98Player::Sample2Tick(UINT32 samples) const
{
	return (UINT32)(samples * _tsDiv / _tsMult);
}

double S98Player::Tick2Second(UINT32 ticks) const
{
	return ticks * _fileHdr.tickMult / (double)_fileHdr.tickDiv;
}

UINT8 S98Player::GetState(void) const
{
	return _playState;
}

UINT32 S98Player::GetCurFileOfs(void) const
{
	return _filePos;
}

UINT32 S98Player::GetCurTick(void) const
{
	return _playTick;
}

UINT32 S98Player::GetCurSample(void) const
{
	return _playSmpl;
}

UINT32 S98Player::GetTotalTicks(void) const
{
	return _totalTicks;
}

UINT32 S98Player::GetLoopTicks(void) const
{
	if (! _fileHdr.loopOfs)
		return 0;
	else
		return _totalTicks - _loopTick;
}

UINT32 S98Player::GetCurrentLoop(void) const
{
	return _curLoop;
}


static void SetSSGCore(void* userParam, VGM_BASEDEV* cDev, DEVLINK_INFO* dLink)
{
	if (dLink->devID == DEVID_AY8910)
	{
		// possible AY8910 sound core selection here
	}
	
	return;
}

UINT8 S98Player::Start(void)
{
	size_t curDev;
	UINT8 retVal;
	
	_devices.clear();
	_devices.resize(_devHdrs.size());
	for (curDev = 0; curDev < _devHdrs.size(); curDev ++)
	{
		const S98_DEVICE* devHdr = &_devHdrs[curDev];
		S98_CHIPDEV* cDev = &_devices[curDev];
		DEV_GEN_CFG devCfg;
		VGM_BASEDEV* clDev;
		UINT8 deviceID;
		
		cDev->base.defInf.dataPtr = NULL;
		cDev->base.linkDev = NULL;
		devCfg.emuCore = 0x00;
		devCfg.srMode = DEVRI_SRMODE_NATIVE;
		devCfg.flags = 0x00;
		devCfg.clock = devHdr->clock;
		devCfg.smplRate = _outSmplRate;
		
		deviceID = (devHdr->devType < S98DEV_END) ? S98_DEV_LIST[devHdr->devType] : 0xFF;
		if (deviceID == 0xFF)
		{
			cDev->base.defInf.dataPtr = NULL;
			cDev->base.defInf.devDef = NULL;
			continue;
		}
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
				}
				else
				{
					ayCfg.chipType = AYTYPE_AY8910;
					ayCfg.chipFlags = 0x00;
					devCfg.clock /= 2;
				}
				
				retVal = SndEmu_Start(deviceID, (DEV_GEN_CFG*)&ayCfg, &cDev->base.defInf);
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
				
				retVal = SndEmu_Start(deviceID, (DEV_GEN_CFG*)&snCfg, &cDev->base.defInf);
			}
			break;
		default:
			retVal = SndEmu_Start(deviceID, &devCfg, &cDev->base.defInf);
			break;
		}
		if (retVal)
		{
			cDev->base.defInf.dataPtr = NULL;
			cDev->base.defInf.devDef = NULL;
			continue;
		}
		SndEmu_GetDeviceFunc(cDev->base.defInf.devDef, RWF_REGISTER | RWF_WRITE, DEVRW_A8D8, 0, (void**)&cDev->write);
		
		SetupLinkedDevices(&cDev->base, &SetSSGCore, this);
		
		for (clDev = &cDev->base; clDev != NULL; clDev = clDev->linkDev)
		{
			Resmpl_SetVals(&clDev->resmpl, 0xFF, 0x100, _outSmplRate);
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
	
	_playState |= PLAYSTATE_PLAY;
	Reset();
	if (_eventCbFunc != NULL)
		_eventCbFunc(this, _eventCbParam, PLREVT_START, NULL);
	
	return 0x00;
}

UINT8 S98Player::Stop(void)
{
	size_t curDev;
	
	_playState &= ~PLAYSTATE_PLAY;
	
	for (curDev = 0; curDev < _devices.size(); curDev ++)
	{
		S98_CHIPDEV* cDev = &_devices[curDev];
		FreeDeviceTree(&cDev->base, 0);
	}
	_devices.clear();
	if (_eventCbFunc != NULL)
		_eventCbFunc(this, _eventCbParam, PLREVT_STOP, NULL);
	
	return 0x00;
}

UINT8 S98Player::Reset(void)
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
		S98_CHIPDEV* cDev = &_devices[curDev];
		VGM_BASEDEV* clDev;
		
		cDev->base.defInf.devDef->Reset(cDev->base.defInf.dataPtr);
		for (clDev = &cDev->base; clDev != NULL; clDev = clDev->linkDev)
		{
			// TODO: Resmpl_Reset(&clDev->resmpl);
		}
		
		if (_devHdrs[curDev].devType == S98DEV_OPNA)
		{
			DEV_INFO* defInf = &cDev->base.defInf;
			DEVFUNC_WRITE_MEMSIZE SetRamSize = NULL;
			
			// setup DeltaT RAM size
			SndEmu_GetDeviceFunc(defInf->devDef, RWF_MEMORY | RWF_WRITE, DEVRW_MEMSIZE, 0, (void**)&SetRamSize);
			if (SetRamSize != NULL)
				SetRamSize(defInf->dataPtr, 0x40000);	// 256 KB
			
			// The YM2608 defaults to OPN mode. (YM2203 fallback),
			// so put it into OPNA mode (6 channels).
			cDev->write(defInf->dataPtr, 0, 0x29);
			cDev->write(defInf->dataPtr, 1, 0x80);
		}
	}
	
	return 0x00;
}

UINT32 S98Player::Render(UINT32 smplCnt, WAVE_32BS* data)
{
	UINT32 curSmpl;
	UINT32 smplFileTick;
	UINT32 maxSmpl;
	INT32 smplStep;	// might be negative due to rounding errors in Tick2Sample
	size_t curDev;
	
	curSmpl = 0;
	while(curSmpl < smplCnt)
	{
		smplFileTick = Sample2Tick(_playSmpl);
		ParseFile(smplFileTick - _playTick);
		
		// render as many samples at once as possible (for better performance)
		maxSmpl = Tick2Sample(_fileTick);
		smplStep = maxSmpl - _playSmpl;
		if (smplStep < 1)
			smplStep = 1;
		else if ((UINT32)smplStep > smplCnt - curSmpl)
			smplStep = smplCnt - curSmpl;
		
		for (curDev = 0; curDev < _devices.size(); curDev ++)
		{
			VGM_BASEDEV* clDev;
			
			for (clDev = &_devices[curDev].base; clDev != NULL; clDev = clDev->linkDev)
			{
				if (clDev->defInf.dataPtr != NULL)
					Resmpl_Execute(&clDev->resmpl, smplStep, &data[curSmpl]);
			}
		}
		curSmpl += smplStep;
		_playSmpl += smplStep;
		if (_psTrigger & PLAYSTATE_END)
		{
			_psTrigger &= ~PLAYSTATE_END;
			break;
		}
	}
	
	return curSmpl;
}

void S98Player::ParseFile(UINT32 ticks)
{
	_playTick += ticks;
	if (_playState & PLAYSTATE_END)
		return;
	
	while(_fileTick <= _playTick && ! (_playState & PLAYSTATE_END))
		DoCommand();
	
	return;
}

void S98Player::DoCommand(void)
{
	if (_filePos >= _fLoad->GetFileSize())
	{
		_playState |= PLAYSTATE_END;
		_psTrigger |= PLAYSTATE_END;
		if (_eventCbFunc != NULL)
			_eventCbFunc(this, _eventCbParam, PLREVT_END, NULL);
		fprintf(stderr, "S98 file ends early! (filePos 0x%06X, fileSize 0x%06X)\n", _filePos, _fLoad->GetFileSize());
		return;
	}
	
	UINT8 curCmd;
	
	curCmd = _fileData[_filePos];
	_filePos ++;
	switch(curCmd)
	{
	case 0xFF:	// advance 1 tick
		_fileTick ++;
		return;
	case 0xFE:	// advance multiple ticks
		_fileTick += 2 + ReadVarInt(_filePos);
		return;
	case 0xFD:
		if (! _fileHdr.loopOfs)
		{
			_playState |= PLAYSTATE_END;
			_psTrigger |= PLAYSTATE_END;
			if (_eventCbFunc != NULL)
				_eventCbFunc(this, _eventCbParam, PLREVT_END, NULL);
		}
		else
		{
			_curLoop ++;
			if (_eventCbFunc != NULL)
			{
				UINT8 retVal;
				
				retVal = _eventCbFunc(this, _eventCbParam, PLREVT_LOOP, &_curLoop);
				if (retVal == 0x01)
				{
					_playState |= PLAYSTATE_END;
					_psTrigger |= PLAYSTATE_END;
					return;
				}
			}
			_filePos = _fileHdr.loopOfs;
		}
		return;
	}
	
	{
		UINT8 deviceID = curCmd >> 1;
		if (deviceID < _devices.size())
		{
			S98_CHIPDEV* cDev = &_devices[deviceID];
			DEV_DATA* dataPtr = cDev->base.defInf.dataPtr;
			
			UINT8 port = curCmd & 0x01;
			UINT8 reg = _fileData[_filePos + 0x00];
			UINT8 data = _fileData[_filePos + 0x01];
			
			if (_devHdrs[deviceID].devType == S98DEV_DCSG)
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
		}
	}
	_filePos += 0x02;
	return;
}

UINT32 S98Player::ReadVarInt(UINT32& filePos)
{
	UINT32 tickVal = 0;
	UINT8 tickShift = 0;
	UINT8 moreFlag;
	
	do
	{
		moreFlag = _fileData[filePos] & 0x80;
		tickVal |= (_fileData[filePos] & 0x7F) << tickShift;
		tickShift += 7;
		filePos ++;
	} while(moreFlag);
	
	return tickVal;
}
