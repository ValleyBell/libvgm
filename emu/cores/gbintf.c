#include <stddef.h>	// for NULL
#include "../EmuStructs.h"
#include "../SoundDevs.h"

#include "gbintf.h"
#ifdef EC_GB_MAME
#include "gb_mame.h"
#endif
#ifdef EC_GB_SAMEBOY
#include "sameboy_apu.h"
#endif


static const char* DeviceName(const DEV_GEN_CFG* devCfg)
{
	return "GameBoy DMG";
}

#define DEV_CHN_COUNT	4
static UINT16 DeviceChannels(const DEV_GEN_CFG* devCfg)
{
	return DEV_CHN_COUNT;
}

static const char** DeviceChannelNames(const DEV_GEN_CFG* devCfg)
{
	static const char* names[DEV_CHN_COUNT] =
	{
		"Square 1", "Square 2", "Wave", "Noise",
	};
	return names;
}

static const DEVLINK_IDS* DeviceLinkIDs(const DEV_GEN_CFG* devCfg)
{
	return NULL;
}

const DEV_DECL sndDev_GB_DMG =
{
	DEVID_GB_DMG,
	DeviceName,
	DeviceChannels,
	DeviceChannelNames,
	DeviceLinkIDs,
	{	// cores
#ifdef EC_GB_SAMEBOY
		&devDef_GB_SameBoy,
#endif
#ifdef EC_GB_MAME
		&devDef_GB_MAME,
#endif
		NULL
	}
};
