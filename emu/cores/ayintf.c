#include <stddef.h>	// for NULL
#include "../EmuStructs.h"
#include "../SoundDevs.h"

#include "ayintf.h"
#ifdef EC_AY8910_MAME
#include "ay8910.h"
#endif
#ifdef EC_AY8910_EMU2149
#include "emu2149.h"
#endif


static const char* DeviceName(const DEV_GEN_CFG* devCfg)
{
	const AY8910_CFG* ayCfg = (const AY8910_CFG*)devCfg;
	UINT8 chipType = (devCfg != NULL) ? ayCfg->chipType : AYTYPE_AY8910;
	if ((chipType & 0xF0) == 0x20)
		chipType = AYTYPE_YM2149;
	
	switch(chipType)
	{
	case AYTYPE_AY8910:
		return "AY-3-8910";
	case AYTYPE_AY8912:
		return "AY-3-8912";
	case AYTYPE_AY8913:
		return "AY-3-8913";
	case AYTYPE_AY8930:
		return "AY8930";
	case AYTYPE_AY8914:
		return "AY-3-8914";
	case AYTYPE_YM2149:
		return "YM2149";
	case AYTYPE_YM3439:
		return "YM3439";
	case AYTYPE_YMZ284:
		return "YMZ284";
	case AYTYPE_YMZ294:
		return "YMZ294";
	default:
		return "AY-89xx";
	}
}

static UINT16 DeviceChannels(const DEV_GEN_CFG* devCfg)
{
	return 3;
}

static const char** DeviceChannelNames(const DEV_GEN_CFG* devCfg)
{
	return NULL;
}

static const DEVLINK_IDS* DeviceLinkIDs(const DEV_GEN_CFG* devCfg)
{
	return NULL;
}

const DEV_DECL sndDev_AY8910 =
{
	DEVID_AY8910,
	DeviceName,
	DeviceChannels,
	DeviceChannelNames,
	DeviceLinkIDs,
	{	// cores
#ifdef EC_AY8910_EMU2149
		&devDef_YM2149_Emu,
#endif
#ifdef EC_AY8910_MAME
		&devDef_AY8910_MAME,
#endif
		NULL
	}
};
