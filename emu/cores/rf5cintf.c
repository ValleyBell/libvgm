#include <stddef.h>	// for NULL
#include "../EmuStructs.h"
#include "../SoundDevs.h"

#include "rf5cintf.h"
#ifdef EC_RF5C68_MAME
#include "rf5c68.h"
#endif
#ifdef EC_RF5C68_GENS
#include "scd_pcm.h"
#endif


static const char* DeviceName(const DEV_GEN_CFG* devCfg)
{
	if (devCfg != NULL)
	{
		if (devCfg->flags == 1)
			return "RF5C164";
		else if (devCfg->flags == 2)
			return "RF5C105";
	}
	return "RF5C68";
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

const DEV_DECL sndDev_RF5C68 =
{
	DEVID_RF5C68,
	DeviceName,
	DeviceChannels,
	DeviceChannelNames,
	DeviceLinkIDs,
	{	// cores
#ifdef EC_RF5C68_MAME
		&devDef_RF5C68_MAME,
#endif
#ifdef EC_RF5C68_GENS
		&devDef_RF5C68_Gens,
#endif
		NULL
	}
};
