// license:MIT
// copyright-holders:Lior Halphon
#define _USE_MATH_DEFINES
#include "emutypes.h"
#include "../../_stdbool.h"
#include <math.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

#include "../EmuStructs.h"
#include "../SoundDevs.h"
#include "../EmuCores.h"
#include "../snddef.h"
#include "../EmuHelper.h"
#include "../RatioCntr.h"

#include "sameboy_apu.h"
#include "sameboy_apu_private.h"
#include "gbintf.h"


static void gb_sameboy_update(void *chip, UINT32 samples, DEV_SMPL **outputs);
static UINT8 device_start_gb_sameboy(const DEV_GEN_CFG* cfg, DEV_INFO* retDevInf);
static void device_stop_gb_sameboy(void *chip);
static void device_reset_gb_sameboy(void *chip);

static void gb_sameboy_set_mute_mask(void *chip, UINT32 MuteMask);
static UINT32 gb_sameboy_get_mute_mask(void *chip);
static void gb_sameboy_set_options(void *chip, UINT32 Flags);

static void gb_sameboy_w(void *chip, UINT8 offset, UINT8 data);
static UINT8 gb_sameboy_r(void *chip, UINT8 offset);


static DEVDEF_RWFUNC devFunc[] =
{
	{RWF_REGISTER | RWF_WRITE, DEVRW_A8D8, 0, gb_sameboy_w},
	{RWF_REGISTER | RWF_READ, DEVRW_A8D8, 0, gb_sameboy_r},
	{RWF_CHN_MUTE | RWF_WRITE, DEVRW_ALL, 0, gb_sameboy_set_mute_mask},
	{0x00, 0x00, 0, NULL}
};
DEV_DEF devDef_GB_SameBoy =
{
	"GameBoy DMG", "SameBoy", FCC_SBOY,
	
	device_start_gb_sameboy,
	device_stop_gb_sameboy,
	device_reset_gb_sameboy,
	gb_sameboy_update,
	
	gb_sameboy_set_options,	// SetOptionBits
	gb_sameboy_set_mute_mask,
	NULL,	// SetPanning
	NULL,	// SetSampleRateChangeCallback
	NULL,	// SetLoggingCallback
	NULL,	// LinkDevice
	
	devFunc,	// rwFuncs
};



static uint32_t GB_get_clock_rate(GB_gameboy_t *gb)
{
    return gb->clock_rate;
}

static bool GB_is_cgb(const GB_gameboy_t *gb)
{
    return gb->model >= GB_MODEL_CGB_0;
}

INLINE uint32_t sample_fraction_multiply(GB_gameboy_t *gb, unsigned multiplier)
{
    if (unlikely(multiplier == 0)) return 0;
    if (likely(multiplier < GB_QUICK_MULTIPLY_COUNT + 1)) {
        return gb->apu_output.quick_fraction_multiply_cache[multiplier - 1];
    }
    return gb->apu_output.quick_fraction_multiply_cache[0] * multiplier;
}

static const uint8_t duties[] = {
    0, 0, 0, 0, 0, 0, 0, 1,
    1, 0, 0, 0, 0, 0, 0, 1,
    1, 0, 0, 0, 0, 1, 1, 1,
    0, 1, 1, 1, 1, 1, 1, 0,
};

static bool GB_apu_is_DAC_enabled(GB_gameboy_t *gb, GB_channel_t index)
{
    if (gb->model > GB_MODEL_CGB_E) {
        /* On the AGB, mixing is done digitally, so there are no per-channel
           DACs. Instead, all channels are summed digital regardless of
           whatever the DAC state would be on a CGB or earlier model. */
        return true;
    }
    
    switch (index) {
        case GB_SQUARE_1:
            return gb->io_registers[GB_IO_NR12] & 0xF8;

        case GB_SQUARE_2:
            return gb->io_registers[GB_IO_NR22] & 0xF8;

        case GB_WAVE:
            return gb->apu.wave_channel.enable;

        case GB_NOISE:
            return gb->io_registers[GB_IO_NR42] & 0xF8;
            
        nodefault;
    }

    return false;
}

static uint8_t agb_bias_for_channel(GB_gameboy_t *gb, GB_channel_t index)
{
    if (!gb->apu.is_active[index]) return 0;
    
    switch (index) {
        case GB_SQUARE_1:
            return gb->apu.square_channels[GB_SQUARE_1].current_volume;
        case GB_SQUARE_2:
            return gb->apu.square_channels[GB_SQUARE_2].current_volume;
        case GB_WAVE:
            return 0;
        case GB_NOISE:
            return gb->apu.noise_channel.current_volume;
            
        nodefault;
    }
    return 0;
}

static void update_sample(GB_gameboy_t *gb, GB_channel_t index, int8_t value, unsigned cycles_offset)
{
    if (gb->model > GB_MODEL_CGB_E) {
        /* On the AGB, because no analog mixing is done, the behavior of NR51 is a bit different.
           A channel that is not connected to a terminal is idenitcal to a connected channel
           playing PCM sample 0. */
        gb->apu.samples[index] = value;
        
        if (gb->apu_output.sample_rate) {
            uint8_t bias;
            bool left, right;
            GB_sample_t output;
            unsigned right_volume = (gb->io_registers[GB_IO_NR50] & 7) + 1;
            unsigned left_volume = ((gb->io_registers[GB_IO_NR50] >> 4) & 7) + 1;
            int8_t silence = 0;
            if (index == GB_WAVE) {
                /* For some reason, channel 3 is inverted on the AGB, and has a different "silence" value */
                value ^= 0xF;
                silence = 7 * 2;
            }
            
            bias = agb_bias_for_channel(gb, index);
            
            left = gb->io_registers[GB_IO_NR51] & (0x10 << index);
            right = gb->io_registers[GB_IO_NR51] & (1 << index);
            
            output.left = (0xF - (left? value * 2 + bias : silence)) * left_volume;
            output.right = (0xF - (right? value * 2 + bias : silence)) * right_volume;
            
            if (unlikely(gb->apu_output.channel_muted[index])) {
                output.left = output.right = 0;
            }
            gb->apu_output.channel_output[index] = output;
        }
        
        return;
    }
    
    if (value == 0 && gb->apu.samples[index] == 0) return;
    
    if (!GB_apu_is_DAC_enabled(gb, index)) {
        value = gb->apu.samples[index];
    }
    else {
        gb->apu.samples[index] = value;
    }

    if (gb->apu_output.sample_rate) {
        unsigned left_volume = 0, right_volume = 0;
        GB_sample_t output = {{0, 0}};
        if (gb->io_registers[GB_IO_NR51] & (1 << index)) {
            right_volume = (gb->io_registers[GB_IO_NR50] & 7) + 1;
        }
        if (gb->io_registers[GB_IO_NR51] & (0x10 << index)) {
            left_volume = ((gb->io_registers[GB_IO_NR50] >> 4) & 7) + 1;
        }
        if (likely(!gb->apu_output.channel_muted[index])) {
            output.left = (0xF - value * 2) * left_volume;
            output.right = (0xF - value * 2) * right_volume;
        }
        gb->apu_output.channel_output[index] = output;
    }
}

static double smooth(double x)
{
    return 3*x*x - 2*x*x*x;
}

static signed interference(GB_gameboy_t *gb)
{
    /* These aren't scientifically measured, but based on ear based on several recordings */
    signed ret = 0;
    if (gb->halted) {
        if (gb->model <= GB_MODEL_CGB_E) {
            ret -= MAX_CH_AMP / 5;
        }
        else {
            ret -= MAX_CH_AMP / 12;
        }
    }
#if 0
    if (gb->io_registers[GB_IO_LCDC] & GB_LCDC_ENABLE) {
        ret += MAX_CH_AMP / 7;
        if ((gb->io_registers[GB_IO_STAT] & 3) == 3 && gb->model <= GB_MODEL_CGB_E) {
            ret += MAX_CH_AMP / 14;
        }
        else if ((gb->io_registers[GB_IO_STAT] & 3) == 1) {
            ret -= MAX_CH_AMP / 7;
        }
    }
#endif
    
    if (gb->apu.global_enable) {
        ret += MAX_CH_AMP / 10;
    }
    
#if 0
    if (GB_is_cgb(gb) && gb->model <= GB_MODEL_CGB_E && (gb->io_registers[GB_IO_RP] & 1)) {
        ret += MAX_CH_AMP / 10;
    }
#endif
    
    if (!GB_is_cgb(gb)) {
        ret /= 4;
    }
    
    ret += rand() % (MAX_CH_AMP / 12);
    
    return ret;
}

static void render(GB_gameboy_t *gb)
{
    GB_sample_t output = {{0, 0}};
    GB_sample_t filtered_output;
    unsigned i;

    unrolled for (i = 0; i < GB_N_CHANNELS; i++) {
        double multiplier = CH_STEP;
        
        if (gb->model <= GB_MODEL_CGB_E) {
            if (!GB_apu_is_DAC_enabled(gb, i)) {
                gb->apu_output.dac_discharge[i] -= ((double) DAC_DECAY_SPEED) / gb->apu_output.sample_rate;
                if (gb->apu_output.dac_discharge[i] < 0) {
                    multiplier = 0;
                    gb->apu_output.dac_discharge[i] = 0;
                }
                else {
                    multiplier *= smooth(gb->apu_output.dac_discharge[i]);
                }
            }
            else {
                gb->apu_output.dac_discharge[i] += ((double) DAC_ATTACK_SPEED) / gb->apu_output.sample_rate;
                if (gb->apu_output.dac_discharge[i] > 1) {
                    gb->apu_output.dac_discharge[i] = 1;
                }
                else {
                    multiplier *= smooth(gb->apu_output.dac_discharge[i]);
                }
            }
        }

        output.left += (int16_t)(gb->apu_output.channel_output[i].left * multiplier);
        output.right += (int16_t)(gb->apu_output.channel_output[i].right * multiplier);
    }
    gb->apu_output.cycles_since_render = 0;
    if (unlikely(gb->apu_output.sample_fraction < (1 << 28))) {
        gb->apu_output.sample_fraction = 0;
    }
    else {
        gb->apu_output.sample_fraction -= 1 << 28;
    }
    
    //if (gb->sgb && gb->sgb->intro_animation < GB_SGB_INTRO_ANIMATION_LENGTH) return;

    filtered_output.left = gb->apu_output.highpass_mode? (output.left  - (int16_t)gb->apu_output.highpass_diff.left) : output.left;
    filtered_output.right = gb->apu_output.highpass_mode? (output.right - (int16_t)gb->apu_output.highpass_diff.right) : output.right;

    switch (gb->apu_output.highpass_mode) {
        case GB_HIGHPASS_OFF:
            gb->apu_output.highpass_diff.left = gb->apu_output.highpass_diff.right = 0;
            break;
        case GB_HIGHPASS_ACCURATE:
            gb->apu_output.highpass_diff.left = output.left  - (output.left  - gb->apu_output.highpass_diff.left)  * gb->apu_output.highpass_rate;
            gb->apu_output.highpass_diff.right = output.right - (output.right - gb->apu_output.highpass_diff.right) * gb->apu_output.highpass_rate;
            break;
        case GB_HIGHPASS_REMOVE_DC_OFFSET: {
            unsigned mask = gb->io_registers[GB_IO_NR51];
            unsigned left_volume = 0;
            unsigned right_volume = 0;
            unsigned i;
            unrolled for (i = GB_N_CHANNELS; i--;) {
                if (GB_apu_is_DAC_enabled(gb, i)) {
                    if (mask & 1) {
                        left_volume += ((gb->io_registers[GB_IO_NR50] & 7) + 1) * CH_STEP * 0xF;
                    }
                    if (mask & 0x10) {
                        right_volume += (((gb->io_registers[GB_IO_NR50] >> 4) & 7) + 1) * CH_STEP * 0xF;
                    }
                }
                mask >>= 1;
            }
            gb->apu_output.highpass_diff.left = left_volume  * (1 - gb->apu_output.highpass_rate) + gb->apu_output.highpass_diff.left * gb->apu_output.highpass_rate;
            gb->apu_output.highpass_diff.right = right_volume * (1 - gb->apu_output.highpass_rate) + gb->apu_output.highpass_diff.right * gb->apu_output.highpass_rate;
            break;

        case GB_HIGHPASS_MAX:;
        }

    }
    
    
    if (gb->apu_output.interference_volume != 0.0) {
        signed interference_bias = interference(gb);
        int16_t interference_sample = (int16_t)(interference_bias - gb->apu_output.interference_highpass);
        gb->apu_output.interference_highpass = gb->apu_output.interference_highpass * gb->apu_output.highpass_rate +
        (1 - gb->apu_output.highpass_rate) * interference_sample;
        interference_bias *= gb->apu_output.interference_volume;
        
        filtered_output.left = MAX(MIN(filtered_output.left + interference_bias, 0x7FFF), -0x8000);
        filtered_output.right = MAX(MIN(filtered_output.right + interference_bias, 0x7FFF), -0x8000);
    }
    gb->apu_output.output = filtered_output;
}

static void update_square_sample(GB_gameboy_t *gb, GB_channel_t index, unsigned cycles)
{
    uint8_t duty;
    if (gb->apu.square_channels[index].sample_surpressed) {
        if (gb->model > GB_MODEL_CGB_E) {
            update_sample(gb, index, gb->apu.samples[index], 0);
        }
        return;
    }

    duty = gb->io_registers[index == GB_SQUARE_1? GB_IO_NR11 :GB_IO_NR21] >> 6;
    update_sample(gb, index,
                  duties[gb->apu.square_channels[index].current_sample_index + duty * 8]?
                  gb->apu.square_channels[index].current_volume : 0,
                  cycles);
}

INLINE void update_wave_sample(GB_gameboy_t *gb, unsigned cycles)
{
    if (gb->apu.wave_channel.current_sample_index & 1) {
        update_sample(gb, GB_WAVE,
                      (gb->apu.wave_channel.current_sample_byte & 0xF) >> gb->apu.wave_channel.shift,
                      cycles);
    }
    else {
        update_sample(gb, GB_WAVE,
                      (gb->apu.wave_channel.current_sample_byte >> 4) >> gb->apu.wave_channel.shift,
                      cycles);
    }
}

INLINE void set_envelope_clock(GB_envelope_clock_t *clock, bool value, bool direction, uint8_t volume)
{
    if (clock->clock == value) return;
    if (value) {
        clock->clock = true;
        clock->should_lock = (volume == 0xF && direction) || (volume == 0x0 && !direction);
    }
    else {
        clock->clock = false;
        clock->locked |= clock->should_lock;
    }
}

static void _nrx2_glitch(uint8_t *volume, uint8_t value, uint8_t old_value, uint8_t *countdown, GB_envelope_clock_t *lock)
{
    bool should_tick = (value & 7) && !(old_value & 7) && !lock->locked;
    bool should_invert = (value & 8) ^ (old_value & 8);
    if (lock->clock) {
        *countdown = value & 7;
    }
    
    if ((value & 0xF) == 8 && (old_value & 0xF) == 8 && !lock->locked) {
        should_tick = true;
    }
    
    if (should_invert) {
        // The weird and over-the-top way clocks for this counter are connected cause
        // some weird ways for it to invert
        if (value & 8) {
            if (!(old_value & 7) && !lock->locked) {
                *volume ^= 0xF;
            }
            else {
                *volume = 0xE - *volume;
                *volume &= 0xF;
            }
            should_tick = false; // Somehow prevents ticking?
        }
        else {
            *volume = 0x10 - *volume;
            *volume &= 0xF;
        }
    }
    if (should_tick) {
        if (value & 8) {
            (*volume)++;
        }
        else {
            (*volume)--;
        }
        *volume &= 0xF;
    }
    else if (!(value & 7) && lock->clock) {
        set_envelope_clock(lock, false, 0, 0);
    }
}

static void nrx2_glitch(GB_gameboy_t *gb, uint8_t *volume, uint8_t value, uint8_t old_value, uint8_t *countdown, GB_envelope_clock_t *lock)
{
    /* Note: on pre-CGB models *some* of these are non-deterministic. Specifically,
       $x0 writes seem to be  non-deterministic while  $x8 always work as expected.
       TODO: Might be useful  to find which cases are  non-deterministic, and allow
       the debugger to issue  warnings when they're used.  I suspect writes to/from
       $xF are guaranteed to be deterministic. */
    if (gb->model <= GB_MODEL_CGB_C) {
        _nrx2_glitch(volume, 0xFF, old_value, countdown, lock);
        _nrx2_glitch(volume, value, 0xFF, countdown, lock);
    }
    else {
        _nrx2_glitch(volume, value, old_value, countdown, lock);
    }
}

static void tick_square_envelope(GB_gameboy_t *gb, GB_channel_t index)
{
    uint8_t nrx2;
    set_envelope_clock(&gb->apu.square_channels[index].envelope_clock, false, 0, 0);
    if (gb->apu.square_channels[index].envelope_clock.locked) return;
    nrx2 = gb->io_registers[index == GB_SQUARE_1? GB_IO_NR12 : GB_IO_NR22];
    
    if (!(nrx2 & 7)) return;
    if (gb->cgb_double_speed) {
        if (index == GB_SQUARE_1) {
            gb->apu.pcm_mask[0] &= gb->apu.square_channels[GB_SQUARE_1].current_volume | 0xF1;
        }
        else {
            /* Note: CGB-0 behavior is instance specific and non-deterministic. Emulated behavior follows my CGB-0,
                     except that my CGB-0 sometimes yields "1" for 8->7 and 4->3 transitions. */
            uint8_t mask;
            if (unlikely(gb->model == GB_MODEL_CGB_0)) {
                if (gb->apu.square_channels[GB_SQUARE_2].current_volume == 1 && (gb->io_registers[GB_IO_NR22] & 8)) {
                    mask = 0x1F;
                }
                else {
                    mask = 0x3F;
                }
            }
            else {
                mask = 0x3F;
            }
            gb->apu.pcm_mask[0] &= (gb->apu.square_channels[GB_SQUARE_2].current_volume << 4) | mask;
        }
    }
    
    set_envelope_clock(&gb->apu.square_channels[index].envelope_clock, false, 0, 0);
    
    if (nrx2 & 8) {
            gb->apu.square_channels[index].current_volume++;
        }
        else {
            gb->apu.square_channels[index].current_volume--;
        }

    if (gb->apu.is_active[index]) {
        update_square_sample(gb, index, 0);
    }
}

static void tick_noise_envelope(GB_gameboy_t *gb)
{
    uint8_t nr42;
    set_envelope_clock(&gb->apu.noise_channel.envelope_clock, false, 0, 0);
    if (gb->apu.noise_channel.envelope_clock.locked) return;
    
    nr42 = gb->io_registers[GB_IO_NR42];
    if (!(nr42 & 7)) return;

    if (gb->cgb_double_speed) {
        gb->apu.pcm_mask[1] &= (gb->apu.noise_channel.current_volume << 4) | 0x1F;
    }
    
    if (nr42 & 8) {
            gb->apu.noise_channel.current_volume++;
        }
        else {
            gb->apu.noise_channel.current_volume--;
        }

    if (gb->apu.is_active[GB_NOISE]) {
        update_sample(gb, GB_NOISE,
                      (gb->apu.noise_channel.lfsr & 1) ?
                      gb->apu.noise_channel.current_volume : 0,
                      0);
    }
}

static void sweep_calculation_done(GB_gameboy_t *gb, unsigned cycles)
{
    /* APU bug: sweep frequency is checked after adding the sweep delta twice */
    if (gb->apu.channel_1_restart_hold == 0) {
        gb->apu.shadow_sweep_sample_length = gb->apu.square_channels[GB_SQUARE_1].sample_length;
    }
    if (gb->io_registers[GB_IO_NR10] & 8) {
        gb->apu.sweep_length_addend ^= 0x7FF;
    }
    if (gb->apu.shadow_sweep_sample_length + gb->apu.sweep_length_addend > 0x7FF && !(gb->io_registers[GB_IO_NR10] & 8)) {
        gb->apu.is_active[GB_SQUARE_1] = false;
        update_sample(gb, GB_SQUARE_1, 0, gb->apu.square_sweep_calculate_countdown * 2 - cycles);
    }
    gb->apu.channel1_completed_addend = gb->apu.sweep_length_addend;
}

static void trigger_sweep_calculation(GB_gameboy_t *gb)
{
    if ((gb->io_registers[GB_IO_NR10] & 0x70) && gb->apu.square_sweep_countdown == 7) {
        if (gb->io_registers[GB_IO_NR10] & 0x07) {
            gb->apu.square_channels[GB_SQUARE_1].sample_length =
            gb->apu.sweep_length_addend + gb->apu.shadow_sweep_sample_length + !!(gb->io_registers[GB_IO_NR10] & 0x8);
            gb->apu.square_channels[GB_SQUARE_1].sample_length &= 0x7FF;
        }
        if (gb->apu.channel_1_restart_hold == 0) {
            gb->apu.sweep_length_addend = gb->apu.square_channels[GB_SQUARE_1].sample_length;
            gb->apu.sweep_length_addend >>= (gb->io_registers[GB_IO_NR10] & 7);
        }
        
        /* Recalculation and overflow check only occurs after a delay */
        gb->apu.square_sweep_calculate_countdown = gb->io_registers[GB_IO_NR10] & 0x7;
        // TODO: this is a hack because DIV write timing is inaccurate. Will probably break on odd mode.
        gb->apu.square_sweep_calculate_countdown_reload_timer = 1 + gb->apu.lf_div;
        if (!gb->cgb_double_speed && gb->during_div_write) {
            gb->apu.square_sweep_calculate_countdown_reload_timer = 1;
        }
        gb->apu.unshifted_sweep = !(gb->io_registers[GB_IO_NR10] & 0x7);
        gb->apu.square_sweep_countdown = ((gb->io_registers[GB_IO_NR10] >> 4) & 7) ^ 7;
        if (gb->apu.square_sweep_calculate_countdown == 0) {
            gb->apu.square_sweep_instant_calculation_done = true;
        }
    }
}

static noinline void GB_apu_delayed_envelope_tick(GB_gameboy_t *gb)
{
    unsigned i;
    gb->apu.pending_envelope_tick = false;
    if (!gb->apu.global_enable) return;
    
    GB_apu_run(gb, true);
    gb->apu.pcm_mask[0] = gb->apu.pcm_mask[1] = 0xFF;


    unrolled for (i = GB_SQUARE_1; i <= GB_SQUARE_2; i++) {
        if (gb->apu.square_channels[i].envelope_clock.clock) {
            tick_square_envelope(gb, i);
        }
    }
    
    if (gb->apu.noise_channel.envelope_clock.clock) {
        tick_noise_envelope(gb);
    }
}

static noinline void GB_apu_div_event(GB_gameboy_t *gb)
{
    GB_apu_run(gb, true);
    gb->apu.pcm_mask[0] = gb->apu.pcm_mask[1] = 0xFF;

    if (!gb->apu.global_enable) return;
    if (gb->apu.skip_div_event == GB_SKIP_DIV_EVENT_SKIP) {
        gb->apu.skip_div_event = GB_SKIP_DIV_EVENT_SKIPPED;
        return;
    }
    if (gb->apu.skip_div_event == GB_SKIP_DIV_EVENT_SKIPPED) {
        gb->apu.skip_div_event = GB_SKIP_DIV_EVENT_INACTIVE;
    }
    else {
        gb->apu.div_divider++;
    }

    if ((gb->apu.div_divider & 7) == 7) {
        unsigned i;
        unrolled for (i = GB_SQUARE_1; i <= GB_SQUARE_2; i++) {
            if (!gb->apu.square_channels[i].envelope_clock.clock) {
                gb->apu.square_channels[i].volume_countdown--;
                gb->apu.square_channels[i].volume_countdown &= 7;
            }
        }
        if (!gb->apu.noise_channel.envelope_clock.clock) {
            gb->apu.noise_channel.volume_countdown--;
            gb->apu.noise_channel.volume_countdown &= 7;
        }
    }

    if (gb->cgb_double_speed && (gb->model == GB_MODEL_CGB_D || gb->model == GB_MODEL_CGB_E)) {
        gb->apu.pending_envelope_tick = true;
    }
    else {
        unsigned i;
        unrolled for (i = GB_SQUARE_1; i <= GB_SQUARE_2; i++) {
            if (gb->apu.square_channels[i].envelope_clock.clock) {
                tick_square_envelope(gb, i);
            }
        }
        
        if (gb->apu.noise_channel.envelope_clock.clock) {
            tick_noise_envelope(gb);
        }
    }
    
    if ((gb->apu.div_divider & 1) == 1) {
        unsigned i;
        unrolled for (i = GB_SQUARE_1; i <= GB_SQUARE_2; i++) {
            if (gb->apu.square_channels[i].length_enabled) {
                if (gb->apu.square_channels[i].pulse_length) {
                    if (!--gb->apu.square_channels[i].pulse_length) {
                        gb->apu.is_active[i] = false;
                        update_sample(gb, i, 0, 0);
                    }
                }
            }
        }

        if (gb->apu.wave_channel.length_enabled) {
            if (gb->apu.wave_channel.pulse_length) {
                if (!--gb->apu.wave_channel.pulse_length) {
                    if (gb->apu.is_active[GB_WAVE] && gb->model > GB_MODEL_CGB_E) {
                        if (gb->apu.wave_channel.sample_countdown == 0) {
                            gb->apu.wave_channel.current_sample_byte =
                                gb->io_registers[GB_IO_WAV_START + (((gb->apu.wave_channel.current_sample_index + 1) & 0xF) >> 1)];
                        }
                        else if (gb->apu.wave_channel.sample_countdown == 9) {
                            // TODO: wtf?
                            gb->apu.wave_channel.current_sample_byte = gb->io_registers[GB_IO_WAV_START];
                        }
                    }
                    gb->apu.is_active[GB_WAVE] = false;
                    update_sample(gb, GB_WAVE, 0, 0);
                }
            }
        }

        if (gb->apu.noise_channel.length_enabled) {
            if (gb->apu.noise_channel.pulse_length) {
                if (!--gb->apu.noise_channel.pulse_length) {
                    gb->apu.is_active[GB_NOISE] = false;
                    update_sample(gb, GB_NOISE, 0, 0);
                }
            }
        }
    }

    if ((gb->apu.div_divider & 3) == 3) {
        gb->apu.square_sweep_countdown++;
        gb->apu.square_sweep_countdown &= 7;
        trigger_sweep_calculation(gb);
    }
}

static noinline void GB_apu_div_secondary_event(GB_gameboy_t *gb)
{
    unsigned i;
    GB_apu_run(gb, true);
    gb->apu.pcm_mask[0] = gb->apu.pcm_mask[1] = 0xFF;

    if (!gb->apu.global_enable) return;
    unrolled for (i = GB_SQUARE_1; i <= GB_SQUARE_2; i++) {
        uint8_t nrx2 = gb->io_registers[i == GB_SQUARE_1? GB_IO_NR12 : GB_IO_NR22];
        if (gb->apu.is_active[i] && gb->apu.square_channels[i].volume_countdown == 0) {
            set_envelope_clock(&gb->apu.square_channels[i].envelope_clock,
                               (gb->apu.square_channels[i].volume_countdown = nrx2 & 7),
                               nrx2 & 8,
                               gb->apu.square_channels[i].current_volume);

        }
    }
    
    if (gb->apu.is_active[GB_NOISE] && gb->apu.noise_channel.volume_countdown == 0) {
        set_envelope_clock(&gb->apu.noise_channel.envelope_clock,
                           (gb->apu.noise_channel.volume_countdown = gb->io_registers[GB_IO_NR42] & 7),
                           gb->io_registers[GB_IO_NR42] & 8,
                           gb->apu.noise_channel.current_volume);
    }
}

static void update_lfsr(GB_gameboy_t *gb, unsigned cycles_offset)
{
    gb->apu.noise_channel.current_lfsr_sample = gb->apu.noise_channel.lfsr & 1;
    if (gb->apu.is_active[GB_NOISE]) {
        update_sample(gb, GB_NOISE,
                      gb->apu.noise_channel.current_lfsr_sample ?
                      gb->apu.noise_channel.current_volume : 0,
                      cycles_offset);
    }
}

static void step_lfsr(GB_gameboy_t *gb, unsigned cycles_offset)
{
    unsigned high_bit_mask = gb->apu.noise_channel.narrow ? 0x4040 : 0x4000;
    bool new_high_bit = (gb->apu.noise_channel.lfsr ^ (gb->apu.noise_channel.lfsr >> 1) ^ 1) & 1;
    gb->apu.lfsr_bit_7_before_step = gb->apu.noise_channel.lfsr & 0x80;
    gb->apu.noise_channel.lfsr >>= 1;
    
    if (new_high_bit) {
        gb->apu.noise_channel.lfsr |= high_bit_mask;
    }
    else {
        /* This code is not redundent, it's relevant when switching LFSR widths */
        gb->apu.noise_channel.lfsr &= ~high_bit_mask;
    }
    
    update_lfsr(gb, cycles_offset);
    gb->apu.lfsr_stepped_in_narrow = gb->apu.noise_channel.narrow;
}

static void GB_apu_run(GB_gameboy_t *gb, bool force)
{
    uint32_t clock_rate = GB_get_clock_rate(gb);
    bool orig_force = force;
    bool start_ch4;
    uint16_t cycles;
    
restart:;
    cycles = gb->apu.apu_cycles;

    if (force ||
        (cycles + gb->apu_output.cycles_since_render >= gb->apu_output.max_cycles_per_sample) ||
        (gb->apu_output.sample_cycles >= clock_rate) ||
        (gb->apu.square_sweep_calculate_countdown || gb->apu.channel_1_restart_hold || gb->apu.square_sweep_calculate_countdown_reload_timer) ||
        (gb->model <= GB_MODEL_CGB_E && (gb->apu.wave_channel.bugged_read_countdown || (gb->apu.wave_channel.enable && gb->apu.wave_channel.pulsed)))) {
        force = true;
    }
    if (!force) {
        return;
    }
    
    /* Force renders to never be more than max_cycles_per_sample apart by spliting runs. */
    while (cycles + gb->apu_output.cycles_since_render > gb->apu_output.max_cycles_per_sample) {
        /* We're already past max_cycles_per_sample. This can happen when changing clock rates, etc.
           Let this sample render normally. */
        if (unlikely(gb->apu_output.cycles_since_render > gb->apu_output.max_cycles_per_sample)) break;
        
        gb->apu.apu_cycles = gb->apu_output.max_cycles_per_sample - gb->apu_output.cycles_since_render;
        
        if (gb->apu.apu_cycles) {
            // Run for just enough cycles to reach max_cycles_per_sample
            cycles -= gb->apu.apu_cycles;
            GB_apu_run(gb, true);
            // Re-evaluate force if needed
            if (!orig_force) {
                force = false;
                gb->apu.apu_cycles = cycles;
                goto restart;
            }
            // Check if we need another batch
            continue;
        }
        
        // Render if needed
        if (gb->apu_output.sample_cycles >= clock_rate) {
            gb->apu_output.sample_cycles -= clock_rate;
            render(gb);
        }
        break;
    }

    gb->apu.apu_cycles = 0;
    if (!cycles) {
        /* This can happen in pre-CGB stop mode */
        while (unlikely(gb->apu_output.sample_cycles >= clock_rate)) {
            gb->apu_output.sample_cycles -= clock_rate;
            render(gb);
        }
        return;
    }
    
    if (unlikely(gb->apu.wave_channel.bugged_read_countdown)) {
        uint16_t cycles_left = cycles;
        while (cycles_left) {
            cycles_left--;
            if (--gb->apu.wave_channel.bugged_read_countdown == 0) {
                uint16_t address_bus = rand() & 0x7FFF;
                gb->apu.wave_channel.current_sample_byte =
                    gb->io_registers[GB_IO_WAV_START + (address_bus & 0xF)];
                if (gb->apu.is_active[GB_WAVE]) {
                    update_wave_sample(gb, 0);
                }
                break;
            }
        }
    }
    
    start_ch4 = false;
    if (likely(!gb->stopped || GB_is_cgb(gb))) {
        unsigned sweep_cycles, i;
        if (gb->apu.noise_channel.dmg_delayed_start) {
            if (gb->apu.noise_channel.dmg_delayed_start == cycles) {
                gb->apu.noise_channel.dmg_delayed_start = 0;
                start_ch4 = true;
            }
            else if (gb->apu.noise_channel.dmg_delayed_start > cycles) {
                gb->apu.noise_channel.dmg_delayed_start -= cycles;
            }
            else {
                /* Split it into two */
                cycles -= gb->apu.noise_channel.dmg_delayed_start;
                gb->apu.apu_cycles = gb->apu.noise_channel.dmg_delayed_start;
                GB_apu_run(gb, true);
            }
        }
        /* To align the square signal to 1MHz */
        gb->apu.lf_div ^= cycles & 1;
        gb->apu.noise_channel.alignment += cycles;
        
        sweep_cycles = cycles / 2;
        if ((cycles & 1) && !gb->apu.lf_div) {
            sweep_cycles++;
        }

        if (gb->apu.square_sweep_calculate_countdown_reload_timer > sweep_cycles) {
            gb->apu.square_sweep_calculate_countdown_reload_timer -= sweep_cycles;
            sweep_cycles = 0;
        }
        else {
            if (gb->apu.square_sweep_calculate_countdown_reload_timer && !gb->apu.square_sweep_calculate_countdown && gb->apu.square_sweep_instant_calculation_done) {
                sweep_calculation_done(gb, cycles);
            }
            gb->apu.square_sweep_instant_calculation_done = false;
            sweep_cycles -= gb->apu.square_sweep_calculate_countdown_reload_timer;
            gb->apu.square_sweep_calculate_countdown_reload_timer = 0;
        }
        
        if (gb->apu.square_sweep_calculate_countdown &&
            (((gb->io_registers[GB_IO_NR10] & 7) || gb->apu.unshifted_sweep))) { // Calculation is paused if the lower bits are 0
            if (gb->apu.square_sweep_calculate_countdown > sweep_cycles) {
                gb->apu.square_sweep_calculate_countdown -= sweep_cycles;
            }
            else {
                gb->apu.square_sweep_calculate_countdown = 0;
                sweep_calculation_done(gb, cycles);
            }
        }
        
        if (gb->apu.channel_1_restart_hold) {
            if (gb->apu.channel_1_restart_hold > cycles) {
                gb->apu.channel_1_restart_hold -= cycles;
            }
            else {
                gb->apu.channel_1_restart_hold = 0;
            }
        }

        unrolled for (i = GB_SQUARE_1; i <= GB_SQUARE_2; i++) {
            if (gb->apu.is_active[i]) {
                uint16_t cycles_left = cycles;
                if (unlikely(gb->apu.square_channels[i].delay)) {
                    if (gb->apu.square_channels[i].delay < cycles_left) {
                        gb->apu.square_channels[i].delay = 0;
                    }
                    else {
                        gb->apu.square_channels[i].delay -= cycles_left;
                    }
                }
                while (unlikely(cycles_left > gb->apu.square_channels[i].sample_countdown)) {
                    static const uint8_t edge_sample_indices[] = {7, 7, 5, 1};
                    uint8_t duty, edge_sample_index;
                    cycles_left -= gb->apu.square_channels[i].sample_countdown + 1;
                    gb->apu.square_channels[i].sample_countdown = (gb->apu.square_channels[i].sample_length ^ 0x7FF) * 2 + 1;
                    gb->apu.square_channels[i].current_sample_index++;
                    gb->apu.square_channels[i].current_sample_index &= 0x7;
                    gb->apu.square_channels[i].sample_surpressed = false;
                    if (cycles_left == 0 && gb->apu.samples[i] == 0) {
                        gb->apu.pcm_mask[0] &= i == GB_SQUARE_1? 0xF0 : 0x0F;
                    }
                    gb->apu.square_channels[i].did_tick = true;
                    update_square_sample(gb, i, cycles - cycles_left);

                    duty = gb->io_registers[i == GB_SQUARE_1? GB_IO_NR11 :GB_IO_NR21] >> 6;
                    edge_sample_index = edge_sample_indices[duty];
                    if (gb->apu.square_channels[i].current_sample_index == edge_sample_index) {
                        gb->apu_output.edge_triggered[i] = true;
                    }
                }
                gb->apu.square_channels[i].just_reloaded = cycles_left == 0;
                if (cycles_left) {
                    gb->apu.square_channels[i].sample_countdown -= cycles_left;
                }
            }
        }

        gb->apu.wave_channel.wave_form_just_read = false;
        if (gb->apu.is_active[GB_WAVE]) {
            uint16_t cycles_left = cycles;
            while (unlikely(cycles_left > gb->apu.wave_channel.sample_countdown)) {
                cycles_left -= gb->apu.wave_channel.sample_countdown + 1;
                gb->apu.wave_channel.sample_countdown = gb->apu.wave_channel.sample_length ^ 0x7FF;
                gb->apu.wave_channel.current_sample_index++;
                gb->apu.wave_channel.current_sample_index &= 0x1F;
                gb->apu.wave_channel.current_sample_byte =
                    gb->io_registers[GB_IO_WAV_START + (gb->apu.wave_channel.current_sample_index >> 1)];
                update_wave_sample(gb, cycles - cycles_left);
                gb->apu.wave_channel.wave_form_just_read = true;
                if (gb->apu.wave_channel.current_sample_index == 0) {
                    gb->apu_output.edge_triggered[GB_WAVE] = true;
                }
            }
            if (cycles_left) {
                gb->apu.wave_channel.sample_countdown -= cycles_left;
                gb->apu.wave_channel.wave_form_just_read = false;
            }
        }
        else if (gb->apu.wave_channel.enable && gb->apu.wave_channel.pulsed && gb->model <= GB_MODEL_CGB_E) {
            uint16_t cycles_left = cycles;
            while (unlikely(cycles_left > gb->apu.wave_channel.sample_countdown)) {
                cycles_left -= gb->apu.wave_channel.sample_countdown + 1;
                gb->apu.wave_channel.sample_countdown = gb->apu.wave_channel.sample_length ^ 0x7FF;
                if (cycles_left) {
                    uint16_t address_bus = rand() & 0x7FFF;
                    gb->apu.wave_channel.current_sample_byte =
                    gb->io_registers[GB_IO_WAV_START + (address_bus & 0xF)];
                }
                else {
                    gb->apu.wave_channel.bugged_read_countdown = 1;
                }
            }
            if (cycles_left) {
                gb->apu.wave_channel.sample_countdown -= cycles_left;
            }
            if (gb->apu.wave_channel.sample_countdown == 0) {
                gb->apu.wave_channel.bugged_read_countdown = 2;
            }
        }
        
        // TODO: verify these conditions one a DMG somehow
        if (gb->apu.noise_counter_active || gb->apu.noise_background_counter_active) {
            uint16_t cycles_left = cycles;
            unsigned divisor = (gb->io_registers[GB_IO_NR43] & 0x07) << 2;
            if (!divisor) divisor = 2;
            if (gb->apu.noise_channel.counter_countdown == 0) {
                gb->apu.noise_channel.counter_countdown = divisor;
            }
            // This while doesn't get an unlikely because the noise channel steps frequently enough
            while (cycles_left >= gb->apu.noise_channel.counter_countdown) {
                uint16_t mask;
                bool old_bit, new_bit;
                cycles_left -= gb->apu.noise_channel.counter_countdown;
                gb->apu.noise_channel.counter_countdown = divisor;
                mask = 1 << (gb->io_registers[GB_IO_NR43] >> 4);
                old_bit = gb->apu.noise_channel.counter & mask;
                gb->apu.noise_channel.counter++;
                gb->apu.noise_channel.counter &= 0x3FFF;
                gb->apu.noise_channel.did_step_counter = true;
                new_bit = gb->apu.noise_channel.counter & mask;

                /* Step LFSR */
                if (new_bit && !old_bit && gb->apu.is_active[GB_NOISE]) {
                    if (cycles_left == 0 && gb->apu.samples[GB_NOISE] == 0 && !gb->cgb_double_speed) {
                        gb->apu.pcm_mask[1] &= 0x0F;
                    }
                    step_lfsr(gb, cycles - cycles_left);
                }
            }
            if (cycles_left) {
                if (likely(gb->apu.noise_counter_active || gb->apu.noise_background_counter_active)) {
                    gb->apu.noise_channel.counter_countdown -= cycles_left;
                    gb->apu.noise_channel.countdown_reloaded = false;
                }
            }
            else {
                gb->apu.noise_channel.countdown_reloaded = true;
                gb->apu_output.edge_triggered[GB_NOISE] = true;
            }
        }
    }

    if (gb->apu_output.sample_rate) {
        gb->apu_output.cycles_since_render += cycles;
        gb->apu_output.sample_fraction += sample_fraction_multiply(gb, cycles);
        assert(gb->apu_output.sample_fraction < (4 << 28));

        if (gb->apu_output.sample_cycles >= clock_rate) {
            gb->apu_output.sample_cycles -= clock_rate;
            render(gb);
        }
    }
    if (start_ch4) {
        GB_apu_write(gb, GB_IO_NR44, gb->io_registers[GB_IO_NR44] | 0x80);
    }
}

static void GB_apu_init(GB_gameboy_t *gb)
{
    unsigned ch;
    memset(&gb->apu, 0, sizeof(gb->apu));
    gb->apu.apu_cycles_in_2mhz = true;
    gb->apu.lf_div = 1;
    gb->apu.wave_channel.shift = 4;
    /* APU glitch: When turning the APU on while DIV's bit 4 (or 5 in double speed mode) is on,
       the first DIV/APU event is skipped. */
    if (gb->div_counter & (gb->cgb_double_speed? 0x2000 : 0x1000)) {
        gb->apu.skip_div_event = GB_SKIP_DIV_EVENT_SKIP;
        gb->apu.div_divider = 1;
    }
    gb->apu.square_channels[GB_SQUARE_1].sample_countdown = -1;
    gb->apu.square_channels[GB_SQUARE_2].sample_countdown = -1;
    
    for (ch = 0; ch < GB_N_CHANNELS; ch++)
        gb->apu_output.dac_discharge[ch] = 1.0;
    gb->apu_output.highpass_diff.left = gb->apu_output.highpass_diff.right = 0;
    if (0 && gb->apu_output.highpass_mode != GB_HIGHPASS_OFF)
    {
        // prevent initial pop -Valley Bell
        gb->apu_output.highpass_diff.left = gb->apu_output.highpass_diff.right = 0xF * 8 * CH_STEP * GB_N_CHANNELS;
    }
}

static uint8_t GB_apu_read(GB_gameboy_t *gb, uint8_t reg)
{
    static const uint8_t read_mask[GB_IO_WAV_END - GB_IO_NR10 + 1] = {
     /* NRX0  NRX1  NRX2  NRX3  NRX4 */
        0x80, 0x3F, 0x00, 0xFF, 0xBF, // NR1X
        0xFF, 0x3F, 0x00, 0xFF, 0xBF, // NR2X
        0x7F, 0xFF, 0x9F, 0xFF, 0xBF, // NR3X
        0xFF, 0xFF, 0x00, 0x00, 0xBF, // NR4X
        0x00, 0x00, 0x70, 0xFF, 0xFF, // NR5X

        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // Unused
        // Wave RAM
        0, /* ... */
    };

    GB_apu_run(gb, true);
    if (reg == GB_IO_NR52) {
        uint8_t value = 0;
        unsigned i;
        for (i = 0; i < GB_N_CHANNELS; i++) {
            value >>= 1;
            if (gb->apu.is_active[i]) {
                value |= 0x8;
            }
        }
        if (gb->apu.global_enable) {
            value |= 0x80;
        }
        value |= 0x70;
        return value;
    }

    if (reg >= GB_IO_WAV_START && reg <= GB_IO_WAV_END && gb->apu.is_active[GB_WAVE]) {
        if (!GB_is_cgb(gb) && !gb->apu.wave_channel.wave_form_just_read) {
            return 0xFF;
        }
        if (gb->model > GB_MODEL_CGB_E) {
            return 0xFF;
        }
        reg = GB_IO_WAV_START + gb->apu.wave_channel.current_sample_index / 2;
    }

    return gb->io_registers[reg] | read_mask[reg - GB_IO_NR10];
}

static noinline void nr10_write_glitch(GB_gameboy_t *gb, uint8_t value)
{
    // TODO: Check all of these in APU odd mode
    if (gb->model <= GB_MODEL_CGB_C) {
        if (gb->apu.square_sweep_calculate_countdown_reload_timer == 1 && !gb->apu.lf_div) {
            if (gb->cgb_double_speed) {
                /* This is some instance-specific data corruption. It might also be affect by revision.
                 At least for my CGB-0 (haven't tested any other CGB-0s), the '3' case is non-deterministic. */
                static const uint8_t corruption[8] =    {7, 7, 5, 7, 3, 3, 5, 7}; // Two of my CGB-Cs, CGB-A
                // static const uint8_t corruption[8] = {7, 7, 1, 3, 3, 3, 5, 7}; // My other CGB-C, Coffee Bat's CGB-C
                // static const uint8_t corruption[8] = {7, 1, 1, 3, 3, 5, 5, 7}; // My CGB-B
                // static const uint8_t corruption[8] = {7, 7, 1, *, 3, 3, 5, 7}; // My CGB-0
                                
                // static const uint8_t corruption[8] = {7, 5, 1, 3, 3, 1, 5, 7}; // PinoBatch's CGB-B
                // static const uint8_t corruption[8] = {7, 5, 1, 3, 3, *, 5, 7}; // GenericHeroGuy CGB-C
                

                // TODO: How does this affect actual frequency calculation?
                
                gb->apu.square_sweep_calculate_countdown = corruption[gb->apu.square_sweep_calculate_countdown & 7];
                /* TODO: the value of 1 needs special handling, but it doesn't occur with the instance I'm emulating here */
            }
        }
        else if (gb->apu.square_sweep_calculate_countdown_reload_timer > 1) {
            if (gb->cgb_double_speed) {
                // TODO: How does this affect actual frequency calculation?
                gb->apu.square_sweep_calculate_countdown = value & 7;
            }
        }
        else if (gb->apu.square_sweep_calculate_countdown) {
            // No clue why 1 is a special case here
            bool should_zombie_step = false;
            if (!(gb->io_registers[GB_IO_NR10] & 7)) {
                should_zombie_step = gb->apu.lf_div ^ gb->cgb_double_speed;
            }
            else if (gb->cgb_double_speed && gb->apu.square_sweep_calculate_countdown == 1) {
                should_zombie_step = true;
            }
            
            if (should_zombie_step) {
                gb->apu.square_sweep_calculate_countdown--;
                if (gb->apu.square_sweep_calculate_countdown <= 1) {
                    gb->apu.square_sweep_calculate_countdown = 0;
                    sweep_calculation_done(gb, 0);
                }
            }
        }
    }
    else {
        if (gb->apu.square_sweep_calculate_countdown_reload_timer == 2) {
            // Countdown just reloaded, re-reload it
            gb->apu.square_sweep_calculate_countdown = value & 0x7;
            if (!gb->apu.square_sweep_calculate_countdown) {
                gb->apu.square_sweep_calculate_countdown_reload_timer = 0;
            }
            else {
                // TODO: How does this affect actual frequency calculation?
            }
        }
        if ((value & 7) && !(gb->io_registers[GB_IO_NR10] & 7) && !gb->apu.lf_div && gb->apu.square_sweep_calculate_countdown > 1) {
            // TODO: Another odd glitch? Ditto
            gb->apu.square_sweep_calculate_countdown--;
            if (!gb->apu.square_sweep_calculate_countdown) {
                sweep_calculation_done(gb, 0);
            }
        }
    }

}

static void prepare_noise_start(GB_gameboy_t *gb)
{
    /*
     TODO: When restarting a channel right after starting it, before it has the chance to tick the counter, things
     behave differently. Only certain behaviors of this edge case are emulated.
    */
    
    /*
     TODO: Restarting a channel in double speed mode under CGB-C and older is not accurate if the divisor is 0 or 1.
           Specifically in the 0 case, the initial LFSR value seems to be deterministic, but dependant on various
           parameters. It is neither 0 or the equaly unexplained 0x0055.
    */
    bool was_started_with_dac_disabled, was_background_counting, instant_step, div_1_glitch;
    unsigned divisor;
    gb->apu.noise_counter_active = gb->io_registers[GB_IO_NR42] & 0xF8; // Resets on APU off and DAC disable
    was_started_with_dac_disabled = gb->apu.noise_started_with_dac_disabled;
    gb->apu.noise_started_with_dac_disabled = !gb->apu.noise_counter_active;
    divisor = (gb->io_registers[GB_IO_NR43] & 0x07);
    was_background_counting = gb->apu.noise_background_counter_active;
    gb->apu.noise_background_counter_active = true;
    instant_step = false;
    div_1_glitch = false;
    
    if (divisor > 1 && gb->apu.noise_channel.counter_countdown == 1) {
        gb->apu.noise_channel.counter++;
        gb->apu.noise_channel.counter &= 0x3FFF;
    }
    else if (divisor > 1 && gb->apu.noise_channel.counter_countdown == 2 && gb->apu.is_active[GB_NOISE] && gb->model <= GB_MODEL_CGB_C && gb->cgb_double_speed) {
        gb->apu.noise_channel.counter++;
        gb->apu.noise_channel.counter &= 0x3FFF;
    }
    else if (gb->apu.noise_channel.counter_countdown == 2 &&
        (gb->apu.noise_channel.alignment & 3) == 0 &&
        gb->apu.is_active[GB_NOISE]) {
        if (divisor == 0) {
            divisor = 8;
        }
        else if (divisor == 1) {
            uint16_t mask;
            bool old_bit, new_bit;
            if (!gb->apu.noise_channel.did_step_counter) {
                div_1_glitch = true;
            }
            
            mask = 1 << (gb->io_registers[GB_IO_NR43] >> 4);
            old_bit = gb->apu.noise_channel.counter & mask;
            gb->apu.noise_channel.counter++;
            gb->apu.noise_channel.counter &= 0x3FFF;
            new_bit = gb->apu.noise_channel.counter & mask;
            
            if ((new_bit && !old_bit)) {
                instant_step = true;
            }
        }
    }
    gb->apu.noise_channel.counter_countdown = divisor == 0? 6 : divisor * 4 + 6;
    if  (gb->apu.noise_channel.alignment & 1) {
        if (!divisor) {
            if (gb->model <= GB_MODEL_CGB_C) {
                gb->apu.noise_channel.counter_countdown++;
            }
            else if (was_background_counting) {
                gb->apu.noise_channel.counter_countdown--;
            }
            else {
                gb->apu.noise_channel.counter_countdown++;
            }
        }
        else {
            if (gb->apu.noise_channel.alignment & 2) {
                if (divisor == 1 && !gb->apu.is_active[GB_NOISE]) {
                    gb->apu.noise_channel.counter_countdown++;
                }
                else {
                    gb->apu.noise_channel.counter_countdown -= 3;
                }
            }
            else {
                gb->apu.noise_channel.counter_countdown--;
                if (divisor == 1 && gb->apu.is_active[GB_NOISE]) {
                    gb->apu.noise_channel.counter_countdown -= 4;
                }
            }
        }
    }
    else {
        if (divisor) {
            if (gb->apu.noise_channel.alignment & 2) {
                if (gb->cgb_double_speed && gb->model <= GB_MODEL_CGB_C && divisor == 1) {
                    gb->apu.noise_channel.counter_countdown += 2;
                }
                else {
                    gb->apu.noise_channel.counter_countdown -= 2;
                }
            }
            else if (divisor > 1 && (!gb->cgb_double_speed || gb->model > GB_MODEL_CGB_C)) {
                gb->apu.noise_channel.counter_countdown -= 4;
            }
            /* TODO: This quirk seems way too specific */
            else if (divisor == 1 && gb->apu.is_active[GB_NOISE] && !(gb->io_registers[GB_IO_NR43] & 0xf0)) {
                gb->apu.noise_channel.counter_countdown -= 4;
            }
        }
        else if (gb->cgb_double_speed && gb->model <= GB_MODEL_CGB_C) {
            gb->apu.noise_channel.counter_countdown += 2;
        }
    }
    
    /* Background counting glitches */
    /* TODO: Double speed mode not tested */
    if (divisor > 1) {
        if (!gb->apu.noise_counter_active && !(gb->apu.noise_channel.alignment & 3)) {
            gb->apu.noise_channel.counter_countdown += 4;
        }
    }
    else {
        if (was_background_counting && !gb->apu.is_active[GB_NOISE] && !(gb->apu.noise_channel.alignment & 3)) {
            if (divisor == 0) {
                if (was_started_with_dac_disabled) { // TODO: Why is it different?
                    gb->apu.noise_channel.counter_countdown += 28;
                }
            }
            else {
                gb->apu.noise_channel.counter_countdown -= 4;
            }
        }
    }
    
    /* TODO: This is weird, is the clock going out of sync? */
    if (!divisor && gb->model <= GB_MODEL_CGB_C && was_background_counting && !gb->apu.is_active[GB_NOISE] && gb->cgb_double_speed) {
        gb->apu.noise_channel.counter_countdown--;
    }
    if (div_1_glitch) {
        gb->apu.noise_channel.counter_countdown -= 4;
    }
    
    if (!divisor && gb->apu.is_active[GB_NOISE] && (gb->apu.noise_channel.alignment & 3) == 3) {
        /* TODO: I have no clue where this number comes from, but this number is confirmed for this edge case even for
                 side LFSR, despite being seemingly arbitrary. */
        gb->apu.noise_channel.lfsr = 0x0055;
    }
    else {
        gb->apu.noise_channel.lfsr = 0;
    }
    if (instant_step) {
        step_lfsr(gb, 0);
    }
}

static void nr43_write(GB_gameboy_t *gb, uint8_t new)
{
    /*
        NR43 writes cause glitch signals to the LFSR. They are often non-deterministic, and
        they're revision and instance specific.  This implementation is trying to emulate a
        simplified and deterministic "variant" of specific instances of revisions I own.
     
        For more details:
        https://github.com/LIJI32/SameBoy/issues/397#issuecomment-3733625631
    */
    
    /*
        TODO: Non-determinism aside, this is currently only 100% accurate in CGB-E mode, where
        my specific CGB-E is currently emulated.  My CGB-D, under rare cases, samples a second
        intermediate value,  and this is not  currently emulated.  AGB revisions are extremely
        glitchy, and are hard to research.
     
        Due to FF-write glitches in pre-CGB-D revisions, all writes (even no-change writes) go
        through 3 intermediate values by definition. Also, the effective counter value used is
        ORed with the next (or previous, timing needs to be verified) value.
    */
    uint8_t old, glitch_value;
    uint16_t effective_counter;
    bool old_bit, glitch_bit, new_bit, force_glitch;
    bool old_narrow = gb->apu.noise_channel.narrow;
    gb->apu.noise_channel.narrow = new & 8;
    old = gb->io_registers[GB_IO_NR43];
    gb->io_registers[GB_IO_NR43] = new;
    
    if ((old & 0xF0) == (new & 0xF0)) return;
    
    effective_counter = gb->apu.noise_channel.counter;
    if (gb->model <= GB_MODEL_CGB_C && gb->apu.noise_channel.countdown_reloaded) {
        effective_counter |= (effective_counter - 1) & 0x3FFF;
    }
    old_bit = (effective_counter >> (old >> 4)) & 1;

    glitch_value = (old & 0x7F) | (new & 0x80);
    glitch_bit = (effective_counter >> (glitch_value >> 4)) & 1;
    new_bit = (effective_counter >> (new >> 4)) & 1;
    force_glitch = false;

    if (gb->model == GB_MODEL_CGB_D) {
        if (new_bit && glitch_bit && old_bit) {
            if ((old ^ new) & 0x70) {
                force_glitch = true;
            }
        }
    }
    
    if (gb->model > GB_MODEL_CGB_E) {
        /* AGB behavior is very glitchy and incosistent. It can have 2 intermediate values for
           NR43, and sometimes even 3, and the pattern isn't very consistent. This is a *very*
           rough approximation of the behavior.
         
           Due to having so up to 3 intermediate value, glitch behavior is complicated to look
           into, so currently CGB-E behavior is arbitrarily used if a glitch occurs. */
        
        uint8_t glitch_value2 = 0;
        uint8_t glitch_bit2;
        if (new >= 0x80 && old >= 0x80) {
            glitch_value  = (old & 0xCF) | (new & 0x30);
            glitch_value2 = (old & 0x8F) | (new & 0x70);
        }
        else {
            glitch_value = (old & 0xDF) | (new & 0x20);
            glitch_value2 = (old & 0xCF) | (new & 0x30);
        }
        glitch_bit = (gb->apu.noise_channel.counter >> (glitch_value >> 4)) & 1;
        glitch_bit2 = (gb->apu.noise_channel.counter >> (glitch_value2 >> 4)) & 1;
        if (glitch_bit != glitch_bit2) {
            if (new_bit == old_bit) {
                glitch_bit = !new_bit;
            }
            else if (!glitch_bit && old_bit) {
                force_glitch = true;
            }
        }
    }
    
    /* Step LFSR */
    
    if ((old_bit == new_bit && new_bit != glitch_bit) || force_glitch) {
        /* Glitching write.  Has two categories,  both have  non-deterministic
           variants. These are the most common variants of the two categories,
           which are deterministic. */
        if (new_bit) {
            /* Category 1 */
            if (gb->model >= GB_MODEL_CGB_E) {
                if (!(new & 0x80)) {
                    step_lfsr(gb, 0);
                }
                else {
                    /* Only happens under this odd condition */
                    uint8_t t1 = (old >> 4) & 7;
                    uint8_t t2 = (new >> 4) & 7;
                    
                    if ((t1 ^ 7) + t2 > 7 || ((t1 ^ 7) & t2)) {
                        /* Copy bit 8 to bit 7 */
                        gb->apu.noise_channel.lfsr &= ~0x80;
                        gb->apu.noise_channel.lfsr |= (gb->apu.noise_channel.lfsr >> 1) & 0x80;
                        
                        /* All specific cases have non-deterministic behaviors involved */
                        if ((t1 == 0 || t1 == 4) && t2 == 3) {
                            gb->apu.noise_channel.lfsr &= (gb->apu.noise_channel.lfsr >> 1) | 0x545;
                            update_lfsr(gb, 0);
                        }
                        else if (t1 == 2 && t2 == 3) {
                            uint16_t mask = 0x555;
                            if ((gb->apu.noise_channel.lfsr & 0xC) == 0xC) {
                                mask |= 8;
                            }
                            if ((gb->apu.noise_channel.lfsr & 0xC00) == 0xC00) {
                                mask |= 0x800;
                            }
                            
                            gb->apu.noise_channel.lfsr &= (gb->apu.noise_channel.lfsr >> 1) | mask;
                            update_lfsr(gb, 0);
                        }
                        if (!gb->apu.noise_channel.narrow && old_narrow && gb->apu.lfsr_stepped_in_narrow) {
                            /* TODO: Behaves weirder in non-deterministic t1 == 0/4 scenarios? */
                            if (gb->apu.lfsr_bit_7_before_step) {
                                gb->apu.noise_channel.lfsr |= 0x40;
                            }
                            else {
                                gb->apu.noise_channel.lfsr &= ~0x40;
                            }
                        }
                        gb->apu.noise_channel.lfsr |= gb->apu.noise_channel.narrow ? 0x4040 : 0x4000;
                        /* TODO: verify */
                        gb->apu.lfsr_stepped_in_narrow = gb->apu.noise_channel.narrow;
                    }
                }
            }
            else if (gb->model == GB_MODEL_CGB_D) {
                static const uint8_t glitch_map_l2h[8 * 8] = {
                    /*[000] =*/ 0x00, 0x01, 0x01, 0x21, 0x02, 0x21,
                    /*[010] =*/ 0x03, 0x00, 0x21, 0x01, 0x04, 0x04,
                    /*[020] =*/ 0x05, 0x01, 0x00, 0x01, 0x04, 0x21,
                    /*[030] =*/ 0x03, 0x05, 0x05, 0x00, 0x01, 0x01,
                    /*[040] =*/ 0x05, 0x01, 0x01, 0x21, 0x00, 0x01,
                    /*[050] =*/ 0x05, 0x05, 0x21, 0x01, 0x05, 0x00,
                    /*[060] =*/ 0x05, 0x01, 0x05, 0x01, 0x05, 0x01,
                    /*[070] =*/ 0x03, 0x05, 0x05, 0x05, 0x05, 0x05,
                };
                
                /* The following transitions are a bit non-deterministic (except under forced glitched):
                   1 -> c, 2 -> c, 3 -> c, 3 -> d */
                
                static const uint8_t glitch_map_h2l[8 * 8] = {
                    /*[000] =*/ 0x00, 0x27, 0x26, 0x37, 0x21, 0x38, 0x01, 0x01,
                    /*[010] =*/ 0x01, 0x00, 0x38, 0x21, 0x21, 0x21, 0x01, 0x01,
                    /*[020] =*/ 0x01, 0x27, 0x00, 0x28, 0x21, 0x38, 0x01, 0x01,
                    /*[030] =*/ 0x01, 0x02, 0x01, 0x00, 0x31, 0x21, 0x01, 0x01,
                    /*[040] =*/ 0x06, 0x28, 0x28, 0x38, 0x00, 0x27, 0x01, 0x01,
                    /*[050] =*/ 0x01, 0x03, 0x38, 0x21, 0x01, 0x00, 0x01, 0x01,
                };
                /* The following transitions are a bit non-deterministic (except under forced glitched):
                   8 -> 5, 2 -> 9, a -> 3, c -> 3 */
                
                const uint8_t *glitch_map = old & 0x80? &glitch_map_h2l[0] : &glitch_map_l2h[0];
            
                
                unsigned glitch = glitch_map[((old & 0x70) >> 1) | ((new & 0x70) >> 4)];
                uint16_t old_lfsr, lfsr_mask;
                if (force_glitch) {
                    if (!((new ^ old) & 0x80)) {
                        glitch = glitch & 0x20? 5 : 0;
                    }
                    else if (!(new & 0x80)) {
                        glitch = glitch & 0x10? 5 : 0;
                    }
                    else if ((glitch & 0xF) == 1 || (glitch & 0xF) == 4) {
                        glitch = 5;
                    }
                    else {
                        glitch = 0;
                    }
                }
                else {
                    glitch &= 0xF;
                }
                old_lfsr = gb->apu.noise_channel.lfsr;
                lfsr_mask = gb->apu.noise_channel.narrow ? 0x4040 : 0x4000;
                switch (glitch) {
                    case 6: // Like 2, but conditional
                    case 4: // Like 2, but conditional
                        if ((gb->apu.noise_channel.lfsr & (glitch == 4? 0x60 : 0x40)) != 0x40) { // Todo check wide mode
                        case 2: // And bit 1 with bit 0 before doing glitch 1
                            if (!(gb->apu.noise_channel.lfsr & 1)) {
                                gb->apu.noise_channel.lfsr &= ~2;
                            }
                        }
                    case 1: // Step and set the LFSR bit
                    case 8: // Step and set the LFSR bit conditionally
                        step_lfsr(gb, 0);
                    case 5: // Just set LFSR
                        if ((glitch != 8) || (old_lfsr & 3) != 2) {
                            gb->apu.noise_channel.lfsr |= lfsr_mask;
                        }
                        else {
                            gb->apu.noise_channel.lfsr |= old_lfsr & lfsr_mask;
                        }
                        break;
                    
                    case 7: // Step and OR the LFSR bit with its old value
                        step_lfsr(gb, 0);
                        gb->apu.noise_channel.lfsr |= old_lfsr & lfsr_mask;
                        break;
                        
                    case 3: // A bit of a mess
                        step_lfsr(gb, 0);
                        gb->apu.noise_channel.lfsr &= old_lfsr;
                        gb->apu.noise_channel.lfsr |= old_lfsr & 1;
                        gb->apu.noise_channel.lfsr |= lfsr_mask;
                        update_lfsr(gb, 0);
                        break;
                        
                        
                    default: break;
                }
            }
        }
        else {
            /* Category 2 */
            if (gb->model >= GB_MODEL_CGB_E) {
                static const uint8_t glitch_map[8 * 8] = {
                /*      8              9              A              B              C              D           */
                    /*[000] =*/ 0, /*[001] =*/ 0, /*[002] =*/ 4, /*[003] =*/ 2, /*[004] =*/ 2, /*[005] =*/ 2, // 0
                    /*[010] =*/ 0, /*[011] =*/ 0, /*[012] =*/ 2, /*[013] =*/ 4, /*[014] =*/ 2, /*[015] =*/ 2, // 1
                    /*[020] =*/ 1, /*[021] =*/ 2, /*[022] =*/ 0, /*[023] =*/ 1, /*[024] =*/ 5, /*[025] =*/ 3, // 2
                    /*[030] =*/ 0, /*[031] =*/ 0, /*[032] =*/ 0, /*[033] =*/ 0, /*[034] =*/ 2, /*[035] =*/ 2, // 3
                    /*[040] =*/ 0, /*[041] =*/ 2, /*[042] =*/ 2, /*[043] =*/ 2, /*[044] =*/ 0, /*[045] =*/ 0, // 4
                    /*[050] =*/ 6, /*[051] =*/ 0, /*[052] =*/ 2, /*[053] =*/ 2, /*[054] =*/ 0, /*[055] =*/ 0, // 5
                };
                
                /* The following transitions are a bit non-deterministic:
                 2 -> 8, 0 -> A, 2 -> C */
                
                unsigned glitch = new & 0x80? glitch_map[((old & 0x70) >> 1) | ((new & 0x70) >> 4)] : 0;
                switch (glitch) {
                    case 1: /* Step, followed by bit 1 &= bit 0 */
                    case 6: /* Variant of type 1: LFSR bit - 1 glitched by LFSR bit, LFSR bit - 2, and bit 0 */
                        step_lfsr(gb, 0);
                        if (glitch == 6) {
                            /* TODO: Verify wide mode */
                            if ((gb->apu.noise_channel.narrow &&
                                ((gb->apu.noise_channel.lfsr & 0x71) == 0x20)) ||
                                (gb->apu.noise_channel.lfsr & 0x71) == 0x61) {
                                gb->apu.noise_channel.lfsr &= ~0x20;
                            }
                            if ((gb->apu.noise_channel.lfsr & 0x7001) == 0x2000 ||
                                (gb->apu.noise_channel.lfsr & 0x7001) == 0x6001) {
                                gb->apu.noise_channel.lfsr &= ~0x2000;
                            }
                        }
                        if ((gb->apu.noise_channel.lfsr & 0x3) == 2) {
                            gb->apu.noise_channel.lfsr &= ~2;
                        }
                        break;
                    case 2: { /* Step, bitwise AND with previous, except for bit 0 */
                        uint16_t prev = gb->apu.noise_channel.lfsr;
                        step_lfsr(gb, 0);
                        gb->apu.noise_channel.lfsr &= prev | 1;
                        break;
                    }
                        
                    case 5:; /* Non deterministic variant of type 3:
                              The LFSR is unset if bit 0 & 1 are 0b10.
                              Bit 3 is complex and non-deterministic (TODO: wide mode) */
                        if ((gb->apu.noise_channel.lfsr & 0x3) == 2) {
                            gb->apu.noise_channel.lfsr &= gb->apu.noise_channel.narrow? ~0x4040 : ~0x4000;
                        }
                        
                        if ((gb->apu.noise_channel.lfsr & 0x19) == 8) {
                            gb->apu.noise_channel.lfsr &= ~8;
                        }
                        
                    case 3: /* No step, bit 0 = bit 1, some other bits have AND glitches with next*/
                        gb->apu.noise_channel.lfsr &= ~1;
                        gb->apu.noise_channel.lfsr |= (gb->apu.noise_channel.lfsr >> 1) & 1;
                        
                        update_lfsr(gb, 0);
                        /* TODO: verify */
                        gb->apu.lfsr_stepped_in_narrow = gb->apu.noise_channel.narrow;
                        break;
                        
                    case 4: { /* Step, bit 1 &= bit 0, LFSR bit -1 &= LFSR bit */
                        uint16_t prev = gb->apu.noise_channel.lfsr;
                        step_lfsr(gb, 0);
                        gb->apu.noise_channel.lfsr &= prev | (gb->apu.noise_channel.narrow? ~0x2022 : ~0x2002);
                        break;
                    }
                        
                    default: /* No glitch, plain step*/
                        step_lfsr(gb, 0);
                        break;
                        
                }
            }
            else {
                step_lfsr(gb, 0);
            }
        }
    }
    else if (!old_bit && new_bit) {
        if (gb->model <= GB_MODEL_CGB_C) {
            bool previous_narrow = gb->apu.noise_channel.narrow;
            gb->apu.noise_channel.narrow = true;
            step_lfsr(gb, 0);
            gb->apu.noise_channel.narrow = previous_narrow;
            if ((new & 0xf0) <= 0x20 && glitch_bit && !(effective_counter & 8)) { // No clue why that specific bit is tested
                // Non-deterministic, not fully tested for revision differences and wide mode
                // Step twice?
                step_lfsr(gb, 0);
                gb->apu.noise_channel.lfsr &= ~(gb->apu.noise_channel.narrow? 0x4040 : 0x4000);
                gb->apu.noise_channel.lfsr |= (gb->apu.noise_channel.lfsr & (gb->apu.noise_channel.narrow? 0x2020 : 0x2000)) << 1;
            }
        }
        else {
            step_lfsr(gb, 0);
        }
    }
    else if (gb->model <= GB_MODEL_CGB_C) {
        if ((new & 0xf0) <= 0x20 && !glitch_bit && !new_bit && !old_bit && (effective_counter & 8)) { // No clue why that specific bit is tested
            // Step twice?
            step_lfsr(gb, 0);
        }
    }
}

static void GB_apu_write(GB_gameboy_t *gb, uint8_t reg, uint8_t value)
{
    unsigned i;
    GB_apu_run(gb, true);
    if (!gb->apu.global_enable && reg != GB_IO_NR52 && reg < GB_IO_WAV_START && (GB_is_cgb(gb) ||
                                                                                (
                                                                                reg != GB_IO_NR11 &&
                                                                                reg != GB_IO_NR21 &&
                                                                                reg != GB_IO_NR31 &&
                                                                                reg != GB_IO_NR41
                                                                                )
                                                                                )) {
        return;
    }

    if (reg >= GB_IO_WAV_START && reg <= GB_IO_WAV_END && gb->apu.is_active[GB_WAVE]) {
        if ((!GB_is_cgb(gb) && !gb->apu.wave_channel.wave_form_just_read) || gb->model > GB_MODEL_CGB_E) {
            return;
        }
        reg = GB_IO_WAV_START + gb->apu.wave_channel.current_sample_index / 2;
    }

    /* Todo: this can and should be rewritten with a function table. */
    switch (reg) {
        /* Globals */
        case GB_IO_NR50:
        case GB_IO_NR51:
            gb->io_registers[reg] = value;
            /* These registers affect the output of all 4 channels (but not the output of the PCM registers).*/
            /* We call update_samples with the current value so the APU output is updated with the new outputs */
            for (i = GB_N_CHANNELS; i--;) {
                int8_t sample = gb->apu.samples[i];
                gb->apu.samples[i] = 0x10; // Invalidate to force update
                update_sample(gb, i, sample, 0);
            }
            break;
        case GB_IO_NR52: {

            uint8_t old_pulse_lengths[] = {
                (uint8_t)gb->apu.square_channels[0].pulse_length,
                (uint8_t)gb->apu.square_channels[1].pulse_length,
                (uint8_t)gb->apu.wave_channel.pulse_length,
                (uint8_t)gb->apu.noise_channel.pulse_length
            };
            if ((value & 0x80) && !gb->apu.global_enable) {
                GB_apu_init(gb);
                gb->apu.global_enable = true;
            }
            else if (!(value & 0x80) && gb->apu.global_enable)  {
                for (i = GB_N_CHANNELS; i--;) {
                    update_sample(gb, i, 0, 0);
                }
                memset(&gb->apu, 0, sizeof(gb->apu));
                memset(gb->io_registers + GB_IO_NR10, 0, GB_IO_WAV_START - GB_IO_NR10);
                gb->apu.global_enable = false;
                gb->apu.apu_cycles_in_2mhz = true;
            }

            if (!GB_is_cgb(gb) && (value & 0x80)) {
                gb->apu.square_channels[0].pulse_length = old_pulse_lengths[0];
                gb->apu.square_channels[1].pulse_length = old_pulse_lengths[1];
                gb->apu.wave_channel.pulse_length = old_pulse_lengths[2];
                gb->apu.noise_channel.pulse_length = old_pulse_lengths[3];
            }
        }
        break;

        /* Square channels */
        case GB_IO_NR10: {
            bool old_negate;
            if (unlikely(gb->apu.square_sweep_calculate_countdown || gb->apu.square_sweep_calculate_countdown_reload_timer)) {
                nr10_write_glitch(gb, value);
            }
            old_negate = gb->io_registers[GB_IO_NR10] & 8;
            gb->io_registers[GB_IO_NR10] = value;
            if (gb->model <= GB_MODEL_CGB_C) {
                old_negate = true;
            }
            if (gb->apu.shadow_sweep_sample_length + gb->apu.channel1_completed_addend + old_negate > 0x7FF &&
                !(value & 8)) {
                gb->apu.is_active[GB_SQUARE_1] = false;
                update_sample(gb, GB_SQUARE_1, 0, 0);
            }
            trigger_sweep_calculation(gb);
            break;
        }

        case GB_IO_NR11:
        case GB_IO_NR21: {
            GB_channel_t index = reg == GB_IO_NR21? GB_SQUARE_2: GB_SQUARE_1;
            gb->apu.square_channels[index].pulse_length = (0x40 - (value & 0x3F));
            if (!gb->apu.global_enable) {
                value &= 0x3F;
            }
            break;
        }

        case GB_IO_NR12:
        case GB_IO_NR22: {
            GB_channel_t index = reg == GB_IO_NR22? GB_SQUARE_2: GB_SQUARE_1;
            if ((value & 0xF8) == 0) {
                /* This disables the DAC */
                gb->io_registers[reg] = value;
                gb->apu.is_active[index] = false;
                update_sample(gb, index, 0, 0);
            }
            else if (gb->apu.is_active[index]) {
                nrx2_glitch(gb, &gb->apu.square_channels[index].current_volume,
                            value, gb->io_registers[reg], &gb->apu.square_channels[index].volume_countdown,
                            &gb->apu.square_channels[index].envelope_clock);
                update_square_sample(gb, index, 0);
            }

            break;
        }

        case GB_IO_NR13:
        case GB_IO_NR23: {
            GB_channel_t index = reg == GB_IO_NR23? GB_SQUARE_2: GB_SQUARE_1;
            gb->apu.square_channels[index].sample_length &= ~0xFF;
            gb->apu.square_channels[index].sample_length |= value & 0xFF;
            if (gb->apu.square_channels[index].just_reloaded) {
                gb->apu.square_channels[index].sample_countdown = (gb->apu.square_channels[index].sample_length ^ 0x7FF) * 2 + 1;
            }
            break;
        }

        case GB_IO_NR14:
        case GB_IO_NR24: {
            uint16_t old_sample_length;
            GB_channel_t index = reg == GB_IO_NR24? GB_SQUARE_2: GB_SQUARE_1;
            bool was_active = gb->apu.is_active[index];
            /* TODO: When the sample length changes right before being updated from ≥$700 to <$700, the countdown
                     should change to the old length, but the current sample should not change. Because our write
                     timing isn't accurate to the T-cycle, we hack around it by stepping the sample index backwards. */
            if ((value & 0x80) == 0 && gb->apu.is_active[index] && (gb->io_registers[reg] & 0x7) == 7 && (value & 7) != 7) {
                /* On an AGB, as well as on CGB C and earlier (TODO: Tested: 0, B and C), it behaves slightly different on
                   double speed. */
                if (gb->model == GB_MODEL_CGB_E || gb->model == GB_MODEL_CGB_D || gb->apu.square_channels[index].sample_countdown & 1) {
                    if (gb->apu.square_channels[index].did_tick &&
                        gb->apu.square_channels[index].sample_countdown >> 1 == (gb->apu.square_channels[index].sample_length ^ 0x7FF)) {
                        gb->apu.square_channels[index].current_sample_index--;
                        gb->apu.square_channels[index].current_sample_index &= 7;
                        gb->apu.square_channels[index].sample_surpressed = false;
                    }
                }
            }

            old_sample_length = gb->apu.square_channels[index].sample_length;
            gb->apu.square_channels[index].sample_length &= 0xFF;
            gb->apu.square_channels[index].sample_length |= (value & 7) << 8;
            if (gb->apu.square_channels[index].just_reloaded) {
                gb->apu.square_channels[index].sample_countdown = (gb->apu.square_channels[index].sample_length ^ 0x7FF) * 2 + 1;
            }
            if (value & 0x80) {
                /* Current sample index remains unchanged when restarting channels 1 or 2. It is only reset by
                   turning the APU off. */
                bool force_unsurpressed = false;
                gb->apu.square_channels[index].envelope_clock.locked = false;
                gb->apu.square_channels[index].envelope_clock.clock = false;
                gb->apu.square_channels[index].did_tick = false;
                if (!gb->apu.is_active[index]) {
                    if (gb->model == GB_MODEL_CGB_E || gb->model == GB_MODEL_CGB_D) {
                        if (!(value & 4) && !(((gb->apu.square_channels[index].sample_countdown - gb->apu.square_channels[index].delay) / 2) & 0x400)) {
                            gb->apu.square_channels[index].current_sample_index++;
                            gb->apu.square_channels[index].current_sample_index &= 0x7;
                            force_unsurpressed = true;
                        }
                    }
                    gb->apu.square_channels[index].delay = 6 + gb->apu.lf_div * (gb->model < GB_MODEL_CGB_D && gb->cgb_double_speed? 1 : -1);
                    gb->apu.square_channels[index].sample_countdown = (gb->apu.square_channels[index].sample_length ^ 0x7FF) * 2 + gb->apu.square_channels[index].delay;
                }
                else {
                    unsigned extra_delay = 0;
                    if (gb->model == GB_MODEL_CGB_E || gb->model == GB_MODEL_CGB_D) {
                        if (!gb->apu.square_channels[index].just_reloaded && !(value & 4) && !(((gb->apu.square_channels[index].sample_countdown - 1 - gb->apu.square_channels[index].delay) / 2) & 0x400)) {
                            gb->apu.square_channels[index].current_sample_index++;
                            gb->apu.square_channels[index].current_sample_index &= 0x7;
                            gb->apu.square_channels[index].sample_surpressed = false;
                        }
                        /* Todo: verify with the schematics what's going on in here */
                        else if (gb->apu.square_channels[index].sample_length == 0x7FF &&
                                 old_sample_length != 0x7FF &&
                                 (gb->apu.square_channels[index].sample_surpressed)) {
                            extra_delay += 2;
                        }
                    }
                    /* Timing quirk: if already active, sound starts 2 (2MHz) ticks earlier.*/
                    gb->apu.square_channels[index].delay = 4 - gb->apu.lf_div + extra_delay;
                    gb->apu.square_channels[index].sample_countdown = (gb->apu.square_channels[index].sample_length ^ 0x7FF) * 2 + gb->apu.square_channels[index].delay;
                }
                gb->apu.square_channels[index].current_volume = gb->io_registers[index == GB_SQUARE_1 ? GB_IO_NR12 : GB_IO_NR22] >> 4;
                /* The volume changes caused by NRx4 sound start takes effect instantly (i.e. the effect the previously
                   started sound). The playback itself is not instant which is why we don't update the sample for other
                   cases. */
                if (gb->apu.is_active[index]) {
                    update_square_sample(gb, index, 0);
                }

                gb->apu.square_channels[index].volume_countdown = gb->io_registers[index == GB_SQUARE_1 ? GB_IO_NR12 : GB_IO_NR22] & 7;

                if ((gb->io_registers[index == GB_SQUARE_1 ? GB_IO_NR12 : GB_IO_NR22] & 0xF8) != 0 && !gb->apu.is_active[index]) {
                    gb->apu.is_active[index] = true;
                    update_sample(gb, index, 0, 0);
                    gb->apu.square_channels[index].sample_surpressed = true && !force_unsurpressed;
                }
                if (gb->legacyMode)
                    gb->apu.square_channels[index].pulse_length = 0x40 - (gb->io_registers[reg - (GB_IO_NR14 - GB_IO_NR11)] & 0x3F);	// VGM log fix -Valley Bell
                if (gb->apu.square_channels[index].pulse_length == 0) {
                    gb->apu.square_channels[index].pulse_length = 0x40;
                    gb->apu.square_channels[index].length_enabled = false;
                }

                if (index == GB_SQUARE_1) {
                    gb->apu.square_sweep_instant_calculation_done = false;
                    gb->apu.shadow_sweep_sample_length = 0;
                    gb->apu.channel1_completed_addend = 0;
                    if (gb->io_registers[GB_IO_NR10] & 7) {
                        /* APU bug: if shift is nonzero, overflow check also occurs on trigger */
                        gb->apu.square_sweep_calculate_countdown = gb->io_registers[GB_IO_NR10] & 0x7;
                        if ((gb->apu.lf_div ^ !gb->cgb_double_speed) && gb->model <= GB_MODEL_CGB_C) {
                            gb->apu.square_sweep_calculate_countdown_reload_timer = 3;
                        }
                        else {
                            gb->apu.square_sweep_calculate_countdown_reload_timer = 2;
                        }
                        gb->apu.unshifted_sweep = false;
                        if (!was_active) {
                            gb->apu.square_sweep_calculate_countdown_reload_timer++;
                        }
                        gb->apu.sweep_length_addend = gb->apu.square_channels[GB_SQUARE_1].sample_length;
                        gb->apu.sweep_length_addend >>= (gb->io_registers[GB_IO_NR10] & 7);
                    }
                    else {
                        gb->apu.sweep_length_addend = 0;
                    }
                    gb->apu.channel_1_restart_hold = 2 - gb->apu.lf_div + (GB_is_cgb(gb) && gb->model != GB_MODEL_CGB_D) * 2;
                    gb->apu.square_sweep_countdown = ((gb->io_registers[GB_IO_NR10] >> 4) & 7) ^ 7;
                }
            }

            /* APU glitch - if length is enabled while the DIV-divider's LSB is 1, tick the length once. */
            if (((value & 0x40) || (GB_is_cgb(gb) && gb->model <= GB_MODEL_CGB_B)) && // Current value is irrelevant on CGB-B and older
                !gb->apu.square_channels[index].length_enabled &&
                (gb->apu.div_divider & 1) &&
                gb->apu.square_channels[index].pulse_length) {
                gb->apu.square_channels[index].pulse_length--;
                if (gb->apu.square_channels[index].pulse_length == 0) {
                    if (value & 0x80) {
                        gb->apu.square_channels[index].pulse_length = 0x3F;
                    }
                    else {
                        gb->apu.is_active[index] = false;
                        update_sample(gb, index, 0, 0);
                    }
                }
            }
            gb->apu.square_channels[index].length_enabled = value & 0x40;
            break;
        }

        /* Wave channel */
        case GB_IO_NR30:
            gb->apu.wave_channel.enable = value & 0x80;
            if (!gb->apu.wave_channel.enable) {
                gb->apu.wave_channel.pulsed = false;
                if (gb->apu.is_active[GB_WAVE] && gb->noWaveCorrupt) {
                    // Todo: I assume this happens on pre-CGB models; test this with an audible test
                    if (gb->apu.wave_channel.sample_countdown == 0 && gb->model <= GB_MODEL_CGB_E) {
                        uint16_t pc = rand() & 0x7FFF;  // simulate PC position using random
                        gb->apu.wave_channel.current_sample_byte = gb->io_registers[GB_IO_WAV_START + (pc & 0xF)];
                    }
                    else if (gb->apu.wave_channel.wave_form_just_read && gb->model <= GB_MODEL_CGB_C) {
                        gb->apu.wave_channel.current_sample_byte = gb->io_registers[GB_IO_WAV_START + (GB_IO_NR30 & 0xF)];
                    }
                }
                gb->apu.is_active[GB_WAVE] = false;
                update_sample(gb, GB_WAVE, 0, 0);
            }
            break;
        case GB_IO_NR31:
            gb->apu.wave_channel.pulse_length = (0x100 - value);
            break;
        case GB_IO_NR32:
            {
                static const uint8_t shift_vals[] = {4, 0, 1, 2};
                gb->apu.wave_channel.shift = shift_vals[(value >> 5) & 3];
            }
            if (gb->apu.is_active[GB_WAVE]) {
                update_wave_sample(gb, 0);
            }
            break;
        case GB_IO_NR33:
            gb->apu.wave_channel.sample_length &= ~0xFF;
            gb->apu.wave_channel.sample_length |= value & 0xFF;
            if (gb->apu.wave_channel.bugged_read_countdown == 1) { // Just reloaded countdown
                /* TODO: not verified with a test ROM yet */
                gb->apu.wave_channel.sample_countdown = gb->apu.wave_channel.sample_length ^ 0x7FF;
            }
            break;
        case GB_IO_NR34:
            gb->apu.wave_channel.sample_length &= 0xFF;
            gb->apu.wave_channel.sample_length |= (value & 7) << 8;
            if (value & 0x80) {
                gb->apu.wave_channel.pulsed = true;
                /* DMG bug: wave RAM gets corrupted if the channel is retriggerred 1 cycle before the APU
                            reads from it. */
                if (gb->noWaveCorrupt && !GB_is_cgb(gb) &&
                    gb->apu.is_active[GB_WAVE] &&
                    gb->apu.wave_channel.sample_countdown == 0) {
                    unsigned offset = ((gb->apu.wave_channel.current_sample_index + 1) >> 1) & 0xF;

                    /* This glitch varies between models and even specific instances:
                       DMG-B:     Most of them behave as emulated. A few behave differently.
                       SGB:       As far as I know, all tested instances behave as emulated.
                       MGB, SGB2: Most instances behave non-deterministically, a few behave as emulated.
                     
                       For DMG-B emulation I emulate the most common behavior, which blargg's tests expect (not my own DMG-B, which fails it)
                       For MGB emulation, I emulate my Game Boy Light, which happens to be deterministic.

                      Additionally, I believe DMGs, including those we behave differently than emulated,
                      are all deterministic. */
                    if (offset < 4 && gb->model != GB_MODEL_MGB) {
                        gb->io_registers[GB_IO_WAV_START] = gb->io_registers[GB_IO_WAV_START + offset];
                    }
                    else {
                        memcpy(gb->io_registers + GB_IO_WAV_START,
                               gb->io_registers + GB_IO_WAV_START + (offset & ~3),
                               4);
                    }
                }
                gb->apu.wave_channel.current_sample_index = 0;
                if (gb->apu.is_active[GB_WAVE] && gb->apu.wave_channel.sample_countdown == 0) {
                    gb->apu.wave_channel.current_sample_byte = gb->io_registers[GB_IO_WAV_START];
                }
                if (gb->apu.wave_channel.enable) {
                    gb->apu.is_active[GB_WAVE] = true;
                    update_sample(gb, GB_WAVE,
                                  (gb->apu.wave_channel.current_sample_byte >> 4) >> gb->apu.wave_channel.shift,
                                  0);
                }
                gb->apu.wave_channel.sample_countdown = (gb->apu.wave_channel.sample_length ^ 0x7FF) + 3;
                if (gb->legacyMode)
                    gb->apu.wave_channel.pulse_length = 0x100 - gb->io_registers[GB_IO_NR31];	// VGM log fix -Valley Bell
                if (gb->apu.wave_channel.pulse_length == 0) {
                    gb->apu.wave_channel.pulse_length = 0x100;
                    gb->apu.wave_channel.length_enabled = false;
                }
                /* Note that we don't change the sample just yet! This was verified on hardware. */
            }

            /* APU glitch - if length is enabled while the DIV-divider's LSB is 1, tick the length once. */
            if (((value & 0x40) || (GB_is_cgb(gb) && gb->model <= GB_MODEL_CGB_B)) && // Current value is irrelevant on CGB-B and older
                !gb->apu.wave_channel.length_enabled &&
                (gb->apu.div_divider & 1) &&
                gb->apu.wave_channel.pulse_length) {
                gb->apu.wave_channel.pulse_length--;
                if (gb->apu.wave_channel.pulse_length == 0) {
                    if (value & 0x80) {
                        gb->apu.wave_channel.pulse_length = 0xFF;
                    }
                    else {
                        gb->apu.is_active[GB_WAVE] = false;
                        update_sample(gb, GB_WAVE, 0, 0);
                    }
                }
            }
            gb->apu.wave_channel.length_enabled = value & 0x40;

            break;

        /* Noise Channel */

        case GB_IO_NR41: {
            gb->apu.noise_channel.pulse_length = (0x40 - (value & 0x3F));
            break;
        }

        case GB_IO_NR42: {
            if ((value & 0xF8) == 0) {
                /* This disables the DAC */
                if (gb->apu.is_active[GB_NOISE] && gb->io_registers[GB_IO_NR43] & 7) {
                    if (gb->apu.noise_channel.counter_countdown <= 2) {
                        gb->apu.noise_channel.counter++;
                    }
                    gb->apu.noise_background_counter_active = false;
                }

                gb->io_registers[reg] = value;
                gb->apu.is_active[GB_NOISE] = false;
                update_sample(gb, GB_NOISE, 0, 0);
                gb->apu.noise_counter_active = false;
            }
            else if (gb->apu.is_active[GB_NOISE]) {
                nrx2_glitch(gb, &gb->apu.noise_channel.current_volume,
                            value, gb->io_registers[reg], &gb->apu.noise_channel.volume_countdown,
                            &gb->apu.noise_channel.envelope_clock);
                update_sample(gb, GB_NOISE,
                              gb->apu.noise_channel.current_lfsr_sample ?
                              gb->apu.noise_channel.current_volume : 0,
                              0);
            }
            break;
        }

        case GB_IO_NR43: {
            if (gb->apu.noise_channel.countdown_reloaded) {
                unsigned divisor = (value & 0x07) << 2;
                if (!divisor) divisor = 2;
                if (gb->model > GB_MODEL_CGB_C) {
                    static const uint8_t align_vals[] = {2, 1, 0, 3};
                    gb->apu.noise_channel.counter_countdown =
                    divisor + (divisor == 2? 0 : align_vals[(gb->apu.noise_channel.alignment) & 3]);
                }
                else {
                    static const uint8_t align_vals[] = {2, 1, 4, 3};
                    gb->apu.noise_channel.counter_countdown =
                    divisor + (divisor == 2? 0 : align_vals[(gb->apu.noise_channel.alignment) & 3]);
                }
            }
            if (gb->model <= GB_MODEL_CGB_C) {
                /* TODO: CGB≤C (and DMG) have various unemulated quirks when you write to NR43 just as the counter reloads */
                if (gb->apu.noise_channel.countdown_reloaded) {
                    bool old_bit = (gb->apu.noise_channel.counter >> (gb->io_registers[GB_IO_NR43] >> 4)) & 1;
                    bool glitch_bit = (gb->apu.noise_channel.counter >> 7) & 1;
                    bool new_bit = (gb->apu.noise_channel.counter >> (value >> 4)) & 1;
                    
                    if (!old_bit && new_bit && glitch_bit) {
                        uint16_t previous_counter = (gb->apu.noise_channel.counter - 1) & 0x3FFF;
                        bool old_bit = (previous_counter >> (gb->io_registers[GB_IO_NR43] >> 4)) & 1;
                        bool glitch_bit = (previous_counter >> 7) & 1;
                        bool new_bit = (previous_counter >> (value >> 4)) & 1;
                        if (old_bit && !new_bit && glitch_bit) {
                            step_lfsr(gb, 0);
                        }
                    }
                }
                nr43_write(gb, 0xff);
            }
            nr43_write(gb, value);
            
            break;
        }

        case GB_IO_NR44: {
            if (value & 0x80) {
                gb->apu.noise_channel.envelope_clock.locked = false;
                gb->apu.noise_channel.envelope_clock.clock = false;
                if (!GB_is_cgb(gb) && (gb->apu.noise_channel.alignment & 3) != 0) {
                    gb->apu.noise_channel.dmg_delayed_start = 6;
                }
                else {
                    gb->apu.noise_channel.lfsr = 0;
                    prepare_noise_start(gb);
                    
                    gb->apu.noise_channel.current_volume = gb->io_registers[GB_IO_NR42] >> 4;
                    gb->apu.noise_channel.current_lfsr_sample = false;
                    gb->apu.noise_channel.volume_countdown = gb->io_registers[GB_IO_NR42] & 7;
                    gb->apu.noise_channel.did_step_counter = (gb->apu.noise_channel.alignment & 3) == 2;

                    if (gb->io_registers[GB_IO_NR42] & 0xF8) {
                        gb->apu.is_active[GB_NOISE] = true;
                        update_sample(gb, GB_NOISE, 0, 0);
                    }

                    if (gb->legacyMode)
                        gb->apu.noise_channel.pulse_length = 0x40 - (gb->io_registers[GB_IO_NR41] & 0x3F);	// VGM log fix -Valley Bell
                    if (gb->apu.noise_channel.pulse_length == 0) {
                        gb->apu.noise_channel.pulse_length = 0x40;
                        gb->apu.noise_channel.length_enabled = false;
                    }
                }
            }

            /* APU glitch - if length is enabled while the DIV-divider's LSB is 1, tick the length once. */
            if ((value & 0x40) &&
                !gb->apu.noise_channel.length_enabled &&
                (gb->apu.div_divider & 1) &&
                gb->apu.noise_channel.pulse_length) {
                gb->apu.noise_channel.pulse_length--;
                if (gb->apu.noise_channel.pulse_length == 0) {
                    if (value & 0x80) {
                        gb->apu.noise_channel.pulse_length = 0x3F;
                    }
                    else {
                        gb->apu.is_active[GB_NOISE] = false;
                        update_sample(gb, GB_NOISE, 0, 0);
                    }
                }
            }
            gb->apu.noise_channel.length_enabled = value & 0x40;
            break;
        }
    }
    gb->io_registers[reg] = value;
}

static void GB_set_sample_rate(GB_gameboy_t *gb, unsigned sample_rate)
{
    gb->apu_output.sample_rate = sample_rate;
    if (sample_rate) {
        unsigned i;
        gb->apu_output.highpass_rate = pow(0.999958, GB_get_clock_rate(gb) / (double)sample_rate);
        gb->apu_output.max_cycles_per_sample = (uint32_t)ceil(GB_get_clock_rate(gb) / 2.0 / sample_rate);
        gb->apu_output.quick_fraction_multiply_cache[0] = (uint32_t)(sample_rate * 2.0 / GB_get_clock_rate(gb) * (1 << 28) + 0.5);
        for (i = 1; i < GB_QUICK_MULTIPLY_COUNT; i++) {
            gb->apu_output.quick_fraction_multiply_cache[i] = gb->apu_output.quick_fraction_multiply_cache[0] * (i + 1);
        }
    }
    else {
        gb->apu_output.max_cycles_per_sample = 0x400;
    }
}

static unsigned GB_get_sample_rate(GB_gameboy_t *gb)
{
    return gb->apu_output.sample_rate;
}

//static void GB_apu_set_sample_callback(GB_gameboy_t *gb, GB_sample_callback_t callback)
//{
//    gb->apu_output.sample_callback = callback;
//}

static void GB_set_highpass_filter_mode(GB_gameboy_t *gb, GB_highpass_mode_t mode)
{
    gb->apu_output.highpass_mode = mode;
}

static void GB_set_interference_volume(GB_gameboy_t *gb, double volume)
{
    gb->apu_output.interference_volume = volume;
}


static void GB_set_channel_muted(GB_gameboy_t *gb, GB_channel_t channel, bool muted)
{
    assert(channel < GB_N_CHANNELS);
    gb->apu_output.channel_muted[channel] = muted;
}

static bool GB_is_channel_muted(GB_gameboy_t *gb, GB_channel_t channel)
{
    return gb->apu_output.channel_muted[channel];
}



// from timing.c
static void GB_set_internal_div_counter(GB_gameboy_t *gb, uint16_t value)
{
    /* TIMA increases when a specific high-bit becomes a low-bit. */
    uint16_t triggers = gb->div_counter & ~value;
    
    /* TODO: Can switching to double speed mode trigger an event? */
    uint16_t apu_bit = gb->cgb_double_speed? 0x2000 : 0x1000;
    if (unlikely(triggers & apu_bit)) {
        GB_apu_div_event(gb);
    }
    else {
        uint16_t secondary_triggers = ~gb->div_counter & value;
        if (unlikely(secondary_triggers & apu_bit)) {
            GB_apu_div_secondary_event(gb);
        }
    }
    gb->div_counter = value;
}

#define GB_STATE_MACHINE(gb, unit, cycles) \
(gb)->unit##_cycles += cycles; \
if ((gb)->unit##_cycles <= 0) {\
    return;\
}\
switch ((gb)->unit##_state)

#define GB_STATE(gb, unit, state) case state: goto unit##state

#define GB_SLEEP(gb, unit, state, cycles) do {\
    (gb)->unit##_cycles -= cycles; \
    if (unlikely((gb)->unit##_cycles <= 0)) {\
        (gb)->unit##_state = state;\
        return;\
        unit##state:; \
    }\
} while (0)

static void timers_run(GB_gameboy_t *gb, uint8_t cycles)
{
    if (gb->stopped) {
        if (GB_is_cgb(gb)) {
            gb->apu.apu_cycles += 1 << !gb->cgb_double_speed;
        }
        gb->apu_output.sample_cycles += (gb->apu_output.sample_rate << !gb->cgb_double_speed) << 1;
        return;
    }
    
    GB_STATE_MACHINE(gb, div, cycles) {
        GB_STATE(gb, div, 1);
        GB_STATE(gb, div, 2);
    }
    
    GB_SLEEP(gb, div, 1, 3);
    while (true) {
        //advance_tima_state_machine(gb);
        GB_set_internal_div_counter(gb, gb->div_counter + 4);
        gb->apu.apu_cycles += 1 << !gb->cgb_double_speed;
        gb->apu_output.sample_cycles += (gb->apu_output.sample_rate << !gb->cgb_double_speed) << 1;
        GB_SLEEP(gb, div, 2, 4);
        if (unlikely(gb->apu.pending_envelope_tick)) {
            GB_apu_delayed_envelope_tick(gb);
        }
    }
}

static void GB_advance_cycles(GB_gameboy_t *gb, uint8_t cycles)
{
    gb->apu.pcm_mask[0] = gb->apu.pcm_mask[1] = 0xFF; // Sort of hacky, but too many cross-component interactions to do it right

    timers_run(gb, cycles);
    if (unlikely(!gb->cgb_double_speed)) {
        cycles <<= 1;
    }
    
    GB_apu_run(gb, false);
}


static void gb_sameboy_update(void *chip, UINT32 samples, DEV_SMPL **outputs)
{
	GB_gameboy_t *gb = (GB_gameboy_t *)chip;
	UINT32 i;

	for (i = 0; i < samples; i++)
	{
		RC_STEP(&gb->cycleCntr);
		GB_advance_cycles(gb, RC_GET_VAL(&gb->cycleCntr));
		RC_MASK(&gb->cycleCntr);

		outputs[0][i] = gb->apu_output.output.left;
		outputs[1][i] = gb->apu_output.output.right;
	}
}

static UINT8 device_start_gb_sameboy(const DEV_GEN_CFG* cfg, DEV_INFO* retDevInf)
{
	GB_gameboy_t *gb;

	gb = (GB_gameboy_t *)calloc(1, sizeof(GB_gameboy_t));
	if (gb == NULL)
		return 0xFF;

	gb->clock_rate = cfg->clock;
	gb->smpl_rate = gb->clock_rate / 64;
	SRATE_CUSTOM_HIGHEST(cfg->srMode, gb->smpl_rate, cfg->smplRate);

	gb->model = (cfg->flags & 0x01) ? GB_MODEL_CGB_A : GB_MODEL_DMG_B;
	gb->cgb_double_speed = false;
	gb->during_div_write = false;
	gb->halted = false;
	gb->stopped = false;
	RC_SET_RATIO(&gb->cycleCntr, cfg->clock, gb->smpl_rate);

	gb_sameboy_set_mute_mask(gb, 0x00);
	GB_set_highpass_filter_mode(gb, GB_HIGHPASS_ACCURATE);
	GB_set_interference_volume(gb, 0.0);
	gb->noWaveCorrupt = false;
	gb->legacyMode = false;

	gb->_devData.chipInf = gb;
	INIT_DEVINF(retDevInf, &gb->_devData, gb->smpl_rate, &devDef_GB_SameBoy);

	return 0x00;
}

static void device_stop_gb_sameboy(void *chip)
{
	GB_gameboy_t *gb = (GB_gameboy_t *)chip;
	
	free(gb);
	
	return;
}

static void device_reset_gb_sameboy(void *chip)
{
	GB_gameboy_t *gb = (GB_gameboy_t *)chip;
	UINT32 muteMask;

	muteMask = gb_sameboy_get_mute_mask(gb);

	GB_apu_init(gb);
	gb->apu_output.cycles_since_render = 0;
	gb->apu_output.sample_cycles = 0;
	gb->apu_output.sample_fraction = 0;
	GB_set_sample_rate(gb, gb->smpl_rate);
	RC_RESET(&gb->cycleCntr);

	gb_sameboy_set_mute_mask(gb, muteMask);

	return;
}

static void gb_sameboy_set_mute_mask(void *chip, UINT32 MuteMask)
{
	GB_gameboy_t *gb = (GB_gameboy_t *)chip;
	
	gb->apu_output.channel_muted[0] = (MuteMask >> 0) & 0x01;
	gb->apu_output.channel_muted[1] = (MuteMask >> 1) & 0x01;
	gb->apu_output.channel_muted[2] = (MuteMask >> 2) & 0x01;
	gb->apu_output.channel_muted[3] = (MuteMask >> 3) & 0x01;
	
	return;
}

static UINT32 gb_sameboy_get_mute_mask(void *chip)
{
	GB_gameboy_t *gb = (GB_gameboy_t *)chip;
	UINT32 muteMask;
	
	muteMask =	(gb->apu_output.channel_muted[0] << 0) |
				(gb->apu_output.channel_muted[1] << 1) |
				(gb->apu_output.channel_muted[2] << 2) |
				(gb->apu_output.channel_muted[3] << 3);
	
	return muteMask;
}

static void gb_sameboy_set_options(void *chip, UINT32 Flags)
{
	GB_gameboy_t *gb = (GB_gameboy_t *)chip;
	
	gb->noWaveCorrupt = (Flags & 0x02) >> 1;
	gb->legacyMode = (Flags & 0x80) >> 7;
	
	return;
}

static void gb_sameboy_w(void *chip, UINT8 offset, UINT8 data)
{
	GB_gameboy_t *gb = (GB_gameboy_t *)chip;
	GB_apu_write(gb, GB_IO_NR10 + offset, data);
}

static UINT8 gb_sameboy_r(void *chip, UINT8 offset)
{
	GB_gameboy_t *gb = (GB_gameboy_t *)chip;
	return GB_apu_read(gb, GB_IO_NR10 + offset);
}
