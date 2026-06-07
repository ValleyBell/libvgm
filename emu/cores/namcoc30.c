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
		- namcoc15.c
	- 8-voice stereo (System 1)
		- namcoc30.c (this file)

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
#include "namcoc30.h"

#define RAM_SIZE 0x100 // 256 bytes

#define INTERNAL_RATE 192000

typedef struct _namcoc30_state
{
	DEV_DATA _devData;
	
	struct
	{
		UINT32 freq;
		UINT8 volume[2];
		UINT8 waveform;
		UINT32 addr;
		UINT8 noise_enable;
		INT32 noise_hold;
		UINT32 noise_seed;
		UINT8 noise_state;
	} voice[8];
	
	UINT8 regs[0x40];
	UINT8 ram[RAM_SIZE]; // Internal RAM
	UINT32 clock;
	UINT32 rate;
	UINT32 fracbits;
	UINT8 stereo;
	UINT8 mute_mask;
} namcoc30_state;

static void namcoc30_update(void* param, UINT32 samples, DEV_SMPL** outputs);
static UINT8 device_start_namcoc30(const DEV_GEN_CFG* cfg, DEV_INFO* retDevInf);
static void device_stop_namcoc30(void* chip);
static void device_reset_namcoc30(void* chip);
static void namcoc30_set_mute_mask(void* chip, UINT32 mute_mask);
static UINT8 namcoc30_read(void* chip, UINT8 address);
static void namcoc30_write(void* chip, UINT8 address, UINT8 data);
static void namcoc30_write_ram(void* chip, UINT8 address, UINT8 data);
static void namcoc30_write_ramblock(void *info, UINT32 offset, UINT32 length, const UINT8* data);

// Add PROM write handler
static DEVDEF_RWFUNC devFunc[] = {
	{RWF_REGISTER | RWF_READ, DEVRW_A8D8, 0, namcoc30_read},
	{RWF_REGISTER | RWF_WRITE, DEVRW_A8D8, 0, namcoc30_write},
	{RWF_CHN_MUTE | RWF_WRITE, DEVRW_ALL, 0, namcoc30_set_mute_mask},
	{RWF_MEMORY | RWF_WRITE, DEVRW_A16D8, 0, namcoc30_write_ram},
	{RWF_MEMORY | RWF_WRITE, DEVRW_BLOCK, 0, namcoc30_write_ramblock},
	{0x00, 0x00, 0, NULL}
};

static DEV_DEF devDef = {
	"Namco C30", "MAME", FCC_MAME,
	device_start_namcoc30,
	device_stop_namcoc30,
	device_reset_namcoc30,
	namcoc30_update,

	NULL,   // SetOptionBits
	namcoc30_set_mute_mask,
	NULL,   // SetPanning
	NULL,   // SetSampleRateChangeCallback
	NULL,   // SetLoggingCallback
	NULL,   // LinkDevice

	devFunc
};

static const char* DeviceName(const DEV_GEN_CFG* devCfg)
{
	return "Namco C30";
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

const DEV_DECL sndDev_NAMCOC30 =
{
	DEVID_NAMCOC30,
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
static UINT8 device_start_namcoc30(const DEV_GEN_CFG* cfg, DEV_INFO* retDevInf)
{
	namcoc30_state* info = calloc(1, sizeof(namcoc30_state));
	int clock_multiple;
	if (!info) return 0xFF;

	info->clock = cfg->clock;
	info->rate = info->clock;
	info->stereo = cfg->flags;

	for (clock_multiple = 0; info->rate < INTERNAL_RATE; clock_multiple++)
	{
		info->rate <<= 1;
	}

	info->fracbits = clock_multiple + 15;

	memset(info->ram, 0x00, RAM_SIZE);
	
	info->mute_mask = 0x00;
	
	info->_devData.chipInf = info;
	INIT_DEVINF(retDevInf, &info->_devData, info->rate, &devDef);
	return 0x00;
}

// Device stop
static void device_stop_namcoc30(void* chip)
{
	free(chip);
}

// Device reset
static void device_reset_namcoc30(void* chip)
{
	namcoc30_state* info = (namcoc30_state*)chip;
	int ch;

	memset(info->voice, 0, sizeof(info->voice));
	memset(info->regs, 0, sizeof(info->regs));
	for (ch = 0; ch < 8; ch++)
	{
		// initialize noise seed
		info->voice[ch].noise_seed = 1;
	}
}

// Modified update function using internal PROM
static void namcoc30_update(void* param, UINT32 samples, DEV_SMPL** outputs)
{
	namcoc30_state* info = (namcoc30_state*)param;
	DEV_SMPL* lbuffer = outputs[0];
	DEV_SMPL* rbuffer = outputs[1];
	UINT32 i;

	for (i = 0; i < samples; i++)
	{
		INT32 lmix = 0;
		INT32 rmix = 0;
		int ch;

		for (ch = 0; ch < 8; ch++)
		{
			UINT8 lvol, rvol;
			UINT16 ram_addr;
			INT8 sample;
			INT32 noise_freq, hold_time, noise_delta, noise_lout, noise_rout, noise_cnt;

			lvol = info->voice[ch].volume[0];
			rvol = info->stereo ? info->voice[ch].volume[1] : 0;

			if (((lvol == 0) && (rvol == 0)) || (info->mute_mask & (1 << ch)))
				continue;

			if (info->voice[ch].noise_enable)
			{
				noise_freq = info->voice[ch].freq & 0xFF;
				hold_time = 1 << (info->fracbits - 16);
				noise_delta = noise_freq << 4;
				noise_lout = 0x07 * (lvol >> 1);
				noise_rout = info->stereo ? (0x07 * (rvol >> 1)) : 0;
				if (info->voice[ch].noise_state)
				{
					lmix += noise_lout;
					if (info->stereo)
						rmix += noise_rout;
				}
				else
				{
					lmix -= noise_lout;
					if (info->stereo)
						rmix -= noise_rout;
				}

				if (info->voice[ch].noise_hold)
				{
					info->voice[ch].noise_hold--;
					continue;
				}

				info->voice[ch].noise_hold = hold_time;
				info->voice[ch].addr += noise_delta;
				noise_cnt = (info->voice[ch].addr >> 12);
				info->voice[ch].addr &= (1 << 12) - 1;
				for (; noise_cnt > 0; noise_cnt--)
				{
					if ((info->voice[ch].noise_seed + 1) & 2)
						info->voice[ch].noise_state ^= 1;
					if (info->voice[ch].noise_seed & 1)
						info->voice[ch].noise_seed ^= 0x28000;
					info->voice[ch].noise_seed >>= 1;
				}
			}
			else
			{
				ram_addr = (info->voice[ch].waveform << 5) | 
							((info->voice[ch].addr >> info->fracbits) & 0x1F);
				sample = ((info->ram[ram_addr >> 1] >> ((~ram_addr & 1) << 2)) & 0x0F) - 8; // Convert to signed
				lmix += sample * lvol;
				if (info->stereo)
					rmix += sample * rvol;
				info->voice[ch].addr += info->voice[ch].freq;
			}
		}

		lbuffer[i] = lmix * 16; // Scale to 16-bit
		rbuffer[i] = info->stereo ? (rmix * 16) : lbuffer[i]; // Stereo output
	}
}

/* namcos1 register map
    0x00        ch 0    left volume
    0x01        ch 0    waveform select & frequency
    0x02-0x03   ch 0    frequency
    0x04        ch 0    right volume AND
    0x04        ch 1    noise sw

    0x08        ch 1    left volume
    0x09        ch 1    waveform select & frequency
    0x0a-0x0b   ch 1    frequency
    0x0c        ch 1    right volume AND
    0x0c        ch 2    noise sw

    .
    .
    .

    0x38        ch 7    left volume
    0x39        ch 7    waveform select & frequency
    0x3a-0x3b   ch 7    frequency
    0x3c        ch 7    right volume AND
    0x3c        ch 0    noise sw
*/

// Write handlers
static void namcoc30_write(void* chip, UINT8 address, UINT8 data)
{
	namcoc30_state* info = (namcoc30_state*)chip;
	int ch;

	ch = address >> 3;
	if (ch >= 8)
		return;

	info->regs[address] = data;

	switch (address & 0x07)
	{
		case 0x00:
			info->voice[ch].volume[0] = data & 0x0F;
			break;
		case 0x01:
			info->voice[ch].freq = (info->voice[ch].freq & 0x0FFFF) | ((data & 0x0F) << 16);
			info->voice[ch].waveform = (data >> 4) & 0x0F;
			break;
		case 0x02:
			info->voice[ch].freq = (info->voice[ch].freq & 0xF00FF) | (data << 8);
			break;
		case 0x03:
			info->voice[ch].freq = (info->voice[ch].freq & 0xFFF00) | data;
			break;
		case 0x04:
			info->voice[ch].volume[1] = data & 0x0F;
			info->voice[(ch + 1) & 7].noise_enable = (data >> 7) & 0x01;
			break;
	}
}

static UINT8 namcoc30_read(void* chip, UINT8 address)
{
	namcoc30_state* info = (namcoc30_state*)chip;
	int ch, reg;

	// reading from register 5 returns the counter (used by baraduke)
	ch = address >> 3;
	if (ch >= 8)
		return 0;

	reg = address & 0x07;
	if (reg == 0x05)
	{
		return (info->voice[ch].addr >> info->fracbits) & 0x1F;
	}
	return info->regs[address];
}

static void namcoc30_write_ram(void* chip, UINT8 address, UINT8 data)
{
	namcoc30_state *info = (namcoc30_state *)chip;
	info->ram[address & 0xFF] = data;
}

// Mute mask
static void namcoc30_set_mute_mask(void* chip, UINT32 mute_mask)
{
	namcoc30_state* info = (namcoc30_state*)chip;
	info->mute_mask = mute_mask & 0xFF;
}

static void namcoc30_write_ramblock(void *info, UINT32 offset, UINT32 length, const UINT8* data)
{
	namcoc30_state *chip = (namcoc30_state *)info;
	
	if (offset >= RAM_SIZE)
		return;
	if (offset + length > RAM_SIZE)
		length = RAM_SIZE - offset;
	
	memcpy(chip->ram + offset, data, length);
}
