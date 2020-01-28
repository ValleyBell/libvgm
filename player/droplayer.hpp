#ifndef __DROPLAYER_HPP__
#define __DROPLAYER_HPP__

#include <stdtype.h>
#include <emu/EmuStructs.h>
#include <emu/Resampler.h>
#include "helper.h"
#include "playerbase.hpp"
#include "droplayerbase.h"
#include "../utils/DataLoader.h"
#include <vector>

class DROPlayer : public PlayerBase
{
public:
	DROPlayer();
	~DROPlayer();
	
	UINT32 GetPlayerType(void) const;
	const char* GetPlayerName(void) const;
	static UINT8 IsMyFile(DATA_LOADER *fileLoader);
	UINT8 LoadFile(DATA_LOADER *fileLoader);
	UINT8 UnloadFile(void);
	const DRO_HEADER* GetFileHeader(void) const;
	
	const char* const* GetTags(void);
	UINT8 GetSongInfo(PLR_SONG_INFO& songInf);
	UINT8 GetSongDeviceInfo(std::vector<PLR_DEV_INFO>& devInfList) const;
	UINT8 SetDeviceOptions(UINT32 id, const PLR_DEV_OPTS& devOpts);
	UINT8 GetDeviceOptions(UINT32 id, PLR_DEV_OPTS& devOpts) const;
	UINT8 SetDeviceMuting(UINT32 id, const PLR_MUTE_OPTS& muteOpts);
	UINT8 GetDeviceMuting(UINT32 id, PLR_MUTE_OPTS& muteOpts) const;
	UINT8 SetPlayerOptions(const DRO_PLAY_OPTIONS& playOpts);
	UINT8 GetPlayerOptions(DRO_PLAY_OPTIONS& playOpts) const;
	
	//UINT32 GetSampleRate(void) const;
	UINT8 SetSampleRate(UINT32 sampleRate);
	//UINT8 SetPlaybackSpeed(double speed);
	//void SetCallback(PLAYER_EVENT_CB cbFunc, void* cbParam);
	UINT32 Tick2Sample(UINT32 ticks) const;
	UINT32 Sample2Tick(UINT32 samples) const;
	double Tick2Second(UINT32 ticks) const;
	//double Sample2Second(UINT32 samples) const;
	
	UINT8 GetState(void) const;
	UINT32 GetCurPos(UINT8 unit) const;
	UINT32 GetCurLoop(void) const;
	UINT32 GetTotalTicks(void) const;	// get time for playing once in ticks
	UINT32 GetLoopTicks(void) const;	// get time for one loop in ticks
	//UINT32 GetTotalPlayTicks(UINT32 numLoops) const;	// get time for playing + looping (without fading)
	
	UINT8 Start(void);
	UINT8 Stop(void);
	UINT8 Reset(void);
	UINT8 Seek(UINT8 unit, UINT32 pos);
	UINT32 Render(UINT32 smplCnt, WAVE_32BS* data);
	
private:
	size_t DeviceID2OptionID(UINT32 id) const;
	void RefreshMuting(DRO_CHIPDEV& chipDev, const PLR_MUTE_OPTS& muteOpts);
	
	void ScanInitBlock(void);
	
	UINT8 SeekToTick(UINT32 tick);
	UINT8 SeekToFilePos(UINT32 pos);
	void ParseFile(UINT32 ticks);
	void DoCommand_v1(void);
	void DoCommand_v2(void);
	void DoFileEnd(void);
	void WriteReg(UINT8 port, UINT8 reg, UINT8 data);
	
	DATA_LOADER* _dLoad;
	const UINT8* _fileData;	// data pointer for quick access, equals _dLoad->GetFileData().data()
	
	DRO_HEADER _fileHdr;
	std::vector<UINT8> _devTypes;
	std::vector<UINT8> _devPanning;
	UINT8 _realHwType;
	UINT8 _portShift;	// 0 for OPL2 (1 port per chip), 1 for OPL3 (2 ports per chip)
	UINT8 _portMask;	// (1 << _portShift) - 1
	UINT32 _dataOfs;
	UINT32 _tickFreq;
	UINT32 _totalTicks;
	
	// information about the initialization block (ends with first delay)
	UINT32 _initBlkEndOfs;	// offset of end of init. block (for special fixes)
	std::vector<bool> _initRegSet;	// registers set during init. block
	UINT8 _initOPL3Enable;	// value of OPL3 enable register set during init. block
	
	//UINT32 _outSmplRate;
	
	// tick/sample conversion rates
	UINT64 _tsMult;
	UINT64 _tsDiv;
	
	DRO_PLAY_OPTIONS _playOpts;
	PLR_DEV_OPTS _devOpts[3];	// 0 = 1st OPL2, 1 = 2nd OPL2, 2 = 1st OPL3
	std::vector<DRO_CHIPDEV> _devices;
	size_t _optDevMap[3];	// maps _devOpts vector index to _devices vector
	
	UINT32 _filePos;
	UINT32 _fileTick;
	UINT32 _playTick;
	UINT32 _playSmpl;
	
	UINT8 _playState;
	UINT8 _psTrigger;	// used to temporarily trigger special commands
	UINT8 _selPort;		// currently selected OPL chip (for DRO v1)
	//PLAYER_EVENT_CB _eventCbFunc;
	//void* _eventCbParam;
};

#endif	// __DROPLAYER_HPP__
