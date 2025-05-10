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
	if (devCfg == NULL)
		return "AY-3-8910";
	
	switch(ayCfg->chipType)
	{
	case 0x00:
		return "AY-3-8910";
	case 0x01:
		return "AY-3-8912";
	case 0x02:
		return "AY-3-8913";
	case 0x03:
		return "AY8930";
	case 0x04:
		return "AY-3-8914";
	case 0x10:
		return "YM2149";
	case 0x11:
		return "YM3439";
	case 0x12:
		return "YMZ284";
	case 0x13:
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

const DEV_DECL sndDev_AY8910 =
{
	DEVID_AY8910,
	DeviceName,
	DeviceChannels,
	DeviceChannelNames,
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
