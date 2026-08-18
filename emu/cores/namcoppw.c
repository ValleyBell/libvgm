// license:BSD-3-Clause
// copyright-holders:Nicola Salmoria,Aaron Giles
/***************************************************************************

	NAMCO WSG sound driver.

	This driver handles the four known types of NAMCO wavetable sounds:
	- 3-voice mono (PROM-based design: Pac-Man, Pengo, Dig Dug, etc)
		- namcowsg.c
	- 8-voice quadrophonic (Pole Position 1, Pole Position 2)
		- namcoppw.c (this file)
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
#include "namcoppw.h"

#define PROM_SIZE 0x100 // 256 bytes (low nibble only)

#define INTERNAL_RATE 192000

typedef struct _namcoppw_state
{
	DEV_DATA _devData;
	
	struct
	{
		UINT32 freq;
		UINT8 volume[4];
		UINT8 waveform;
		UINT8 external;
		UINT32 addr;
	} voice[8];
	
	UINT8 regs[0x40];
	UINT8 prom[PROM_SIZE]; // Internal PROM storage
	UINT32 clock;
	UINT32 rate;
	UINT32 fracbits;
	UINT8 mute_mask;
} namcoppw_state;

static void namcoppw_update(void* param, UINT32 samples, DEV_SMPL** outputs);
static UINT8 device_start_namcoppw(const DEV_GEN_CFG* cfg, DEV_INFO* retDevInf);
static void device_stop_namcoppw(void* chip);
static void device_reset_namcoppw(void* chip);
static void namcoppw_set_mute_mask(void* chip, UINT32 mute_mask);
static UINT8 namcoppw_read(void* chip, UINT8 address);
static void namcoppw_write(void* chip, UINT8 address, UINT8 data);
static void namcoppw_write_prom(void *info, UINT32 offset, UINT32 length, const UINT8* data);

// Add PROM write handler
static DEVDEF_RWFUNC devFunc[] = {
	{RWF_REGISTER | RWF_READ, DEVRW_A8D8, 0, namcoppw_read},
	{RWF_REGISTER | RWF_WRITE, DEVRW_A8D8, 0, namcoppw_write},
	{RWF_CHN_MUTE | RWF_WRITE, DEVRW_ALL, 0, namcoppw_set_mute_mask},
	{RWF_MEMORY | RWF_WRITE, DEVRW_BLOCK, 0, namcoppw_write_prom},
	{0x00, 0x00, 0, NULL}
};

static DEV_DEF devDef = {
	"Namco Pole Position WSG", "MAME", FCC_MAME,
	device_start_namcoppw,
	device_stop_namcoppw,
	device_reset_namcoppw,
	namcoppw_update,

	NULL,   // SetOptionBits
	namcoppw_set_mute_mask,
	NULL,   // SetPanning
	NULL,   // SetSampleRateChangeCallback
	NULL,   // SetLoggingCallback
	NULL,   // LinkDevice

	devFunc
};

static const char* DeviceName(const DEV_GEN_CFG* devCfg)
{
	return "Namco Pole Position WSG";
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

const DEV_DECL sndDev_NAMCOPPW =
{
	DEVID_NAMCOPPW,
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
static UINT8 device_start_namcoppw(const DEV_GEN_CFG* cfg, DEV_INFO* retDevInf)
{
	namcoppw_state* info = calloc(1, sizeof(namcoppw_state));
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
static void device_stop_namcoppw(void* chip)
{
	free(chip);
}

// Device reset
static void device_reset_namcoppw(void* chip)
{
	namcoppw_state* info = (namcoppw_state*)chip;
	memset(info->voice, 0, sizeof(info->voice));
	memset(info->regs, 0, sizeof(info->regs));
}

// Modified update function using internal PROM
static void namcoppw_update(void* param, UINT32 samples, DEV_SMPL** outputs)
{
	namcoppw_state* info = (namcoppw_state*)param;
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
			UINT16 prom_addr;
			INT8 sample;
			UINT16 flvol, frvol, rlvol, rrvol;

			flvol = info->voice[ch].volume[0];
			frvol = info->voice[ch].volume[1];
			rlvol = info->voice[ch].volume[2];
			rrvol = info->voice[ch].volume[3];
			if ((info->voice[ch].external) ||
				((flvol == 0) && (frvol == 0) && (rlvol == 0) && (rrvol == 0)) ||
				(info->mute_mask & (1 << ch)))
				continue;

			prom_addr = (info->voice[ch].waveform << 5) | 
						((info->voice[ch].addr >> info->fracbits) & 0x1F);
			sample = info->prom[prom_addr] - 8; // Convert to signed
			lmix += sample * (flvol + rlvol);
			rmix += sample * (frvol + rrvol);

			info->voice[ch].addr += info->voice[ch].freq;
		}

		lbuffer[i] = lmix * 8; // Scale to 16-bit
		rbuffer[i] = rmix * 8; // 4 output channels (downmix to stereo for now)
	}
}

/* polepos register map
Note: even if there are 8 voices, the game doesn't use the first 2 because
it select the 54XX/52XX outputs on those channels

    0x00-0x01   ch 0    frequency
    0x02        ch 0    xxxx---- GAIN 2 volume
    0x03        ch 0    xxxx---- GAIN 3 volume
                        ----xxxx GAIN 4 volume

    0x04-0x07   ch 1

    .
    .
    .

    0x1c-0x1f   ch 7

    0x23        ch 0    xxxx---- GAIN 1 volume
                        -----xxx waveform select
                        ----x-xx channel output select
                                 0-7 (all the same, shared with waveform select) = wave
                                 8 = CHANL1 (54XX pins 17-20)
                                 9 = CHANL2 (54XX pins 8-11)
                                 A = CHANL3 (54XX pins 4-7)
                                 B = CHANL4 (52XX)
    0x27        ch 1
    0x2b        ch 2
    0x2f        ch 3
    0x33        ch 4
    0x37        ch 5
    0x3b        ch 6
    0x3f        ch 7
*/

// Write handlers
static void namcoppw_write(void* chip, UINT8 address, UINT8 data)
{
	namcoppw_state* info = (namcoppw_state*)chip;
	int ch;

	ch = (address & 0x1F) >> 2;

	info->regs[address] = data;

	switch (address & 0x23)
	{
		case 0x00:
			info->voice[ch].freq = (info->voice[ch].freq & 0xFF00) | data;
			break;
		case 0x01:
			info->voice[ch].freq = (info->voice[ch].freq & 0x00FF) | (data << 8);
			break;
		case 0x23:
			info->voice[ch].waveform = data & 0x07;
			// if 54XX or 52XX selected, silence this voice
			info->voice[ch].external = (data >> 3) & 0x01;
			// rear speakers ?
			info->voice[ch].volume[2] = (data >> 4) & 0x0F;
			break;
		case 0x02:
			info->voice[ch].volume[3] = (data >> 4) & 0x0F;
			break;
		case 0x03:
			// front speakers ?
			info->voice[ch].volume[0] = (data >> 4) & 0x0F;
			info->voice[ch].volume[1] = data & 0x0F;
			break;
	}
}

static UINT8 namcoppw_read(void* chip, UINT8 address)
{
	namcoppw_state* info = (namcoppw_state*)chip;
	return info->regs[address];
}

// Mute mask
static void namcoppw_set_mute_mask(void* chip, UINT32 mute_mask)
{
	namcoppw_state* info = (namcoppw_state*)chip;
	info->mute_mask = mute_mask & 0xFF;
}

static void namcoppw_write_prom(void *info, UINT32 offset, UINT32 length, const UINT8* data)
{
	namcoppw_state *chip = (namcoppw_state *)info;
	
	if (offset >= PROM_SIZE)
		return;
	if (offset + length > PROM_SIZE)
		length = PROM_SIZE - offset;
	
	memcpy(chip->prom + offset, data, length);
}
