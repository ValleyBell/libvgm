#include <stddef.h>	// for NULL
#include "../EmuStructs.h"
#include "../SoundDevs.h"

#include "c6280intf.h"
#ifdef EC_C6280_MAME
#include "c6280_mame.h"
#endif
#ifdef EC_C6280_OOTAKE
#include "Ootake_PSG.h"
#endif


static const char* DeviceName(const DEV_GEN_CFG* devCfg)
{
	return "C6280";
}

static UINT16 DeviceChannels(const DEV_GEN_CFG* devCfg)
{
	return 6;
}

static const char** DeviceChannelNames(const DEV_GEN_CFG* devCfg)
{
	return NULL;
}

static const DEVLINK_IDS* DeviceLinkIDs(const DEV_GEN_CFG* devCfg)
{
	return NULL;
}

const DEV_DECL sndDev_C6280 =
{
	DEVID_C6280,
	DeviceName,
	DeviceChannels,
	DeviceChannelNames,
	DeviceLinkIDs,
	{	// cores
#ifdef EC_C6280_OOTAKE
		&devDef_C6280_Ootake,
#endif
#ifdef EC_C6280_MAME
		&devDef_C6280_MAME,
#endif
		NULL
	}
};
