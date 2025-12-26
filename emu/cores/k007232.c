// SPDX-License-Identifier: BSD-3-Clause
// copyright-holders:Nicola Salmoria,Hiromitsu Shioya, Mao, cam900
/*********************************************************/
/*    Konami PCM controller                              */
/*********************************************************/

/*
  Changelog, Hiromitsu Shioya 02/05/2002
    fixed start address decode timing. (sample loop bug.)

  Changelog, Mish, August 1999:
    Removed interface support for different memory regions per channel.
    Removed interface support for differing channel volume.

    Added bankswitching.
    Added support for multiple chips.

    (NB: Should different memory regions per channel be needed, the bankswitching function can set this up).

  Changelog, Nicola, August 1999:
    Added Support for the k007232_VOL() macro.
    Added external port callback, and functions to set the volume of the channels
*/



/**********************************************************************************************
    Konami 007232 PCM controller (Full Working Implementation for libvgm)
	This is essentially a mix between MAME code and improvements by Mao and cam900.
	References:
	https://gitlab.com/cam900/vgsound_emu/-/tree/main/vgsound_emu/src/k007232
	https://git.redump.net/mame/tree/src/devices/sound/k007232.cpp
	https://git.redump.net/mame/tree/src/devices/sound/k007232.h
	https://github.com/furrtek/SiliconRE/tree/master/Konami/007232
	M1's K007232 source code. https://vgmrips.net/forum/viewtopic.php?t=110
	https://github.com/tildearrow/furnace/blob/master/extern/vgsound_emu-modified/vgsound_emu/src/k007232/k007232.cpp
***********************************************************************************************/

#include <stdlib.h>
#include <string.h>
#include "../../stdtype.h"
#include "../EmuStructs.h"
#include "../SoundDevs.h"
#include "../EmuCores.h"
#include "../snddef.h"
#include "../EmuHelper.h"
#include "k007232.h"

#define K007232_PCM_MAX   2
#define K007232_CLOCKDIV  128
#define K007232_ADDR_MASK 0x1FFFF

typedef struct {
	UINT8  vol[2];    // [0]=left, [1]=right
	UINT32 addr;      // current PCM address (17 bits)
	INT32  counter;
	UINT32 start;     // start address (17 bits)
	UINT16 step;      // frequency/step value (12 bits)
	UINT32 bank;      // base bank address (upper bits, shifted left by 17)
	UINT8  play;      // playing flag
	UINT8  mute;
} K007232_Channel;

typedef struct {
	DEV_DATA    _devData;

	K007232_Channel channel[K007232_PCM_MAX];
	UINT8 wreg[0x10];
	UINT8 *rom;
	UINT32 rom_size;
	UINT32 rom_mask;

	UINT8 loop_en;       // loop control register

	void (*port_write_cb)(UINT8 data); // external port write callback
} k007232_state;

// --- Forward declarations ---
static UINT8 device_start_k007232(const DEV_GEN_CFG* cfg, DEV_INFO* retDevInf);
static void device_stop_k007232(void* chip);
static void device_reset_k007232(void* chip);
static void k007232_update(void* chip, UINT32 samples, DEV_SMPL **outputs);
static void k007232_write(void* chip, UINT8 offset, UINT8 data);
static UINT8 k007232_read(void* chip, UINT8 offset);
static void k007232_write_rom(void* chip, UINT32 offset, UINT32 length, const UINT8* data);
static void k007232_alloc_rom(void* chip, UINT32 memsize);
static void k007232_set_mute_mask(void* chip, UINT32 MuteMask);
void k007232_set_port_write_cb(void* chip, void (*cb)(UINT8 data));	// TODO: integrate into DEVDEF_RWFUNC list

// --- Device definition ---
static DEVDEF_RWFUNC devFunc[] =
{
	{RWF_REGISTER | RWF_WRITE, DEVRW_A8D8, 0, k007232_write},
	{RWF_REGISTER | RWF_READ, DEVRW_A8D8, 0, k007232_read},
	{RWF_MEMORY | RWF_WRITE, DEVRW_BLOCK, 0, k007232_write_rom},
	{RWF_MEMORY | RWF_WRITE, DEVRW_MEMSIZE, 0, k007232_alloc_rom},
	{RWF_CHN_MUTE | RWF_WRITE, DEVRW_ALL, 0, k007232_set_mute_mask},
	{0, 0, 0, NULL}
};
static DEV_DEF devDef =
{
	"K007232", "MAME", FCC_MAME,

	device_start_k007232,
	device_stop_k007232,
	device_reset_k007232,
	k007232_update,
	
	NULL,	// SetOptionBits
	k007232_set_mute_mask,
	NULL,	// SetPanning
	NULL,	// SetSampleRateChangeCallback
	NULL,	// SetLoggingCallback
	NULL,	// LinkDevice
	
	devFunc
};

static const char* DeviceName(const DEV_GEN_CFG* devCfg)
{
	return "K007232";
}

static UINT16 DeviceChannels(const DEV_GEN_CFG* devCfg)
{
	return K007232_PCM_MAX;
}

static const char** DeviceChannelNames(const DEV_GEN_CFG* devCfg)
{
	return NULL;
}

static const DEVLINK_IDS* DeviceLinkIDs(const DEV_GEN_CFG* devCfg)
{
	return NULL;
}

const DEV_DECL sndDev_K007232 =
{
	DEVID_K007232,
	DeviceName,
	DeviceChannels,
	DeviceChannelNames,
	DeviceLinkIDs,
	{	// cores
		&devDef,
		NULL
	}
};


// --- Core implementation ---
static UINT8 device_start_k007232(const DEV_GEN_CFG* cfg, DEV_INFO* retDevInf)
{
	k007232_state* chip = (k007232_state*)calloc(1, sizeof(k007232_state));
	UINT32 rate = cfg->clock / K007232_CLOCKDIV;
	int i;

	chip->rom = NULL;
	chip->rom_size = 0;
	chip->rom_mask = 0;

	for (i = 0; i < K007232_PCM_MAX; i++) {
		chip->channel[i].vol[0] = 255;
		chip->channel[i].vol[1] = 255;
		chip->channel[i].mute = 0;
	}
												
	chip->port_write_cb = NULL;

	INIT_DEVINF(retDevInf, &chip->_devData, rate, &devDef);
	chip->_devData.chipInf = chip;
	return 0;
}

static void device_stop_k007232(void* chip)
{
	k007232_state* c = (k007232_state*)chip;
	if (c->rom) free(c->rom);
	free(chip);
}

static void device_reset_k007232(void* chip)
{
	k007232_state* c = (k007232_state*)chip;
	int i;

	memset(c->wreg, 0, sizeof(c->wreg));
	c->loop_en = 0;
	for (i = 0; i < K007232_PCM_MAX; i++) {
		K007232_Channel* ch = &c->channel[i];
		ch->addr = 0;
		ch->start = 0;
		ch->counter = 0x1000;
		ch->step = 0;
		ch->play = 0;
		ch->vol[0] = 255;
		ch->vol[1] = 255;
	}

	c->channel[0].vol[0] = 255; c->channel[0].vol[1] = 0;
	c->channel[1].vol[0] = 0; c->channel[1].vol[1] = 255;
}

// --- Streamed update ---
static void k007232_update(void* chip, UINT32 samples, DEV_SMPL **outputs)
{
	k007232_state* c = (k007232_state*)chip;
	UINT32 i, j;

	if(c->rom == NULL)
	{
		memset(outputs[0], 0, samples*sizeof(*outputs[0]));
		memset(outputs[1], 0, samples*sizeof(*outputs[1]));
		return;
	}

	for (j = 0; j < samples; j++) {
		INT32 lsum = 0, rsum = 0;
		for (i = 0; i < K007232_PCM_MAX; i++) {
			K007232_Channel* ch = &c->channel[i];
			if (ch->play && !ch->mute) {
				UINT32 pcm_addr = ch->bank + (ch->addr & K007232_ADDR_MASK);
				UINT8 value;
				INT16 out;

				if (pcm_addr >= c->rom_size)
					continue;

				value = c->rom[pcm_addr];
				out = (value & 0x7F) - 0x40;
				lsum += out * ch->vol[0];
				rsum += out * ch->vol[1];

				ch->counter -= 32;
				while (ch->counter < 0 && ch->play) {
					ch->counter += 0x1000 - ch->step;
					ch->addr++;
					pcm_addr = ch->bank + (ch->addr & K007232_ADDR_MASK);
					if (pcm_addr >= c->rom_size) {
						ch->play = 0;
						break;
					}
					value = c->rom[pcm_addr];
					if ((value & 0x80) || (ch->addr > K007232_ADDR_MASK)) {
						if (c->loop_en & (1 << i))
							ch->addr = ch->start;
						else
							ch->play = 0;
					}
				}
			}
		}
		outputs[0][j] = lsum;
		outputs[1][j] = rsum;
	}
}

// --- Register interface ---
static void k007232_write(void* chip, UINT8 offset, UINT8 data)
{
	k007232_state* c = (k007232_state*)chip;
	int ch = offset / 6;
	int reg_base = ch * 6;
	K007232_Channel* v = (ch < 2) ? &c->channel[ch] : NULL;

	if (offset < (sizeof(c->wreg) / sizeof(c->wreg[0])))
		c->wreg[offset] = data;
	switch (offset) {
	case 0x00: case 0x06: // Pitch LSB
	case 0x01: case 0x07: // Pitch MSB
	{
		UINT8 pitch_lsb = c->wreg[reg_base + 0];
		UINT8 pitch_msb = c->wreg[reg_base + 1];
		UINT8 mode = (pitch_msb >> 4) & 0x03;
		switch (mode) {
		case 0x00: // 12-bit frequency mode (normal) (not implemented / dummy)
			v->step = ((pitch_msb & 0x0F) << 8) | pitch_lsb;
			break;
		case 0x01: // 8-bit frequency mode (inverted) (not implemented / dummy)
			v->step = (256 - pitch_lsb) << 4;
			break;
		case 0x02: // 4-bit frequency mode (not implemented / dummy)
			v->step = (16 - (pitch_msb & 0x0F)) << 8;
			break;
		case 0x03: // Reserved/invalid
		default:
			v->step = ((pitch_msb & 0x0F) << 8) | pitch_lsb;
			break;
		}
		break;
	}
	case 0x02: case 0x08: // Start address LSB
	case 0x03: case 0x09: // Start address MID
	case 0x04: case 0x0A: // Start address MSB
		v->start = ((c->wreg[reg_base + 4] & 0x01) << 16) | (c->wreg[reg_base + 3] << 8) | c->wreg[reg_base + 2];
		break;
	case 0x05: case 0x0B: // Key on trigger (play sound)
		v->play = 1;
		v->addr = v->start;
		v->counter = 0x1000;
		break;
	case 0x0C: // External port write (usually volume/pan nibble) 
		if (c->port_write_cb != NULL)
			c->port_write_cb(data);
		break;
	case 0x0D: // Loop enable
		c->loop_en = data;
		break;
	case 0x10: // Left volume for channel 0
	case 0x11: // Right volume for channel 0
	case 0x12: // Left volume for channel 1
	case 0x13: // Right volume for channel 1
		ch = (offset >> 1) & 1;
		c->channel[ch].vol[offset & 1] = data;
		break;
	case 0x14: // Bankswitch registers
	case 0x15:
		ch = offset & 1;
		c->channel[ch].bank = data << 17;
		break;
	}
}

static UINT8 k007232_read(void* chip, UINT8 offset)
{
	k007232_state* c = (k007232_state*)chip;
	if (offset == 5 || offset == 11)
	{
		K007232_Channel* v = &c->channel[(offset >= 6) ? 1 : 0];
		v->play = 1;
		v->addr = v->start;
		v->counter = 0x1000;
	}
	return 0;
}

// --- ROM loading and memory ---
static void k007232_alloc_rom(void* chip, UINT32 memsize)
{
	k007232_state* c = (k007232_state*)chip;

	if (c->rom_size == memsize)
		return;

	c->rom = (UINT8*)realloc(c->rom, memsize);
	c->rom_size = memsize;
	memset(c->rom, 0xFF, memsize);

	c->rom_mask = pow2_mask(memsize);

	return;
}

static void k007232_write_rom(void* chip, UINT32 offset, UINT32 length, const UINT8* data)
{
	k007232_state* c = (k007232_state*)chip;
	if (offset >= c->rom_size) return;
	if (offset + length > c->rom_size) length = c->rom_size - offset;
	memcpy(&c->rom[offset], data, length);
}

// --- Mute mask, callback, etc. ---
static void k007232_set_mute_mask(void* chip, UINT32 MuteMask)
{
	k007232_state* c = (k007232_state*)chip;
	int i;
	for (i = 0; i < K007232_PCM_MAX; i++)
		c->channel[i].mute = (MuteMask & (1 << i)) ? 1 : 0;
}

// --- Optional: attach external volume/pan callback (for host integration like Ajax and Chequered Flag) ---
void k007232_set_port_write_cb(void* chip, void (*cb)(UINT8 data))
{
	k007232_state* c = (k007232_state*)chip;
	c->port_write_cb = cb;
}
