// license:BSD-3-Clause
// copyright-holders:Nicola Salmoria,Aaron Giles
/***************************************************************************

	NAMCO WSG sound driver.

	This driver handles the four known types of NAMCO wavetable sounds:
	- 3-voice mono (PROM-based design: Pac-Man, Pengo, Dig Dug, etc)
		- namcowsg.c (this file)
	- 8-voice quadrophonic (Pole Position 1, Pole Position 2)
		- namcoppw.c
	- 8-voice mono (custom 15XX: Mappy, Dig Dug 2, etc)
		- namcoc15.c
	- 8-voice stereo (System 1)
		- namcoc30.c

	The 15XX custom does not have a DAC of its own; instead, it streams
	the 4-bit PROM data directly into the 99XX custom DAC. Most pre-99XX
	(and pre-15XX) Namco games use a LS273 latch (cleared when sound is
	disabled), a 4.7K/2.2K/1K/470 resistor-weighted DAC, and a 4066 and
	second group of resistors (10K/22K/47K/100K) for volume control.
	Pole Position does more complicated sound mixing: a 4051 multiplexes
	wavetable sound with four signals derived from the 52XX and 54XX, the
	selected signal is distributed to four volume control sections, and
	finally the engine noise is mixed into all four channels. The later
	CUS30 also uses the 99XX DAC, or two 99XX in the optional 16-channel
	stereo configuration, but it uses no PROM and delivers its own samples.

	The CUS30 has been decapped and verified to be a ULA.

***************************************************************************/

#include <stdlib.h>
#include <string.h>
#include <stddef.h>	// for NULL

#include "../../stdtype.h"
#include "../EmuStructs.h"
#include "../SoundDevs.h"
#include "../EmuCores.h"
#include "../snddef.h"
#include "../EmuHelper.h"
#include "namcowsg.h"

#define PROM_SIZE 0x100 // 256 bytes (low nibble only)

#define INTERNAL_RATE 192000

typedef struct _namcowsg_state
{
	DEV_DATA _devData;
	
	struct
	{
		UINT32 freq;
		UINT8 volume;
		UINT8 waveform;
		UINT32 addr;
	} voice[3];

	UINT8 prom[PROM_SIZE]; // Internal PROM storage
	UINT32 clock;
	UINT32 rate;
	UINT32 fracbits;
	UINT8 mute_mask;
} namcowsg_state;

static void namcowsg_update(void* param, UINT32 samples, DEV_SMPL** outputs);
static UINT8 device_start_namcowsg(const DEV_GEN_CFG* cfg, DEV_INFO* retDevInf);
static void device_stop_namcowsg(void* chip);
static void device_reset_namcowsg(void* chip);
static void namcowsg_set_mute_mask(void* chip, UINT32 mute_mask);
static void namcowsg_write(void* chip, UINT8 address, UINT8 data);
static void namcowsg_write_ram(void* chip, UINT8 address, UINT8 data);
static void namcowsg_write_prom(void *info, UINT32 offset, UINT32 length, const UINT8* data);

// Add PROM write handler
static DEVDEF_RWFUNC devFunc[] = {
	{RWF_REGISTER | RWF_WRITE, DEVRW_A8D8, 0, namcowsg_write},
	{RWF_CHN_MUTE | RWF_WRITE, DEVRW_ALL, 0, namcowsg_set_mute_mask},
	{RWF_MEMORY | RWF_WRITE, DEVRW_A16D8, 0, namcowsg_write_ram},
	{RWF_MEMORY | RWF_WRITE, DEVRW_BLOCK, 0, namcowsg_write_prom},
	{0x00, 0x00, 0, NULL}
};

static DEV_DEF devDef = {
	"Namco WSG", "MAME", FCC_MAME,
	device_start_namcowsg,
	device_stop_namcowsg,
	device_reset_namcowsg,
	namcowsg_update,

	NULL,   // SetOptionBits
	namcowsg_set_mute_mask,
	NULL,   // SetPanning
	NULL,   // SetSampleRateChangeCallback
	NULL,   // SetLoggingCallback
	NULL,   // LinkDevice

	devFunc
};

static const char* DeviceName(const DEV_GEN_CFG* devCfg)
{
	return "Namco WSG";
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

const DEV_DECL sndDev_NAMCOWSG =
{
	DEVID_NAMCOWSG,
	DeviceName,
	DeviceChannels,
	DeviceChannelNames,
	DeviceLinkIDs,
	{	// cores
		&devDef,
		NULL
	}
};

// Device start with internal PROM
static UINT8 device_start_namcowsg(const DEV_GEN_CFG* cfg, DEV_INFO* retDevInf)
{
	namcowsg_state* info = calloc(1, sizeof(namcowsg_state));
	int clock_multiple;
	if (!info) return 0xFF;

	info->clock = cfg->clock;
	info->rate = info->clock;

	for (clock_multiple = 0; info->rate < INTERNAL_RATE; clock_multiple++)
	{
		info->rate <<= 1;
	}

	info->fracbits = clock_multiple + 15;

	memset(info->prom, 0x00, PROM_SIZE);
	
	info->mute_mask = 0x00;
	
	info->_devData.chipInf = info;
	INIT_DEVINF(retDevInf, &info->_devData, info->rate, &devDef);
	return 0x00;
}

// Device stop
static void device_stop_namcowsg(void* chip)
{
	free(chip);
}

// Device reset
static void device_reset_namcowsg(void* chip)
{
	namcowsg_state* info = (namcowsg_state*)chip;
	memset(info->voice, 0, sizeof(info->voice));
}

// Modified update function using internal PROM
static void namcowsg_update(void* param, UINT32 samples, DEV_SMPL** outputs)
{
	namcowsg_state* info = (namcowsg_state*)param;
	DEV_SMPL* buffer = outputs[0];
	DEV_SMPL* buffer2 = outputs[1];
	UINT32 i;

	for (i = 0; i < samples; i++)
	{
		INT32 mix = 0;
		int ch;

		for (ch = 0; ch < 3; ch++)
		{
			UINT16 prom_addr;
			INT8 sample;

			if ((info->voice[ch].volume == 0) || (info->mute_mask & (1 << ch)))
				continue;

			prom_addr = (info->voice[ch].waveform << 5) | 
						((info->voice[ch].addr >> info->fracbits) & 0x1F);
			sample = info->prom[prom_addr] - 8; // Convert to signed
			mix += sample * info->voice[ch].volume;

			if (info->voice[ch].volume)
				info->voice[ch].addr += info->voice[ch].freq;
		}

		buffer[i] = mix * 32; // Scale to 16-bit
		buffer2[i] = buffer[i]; // Mono output
	}
}

/* pacman register map
	0x05:       ch 0    waveform select
	0x0a:       ch 1    waveform select
	0x0f:       ch 2    waveform select

	0x10:       ch 0    the first voice has extra frequency bits
	0x11-0x14:  ch 0    frequency
	0x15:       ch 0    volume

	0x16-0x19:  ch 1    frequency
	0x1a:       ch 1    volume

	0x1b-0x1e:  ch 2    frequency
	0x1f:       ch 2    volume
*/

// Write handlers
static void namcowsg_write(void* chip, UINT8 address, UINT8 data)
{
	namcowsg_state* info = (namcowsg_state*)chip;
	int ch;

	data &= 0x0F; // only low nibble is used

	if (address < 0x10)
		ch = (address - 5) / 5;
	else if (address == 0x10)
		ch = 0;
	else
		ch = (address - 0x11) / 5;

	if (ch >= 3)
		return;

	switch (address - ch * 5)
	{
		case 0x05:
			info->voice[ch].waveform = data & 0x07;
			break;

		case 0x10:
			if (ch == 0)
				info->voice[ch].freq = (info->voice[ch].freq & 0xFFFF0) | data;
			break;
		case 0x11:
			info->voice[ch].freq = (info->voice[ch].freq & 0xFFF0F) | (data << 4);
			break;
		case 0x12:
			info->voice[ch].freq = (info->voice[ch].freq & 0xFF0FF) | (data << 8);
			break;
		case 0x13:
			info->voice[ch].freq = (info->voice[ch].freq & 0xF0FFF) | (data << 12);
			break;
		case 0x14:
			info->voice[ch].freq = (info->voice[ch].freq & 0x0FFFF) | (data << 16);
			break;

		case 0x15:
			info->voice[ch].volume = data;
			break;
	}
}

static void namcowsg_write_ram(void* chip, UINT8 address, UINT8 data)
{
	namcowsg_state *info = (namcowsg_state *)chip;
	info->prom[address & 0xFF] = data & 0x0F;
}

// Mute mask
static void namcowsg_set_mute_mask(void* chip, UINT32 mute_mask)
{
	namcowsg_state* info = (namcowsg_state*)chip;
	info->mute_mask = mute_mask & 0x07;
}

static void namcowsg_write_prom(void *info, UINT32 offset, UINT32 length, const UINT8* data)
{
	namcowsg_state *chip = (namcowsg_state *)info;
	
	if (offset >= PROM_SIZE)
		return;
	if (offset + length > PROM_SIZE)
		length = PROM_SIZE - offset;
	
	memcpy(chip->prom + offset, data, length);
}
