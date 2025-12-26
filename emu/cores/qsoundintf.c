#include <stddef.h>	// for NULL
#include "../EmuStructs.h"
#include "../SoundDevs.h"

#include "qsoundintf.h"
#ifdef EC_QSOUND_MAME
#include "qsound_mame.h"
#endif
#ifdef EC_QSOUND_CTR
#include "qsound_ctr.h"
#endif


static const char* DeviceName(const DEV_GEN_CFG* devCfg)
{
	return "QSound";
}

#define DEV_CHN_COUNT	19
static UINT16 DeviceChannels(const DEV_GEN_CFG* devCfg)
{
	return DEV_CHN_COUNT;
}

static const char** DeviceChannelNames(const DEV_GEN_CFG* devCfg)
{
	static const char* names[DEV_CHN_COUNT] =
	{
		"PCM 1", "PCM 2", "PCM 3", "PCM 4", "PCM 5", "PCM 6", "PCM 7", "PCM 8",
		"PCM 9", "PCM 10", "PCM 11", "PCM 12", "PCM 13", "PCM 14", "PCM 15", "PCM 16",
		"ADPCM 1", "ADPCM 2", "ADPCM 3",
	};
	return names;
}

static const DEVLINK_IDS* DeviceLinkIDs(const DEV_GEN_CFG* devCfg)
{
	return NULL;
}

const DEV_DECL sndDev_QSound =
{
	DEVID_QSOUND,
	DeviceName,
	DeviceChannels,
	DeviceChannelNames,
	DeviceLinkIDs,
	{	// cores
#ifdef EC_QSOUND_CTR
		&devDef_QSound_ctr,
#endif
#ifdef EC_QSOUND_MAME
		&devDef_QSound_MAME,
#endif
		NULL
	}
};
