// license:BSD-3-Clause
// copyright-holders:Nicola Salmoria,Aaron Giles
/***************************************************************************

	NAMCO WSG sound driver.

	This driver handles the four known types of NAMCO wavetable sounds:
	- 3-voice mono (PROM-based design: Pac-Man, Pengo, Dig Dug, etc)
		- namcowsg.c
	- 8-voice quadrophonic (Pole Position 1, Pole Position 2)
		- namcoppw.c
	- 8-voice mono (custom 15XX: Mappy, Dig Dug 2, etc)
		- namcoc15.c (this file)
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
#include "namcoc15.h"

#define PROM_SIZE 0x100 // 256 bytes (low nibble only)

#define INTERNAL_RATE 192000

typedef struct _namcoc15_state
{
	DEV_DATA _devData;
	
	struct
	{
		UINT32 freq;
		UINT8 volume;
		UINT8 waveform;
		UINT32 addr;
	} voice[8];
	
	UINT8 regs[0x40];
	UINT8 prom[PROM_SIZE]; // Internal PROM storage
	UINT32 clock;
	UINT32 rate;
	UINT32 fracbits;
	UINT8 mute_mask;
} namcoc15_state;

static void namcoc15_update(void* param, UINT32 samples, DEV_SMPL** outputs);
static UINT8 device_start_namcoc15(const DEV_GEN_CFG* cfg, DEV_INFO* retDevInf);
static void device_stop_namcoc15(void* chip);
static void device_reset_namcoc15(void* chip);
static void namcoc15_set_mute_mask(void* chip, UINT32 mute_mask);
static UINT8 namcoc15_read(void* chip, UINT8 address);
static void namcoc15_write(void* chip, UINT8 address, UINT8 data);
static void namcoc15_write_prom(void *info, UINT32 offset, UINT32 length, const UINT8* data);

// Add PROM write handler
static DEVDEF_RWFUNC devFunc[] = {
	{RWF_REGISTER | RWF_READ, DEVRW_A8D8, 0, namcoc15_read},
	{RWF_REGISTER | RWF_WRITE, DEVRW_A8D8, 0, namcoc15_write},
	{RWF_CHN_MUTE | RWF_WRITE, DEVRW_ALL, 0, namcoc15_set_mute_mask},
	{RWF_MEMORY | RWF_WRITE, DEVRW_BLOCK, 0, namcoc15_write_prom},
	{0x00, 0x00, 0, NULL}
};

static DEV_DEF devDef = {
	"Namco C15", "MAME", FCC_MAME,
	device_start_namcoc15,
	device_stop_namcoc15,
	device_reset_namcoc15,
	namcoc15_update,

	NULL,   // SetOptionBits
	namcoc15_set_mute_mask,
	NULL,   // SetPanning
	NULL,   // SetSampleRateChangeCallback
	NULL,   // SetLoggingCallback
	NULL,   // LinkDevice

	devFunc
};

static const char* DeviceName(const DEV_GEN_CFG* devCfg)
{
	return "Namco C15";
}

static UINT16 DeviceChannels(const DEV_GEN_CFG* devCfg)
{
	return 8;
}

static const char** DeviceChannelNames(const DEV_GEN_CFG* devCfg)
{
	return NULL;
}

static const DEVLINK_IDS* DeviceLinkIDs(const DEV_GEN_CFG* devCfg)
{
	return NULL;
}

const DEV_DECL sndDev_NAMCOC15 =
{
	DEVID_NAMCOC15,
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
static UINT8 device_start_namcoc15(const DEV_GEN_CFG* cfg, DEV_INFO* retDevInf)
{
	namcoc15_state* info = calloc(1, sizeof(namcoc15_state));
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
static void device_stop_namcoc15(void* chip)
{
	free(chip);
}

// Device reset
static void device_reset_namcoc15(void* chip)
{
	namcoc15_state* info = (namcoc15_state*)chip;
	memset(info->voice, 0, sizeof(info->voice));
	memset(info->regs, 0, sizeof(info->regs));
}

// Modified update function using internal PROM
static void namcoc15_update(void* param, UINT32 samples, DEV_SMPL** outputs)
{
	namcoc15_state* info = (namcoc15_state*)param;
	DEV_SMPL* buffer = outputs[0];
	DEV_SMPL* buffer2 = outputs[1];
	UINT32 i;

	for (i = 0; i < samples; i++)
	{
		INT32 mix = 0;
		int ch;

		for (ch = 0; ch < 8; ch++)
		{
			UINT16 prom_addr;
			INT8 sample;

			if ((info->voice[ch].volume == 0) || (info->mute_mask & (1 << ch)))
				continue;

			prom_addr = (info->voice[ch].waveform << 5) | 
						((info->voice[ch].addr >> info->fracbits) & 0x1F);
			sample = info->prom[prom_addr] - 8; // Convert to signed
			mix += sample * info->voice[ch].volume;

			info->voice[ch].addr += info->voice[ch].freq;
		}

		buffer[i] = mix * 16; // Scale to 16-bit
		buffer2[i] = buffer[i]; // Mono output
	}
}

/* 15XX register map
	0x03        ch 0    volume
	0x04-0x05   ch 0    frequency
	0x06        ch 0    waveform select & frequency

	0x0b        ch 1    volume
	0x0c-0x0d   ch 1    frequency
	0x0e        ch 1    waveform select & frequency

	.
	.
	.

	0x3b        ch 7    volume
	0x3c-0x3d   ch 7    frequency
	0x3e        ch 7    waveform select & frequency

Grobda also stuffs values into register offset 0x02 with a frequency of zero
to make 15XX channels act like a 4-bit DAC instead of waveform voices. This
has been emulated by allowing writes to set the upper counter bits directly.
Possibly offsets 0x00 and 0x01 can be used to set the fractional bits.
*/

// Write handlers
static void namcoc15_write(void* chip, UINT8 address, UINT8 data)
{
	namcoc15_state* info = (namcoc15_state*)chip;
	int ch;

	ch = address >> 3;
	if (ch >= 8)
		return;

	info->regs[address] = data;

	switch (address & 0x07)
	{
		case 0x02:
			info->voice[ch].addr &= (1 << info->fracbits) - 1;
			info->voice[ch].addr |= (data & 0x1F) << info->fracbits;
			break;
		case 0x03:
			info->voice[ch].volume = data & 0x0F;
			break;
		case 0x04:
			info->voice[ch].freq = (info->voice[ch].freq & 0xFFF00) | data;
			break;
		case 0x05:
			info->voice[ch].freq = (info->voice[ch].freq & 0xF00FF) | (data << 8);
			break;
		case 0x06:
			info->voice[ch].freq = (info->voice[ch].freq & 0x0FFFF) | ((data & 0x0F) << 16);
			info->voice[ch].waveform = (data >> 4) & 0x07;
			break;
	}
}

static UINT8 namcoc15_read(void* chip, UINT8 address)
{
	namcoc15_state* info = (namcoc15_state*)chip;
	return info->regs[address];
}

// Mute mask
static void namcoc15_set_mute_mask(void* chip, UINT32 mute_mask)
{
	namcoc15_state* info = (namcoc15_state*)chip;
	info->mute_mask = mute_mask & 0xFF;
}

static void namcoc15_write_prom(void *info, UINT32 offset, UINT32 length, const UINT8* data)
{
	namcoc15_state *chip = (namcoc15_state *)info;
	
	if (offset >= PROM_SIZE)
		return;
	if (offset + length > PROM_SIZE)
		length = PROM_SIZE - offset;
	
	memcpy(chip->prom + offset, data, length);
}
