#include <stddef.h>	// for NULL
#include "../EmuStructs.h"
#include "../SoundDevs.h"

#include "saaintf.h"
#ifdef EC_SAA1099_MAME
#include "saa1099_mame.h"
#endif
#ifdef EC_SAA1099_NRS
#undef EC_SAA1099_NRS	// not yet added
//#include "saa1099_nrs.h"
#endif
#ifdef EC_SAA1099_VB
#include "saa1099_vb.h"
#endif


static const char* DeviceName(const DEV_GEN_CFG* devCfg)
{
	return "SAA1099";
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

const DEV_DECL sndDev_SAA1099 =
{
	DEVID_SAA1099,
	DeviceName,
	DeviceChannels,
	DeviceChannelNames,
	DeviceLinkIDs,
	{	// cores
#ifdef EC_SAA1099_VB
		&devDef_SAA1099_VB,
#endif
#ifdef EC_SAA1099_MAME
		&devDef_SAA1099_MAME,
#endif
#ifdef EC_SAA1099_NRS
		&devDef_SAA1099_NRS,
#endif
		NULL
	}
};
