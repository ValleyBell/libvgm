#include <stddef.h>	// for NULL
#include "../EmuStructs.h"
#include "../SoundDevs.h"

#include "2151intf.h"
#ifdef EC_YM2151_MAME
#include "ym2151.h"
#endif
#ifdef EC_YM2151_NUKED
#include "nukedopm.h"
#endif


static const char* DeviceName(const DEV_GEN_CFG* devCfg)
{
	return "YM2151";
}

static UINT16 DeviceChannels(const DEV_GEN_CFG* devCfg)
{
	return 8;
}

static const char** DeviceChannelNames(const DEV_GEN_CFG* devCfg)
{
	return NULL;
}

static const DEVLINK_IDS* DeviceLinkIDs(const DEV_GEN_CFG* devCfg)
{
	return NULL;
}

const DEV_DECL sndDev_YM2151 =
{
	DEVID_YM2151,
	DeviceName,
	DeviceChannels,
	DeviceChannelNames,
	DeviceLinkIDs,
	{	// cores
#ifdef EC_YM2151_MAME
		&devDef_YM2151_MAME,
#endif
#ifdef EC_YM2151_NUKED
		&devDef_YM2151_Nuked,
#endif
		NULL
	}
};
