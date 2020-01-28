#ifndef __PLAYERBASE_HPP__
#define __PLAYERBASE_HPP__

#include <stdtype.h>
#include <emu/Resampler.h>
#include "../utils/DataLoader.h"
#include "playerbase.h"
#include <vector>

// callback functions and event constants
class PlayerBase;
typedef UINT8 (*PLAYER_EVENT_CB)(PlayerBase* player, void* userParam, UINT8 evtType, void* evtParam);

//	--- concept ---
//	- Player class does file rendering at fixed volume (but changeable speed)
//	- host program handles master volume + fading + stopping after X loops (notified via callback)

class PlayerBase
{
public:
	PlayerBase();
	virtual ~PlayerBase();
	
	virtual UINT32 GetPlayerType(void) const;
	virtual const char* GetPlayerName(void) const;
	static UINT8 IsMyFile(DATA_LOADER *dataLoader);
	virtual UINT8 LoadFile(DATA_LOADER *dataLoader) = 0;
	virtual UINT8 UnloadFile(void) = 0;
	
	virtual const char* const* GetTags(void) = 0;
	virtual UINT8 GetSongInfo(PLR_SONG_INFO& songInf) = 0;
	virtual UINT8 GetSongDeviceInfo(std::vector<PLR_DEV_INFO>& devInfList) const = 0;
	static UINT8 InitDeviceOptions(PLR_DEV_OPTS& devOpts);
	virtual UINT8 SetDeviceOptions(UINT32 id, const PLR_DEV_OPTS& devOpts) = 0;
	virtual UINT8 GetDeviceOptions(UINT32 id, PLR_DEV_OPTS& devOpts) const = 0;
	virtual UINT8 SetDeviceMuting(UINT32 id, const PLR_MUTE_OPTS& muteOpts) = 0;
	virtual UINT8 GetDeviceMuting(UINT32 id, PLR_MUTE_OPTS& muteOpts) const = 0;
	// player-specific options
	//virtual UINT8 SetPlayerOptions(const ###_PLAY_OPTIONS& playOpts) = 0;
	//virtual UINT8 GetPlayerOptions(###_PLAY_OPTIONS& playOpts) const = 0;
	
	virtual UINT32 GetSampleRate(void) const;
	virtual UINT8 SetSampleRate(UINT32 sampleRate);
	virtual UINT8 SetPlaybackSpeed(double speed);
	virtual void SetCallback(PLAYER_EVENT_CB cbFunc, void* cbParam);
	virtual UINT32 Tick2Sample(UINT32 ticks) const = 0;
	virtual UINT32 Sample2Tick(UINT32 samples) const = 0;
	virtual double Tick2Second(UINT32 ticks) const = 0;
	virtual double Sample2Second(UINT32 samples) const;
	
	virtual UINT8 GetState(void) const = 0;			// get playback state (playing / paused / ...)
	virtual UINT32 GetCurPos(UINT8 unit) const = 0;	// get current playback position
	virtual UINT32 GetCurLoop(void) const = 0;		// get current loop index (0 = 1st loop, 1 = 2nd loop, ...)
	virtual UINT32 GetTotalTicks(void) const = 0;	// get time for playing once in ticks
	virtual UINT32 GetLoopTicks(void) const = 0;	// get time for one loop in ticks
	virtual UINT32 GetTotalPlayTicks(UINT32 numLoops) const;	// get time for playing + looping (without fading)
	
	virtual UINT8 Start(void) = 0;
	virtual UINT8 Stop(void) = 0;
	virtual UINT8 Reset(void) = 0;
	virtual UINT8 Seek(UINT8 unit, UINT32 pos) = 0; // seek to playback position
	virtual UINT32 Render(UINT32 smplCnt, WAVE_32BS* data) = 0;
	
protected:
	UINT32 _outSmplRate;
	PLAYER_EVENT_CB _eventCbFunc;
	void* _eventCbParam;
};

#endif	// __PLAYERBASE_HPP__
