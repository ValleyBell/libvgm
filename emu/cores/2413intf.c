#include <stddef.h>	// for NULL
#include "../EmuStructs.h"
#include "../SoundDevs.h"

#include "2413intf.h"
#ifdef EC_YM2413_MAME
#include "ym2413.h"
#endif
#ifdef EC_YM2413_EMU2413
#include "emu2413.h"
#endif
#ifdef EC_YM2413_NUKED
#include "nukedopll.h"
#endif


static const char* DeviceName(const DEV_GEN_CFG* devCfg)
{
	if (devCfg != NULL && devCfg->flags)
		return "VRC7";
	return "YM2413";
}

#define DEV_CHN_COUNT	14
static UINT16 DeviceChannels(const DEV_GEN_CFG* devCfg)
{
	return DEV_CHN_COUNT;
}

static const char** DeviceChannelNames(const DEV_GEN_CFG* devCfg)
{
	static const char* names[DEV_CHN_COUNT] =
	{
		"1", "2", "3", "4", "5", "6", "7", "8", "9",
		"Bass Drum", "Snare Drum", "Tom Tom", "Cymbal", "Hi-Hat",
	};
	return names;
}

static const DEVLINK_IDS* DeviceLinkIDs(const DEV_GEN_CFG* devCfg)
{
	return NULL;
}

const DEV_DECL sndDev_YM2413 =
{
	DEVID_YM2413,
	DeviceName,
	DeviceChannels,
	DeviceChannelNames,
	DeviceLinkIDs,
	{	// cores
#ifdef EC_YM2413_EMU2413
		&devDef_YM2413_Emu,	// default, because it's better than MAME
#endif
#ifdef EC_YM2413_MAME
		&devDef_YM2413_MAME,
#endif
#ifdef EC_YM2413_NUKED
		&devDef_YM2413_Nuked,
#endif
		NULL
	}
};
