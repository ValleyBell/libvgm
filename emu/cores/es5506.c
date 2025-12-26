#include <stddef.h>

#include "../../stdtype.h"
#include "../EmuStructs.h"
#include "../SoundDevs.h"
#include "es5506.h"

static const char* DeviceName(const DEV_GEN_CFG* devCfg)
{
	return NULL;
}

static UINT16 DeviceChannels(const DEV_GEN_CFG* devCfg)
{
	return 0;
}

static const char** DeviceChannelNames(const DEV_GEN_CFG* devCfg)
{
	return NULL;
}

static const DEVLINK_IDS* DeviceLinkIDs(const DEV_GEN_CFG* devCfg)
{
	return NULL;
}

const DEV_DECL sndDev_ES5506 =
{
	DEVID_ES5506,
	DeviceName,
	DeviceChannels,
	DeviceChannelNames,
	DeviceLinkIDs,
	{	// cores
		NULL
	}
};
