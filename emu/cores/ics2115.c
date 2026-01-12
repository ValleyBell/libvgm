// license:BSD-3-Clause
// copyright-holders:Alex Marshall, nimitz, austere
/*
    ICS2115 by Raiden II team (c) 2010
    members: austere, nimitz, Alex Marshal

    Original driver by O. Galibert, ElSemi

    Use tab size = 4 for your viewing pleasure.

    TODO:
    - Verify BYTE/ROMEN pin behavior
    - DRAM, DMA, MIDI interface is unimplemented
    - Verify interrupt, envelope, timer period
    - Verify unemulated registers

*/

#include <stdlib.h>
#include <string.h>	// for memset
#include <stddef.h>	// for NULL

#include "../../stdtype.h"
#include "../EmuStructs.h"
#include "../SoundDevs.h"
#include "../EmuCores.h"
#include "../snddef.h"
#include "../EmuHelper.h"
#include "ics2115.h"


// ======================> ics2115_device

#define REVISION 0x1
#define VOLUME_BITS 15
#define RAMP_SHIFT 6
//pan law level
//log2(256*128) = 15 for -3db + 1 must be confirmed by real hardware owners
#define PAN_LEVEL 16


#define ACCESSING_BITS_8_15 ((mem_mask & 0xff00) != 0)
#define ACCESSING_BITS_0_7 ((mem_mask & 0x00ff) != 0)

static void ics2115_update(void *param, UINT32 samples, DEV_SMPL **outputs);
static UINT8 device_start_ics2115(const DEV_GEN_CFG* cfg, DEV_INFO* retDevInf);
static void device_stop_ics2115(void *info);
static void device_reset_ics2115(void *info);

static UINT8 ics2115_byte_r(void *info, UINT8 offset);
static void ics2115_byte_w(void *info, UINT8 offset, UINT8 data);

static void ics2115_alloc_rom(void* info, UINT32 memsize);
static void ics2115_write_rom(void *info, UINT32 offset, UINT32 length, const UINT8* data);

static void ics2115_set_mute_mask(void *info, UINT32 MuteMask);
static UINT32 ics2115_get_mute_mask(void *info);
static void ics2115_set_srchg_cb(void *info, DEVCB_SRATE_CHG CallbackFunc, void* DataPtr);

static DEVDEF_RWFUNC devFunc[] =
{
	{RWF_REGISTER | RWF_WRITE, DEVRW_A8D8, 0, ics2115_byte_w},
	{RWF_REGISTER | RWF_READ, DEVRW_A8D8, 0, ics2115_byte_r},
	{RWF_MEMORY | RWF_WRITE, DEVRW_MEMSIZE, 0, ics2115_alloc_rom},
	{RWF_MEMORY | RWF_WRITE, DEVRW_BLOCK, 0, ics2115_write_rom},
	{RWF_CHN_MUTE | RWF_WRITE, DEVRW_ALL, 0, ics2115_set_mute_mask},
	{0x00, 0x00, 0, NULL}
};
static DEV_DEF devDef =
{
	"ICS2115", "MAME", FCC_MAME,
	
	device_start_ics2115,
	device_stop_ics2115,
	device_reset_ics2115,
	ics2115_update,
	
	NULL,	// SetOptionBits
	ics2115_set_mute_mask,
	NULL,	// SetPanning
	ics2115_set_srchg_cb,	// SetSampleRateChangeCallback
	NULL,	// SetLoggingCallback
	NULL,	// LinkDevice
	
	devFunc,	// rwFuncs
};

static const char* DeviceName(const DEV_GEN_CFG* devCfg)
{
	return "ICS2115";
}

static UINT16 DeviceChannels(const DEV_GEN_CFG* devCfg)
{
	return 32;
}

static const char** DeviceChannelNames(const DEV_GEN_CFG* devCfg)
{
	return NULL;
}

static const DEVLINK_IDS* DeviceLinkIDs(const DEV_GEN_CFG* devCfg)
{
	return NULL;
}

const DEV_DECL sndDev_ICS2115 =
{
	DEVID_ICS2115,
	DeviceName,
	DeviceChannels,
	DeviceChannelNames,
	DeviceLinkIDs,
	{	// cores
		&devDef,
		NULL
	}
};

INLINE UINT8 count_leading_zeros_32(UINT32 val)
{
	UINT8 count;
	if (!val) return 32U;
	for (count = 0; (INT32)val >= 0; count++) val <<= 1;
	return count;
}

typedef struct _ics2115_state ics2115_state;

typedef struct {
	ics2115_state *device;

	struct {
		INT32 left;
		UINT32 acc, start, end; // address counters (20.9 fixed point)
		UINT16 fc;              // frequency (6.9 fixed point)
		UINT8 ctl, saddr;
	} osc;

	struct {
		INT32 left;
		UINT32 add;
		UINT32 start, end;
		UINT32 acc;
		UINT16 regacc;
		UINT8 incr;
		UINT8 pan, mode;
	} vol;

	union {
		struct {
			UINT8 ulaw       : 1;   // compressed sample format
			UINT8 stop       : 1;   // stops wave + vol envelope
			UINT8 eightbit   : 1;   // 8 bit sample format
			UINT8 loop       : 1;   // loop enable
			UINT8 loop_bidir : 1;   // bi-directional loop enable
			UINT8 irq        : 1;   // enable IRQ generation
			UINT8 invert     : 1;   // invert direction
			UINT8 irq_pending: 1;   // (read only?) IRQ pending
			// IRQ on variable?
		} bitflags;
		UINT8 value;
	} osc_conf;

	union {
		struct {
			UINT8 done       : 1;   // indicates ramp has stopped
			UINT8 stop       : 1;   // stops the ramp
			UINT8 rollover   : 1;   // rollover (TODO)
			UINT8 loop       : 1;   // loop enable
			UINT8 loop_bidir : 1;   // bi-directional loop enable
			UINT8 irq        : 1;   // enable IRQ generation
			UINT8 invert     : 1;   // invert direction
			UINT8 irq_pending: 1;   // (read only?) IRQ pending
			// noenvelope == (done | stop)
		} bitflags;
		UINT8 value;
	} vol_ctrl;

	// Possibly redundant state. => improvements of wavetable logic
	// may lead to its elimination.
	struct {
		bool on;
		int ramp;       // 100 0000 = 0x40 maximum
	} state;

	UINT16 regs[0x20]; // channel registers

	UINT8  Muted;
} ics2115_voice;

/*
typedef struct {
	UINT8 scale, preset;
	//emu_timer *timer;
	UINT64 period;  // in nsec
} ics2115_timer;
*/

struct _ics2115_state
{
	DEV_DATA _devData;

	void (*irq_func)(void *, UINT8);	// IRQ callback
	void *irq_param;

	DEVCB_SRATE_CHG SmpRateFunc;
	void* SmpRateData;

	UINT8 *rom;
	UINT32 rom_size;
	UINT32 rom_mask;

	ics2115_voice voice[32];
	//ics2115_timer timer[2];

	// tables
	INT16 ulaw[256];
	UINT16 volume[4096];
	UINT16 panlaw[256];

	/* global sound parameters */
	UINT32 clock;
	UINT32 output_rate;

	/* chip registers */
	UINT8 active_osc;
	UINT8 osc_select;
	UINT8 reg_select;
	UINT8 irq_enabled;
	UINT8 irq_pending;
	UINT8 irq_on;

	UINT16 regs[0x40]; // global registers

	/*
	    Unknown variable, seems to be effected by 0x12. Further investigation
	    Required.
	*/
	UINT8 vmode;
};

// internal register helper functions
static void reg_write(ics2115_state *chip, UINT16 data, UINT16 mem_mask);
//static void recalc_timer(ics2115_state *chip, int timer);

// stream helper functions
static UINT8 read_sample(ics2115_state *chip, ics2115_voice *voice, UINT32 addr) { return chip->rom[((voice->osc.saddr & 0x0F) << 20) | (addr & 0xFFFFF)]; }

INLINE UINT32 ics2115_get_output_rate(ics2115_state *chip)
{
	return (chip->clock / 32) / (chip->active_osc + 1);
}

//#define ICS2115_DEBUG
//#define ICS2115_ISOLATE 6


//-------------------------------------------------
//  device_start - device-specific startup
//-------------------------------------------------

static UINT8 device_start_ics2115(const DEV_GEN_CFG* cfg, DEV_INFO* retDevInf)
{
	ics2115_state *chip;
	int i;
	UINT16 lut[8];
	UINT16 lut_initial;

	chip = (ics2115_state *)calloc(1, sizeof(ics2115_state));
	if (chip == NULL)
		return 0xFF;

	chip->irq_func = NULL;
	chip->irq_param = NULL;
	chip->rom = NULL;
	chip->rom_size = 0x00;
	chip->rom_mask = 0x00;

	chip->clock = cfg->clock;
	chip->output_rate = ics2115_get_output_rate(chip);
	ics2115_set_mute_mask(chip, 0x00000000);
	
	chip->_devData.chipInf = chip;
	INIT_DEVINF(retDevInf, &chip->_devData, chip->output_rate, &devDef);

	//Exact formula as per patent 5809466
	//This seems to give the ok fit but it is not good enough.
	/*double maxvol = ((1 << VOLUME_BITS) - 1) * pow(2., (double)1/0x100);
	for (int i = 0; i < 0x1000; i++)
	       chip->volume[i] = floor(maxvol * pow(2.,(double)i/256 - 16) + 0.5);
	*/

	//austere's table, derived from patent 5809466:
	//See section V starting from page 195
	//Subsection F (column 124, page 198) onwards
	for (i = 0; i<4096; i++)
		chip->volume[i] = ((0x100 | (i & 0xff)) << (VOLUME_BITS-9)) >> (15 - (i>>8));

	//u-Law table as per MIL-STD-188-113
	lut_initial = 33 << 2;   //shift up 2-bits for 16-bit range.
	for (i = 0; i < 8; i++)
		lut[i] = (lut_initial << i) - lut_initial;

	for (i = 0; i < 256; i++)
	{
		UINT8 exponent = (~i >> 4) & 0x07;
		UINT8 mantissa = ~i & 0x0f;
		INT16 value = lut[exponent] + (mantissa << (exponent + 3));
		chip->ulaw[i] = (i & 0x80) ? -value : value;
		chip->panlaw[i] = PAN_LEVEL - (31 - count_leading_zeros_32(i)); //chip->panlaw[i] = PAN_LEVEL - log2(i)
	}
	chip->panlaw[0] = 0xfff; //all bits to one when no pan
	return 0x00;
}

static void device_stop_ics2115(void *info)
{
	ics2115_state *chip = (ics2115_state *)info;
	
	free(chip->rom);	chip->rom = NULL;
	free(chip);
	
	return;
}

//-------------------------------------------------
//  device_reset - device-specific reset
//-------------------------------------------------

static void device_reset_ics2115(void *info)
{
	ics2115_state *chip = (ics2115_state *)info;
	int i;
	UINT32 muteMask;
	
	muteMask = ics2115_get_mute_mask(chip);
	chip->irq_enabled = 0;
	chip->irq_pending = 0;
	//possible re-suss
	chip->active_osc = 31;
	chip->osc_select = 0;
	chip->reg_select = 0;
	chip->vmode = 0;
	chip->irq_on = false;
	memset(chip->voice, 0, sizeof(chip->voice));
	/*
	for (auto & elem : chip->timer)
	{
		elem.timer->adjust(attotime::never);
		elem.period = 0;
		elem.scale = 0;
		elem.preset = 0;
	}
	*/
	for (i = 0; i < 32; i++)
	{
		ics2115_voice *v = &chip->voice[i];
		v->osc_conf.value = 2;
		v->osc.fc = 0;
		v->osc.acc = 0;
		v->osc.start = 0;
		v->osc.end = 0;
		v->osc.ctl = 0;
		v->osc.saddr = 0;
		v->vol.acc = 0;
		v->vol.incr = 0;
		v->vol.start = 0;
		v->vol.end = 0;
		v->vol.pan = 0x7f;
		v->vol_ctrl.value = 1;
		v->vol.mode = 0;
		v->state.on = false;
		v->state.ramp = 0;
	}
	ics2115_set_mute_mask(chip, muteMask);
	chip->output_rate = ics2115_get_output_rate(chip);
	if (chip->SmpRateFunc != NULL)
		chip->SmpRateFunc(chip->SmpRateData, chip->output_rate);
	
}


static void keyon(ics2115_state *chip)
{
#ifdef ICS2115_ISOLATE
	if (chip->osc_select != ICS2115_ISOLATE)
		return;
#endif
	//set initial condition (may need to invert?) -- does NOT work since these are set to zero even
	//no ramp up...
	chip->voice[chip->osc_select].state.ramp = 0x40;

#ifdef ICS2115_DEBUG
	logerror("[%02d vs:%04x ve:%04x va:%04x vi:%02x vc:%02x os:%06x oe:%06x oa:%06x of:%04x SA:%02x oc:%02x][%04x]\n", chip->osc_select,
			chip->voice[chip->osc_select].vol.start >> 10,
			chip->voice[chip->osc_select].vol.end >> 10,
			chip->voice[chip->osc_select].vol.acc >> 10,
			chip->voice[chip->osc_select].vol.incr,
			chip->voice[chip->osc_select].vol_ctrl.value,
			chip->voice[chip->osc_select].osc.start >> 12,
			chip->voice[chip->osc_select].osc.end >> 12,
			chip->voice[chip->osc_select].osc.acc >> 12,
			chip->voice[chip->osc_select].osc.fc,
			chip->voice[chip->osc_select].osc.saddr,
			chip->voice[chip->osc_select].osc_conf.value,
			m_volume[(chip->voice[chip->osc_select].vol.acc >> 14)]
			);
#endif
	//testing memory corruption issue with mame stream
	//logerror("m_volume[0x%x]=0x%x\n", mastervolume, m_volume[mastervolume]);
}

static void recalc_irq(ics2115_state *chip)
{
	int i;

	//Suspect
	UINT8 irq = (chip->irq_pending & chip->irq_enabled) ? 1 : 0;
	for (i = 0; (!irq) && (i < 32); i++)
		irq |= (chip->voice[i].vol_ctrl.bitflags.irq_pending || chip->voice[i].osc_conf.bitflags.irq_pending) ? 1 : 0;
	chip->irq_on = irq;

	if (chip->irq_func != NULL)
		chip->irq_func(chip->irq_param, irq);
}

/*
    Using next-state logic from column 126 of patent 5809466.
    VOL(L) = vol.acc
    VINC = vol.inc
    DIR = invert
    BC = boundary cross (start or end )
    BLEN = bi directional loop enable
    LEN loop enable
    UVOL   LEN   BLEN    DIR     BC      Next VOL(L)
    0      x     x       x       x       VOL(L) // no change no vol envelope
    1      x     x       0       0       VOL(L) + VINC // forward dir no bc
    1      x     x       1       0       VOL(L) - VINC // invert no bc
    1      0     x       x       1       VOL(L) // no env len no vol envelope
   ----------------------------------------------------------------------------
    1      1     0       0       1       start - ( end - (VOL(L)  + VINC) )
    1      1     0       1       1       end + ( (VOL(L) - VINC) - start)
    1      1     1       0       1       end + (end - (VOL(L) + VINC) ) // here
    1      1     1       1       1       start - ( (VOL(L) - VINC)- start)
*/
static int update_volume_envelope(ics2115_voice *voice)
{
	UINT8 bc;
	int ret;

	// test for boundary cross
	bc = 0;
	if (voice->vol.acc >= voice->vol.end || voice->vol.acc <= voice->vol.end)
		bc = 1;

	ret = 0;
	if (voice->vol_ctrl.bitflags.done || voice->vol_ctrl.bitflags.stop)
		return ret;

	if (voice->vol_ctrl.bitflags.invert)
	{
		voice->vol.acc -= voice->vol.add;
		voice->vol.left = voice->vol.acc - voice->vol.start;
	}
	else
	{
		voice->vol.acc += voice->vol.add;
		voice->vol.left = voice->vol.end - voice->vol.acc;
	}

	if (voice->vol.left > 0)
		return ret;

	if (voice->vol_ctrl.bitflags.irq)
	{
		voice->vol_ctrl.bitflags.irq_pending = true;
		ret = 1;
	}

	if (voice->osc_conf.bitflags.eightbit)
		return ret;

	if (voice->vol_ctrl.bitflags.loop)
	{
		if (bc)
		{
			if (!voice->vol_ctrl.bitflags.loop_bidir)
			{
				if (!voice->vol_ctrl.bitflags.invert)
					voice->vol.acc = voice->vol.start - (voice->vol.end - (voice->vol.acc + voice->vol.incr));   //  uvol = 1*     len = 1*   blen =  0     dir =  0     bc =  1*      start - ( end - (VOL(L)  + VINC) )
				else
					voice->vol.acc = voice->vol.end + ((voice->vol.acc - voice->vol.incr) - voice->vol.start);   //         1            1    blen =  0     dir =  1           1       end + ( (VOL(L) - VINC) - start)
			}
			else
			{
				if (!voice->vol_ctrl.bitflags.invert)
					voice->vol.acc = voice->vol.end + (voice->vol.end - (voice->vol.acc + voice->vol.incr));     //         1            1     blen = 1      dir = 0           1       end + (end - (VOL(L) + VINC) )
				else
					voice->vol.acc = voice->vol.start - ((voice->vol.acc - voice->vol.incr) - voice->vol.start); //         1            1     beln = 1      dir = 1           1       start - ( (VOL(L) - VINC)- start)
			}
		}
	}
	else
		voice->vol_ctrl.bitflags.done = true;

	return ret;
}

/*UINT32 ics2115_device::ics2115_voice::next_address()
{
    //Patent 6,246,774 B1, Column 111, Row 25
    //LEN   BLEN    DIR     BC      NextAddress
    //x     x       0       0       add+fc
    //x     x       1       0       add-fc
    //0     x       x       1       add
    //1     0       0       1       start-(end-(add+fc))
    //1     0       1       1       end+((add+fc)-start)
    //1     1       0       1       end+(end-(add+fc))
    //1     1       1       1       start-((add-fc)-start)

}*/


static int update_oscillator(ics2115_voice *voice)
{
	int ret;
	ret = 0;
	if (voice->osc_conf.bitflags.stop)
		return ret;
	if (voice->osc_conf.bitflags.invert)
	{
		voice->osc.acc -= voice->osc.fc << 2;
		voice->osc.left = voice->osc.acc - voice->osc.start;
	}
	else
	{
		voice->osc.acc += voice->osc.fc << 2;
		voice->osc.left = voice->osc.end - voice->osc.acc;
	}
	// > instead of >= to stop crackling?
	if (voice->osc.left > 0)
		return ret;
	if (voice->osc_conf.bitflags.irq)
	{
		voice->osc_conf.bitflags.irq_pending = true;
		ret = 1;
	}
	if (voice->osc_conf.bitflags.loop)
	{
		if (voice->osc_conf.bitflags.loop_bidir)
			voice->osc_conf.bitflags.invert = !voice->osc_conf.bitflags.invert;
		//else
		//    logerror("click!\n");

		if (voice->osc_conf.bitflags.invert)
		{
			voice->osc.acc = voice->osc.end + voice->osc.left;
			voice->osc.left = voice->osc.acc - voice->osc.start;
		}
		else
		{
			voice->osc.acc = voice->osc.start - voice->osc.left;
			voice->osc.left = voice->osc.end - voice->osc.acc;
		}
	}
	else
	{
		voice->state.on = false;
		voice->osc_conf.bitflags.stop = true;
		if (!voice->osc_conf.bitflags.invert)
			voice->osc.acc = voice->osc.end;
		else
			voice->osc.acc = voice->osc.start;
	}
	return ret;
}

//TODO: proper interpolation for 8-bit samples (looping)
static INT32 get_sample(ics2115_state *chip, ics2115_voice *voice)
{
	UINT32 curaddr = voice->osc.acc >> 12;
	UINT32 nextaddr;
	INT16 sample1, sample2;
	INT32 diff;
	UINT16 fract;

	if (voice->state.on && voice->osc_conf.bitflags.loop && !voice->osc_conf.bitflags.loop_bidir &&
			(voice->osc.left < (voice->osc.fc << 2)))
	{
		//logerror("C?[%x:%x]", voice->osc.left, voice->osc.acc);
		nextaddr = voice->osc.start >> 12;
	}
	else
		nextaddr = curaddr + 2;

	if (voice->osc_conf.bitflags.ulaw)
	{
		sample1 = chip->ulaw[read_sample(chip, voice, curaddr)];
		sample2 = chip->ulaw[read_sample(chip, voice, curaddr + 1)];
	}
	else if (voice->osc_conf.bitflags.eightbit)
	{
		sample1 = ((INT8)read_sample(chip, voice, curaddr)) << 8;
		sample2 = ((INT8)read_sample(chip, voice, curaddr + 1)) << 8;
	}
	else
	{
		sample1 = read_sample(chip, voice, curaddr + 0) | (((INT8)read_sample(chip, voice, curaddr + 1)) << 8);
		sample2 = read_sample(chip, voice, nextaddr+ 0) | (((INT8)read_sample(chip, voice, nextaddr+ 1)) << 8);
		//sample2 = read_sample(chip, voice, curaddr + 2) | (((INT8)read_sample(chip, voice, curaddr + 3)) << 8);
	}

	//linear interpolation as in US patent 6,246,774 B1, column 2 row 59
	//LEN=1, BLEN=0, DIR=0, start+end interpolation
	diff = sample2 - sample1;
	fract = (voice->osc.acc >> 3) & 0x1ff;

	//no need for interpolation since it's around 1 note a cycle?
	//if (!fract)
	//    return sample1;

	//return sample1;
	return (((INT32)sample1 << 9) + diff * fract) >> 9;
}

static bool playing(ics2115_voice *voice)
{
	return voice->state.on && !(voice->osc_conf.bitflags.stop);
}

static void update_ramp(ics2115_voice *voice)
{
	//slow attack
	if (voice->state.on && !voice->osc_conf.bitflags.stop)
	{
		if (voice->state.ramp < 0x40)
			voice->state.ramp += 0x1;
		else
			voice->state.ramp = 0x40;
	}
	//slow release
	else
	{
		if (voice->state.ramp)
			voice->state.ramp -= 0x1;
	}
}

static UINT8 fill_output(ics2115_state *chip, ics2115_voice *voice, UINT32 samples, INT32 *loutput, INT32 *routput)
{
	UINT32 i;
	UINT8 irq_invalid = 0;
	UINT16 fine = 1 << (3 * (voice->vol.incr >> 6));
	voice->vol.add = (voice->vol.incr & 0x3f) << (10 - fine);

	for (i = 0; i < samples; i++)
	{
		UINT32 volacc = (voice->vol.acc >> 14) & 0xfff;
		INT16 vlefti = volacc - chip->panlaw[255 - voice->vol.pan]; // left index from acc - pan law
		INT16 vrighti = volacc - chip->panlaw[voice->vol.pan]; // right index from acc - pan law
		//check negative values so no cracks, is it a hardware feature ?
		UINT16 vleft = vlefti > 0 ? (chip->volume[vlefti] * voice->state.ramp >> RAMP_SHIFT) : 0;
		UINT16 vright = vrighti > 0 ? (chip->volume[vrighti] * voice->state.ramp >> RAMP_SHIFT) : 0;

		//From GUS doc:
		//In general, it is necessary to remember that all voices are being summed in to the
		//final output, even if they are not running.  This means that whatever data value
		//that the voice is pointing at is contributing to the summation.
		//(austere note: this will of course fix some of the glitches due to multiple transition)
		INT32 sample = get_sample(chip, voice);

		//15-bit volume + (5-bit worth of 32 channel sum) + 16-bit samples = 4-bit extra
		//if (playing(voice))
		if ((!chip->vmode || playing(voice)) && (!voice->Muted))
		{
			loutput[i] += (sample * vleft) >> (5 + VOLUME_BITS);
			routput[i] += (sample * vright) >> (5 + VOLUME_BITS);
		}

		update_ramp(voice);
		if (playing(voice))
		{
			if (update_oscillator(voice))
				irq_invalid = 1;
			if (update_volume_envelope(voice))
				irq_invalid = 1;
		}
	}
	return irq_invalid;
}

static void ics2115_update(void *param, UINT32 samples, DEV_SMPL **outputs)
{
	ics2115_state *chip = (ics2115_state *)param;
	UINT8 irq_invalid;
	int osc;

	memset(outputs[0], 0, samples * sizeof(DEV_SMPL));
	memset(outputs[1], 0, samples * sizeof(DEV_SMPL));
	if (chip->rom == NULL)
		return;

	irq_invalid = 0;
	for (osc = 0; osc <= chip->active_osc; osc++)
	{
		ics2115_voice *voice = &chip->voice[osc];

#ifdef ICS2115_ISOLATE
		if (osc != ICS2115_ISOLATE)
			continue;
#endif
/*
#ifdef ICS2115_DEBUG
        UINT32 curaddr = ((voice->osc.saddr << 20) & 0xffffff) | (voice->osc.acc >> 12);
        INT32 sample = get_sample(voice);
        logerror("[%06x=%04x]", curaddr, (INT16)sample);
#endif
*/
		if (fill_output(chip, voice, samples, outputs[0], outputs[1]))
			irq_invalid = 1;

#ifdef ICS2115_DEBUG
		if (voice->playing())
		{
			logerror("%d", osc);
			if (voice->osc_conf.bitflags.invert)
				logerror("+");
			else if ((voice->osc.fc >> 1) > 0x1ff)
				logerror("*");
			logerror(" ");

			/*int min = 0x7fffffff, max = 0x80000000;
			double average = 0;
			for (int i = 0; i < samples; i++)
			{
			    if (outputs[0][i] > max) max = outputs[0][i];
			    if (outputs[0][i] < min) min = outputs[0][i];
			    average += fabs(outputs[0][i]);
			}
			average /= samples;
			average /= 1 << 16;
			logerror("<Mi:%d Mx:%d Av:%g>", min >> 16, max >> 16, average);*/
		}
#endif
	}

#ifdef ICS2115_DEBUG
	logerror("|");
#endif

	if (irq_invalid)
		recalc_irq(chip);

}

//Helper Function (Reads off current register)
static UINT16 reg_read(ics2115_state *chip)
{
	int i;

	UINT16 ret = 0;
	ics2115_voice *voice = &chip->voice[chip->osc_select];

	switch (chip->reg_select)
	{
		case 0x00: // [osc] Oscillator Configuration
			ret = voice->osc_conf.value;
			if (voice->state.on)
			{
				ret |= 8;
			}
			else
			{
				ret &= ~8;
			}
			ret <<= 8;
			break;

		case 0x01: // [osc] Wavesample frequency
			// freq = fc*33075/1024 in 32 voices mode, fc*44100/1024 in 24 voices mode
			//ret = v->Osc.FC;
			ret = voice->osc.fc;
			break;

		case 0x02: // [osc] Wavesample loop start high
			//TODO: are these returns valid? might be 0x00ff for this one...
			ret = (voice->osc.start >> 16) & 0xffff;
			break;

		case 0x03: // [osc] Wavesample loop start low
			ret = (voice->osc.start >> 0) & 0xff00;
			break;

		case 0x04: // [osc] Wavesample loop end high
			ret = (voice->osc.end >> 16) & 0xffff;
			break;

		case 0x05: // [osc] Wavesample loop end low
			ret = (voice->osc.end >> 0) & 0xff00;
			break;

		case 0x06: // [osc] Volume Increment
			ret = voice->vol.incr;
			break;

		case 0x07: // [osc] Volume Start
			ret = voice->vol.start >> (10+8);
			break;

		case 0x08: // [osc] Volume End
			ret = voice->vol.end >> (10+8);
			break;

		case 0x09: // [osc] Volume accumulator
			//ret = v->Vol.Acc;
			ret = voice->vol.acc  >> (10);
			break;

		case 0x0a: // [osc] Wavesample address
			ret = (voice->osc.acc >> 16) & 0xffff;
			break;

		case 0x0b: // [osc] Wavesample address
			ret = (voice->osc.acc >> 0) & 0xfff8;
			break;


		case 0x0c: // [osc] Pan
			ret = voice->vol.pan << 8;
			break;

		/* DDP3 code (trap15's reversal) */
		/* 0xA13's work:
		    res = read() & 0xC3;
		    if (!(res & 2)) res |= 1;
		    e = d = res;
		*/
		/* 0xA4F's work:
		    while(!(read() & 1))
		*/
		case 0x0d: // [osc] Volume Envelope Control
			//ret = v->Vol.Ctl | ((v->state & FLAG_STATE_VOLIRQ) ? 0x81 : 1);
			// may expect |8 on voice irq with &40 == 0
			// may expect |8 on reg 0 on voice irq with &80 == 0
			// ret = 0xff;
			if (!chip->vmode)
				ret = voice->vol_ctrl.bitflags.irq ? 0x81 : 0x01;
			else
				ret = 0x01;
			//ret = voice->vol_ctrl.bitflags.value | 0x1;
			ret <<= 8;
			break;

		case 0x0e: // Active Voices
			ret = chip->active_osc;
			break;

		case 0x0f: // [osc] Interrupt source/oscillator
		{
			ret = 0xff;
			for (i = 0; i <= chip->active_osc; i++)
			{
				ics2115_voice *v = &chip->voice[i];
				if (v->osc_conf.bitflags.irq_pending || v->vol_ctrl.bitflags.irq_pending)
				{
					ret = i | 0xe0;
					ret &= v->vol_ctrl.bitflags.irq_pending ? (~0x40) : 0xff;
					ret &= v->osc_conf.bitflags.irq_pending ? (~0x80) : 0xff;
					recalc_irq(chip);
					if (v->osc_conf.bitflags.irq_pending)
					{
						v->osc_conf.bitflags.irq_pending = 0;
						ret &= ~0x80;
					}
					if (v->vol_ctrl.bitflags.irq_pending)
					{
						v->vol_ctrl.bitflags.irq_pending = 0;
						ret &= ~0x40;
					}
					break;
				}
			}
			ret <<= 8;
			break;
		}

		case 0x10: // [osc] Oscillator Control
			ret = voice->osc.ctl << 8;
			break;

		case 0x11: // [osc] Wavesample static address 27-20
			ret = voice->osc.saddr << 8;
			break;

		case 0x40: // Timer 0 clear irq
		case 0x41: // Timer 1 clear irq
			//TODO: examine this suspect code
			/*
			ret = chip->timer[chip->reg_select & 0x1].preset;
			chip->irq_pending &= ~(1 << (chip->reg_select & 0x1));
			recalc_irq(chip);
			*/
			ret = 0;
			break;

		case 0x43: // Timer status
			ret = chip->irq_pending & 3;
			break;

		// case 0x48: // Accumulator Monitor Status
		// case 0x49: // Accumulator Monitor Data

		case 0x4a: // IRQ Pending
			ret = chip->irq_pending;
			break;

		case 0x4b: // Address of Interrupting Oscillator
			ret = 0x80;
			break;

		case 0x4c: // Chip Revision
			ret = REVISION;
			break;

		// case 0x4d: // System Control

		// case 0x50: // MIDI Data Register
		// case 0x51: // MIDI Status Register
		// case 0x52: // Host Data Register
		// case 0x53: // Host Status Register
		// case 0x54: // MIDI Emulation Interrupt Control
		// case 0x55: // Host Emulation Interrupt Control
		// case 0x56: // Host/MIDI Emulation Interrupt Status
		// case 0x57: // Emulation Mode

		default:
#ifdef ICS2115_DEBUG
			logerror("ICS2115: Unhandled read %x\n", chip->reg_select);
#endif
			ret = 0;
			break;
	}
	return ret;
}

static void reg_write(ics2115_state *chip, UINT16 data, UINT16 mem_mask)
{
	ics2115_voice *voice = &chip->voice[chip->osc_select];

	switch (chip->reg_select)
	{
		case 0x00: // [osc] Oscillator Configuration
			if (ACCESSING_BITS_8_15)
			{
				voice->osc_conf.value &= 0x80;
				voice->osc_conf.value |= (data >> 8) & 0x7f;
			}
			break;

		case 0x01: // [osc] Wavesample frequency
			// freq = fc*33075/1024 in 32 voices mode, fc*44100/1024 in 24 voices mode
			if (ACCESSING_BITS_8_15)
				voice->osc.fc = (voice->osc.fc & 0x00fe) | (data & 0xff00);
			if (ACCESSING_BITS_0_7)
				//last bit not used!
				voice->osc.fc = (voice->osc.fc & 0xff00) | (data & 0x00fe);
			break;

		case 0x02: // [osc] Wavesample loop start high
			if (ACCESSING_BITS_8_15)
				voice->osc.start = (voice->osc.start & 0x00ffffff) | ((data & 0xff00) << 16);
			if (ACCESSING_BITS_0_7)
				voice->osc.start = (voice->osc.start & 0xff00ffff) | ((data & 0x00ff) << 16);
			break;

		case 0x03: // [osc] Wavesample loop start low
			if (ACCESSING_BITS_8_15)
				voice->osc.start = (voice->osc.start & 0xffff00ff) | (data & 0xff00);
			// This is unused?
			//if (ACCESSING_BITS_0_7)
				//voice->osc.start = (voice->osc.start & 0xffffff00) | (data & 0);
			break;

		case 0x04: // [osc] Wavesample loop end high
			if (ACCESSING_BITS_8_15)
				voice->osc.end = (voice->osc.end & 0x00ffffff) | ((data & 0xff00) << 16);
			if (ACCESSING_BITS_0_7)
				voice->osc.end = (voice->osc.end & 0xff00ffff) | ((data & 0x00ff) << 16);
			break;

		case 0x05: // [osc] Wavesample loop end low
			if (ACCESSING_BITS_8_15)
				voice->osc.end = (voice->osc.end & 0xffff00ff) | (data & 0xff00);
			// lsb is unused?
			break;

		case 0x06: // [osc] Volume Increment
			if (ACCESSING_BITS_8_15)
				voice->vol.incr = (data >> 8) & 0xff;
			break;

		case 0x07: // [osc] Volume Start
			if (ACCESSING_BITS_0_7)
				voice->vol.start = (data & 0xff) << (10+8);
			break;

		case 0x08: // [osc] Volume End
			if (ACCESSING_BITS_0_7)
				voice->vol.end = (data & 0xff) << (10+8);
			break;

		case 0x09: // [osc] Volume accumulator
			if (ACCESSING_BITS_8_15)
				voice->vol.regacc = (voice->vol.regacc & 0x00ff) | (data & 0xff00);
			if (ACCESSING_BITS_0_7)
				voice->vol.regacc = (voice->vol.regacc & 0xff00) | (data & 0x00ff);
			voice->vol.acc = voice->vol.regacc << 10;
			break;

		case 0x0a: // [osc] Wavesample address high
#ifdef ICS2115_DEBUG
#ifdef ICS2115_ISOLATE
			if (chip->osc_select == ICS2115_ISOLATE)
#endif
				logerror("<%d:oa:H=%x>", chip->osc_select, data);
#endif
			if (ACCESSING_BITS_8_15)
				voice->osc.acc = (voice->osc.acc & 0x00ffffff) | ((data & 0xff00) << 16);
			if (ACCESSING_BITS_0_7)
				voice->osc.acc = (voice->osc.acc & 0xff00ffff) | ((data & 0x00ff) << 16);
			break;

		case 0x0b: // [osc] Wavesample address low
#ifdef ICS2115_DEBUG
#ifdef ICS2115_ISOLATE
			if (chip->osc_select == ICS2115_ISOLATE)
#endif
				logerror("<%d:oa:L=%x>", chip->osc_select, data);
#endif
			if (ACCESSING_BITS_8_15)
				voice->osc.acc = (voice->osc.acc & 0xffff00ff) | (data & 0xff00);
			if (ACCESSING_BITS_0_7)
				voice->osc.acc = (voice->osc.acc & 0xffffff00) | (data & 0x00f8);
			break;

		case 0x0c: // [osc] Pan
			if (ACCESSING_BITS_8_15)
				voice->vol.pan = (data >> 8) & 0xff;
			break;

		case 0x0d: // [osc] Volume Envelope Control
			if (ACCESSING_BITS_8_15)
			{
				voice->vol_ctrl.value &= 0x80;
				voice->vol_ctrl.value |= (data >> 8) & 0x7f;
			}
			break;

		case 0x0e: // Active Voices
			//Does this value get added to 1? Not sure. Could trace for writes of 32.
			if (ACCESSING_BITS_8_15)
			{
				chip->active_osc = (data >> 8) & 0x1f; // & 0x1f ? (Guessing)
				chip->output_rate = ics2115_get_output_rate(chip);
				if (chip->SmpRateFunc != NULL)
					chip->SmpRateFunc(chip->SmpRateData, chip->output_rate);
			}
			break;
		//2X8 ?
		case 0x10: // [osc] Oscillator Control
			//Could this be 2X9?
			//[7 R | 6 M2 | 5 M1 | 4-2 Reserve | 1 - Timer 2 Strt | 0 - Timer 1 Strt]

			if (ACCESSING_BITS_8_15)
			{
				data >>= 8;
				voice->osc.ctl = (UINT8)data;
				voice->state.on = !voice->osc.ctl; // some early PGM games need this
				if (!data)
					keyon(chip);
				//guessing here
				else if (data == 0xf)
				{
#ifdef ICS2115_DEBUG
#ifdef ICS2115_ISOLATE
					if (chip->osc_select == ICS2115_ISOLATE)
#endif
					if (!voice->osc_conf.bitflags.stop || !voice->vol_ctrl.bitflags.stop)
						logerror("[%02d STOP]\n", chip->osc_select);
#endif
					if (!chip->vmode)
					{
						//try to key it off as well!
						voice->osc_conf.bitflags.stop = true;
						voice->vol_ctrl.bitflags.stop = true;
					}
				}
#ifdef ICS2115_DEBUG
				else
					logerror("ICS2115: Unhandled* data write %d onto 0x10.\n", data);
#endif
			}
			break;

		case 0x11: // [osc] Wavesample static address 27-20
			if (ACCESSING_BITS_8_15)
				//v->Osc.SAddr = (data >> 8);
				voice->osc.saddr = (data >> 8);
			break;
		case 0x12:
			//Could be per voice! -- investigate.
			if (ACCESSING_BITS_8_15)
				chip->vmode = (data >> 8);
			break;
		case 0x40: // Timer 1 Preset
		case 0x41: // Timer 2 Preset
		/*
			if (ACCESSING_BITS_0_7)
			{
				chip->timer[chip->reg_select & 0x1].preset = data & 0xff;
				recalc_timer(chip->reg_select & 0x1);
			}
			break;
		*/
		case 0x42: // Timer 1 Prescale
		case 0x43: // Timer 2 Prescale
		/*
			if (ACCESSING_BITS_0_7)
			{
				chip->timer[chip->reg_select & 0x1].scale = data & 0xff;
				recalc_timer(chip->reg_select & 0x1);
			}
			break;
		*/
		// case 0x44: // DMA Start Address Low (11:4)
		// case 0x45: // DMA Start Address Low (19:12)
		// case 0x46: // DMA Start Address Low (21:20)
		// case 0x47: // DMA Control

		case 0x4a: // IRQ Enable
			if (ACCESSING_BITS_0_7)
			{
				chip->irq_enabled = data & 0xff;
				recalc_irq(chip);
			}
			break;

		// case 0x4c: // Memory Config
		// case 0x4d: // System Control

		case 0x4f: // Oscillator Address being Programmed
			if (ACCESSING_BITS_0_7)
				chip->osc_select = (data & 0xff) % (1+chip->active_osc);
			break;

		// case 0x50: // MIDI Data Register
		// case 0x51: // MIDI Control Register
		// case 0x52: // Host Data Register
		// case 0x53: // Host Control Register
		// case 0x54: // MIDI Emulation Interrupt Control
		// case 0x55: // Host Emulation Interrupt Control
		// case 0x57: // Emulation Mode

		default:
#ifdef ICS2115_DEBUG
			logerror("ICS2115: Unhandled write %x onto %x [voice = %d]\n", data, chip->reg_select, chip->osc_select);
#endif
			break;
	}
}

static UINT8 ics2115_byte_r(void *info, UINT8 offset)
{
	ics2115_state *chip = (ics2115_state *)info;
	UINT8 ret = 0;

	switch (offset)
	{
		case 0:
			//TODO: check this suspect code
			if (chip->irq_on)
			{
				int i;
				ret |= 0x80;
				if (chip->irq_enabled && (chip->irq_pending & 3))
					ret |= 1;
				for (i = 0; i <= chip->active_osc; i++)
				{
					if (//chip->voice[i].vol_ctrl.bitflags.irq_pending ||
						chip->voice[i].osc_conf.bitflags.irq_pending)
					{
						ret |= 2;
						break;
					}
				}
			}

			break;
		case 1:
			ret = chip->reg_select;
			break;
		case 2:
			ret = (UINT8)(reg_read(chip));
			break;
		case 3:
			ret = reg_read(chip) >> 8;
			break;
		default:
#ifdef ICS2115_DEBUG
			logerror("ICS2115: Unhandled memory read at %x\n", offset);
#endif
			break;
	}
	return ret;
}

static void ics2115_byte_w(void *info, UINT8 offset, UINT8 data)
{
	ics2115_state *chip = (ics2115_state *)info;
	switch (offset)
	{
		case 1:
			chip->reg_select = data;
			break;
		case 2:
			reg_write(chip, data, 0x00ff);
			break;
		case 3:
			reg_write(chip, data << 8, 0xff00);
			break;
		default:
#ifdef ICS2115_DEBUG
			logerror("ICS2115: Unhandled memory write %02x to %x\n", data, offset);
#endif
			break;
	}
}

/*
TIMER_CALLBACK_MEMBER( ics2115_device::timer_cb_0 )
{
	if (!(chip->irq_pending & (1 << 0)))
	{
		chip->irq_pending |= 1 << 0;
		recalc_irq();
	}
}

TIMER_CALLBACK_MEMBER( ics2115_device::timer_cb_1 )
{
	if (!(chip->irq_pending & (1 << 1)))
	{
		chip->irq_pending |= 1 << 1;
		recalc_irq();
	}
}

static void recalc_timer(ics2115_state *chip, int timer)
{
	UINT64 period  = ((chip->timer[timer].scale & 0x1f) + 1) * (chip->timer[timer].preset + 1);
	period = period << (4 + (chip->timer[timer].scale >> 5));

	if (chip->timer[timer].period != period)
	{
		attotime tp = attotime::from_ticks(period, chip->clock);
		logerror("Timer %d period %dns (%dHz)\n", timer, int(tp.as_double()*1e9), int(1/tp.as_double()));
		chip->timer[timer].period = period;
		chip->timer[timer].timer->adjust(tp, 0, tp);
	}
}
*/

static void ics2115_alloc_rom(void* info, UINT32 memsize)
{
	ics2115_state *chip = (ics2115_state *)info;
	
	if (chip->rom_size == memsize)
		return;
	
	chip->rom = (UINT8*)realloc(chip->rom, memsize);
	chip->rom_size = memsize;
	memset(chip->rom, 0, memsize);
	
	chip->rom_mask = pow2_mask(memsize);
	
	return;
}

static void ics2115_write_rom(void *info, UINT32 offset, UINT32 length, const UINT8* data)
{
	ics2115_state *chip = (ics2115_state *)info;
	
	if (offset > chip->rom_size)
		return;
	if (offset + length > chip->rom_size)
		length = chip->rom_size - offset;
	
	memcpy(chip->rom + offset, data, length);
	
	return;
}

static void ics2115_set_mute_mask(void *info, UINT32 MuteMask)
{
	ics2115_state *chip = (ics2115_state *)info;
	UINT8 CurChn;
	
	for (CurChn = 0; CurChn < 32; CurChn ++)
		chip->voice[CurChn].Muted = (MuteMask >> CurChn) & 0x01;
	
	return;
}

static UINT32 ics2115_get_mute_mask(void *info)
{
	ics2115_state *chip = (ics2115_state *)info;
	UINT32 muteMask;
	UINT8 CurChn;
	
	muteMask = 0x00000000;
	for (CurChn = 0; CurChn < 32; CurChn ++)
		muteMask |= (chip->voice[CurChn].Muted << CurChn);
	
	return muteMask;
}

static void ics2115_set_srchg_cb(void *info, DEVCB_SRATE_CHG CallbackFunc, void* DataPtr)
{
	ics2115_state *chip = (ics2115_state *)info;

	// set Sample Rate Change Callback routine
	chip->SmpRateFunc = CallbackFunc;
	chip->SmpRateData = DataPtr;
	
	return;
}
