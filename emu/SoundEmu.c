#include <stddef.h>
#include <stdlib.h>	// for malloc/free

#include "../stdtype.h"
#include "EmuStructs.h"
#include "SoundEmu.h"
#include "SoundDevs.h"

#ifndef SNDDEV_SELECT
// if not asked to select certain sound devices, just include everything (comfort option)
#define SNDDEV_SN76496
#define SNDDEV_YM2413
#define SNDDEV_YM2612
#define SNDDEV_YM2151
#define SNDDEV_SEGAPCM
#define SNDDEV_RF5C68
#define SNDDEV_YM2203
#define SNDDEV_YM2608
#define SNDDEV_YM2610
#define SNDDEV_YM3812
#define SNDDEV_YM3526
#define SNDDEV_Y8950
#define SNDDEV_YMF262
#define SNDDEV_YMF278B
#define SNDDEV_YMZ280B
#define SNDDEV_YMF271
#define SNDDEV_AY8910
#define SNDDEV_32X_PWM
#define SNDDEV_GAMEBOY
#define SNDDEV_NES_APU
#define SNDDEV_YMW258
#define SNDDEV_UPD7759
#define SNDDEV_MSM6258
#define SNDDEV_MSM6295
#define SNDDEV_K051649
#define SNDDEV_K054539
#define SNDDEV_C6280
#define SNDDEV_C140
#define SNDDEV_C219
#define SNDDEV_K053260
#define SNDDEV_POKEY
#define SNDDEV_QSOUND
#define SNDDEV_SCSP
#define SNDDEV_WSWAN
#define SNDDEV_VBOY_VSU
#define SNDDEV_SAA1099
#define SNDDEV_ES5503
#define SNDDEV_ES5506
#define SNDDEV_X1_010
#define SNDDEV_C352
#define SNDDEV_GA20
#define SNDDEV_MIKEY
#define SNDDEV_K007232
#define SNDDEV_K005289
#define SNDDEV_MSM5205
#define SNDDEV_MSM5232
#define SNDDEV_BSMT2000
#define SNDDEV_ICS2115
#endif

#ifdef SNDDEV_SN76496
#include "cores/sn764intf.h"
#endif
#ifdef SNDDEV_YM2413
#include "cores/2413intf.h"
#endif
#ifdef SNDDEV_YM2612
#include "cores/2612intf.h"
#endif
#ifdef SNDDEV_YM2151
#include "cores/2151intf.h"
#endif
#ifdef SNDDEV_SEGAPCM
#include "cores/segapcm.h"
#endif
#ifdef SNDDEV_RF5C68
#include "cores/rf5cintf.h"
#endif
#if defined(SNDDEV_YM2203) || defined(SNDDEV_YM2608) || defined(SNDDEV_YM2610)
#include "cores/opnintf.h"
#endif
#if defined(SNDDEV_YM3812) || defined(SNDDEV_YM3526) || defined(SNDDEV_Y8950)
#include "cores/oplintf.h"
#endif
#ifdef SNDDEV_YMF262
#include "cores/262intf.h"
#endif
#ifdef SNDDEV_YMF278B
#include "cores/ymf278b.h"
#endif
#ifdef SNDDEV_YMF271
#include "cores/ymf271.h"
#endif
#ifdef SNDDEV_YMZ280B
#include "cores/ymz280b.h"
#endif
#ifdef SNDDEV_32X_PWM
#include "cores/pwm.h"
#endif
#ifdef SNDDEV_AY8910
#include "cores/ayintf.h"
#endif
#ifdef SNDDEV_GAMEBOY
#include "cores/gb.h"
#endif
#ifdef SNDDEV_NES_APU
#include "cores/nesintf.h"
#endif
#ifdef SNDDEV_YMW258
#include "cores/multipcm.h"
#endif
#ifdef SNDDEV_UPD7759
#include "cores/upd7759.h"
#endif
#ifdef SNDDEV_MSM6258
#include "cores/okim6258.h"
#endif
#ifdef SNDDEV_MSM6295
#include "cores/okim6295.h"
#endif
#ifdef SNDDEV_K051649
#include "cores/k051649.h"
#endif
#ifdef SNDDEV_K054539
#include "cores/k054539.h"
#endif
#ifdef SNDDEV_C6280
#include "cores/c6280intf.h"
#endif
#ifdef SNDDEV_C140
#include "cores/c140.h"
#endif
#ifdef SNDDEV_C219
#include "cores/c219.h"
#endif
#ifdef SNDDEV_K053260
#include "cores/k053260.h"
#endif
#ifdef SNDDEV_POKEY
#include "cores/pokey.h"
#endif
#ifdef SNDDEV_QSOUND
#include "cores/qsoundintf.h"
#endif
#ifdef SNDDEV_SCSP
#include "cores/scsp.h"
#endif
#ifdef SNDDEV_WSWAN
#include "cores/ws_audio.h"
#endif
#ifdef SNDDEV_VBOY_VSU
#include "cores/vsu.h"
#endif
#ifdef SNDDEV_SAA1099
#include "cores/saaintf.h"
#endif
#ifdef SNDDEV_ES5503
#include "cores/es5503.h"
#endif
#ifdef SNDDEV_ES5506
#include "cores/es5506.h"
#endif
#ifdef SNDDEV_X1_010
#include "cores/x1_010.h"
#endif
#ifdef SNDDEV_C352
#include "cores/c352.h"
#endif
#ifdef SNDDEV_GA20
#include "cores/iremga20.h"
#endif
#ifdef SNDDEV_MIKEY
#include "cores/mikey.h"
#endif
#ifdef SNDDEV_K007232
#include "cores/k007232.h"
#endif
#ifdef SNDDEV_K005289
#include "cores/k005289.h"
#endif
#ifdef SNDDEV_MSM5205
#include "cores/msm5205.h"
#endif
#ifdef SNDDEV_MSM5232
#include "cores/msm5232.h"
#endif
#ifdef SNDDEV_BSMT2000
#include "cores/bsmt2000.h"
#endif
#ifdef SNDDEV_ICS2115
#include "cores/ics2115.h"
#endif

const DEV_DECL* sndEmu_Devices[] = {
#ifdef SNDDEV_SN76496
	&sndDev_SN76496,
#endif
#ifdef SNDDEV_YM2413
	&sndDev_YM2413,
#endif
#ifdef SNDDEV_YM2612
	&sndDev_YM2612,
#endif
#ifdef SNDDEV_YM2151
	&sndDev_YM2151,
#endif
#ifdef SNDDEV_SEGAPCM
	&sndDev_SegaPCM,
#endif
#ifdef SNDDEV_RF5C68
	&sndDev_RF5C68,
#endif
#ifdef SNDDEV_YM2203
	&sndDev_YM2203,
#endif
#ifdef SNDDEV_YM2608
	&sndDev_YM2608,
#endif
#ifdef SNDDEV_YM2610
	&sndDev_YM2610,
#endif
#ifdef SNDDEV_YM3812
	&sndDev_YM3812,
#endif
#ifdef SNDDEV_YM3526
	&sndDev_YM3526,
#endif
#ifdef SNDDEV_Y8950
	&sndDev_Y8950,
#endif
#ifdef SNDDEV_YMF262
	&sndDev_YMF262,
#endif
#ifdef SNDDEV_YMF278B
	&sndDev_YMF278B,
#endif
#ifdef SNDDEV_YMF271
	&sndDev_YMF271,
#endif
#ifdef SNDDEV_YMZ280B
	&sndDev_YMZ280B,
#endif
#ifdef SNDDEV_32X_PWM
	&sndDev_32X_PWM,
#endif
#ifdef SNDDEV_AY8910
	&sndDev_AY8910,
#endif
#ifdef SNDDEV_GAMEBOY
	&sndDev_GB_DMG,
#endif
#ifdef SNDDEV_NES_APU
	&sndDev_NES_APU,
#endif
#ifdef SNDDEV_YMW258
	&sndDev_YMW258,
#endif
#ifdef SNDDEV_UPD7759
	&sndDev_uPD7759,
#endif
#ifdef SNDDEV_MSM6258
	&sndDev_MSM6258,
#endif
#ifdef SNDDEV_MSM6295
	&sndDev_MSM6295,
#endif
#ifdef SNDDEV_K051649
	&sndDev_K051649,
#endif
#ifdef SNDDEV_K054539
	&sndDev_K054539,
#endif
#ifdef SNDDEV_C6280
	&sndDev_C6280,
#endif
#ifdef SNDDEV_C140
	&sndDev_C140,
#endif
#ifdef SNDDEV_C219
	&sndDev_C219,
#endif
#ifdef SNDDEV_K053260
	&sndDev_K053260,
#endif
#ifdef SNDDEV_POKEY
	&sndDev_Pokey,
#endif
#ifdef SNDDEV_QSOUND
	&sndDev_QSound,
#endif
#ifdef SNDDEV_SCSP
	&sndDev_SCSP,
#endif
#ifdef SNDDEV_WSWAN
	&sndDev_WSwan,
#endif
#ifdef SNDDEV_VBOY_VSU
	&sndDev_VBoyVSU,
#endif
#ifdef SNDDEV_SAA1099
	&sndDev_SAA1099,
#endif
#ifdef SNDDEV_ES5503
	&sndDev_ES5503,
#endif
#ifdef SNDDEV_ES5506
	&sndDev_ES5506,
#endif
#ifdef SNDDEV_X1_010
	&sndDev_X1_010,
#endif
#ifdef SNDDEV_C352
	&sndDev_C352,
#endif
#ifdef SNDDEV_GA20
	&sndDev_GA20,
#endif
#ifdef SNDDEV_MIKEY
	&sndDev_Mikey,
#endif
#ifdef SNDDEV_K007232
	&sndDev_K007232,
#endif
#ifdef SNDDEV_K005289
	&sndDev_K005289,
#endif
#ifdef SNDDEV_MSM5205
	&sndDev_MSM5205,
#endif
#ifdef SNDDEV_MSM5232
	&sndDev_MSM5232,
#endif
#ifdef SNDDEV_BSMT2000
	&sndDev_BSMT2000,
#endif
#ifdef SNDDEV_ICS2115
	&sndDev_ICS2115,
#endif
	NULL	// list end
};

static const DEV_DECL* SndEmu_DevDeclFromList(DEV_ID deviceID, const DEV_DECL** deviceList)
{
	const DEV_DECL* const* devPtr = deviceList;
	while(*devPtr != NULL && (*devPtr)->deviceID != deviceID)
		devPtr ++;
	return *devPtr;	// return device with respective deviceID
}

const DEV_DECL* SndEmu_GetDevDecl(DEV_ID deviceID, const DEV_DECL** userDevList, UINT8 opts)
{
	const DEV_DECL** devLists[2] = {userDevList, sndEmu_Devices};
	size_t curDevList;
	
	if (opts & EST_OPT_NO_DEFAULT)
		devLists[1] = NULL;
	for (curDevList = 0; curDevList < 2; curDevList ++)
	{
		if (devLists[curDevList] != NULL)
		{
			const DEV_DECL* device = SndEmu_DevDeclFromList(deviceID, devLists[curDevList]);
			if (device != NULL)
				return device;
		}
	}
	return NULL;
}

static UINT8 SndEmu_StartCore(const DEV_DECL* devDecl, const DEV_GEN_CFG* cfg, DEV_INFO* retDevInf)
{
	const DEV_DEF* const* devCorePtr;
	for (devCorePtr = devDecl->cores; *devCorePtr != NULL; devCorePtr ++)
	{
		const DEV_DEF* devCore = *devCorePtr;
		// emuCore == 0 -> use default
		if (! cfg->emuCore || devCore->coreID == cfg->emuCore)
		{
			UINT8 retVal = devCore->Start(cfg, retDevInf);
			retDevInf->devDecl = devDecl;
			if (! retVal)	// if initialization is successful, reset the chip to ensure a clean state
				devCore->Reset(retDevInf->dataPtr);
			return retVal;
		}
	}
	return EERR_NOT_FOUND;
}

UINT8 SndEmu_Start2(DEV_ID deviceID, const DEV_GEN_CFG* cfg, DEV_INFO* retDevInf, const DEV_DECL** userDevList, UINT8 opts)
{
	const DEV_DECL** devLists[2] = {userDevList, sndEmu_Devices};
	UINT8 retErr = EERR_UNK_DEVICE;
	size_t curDevList;
	
	if (opts & EST_OPT_NO_DEFAULT)
		devLists[1] = NULL;
	for (curDevList = 0; curDevList < 2; curDevList ++)
	{
		if (devLists[curDevList] != NULL)
		{
			const DEV_DECL* device = SndEmu_DevDeclFromList(deviceID, devLists[curDevList]);
			if (device != NULL)
			{
				UINT8 retVal = SndEmu_StartCore(device, cfg, retDevInf);
				if (retVal != EERR_NOT_FOUND || (opts & EST_OPT_STRICT_OVRD))
					return retVal;
				retErr = retVal;
			}
		}
	}
	return retErr;
}

const DEV_DEF* const* SndEmu_GetDevDefList(DEV_ID deviceID)
{
	const DEV_DECL* device = SndEmu_DevDeclFromList(deviceID, sndEmu_Devices);
	return (device != NULL) ? device->cores : NULL;
}

UINT8 SndEmu_Start(DEV_ID deviceID, const DEV_GEN_CFG* cfg, DEV_INFO* retDevInf)
{
	return SndEmu_Start2(deviceID, cfg, retDevInf, NULL, 0x00);
}


UINT8 SndEmu_Stop(DEV_INFO* devInf)
{
	devInf->devDef->Stop(devInf->dataPtr);
	devInf->dataPtr = NULL;
	
	return EERR_OK;
}

void SndEmu_FreeDevLinkData(DEV_INFO* devInf)
{
	UINT32 curLDev;
	
	if (! devInf->linkDevCount)
		return;
	
	for (curLDev = 0; curLDev < devInf->linkDevCount; curLDev ++)
		free(devInf->linkDevs[curLDev].cfg);
	free(devInf->linkDevs);	devInf->linkDevs = NULL;
	devInf->linkDevCount = 0;
	
	return;
}

UINT8 SndEmu_GetDeviceFunc(const DEV_DEF* devDef, UINT8 funcType, UINT8 rwType, UINT16 user, void** retFuncPtr)
{
	UINT32 curFunc;
	const DEVDEF_RWFUNC* tempFnc;
	UINT32 firstFunc;
	UINT32 foundFunc;
	
	foundFunc = 0;
	firstFunc = 0;
	for (curFunc = 0; devDef->rwFuncs[curFunc].funcPtr != NULL; curFunc ++)
	{
		tempFnc = &devDef->rwFuncs[curFunc];
		if (tempFnc->funcType == funcType && tempFnc->rwType == rwType)
		{
			if (! user || user == tempFnc->user)
			{
				if (foundFunc == 0)
					firstFunc = curFunc;
				foundFunc ++;
			}
		}
	}
	if (foundFunc == 0)
		return EERR_NOT_FOUND;
	*retFuncPtr = devDef->rwFuncs[firstFunc].funcPtr;
	if (foundFunc == 1)
		return EERR_OK;
	else
		return EERR_MORE_FOUND;	// found multiple matching functions
}

// opts:
//	0x01: long names (1) / short names (0)
const char* SndEmu_GetDevName(DEV_ID deviceID, UINT8 opts, const DEV_GEN_CFG* devCfg)
{
	const DEV_DECL* device = SndEmu_DevDeclFromList(deviceID, sndEmu_Devices);
	if (device == NULL)
		return NULL;

	if (! (opts & 0x01))
	{
		// special short names
		switch(deviceID)
		{
#ifdef SNDDEV_SEGAPCM
		case DEVID_SEGAPCM:
			return "SegaPCM";
#endif
#ifdef SNDDEV_GAMEBOY
		case DEVID_GB_DMG:
			return "GB DMG";
#endif
#ifdef SNDDEV_WSWAN
		case DEVID_WSWAN:
			return "WSwan";
#endif
		default:
			devCfg = NULL;	// for all other devices, just look up names normally without configuration
			break;
		}
	}
	return device->name(devCfg);
}
