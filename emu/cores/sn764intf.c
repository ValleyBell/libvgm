#include <stddef.h>	// for NULL
#include "../EmuStructs.h"
#include "../SoundDevs.h"

#include "sn764intf.h"
#ifdef EC_SN76496_MAME
#include "sn76496.h"
#endif
#ifdef EC_SN76496_MAXIM
#include "sn76489.h"
#endif


static const char* DeviceName(const DEV_GEN_CFG* devCfg)
{
	const SN76496_CFG* snCfg = (const SN76496_CFG*)devCfg;
	if (devCfg == NULL)
		return "SN76496";
	
	if (snCfg->_genCfg.flags)
		return "T6W28";
	switch(snCfg->shiftRegWidth)
	{
	case 0x0F:
		return (snCfg->clkDiv == 1) ? "SN94624" : "SN76489";
	case 0x10:
		if (snCfg->noiseTaps == 0x0009)
		{
			return "SEGA PSG";
		}
		else if (snCfg->noiseTaps == 0x0022)
		{
			if (snCfg->ncrPSG)	// Tandy noise mode
				return snCfg->negate ? "NCR8496" : "PSSJ-3";
			else
				return "NCR8496";
		}
		else
		{
			return "SN76496";
		}
	case 0x11:
		return (snCfg->clkDiv == 1) ? "SN76494" : "SN76489A";
	default:
		return "SN764xx";
	}
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
		"1", "2", "3", "Noise",
	};
	return names;
}

static const DEVLINK_IDS* DeviceLinkIDs(const DEV_GEN_CFG* devCfg)
{
	return NULL;
}

const DEV_DECL sndDev_SN76496 =
{
	DEVID_SN76496,
	DeviceName,
	DeviceChannels,
	DeviceChannelNames,
	DeviceLinkIDs,
	{	// cores
#ifdef EC_SN76496_MAME
		&devDef_SN76496_MAME,
#endif
#ifdef EC_SN76496_MAXIM
		&devDef_SN76489_Maxim,
#endif
		NULL
	}
};
