#ifndef __VGMPLAYER_HPP__
#define __VGMPLAYER_HPP__

#include "../stdtype.h"
#include <emu/EmuStructs.h>
#include <emu/Resampler.h>
#include "../utils/StrUtils.h"
#include "helper.h"
#include "playerbase.hpp"
#include "../utils/DataLoader.h"
#include "dblk_compr.h"
#include <vector>
#include <string>


#define FCC_VGM 	0x56474D00

// This structure contains only some basic information about the VGM file,
// not the full header.
struct VGM_HEADER
{
	UINT32 fileVer;
	UINT32 eofOfs;
	UINT32 extraHdrOfs;
	UINT32 dataOfs;		// command data start offset
	UINT32 loopOfs;		// loop offset
	UINT32 dataEnd;		// command data end offset
	UINT32 gd3Ofs;		// GD3 tag offset
	
	UINT32 xhChpClkOfs;	// extra header offset: chip clocks
	UINT32 xhChpVolOfs;	// extra header offset: chip volume
	
	UINT32 numTicks;	// total number of samples
	UINT32 loopTicks;	// number of samples for the looping part
	UINT32 recordHz;	// rate of the recording in Hz (60 for NTSC, 50 for PAL, 0 disables rate scaling)
	
	INT8 loopBase;
	UINT8 loopModifier;	// 4.4 fixed point
	INT16 volumeGain;	// 8.8 fixed point, +0x100 = +6 db
};

struct VGM_PLAY_OPTIONS
{
	UINT32 playbackHz;	// set to 60 (NTSC) or 50 (PAL) for region-specific song speed adjustment
						// Note: requires VGM_HEADER.recordHz to be non-zero to work.
	UINT8 hardStopOld;	// enforce silence at end of old VGMs (<1.50), fixes Key Off events being trimmed off
};


class VGMPlayer : public PlayerBase
{
public:
	struct CHIP_DEVICE	// Note: has to be a POD, because I use memset() on it.
	{
		VGM_BASEDEV base;
		UINT8 vgmChipType;
		UINT8 chipID;
		UINT32 flags;
		size_t optID;
		DEVFUNC_WRITE_A8D8 write8;		// write 8-bit data to 8-bit register/offset
		DEVFUNC_WRITE_A16D8 writeM8;	// write 8-bit data to 16-bit memory offset
		DEVFUNC_WRITE_A8D16 writeD16;	// write 16-bit data to 8-bit register/offset
		DEVFUNC_WRITE_A16D16 writeM16;	// write 16-bit data to 16-bit register/offset
		DEVFUNC_WRITE_MEMSIZE romSize;
		DEVFUNC_WRITE_BLOCK romWrite;
		DEVFUNC_WRITE_MEMSIZE romSizeB;
		DEVFUNC_WRITE_BLOCK romWriteB;
	};
	
protected:
	struct HDR_CHIP_DEF
	{
		UINT8 devType;
		UINT16 volume;
		UINT32 clock;
	};
	struct XHDR_DATA32
	{
		UINT8 type;
		UINT32 data;
	};
	struct XHDR_DATA16
	{
		UINT8 type;
		UINT8 flags;
		UINT16 data;
	};
	
	struct SONG_DEV_CFG
	{
		size_t deviceID;	// index for _devices array
		UINT8 vgmChipType;
		UINT8 type;
		UINT16 instance;
		std::vector<UINT8> cfgData;
	};
	
	struct DACSTRM_DEV
	{
		DEV_INFO defInf;
		UINT8 streamID;
		UINT8 bankID;
	};
	struct PCM_BANK
	{
		std::vector<UINT8> data;
		std::vector<UINT32> bankOfs;
		std::vector<UINT32> bankSize;
	};
	
	typedef void (VGMPlayer::*COMMAND_FUNC)(void);	// VGM command member function callback
	struct DEVLINK_CB_DATA
	{
		VGMPlayer* object;
		UINT8 chipType;
	};
	struct COMMAND_INFO
	{
		UINT8 chipType;
		UINT32 cmdLen;
		COMMAND_FUNC func;
	};
	
	struct QSOUND_WORK
	{
		void (*write)(CHIP_DEVICE*, UINT8, UINT16);	// pointer to WriteQSound_A/B
		UINT16 startAddrCache[16];	// QSound register 0x01
		UINT16 pitchCache[16];		// QSound register 0x02
	};
	
public:
	VGMPlayer();
	~VGMPlayer();
	
	UINT32 GetPlayerType(void) const;
	const char* GetPlayerName(void) const;
	static UINT8 IsMyFile(DATA_LOADER *fileLoader);
	UINT8 LoadFile(DATA_LOADER *fileLoader);
	UINT8 UnloadFile(void);
	const VGM_HEADER* GetFileHeader(void) const;
	
	const char* const* GetTags(void);
	UINT8 GetSongInfo(PLR_SONG_INFO& songInf);
	UINT8 GetSongDeviceInfo(std::vector<PLR_DEV_INFO>& devInfList) const;
	UINT8 SetDeviceOptions(UINT32 id, const PLR_DEV_OPTS& devOpts);
	UINT8 GetDeviceOptions(UINT32 id, PLR_DEV_OPTS& devOpts) const;
	UINT8 SetDeviceMuting(UINT32 id, const PLR_MUTE_OPTS& muteOpts);
	UINT8 GetDeviceMuting(UINT32 id, PLR_MUTE_OPTS& muteOpts) const;
	// player-specific options
	UINT8 SetPlayerOptions(const VGM_PLAY_OPTIONS& playOpts);
	UINT8 GetPlayerOptions(VGM_PLAY_OPTIONS& playOpts) const;
	
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
	
protected:
	UINT8 ParseHeader(void);
	void ParseXHdr_Data32(UINT32 fileOfs, std::vector<XHDR_DATA32>& xData);
	void ParseXHdr_Data16(UINT32 fileOfs, std::vector<XHDR_DATA16>& xData);
	
	UINT8 LoadTags(void);
	std::string GetUTF8String(const UINT8* startPtr, const UINT8* endPtr);
	
	size_t DeviceID2OptionID(UINT32 id) const;
	void RefreshMuting(CHIP_DEVICE& chipDev, const PLR_MUTE_OPTS& muteOpts);
	
	void RefreshTSRates(void);
	
	UINT32 GetHeaderChipClock(UINT8 chipType) const;	// returns raw chip clock value from VGM header
	inline UINT32 GetChipCount(UINT8 chipType) const;
	UINT32 GetChipClock(UINT8 chipType, UINT8 chipID) const;
	UINT16 GetChipVolume(UINT8 chipType, UINT8 chipID, UINT8 isLinked) const;
	UINT16 EstimateOverallVolume(void) const;
	void NormalizeOverallVolume(UINT16 overallVol);
	void InitDevices(void);
	
	static void DeviceLinkCallback(void* userParam, VGM_BASEDEV* cDev, DEVLINK_INFO* dLink);
	void LoadOPL4ROM(CHIP_DEVICE* chipDev);
	CHIP_DEVICE* GetDevicePtr(UINT8 chipType, UINT8 chipID);
	
	UINT8 SeekToTick(UINT32 tick);
	UINT8 SeekToFilePos(UINT32 pos);
	void ParseFile(UINT32 ticks);
	
	// --- VGM command functions ---
	void Cmd_invalid(void);
	void Cmd_unknown(void);
	void Cmd_EndOfData(void);				// command 66
	void Cmd_DelaySamples2B(void);			// command 61 - wait for N samples (2-byte parameter)
	void Cmd_Delay60Hz(void);				// command 62 - wait 735 samples (1/60 second)
	void Cmd_Delay50Hz(void);				// command 63 - wait 882 samples (1/50 second)
	void Cmd_DelaySamplesN1(void);			// command 70..7F - wait (N+1) samples
	void DoRAMOfsPatches(UINT8 chipType, UINT8 chipID, UINT32& dataOfs, UINT32& dataLen);
	void Cmd_DataBlock(void);				// command 67
	void Cmd_PcmRamWrite(void);				// command 68
	void Cmd_YM2612PCM_Delay(void);			// command 80..8F - write YM2612 PCM from data block + delay by N samples
	void Cmd_YM2612PCM_Seek(void);			// command E0 - set YM2612 PCM data offset
	void Cmd_DACCtrl_Setup(void);			// command 90
	void Cmd_DACCtrl_SetData(void);			// command 91
	void Cmd_DACCtrl_SetFrequency(void);	// command 92
	void Cmd_DACCtrl_PlayData_Loc(void);	// command 93
	void Cmd_DACCtrl_Stop(void);			// command 94
	void Cmd_DACCtrl_PlayData_Blk(void);	// command 95
	
	void Cmd_GGStereo(void);				// command 4F - set GameGear Stereo mask
	void Cmd_SN76489(void);					// command 50 - SN76489 register write
	void Cmd_Reg8_Data8(void);				// command 51/54/55/5A..5D - Register, Data (8-bit)
	void Cmd_CPort_Reg8_Data8(void);		// command 52/53/56..59/5E/5F - Port (in command byte), Register, Data (8-bit)
	void Cmd_Port_Reg8_Data8(void);			// command D0..D2 - Port, Register, Data (8-bit)
	void Cmd_Ofs8_Data8(void);				// command B3/B5..BB/BE/BF - Offset (8-bit), Data (8-bit)
	void Cmd_Ofs16_Data8(void);				// command C5..C8/D3/D4/D6/E5 - Offset (16-bit), Data (8-bit)
	void Cmd_Ofs8_Data16(void);				// unused - Offset (8-bit), Data (16-bit)
	void Cmd_Ofs16_Data16(void);			// command E1 - Offset (16-bit), Data (16-bit)
	void Cmd_Port_Ofs8_Data8(void);			// command D5 - Port, Offset (8-bit), Data (8-bit)
	void Cmd_DReg8_Data8(void);				// command A0 - Register (with dual-chip bit), Data (8-bit)
	void Cmd_SegaPCM_Mem(void);				// command C0 - SegaPCM memory write
	void Cmd_RF5C_Mem(void);				// command C1/C2 - RF5C68/164 memory write
	void Cmd_RF5C_Reg(void);				// command B0/B1 - RF5C68/164 register write
	void Cmd_PWM_Reg(void);					// command B2 - PWM register write (4-bit offset, 12-bit data)
	void Cmd_QSound_Reg(void);				// command C4 - QSound register write (16-bit data, 8-bit offset)
	static void WriteQSound_A(CHIP_DEVICE* cDev, UINT8 ofs, UINT16 data);	// write by calling write8
	static void WriteQSound_B(CHIP_DEVICE* cDev, UINT8 ofs, UINT16 data);	// write by calling writeD16
	void Cmd_WSwan_Reg(void);				// command BC - WonderSwan register write (Reg8_Data8 with remapping)
	void Cmd_NES_Reg(void);					// command B4 - NES APU register write (Reg8_Data8 with remapping)
	void Cmd_YMW_Bank(void);				// command C3 - YMW258 bank write (Ofs8_Data16 with remapping)
	void Cmd_SAA_Reg(void);					// command BD - SAA1099 register write (Reg8_Data8 with remapping)
	void Cmd_AY_Stereo(void);				// command 30 - set AY8910 stereo mask
	
	CPCONV* _cpcUTF16;	// UTF-16 LE -> UTF-8 codepage conversion
	DATA_LOADER *_dLoad;
	const UINT8* _fileData;	// data pointer for quick access, equals _dLoad->GetFileData().data()
	
	enum
	{
		_HDR_BUF_SIZE = 0x100,
		_OPT_DEV_COUNT = 0x10,	//0x29
		_CHIP_COUNT = 0x29,
		_PCM_BANK_COUNT = 0x40
	};
	
	VGM_HEADER _fileHdr;
	std::vector<XHDR_DATA32> _xHdrChipClk;
	std::vector<XHDR_DATA16> _xHdrChipVol;
	UINT8 _hdrBuffer[_HDR_BUF_SIZE];	// buffer containing the file header
	UINT32 _hdrLenFile;
	UINT32 _tagVer;
	
	enum
	{
		_TAG_TRACK_NAME_EN,
		_TAG_TRACK_NAME_JP,
		_TAG_GAME_NAME_EN,
		_TAG_GAME_NAME_JP,
		_TAG_SYSTEM_NAME_EN,
		_TAG_SYSTEM_NAME_JP,
		_TAG_ARTIST_EN,
		_TAG_ARTIST_JP,
		_TAG_GAME_RELEASE_DATE,
		_TAG_VGM_CREATOR,
		_TAG_NOTES,
		_TAG_COUNT,
	};
	static const char* const _TAG_TYPE_LIST[_TAG_COUNT];
	std::string _tagData[_TAG_COUNT];
	const char* _tagList[2 * _TAG_COUNT + 1];
	
	//UINT32 _outSmplRate;
	
	// tick/sample conversion rates
	UINT64 _tsMult;
	UINT64 _tsDiv;
	
	UINT32 _filePos;
	UINT32 _fileTick;
	UINT32 _playTick;
	UINT32 _playSmpl;
	UINT32 _curLoop;
	
	UINT8 _playState;
	UINT8 _psTrigger;	// used to temporarily trigger special commands
	//PLAYER_EVENT_CB _eventCbFunc;
	//void* _eventCbParam;
	
	static const UINT8 _OPT_DEV_LIST[_OPT_DEV_COUNT];	// list of configurable libvgm devices (different from VGM chip list]
	static const UINT8 _DEV_LIST[_CHIP_COUNT];	// VGM chip ID -> libvgm device ID
	static const UINT32 _CHIPCLK_OFS[_CHIP_COUNT];	// file offsets for chip clocks in VGM header
	static const UINT16 _CHIP_VOLUME[_CHIP_COUNT];	// default volume for chips
	static const UINT16 _PB_VOL_AMNT[_CHIP_COUNT];	// amount of the chip's playback volume in overall gain
	
	static const COMMAND_INFO _CMD_INFO[0x100];	// VGM commands
	static const UINT8 _VGM_BANK_CHIPS[_PCM_BANK_COUNT];	// PCM database ID -> VGM chip
	static const UINT8 _VGM_ROM_CHIPS[0x40][2];	// ROM write datablock ID -> VGM chip / memory type
	static const UINT8 _VGM_RAM_CHIPS[0x40];	// RAM write datablock ID -> VGM chip
	
	VGM_PLAY_OPTIONS _playOpts;
	PLR_DEV_OPTS _devOpts[_OPT_DEV_COUNT * 2];	// space for 2 instances per chip
	size_t _devOptMap[0x100][2];	// maps libvgm device ID to _devOpts vector
	
	std::vector<SONG_DEV_CFG> _songDevCfg;
	size_t _vdDevMap[_CHIP_COUNT][2];	// maps VGM device ID to _devices vector
	size_t _optDevMap[_OPT_DEV_COUNT * 2];	// maps _devOpts vector index to _devices vector
	std::vector<CHIP_DEVICE> _devices;
	
	size_t _dacStrmMap[0x100];	// maps VGM DAC stream ID -> _dacStreams vector
	std::vector<DACSTRM_DEV> _dacStreams;
	
	PCM_BANK _pcmBank[_PCM_BANK_COUNT];
	PCM_COMPR_TBL _pcmComprTbl;
	
	UINT32 _ym2612pcm_bnkPos;
	UINT8 _rf5cBank[2][2];	// [0 RF5C68 / 1 RF5C164][chipID]
	QSOUND_WORK _qsWork[2];
};

#endif	// __VGMPLAYER_HPP__
