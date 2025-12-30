// ymfm wrapper for libvgm
// Provides C interface to ymfm YM2414 (OPZ) emulation

#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#include "../../stdtype.h"

extern "C"
{
#include "../SoundDevs.h"
#include "../EmuStructs.h"
#include "../EmuCores.h"
#include "../snddef.h"
#include "../EmuHelper.h"
#include "ymfmintf.h"
}

#include "ymfm/ymfm.h"
#include "ymfm/ymfm_opz.h"


// FourCC for ymfm cores
#define FCC_YMFM	0x594D464D	// "YMFM"


//*********************************************************
//  INTERFACE IMPLEMENTATION
//*********************************************************

// Simple ymfm interface implementation
class libvgm_ymfm_interface : public ymfm::ymfm_interface
{
public:
	libvgm_ymfm_interface() { }
	virtual ~libvgm_ymfm_interface() { }

	// We use the default implementations for all virtual methods
	// as libvgm doesn't need timer or IRQ functionality for playback
};


//*********************************************************
//  CHIP STATE
//*********************************************************

struct ymfm_ym2414_chip
{
	DEV_DATA _devData;					// Must be first member

	libvgm_ymfm_interface* intf;		// Interface instance
	ymfm::ym2414* chip;					// Chip instance

	UINT32 clock;						// Input clock
	UINT32 sample_rate;					// Output sample rate
	UINT8 address;						// Current register address
};


//*********************************************************
//  HELPER MACROS
//*********************************************************

#define CHP_GET_PTR(devData)	((ymfm_ym2414_chip*)(devData))


//*********************************************************
//  DEVICE FUNCTION DECLARATIONS
//*********************************************************

static UINT8 device_start_ym2414(const DEV_GEN_CFG* cfg, DEV_INFO* retDevInf);
static void device_stop_ym2414(void* chip);
static void device_reset_ym2414(void* chip);
static void ym2414_update(void* chip, UINT32 samples, DEV_SMPL** outputs);
static UINT8 ym2414_write(void* chip, UINT8 offset, UINT8 data);
static UINT8 ym2414_read(void* chip, UINT8 offset);
static void ym2414_set_mute_mask(void* chip, UINT32 muteMask);


//*********************************************************
//  DEVICE DEFINITION
//*********************************************************

static DEVDEF_RWFUNC devFunc[] =
{
	{RWF_REGISTER | RWF_WRITE, DEVRW_A8D8, 0, (void*)ym2414_write},
	{RWF_REGISTER | RWF_READ, DEVRW_A8D8, 0, (void*)ym2414_read},
	{RWF_CHN_MUTE | RWF_WRITE, DEVRW_ALL, 0, (void*)ym2414_set_mute_mask},
	{0x00, 0x00, 0, NULL}
};

static DEV_DEF devDef_YM2414 =
{
	"YM2414", "ymfm", FCC_YMFM,

	device_start_ym2414,
	device_stop_ym2414,
	device_reset_ym2414,
	ym2414_update,

	NULL,	// SetOptionBits
	ym2414_set_mute_mask,
	NULL,	// SetPanning (deprecated)
	NULL,	// SetSampleRateChangeCallback
	NULL,	// SetLoggingCallback
	NULL,	// LinkDevice

	devFunc,
};


//*********************************************************
//  DEVICE FUNCTIONS
//*********************************************************

static UINT8 device_start_ym2414(const DEV_GEN_CFG* cfg, DEV_INFO* retDevInf)
{
	ymfm_ym2414_chip* info;
	DEV_DATA* devData;
	UINT32 rate;

	info = (ymfm_ym2414_chip*)calloc(1, sizeof(ymfm_ym2414_chip));
	if (info == NULL)
		return 0xFF;

	devData = &info->_devData;
	info->clock = cfg->clock;

	// Create interface and chip
	info->intf = new libvgm_ymfm_interface();
	info->chip = new ymfm::ym2414(*info->intf);

	// Calculate sample rate
	rate = info->chip->sample_rate(cfg->clock);

	// Initialize device info
	INIT_DEVINF(retDevInf, devData, rate, &devDef_YM2414);

	return 0x00;
}

static void device_stop_ym2414(void* chip)
{
	ymfm_ym2414_chip* info = CHP_GET_PTR(chip);

	delete info->chip;
	delete info->intf;
	free(info);

	return;
}

static void device_reset_ym2414(void* chip)
{
	ymfm_ym2414_chip* info = CHP_GET_PTR(chip);

	info->chip->reset();
	info->address = 0x00;

	return;
}

static void ym2414_update(void* chip, UINT32 samples, DEV_SMPL** outputs)
{
	ymfm_ym2414_chip* info = CHP_GET_PTR(chip);
	DEV_SMPL* outL = outputs[0];
	DEV_SMPL* outR = outputs[1];
	UINT32 i;

	ymfm::ym2414::output_data output;

	for (i = 0; i < samples; i++)
	{
		info->chip->generate(&output, 1);

		// ymfm outputs 32-bit signed samples
		// libvgm expects DEV_SMPL (also 32-bit signed)
		outL[i] = output.data[0];
		outR[i] = output.data[1];
	}

	return;
}

static UINT8 ym2414_write(void* chip, UINT8 offset, UINT8 data)
{
	ymfm_ym2414_chip* info = CHP_GET_PTR(chip);

	switch (offset & 0x01)
	{
	case 0x00:	// Address
		info->address = data;
		info->chip->write_address(data);
		break;
	case 0x01:	// Data
		info->chip->write_data(data);
		break;
	}

	return 0x00;
}

static UINT8 ym2414_read(void* chip, UINT8 offset)
{
	ymfm_ym2414_chip* info = CHP_GET_PTR(chip);

	switch (offset & 0x01)
	{
	case 0x00:	// Status
		return info->chip->read_status();
	case 0x01:	// Data (not supported on YM2414)
		return 0xFF;
	}

	return 0x00;
}

static void ym2414_set_mute_mask(void* chip, UINT32 muteMask)
{
	// YM2414 has 8 channels
	// TODO: Implement channel muting if needed
	// ymfm doesn't provide a direct mute interface, would need to be
	// implemented at the output stage or by modifying registers
	return;
}


//*********************************************************
//  DEVICE METADATA
//*********************************************************

static const char* DeviceName_YM2414(const DEV_GEN_CFG* devCfg)
{
	return "YM2414";
}

static UINT16 DeviceChannels_YM2414(const DEV_GEN_CFG* devCfg)
{
	return 8;	// 8 FM channels
}

static const char** DeviceChannelNames_YM2414(const DEV_GEN_CFG* devCfg)
{
	return NULL;	// No special channel names
}

static const DEVLINK_IDS* DeviceLinkIDs_YM2414(const DEV_GEN_CFG* devCfg)
{
	return NULL;	// No linked devices
}


//*********************************************************
//  DEVICE DECLARATION
//*********************************************************

extern "C" const DEV_DECL sndDev_YM2414 =
{
	DEVID_YM2414,
	DeviceName_YM2414,
	DeviceChannels_YM2414,
	DeviceChannelNames_YM2414,
	DeviceLinkIDs_YM2414,
	{
#ifdef EC_YM2414_YMFM
		&devDef_YM2414,
#endif
		NULL
	}
};
