#ifndef __SAMEBOY_APU_PRIVATE_H__
#define __SAMEBOY_APU_PRIVATE_H__

#include "../../_stdbool.h"
#include "emutypes.h"

#define likely(x)	(x)
#define unlikely(x)	(x)

#define internal static
#define noinline //__attribute__((noinline))

#define unrolled
#define nounroll

#define nodefault

#if !defined(MIN)
#define MIN(A, B)    ((A) < (B) ? (A) : (B))
#endif
#if !defined(MAX)
#define MAX(A, B)    ((A) < (B) ? (B) : (A))
#endif



#define GB_QUICK_MULTIPLY_COUNT 64

/* Speed = 1 / Length (in seconds) */
#define DAC_DECAY_SPEED 20000
#define DAC_ATTACK_SPEED 20000

/* Divides nicely and never overflows with 4 channels and 8 (1-8) volume levels */
#define MAX_CH_AMP 0x1E00	// same volume as MAME core
#define CH_STEP (MAX_CH_AMP/0xF/8)



/* APU ticks are 2MHz, triggered by an internal APU clock. */

typedef union
{
    struct {
        int16_t left;
        int16_t right;
    };
    uint32_t packed;
} GB_sample_t;

typedef struct
{
    double left;
    double right;
} GB_double_sample_t;

typedef enum {
    GB_SQUARE_1,
    GB_SQUARE_2,
    GB_WAVE,
    GB_NOISE,
    GB_N_CHANNELS
} GB_channel_t;

typedef struct
{
    bool locked:1; // Represents FYNO's output on channel 4
    bool clock:1; // Represents FOSY on channel 4
    bool should_lock:1;  // Represents FYNO's input on channel 4
    uint8_t padding:5;
} GB_envelope_clock_t;

typedef struct
{
    bool global_enable;
    uint16_t apu_cycles;

    uint8_t samples[GB_N_CHANNELS];
    bool is_active[GB_N_CHANNELS];

    uint8_t div_divider; // The DIV register ticks the APU at 512Hz, but is then divided
                         // once more to generate 128Hz and 64Hz clocks

    uint8_t lf_div; // The APU runs in 2MHz, but channels 1, 2 and 4 run in 1MHZ so we divide
                    // need to divide the signal.

    uint8_t square_sweep_countdown; // In 128Hz
    uint8_t square_sweep_calculate_countdown; // In 1 MHz
    uint8_t square_sweep_calculate_countdown_reload_timer; // In 1 Mhz, for glitches related to reloading square_sweep_calculate_countdown
    uint16_t sweep_length_addend;
    uint16_t shadow_sweep_sample_length;
    bool unshifted_sweep;
    bool square_sweep_instant_calculation_done;

    uint8_t channel_1_restart_hold;
    uint16_t channel1_completed_addend;
    struct {
        uint16_t pulse_length; // Reloaded from NRX1 (xorred), in 256Hz DIV ticks
        uint8_t current_volume; // Reloaded from NRX2
        uint8_t volume_countdown; // Reloaded from NRX2
        uint8_t current_sample_index;
        bool sample_surpressed;

        uint16_t sample_countdown; // in APU ticks (Reloaded from sample_length, xorred $7FF)
        uint16_t sample_length; // From NRX3, NRX4, in APU ticks
        bool length_enabled; // NRX4
        GB_envelope_clock_t envelope_clock;
        uint8_t delay; // Hack for CGB D/E phantom step due to how sample_countdown is implemented in SameBoy
        bool did_tick:1;
        bool just_reloaded:1;
        uint8_t padding:6;
    } square_channels[2];

    struct {
        bool enable; // NR30
        uint16_t pulse_length; // Reloaded from NR31 (xorred), in 256Hz DIV ticks
        uint8_t shift; // NR32
        uint16_t sample_length; // NR33, NR34, in APU ticks
        bool length_enabled; // NR34

        uint16_t sample_countdown; // in APU ticks (Reloaded from sample_length, xorred $7FF)
        uint8_t current_sample_index;
        uint8_t current_sample_byte; // Current sample byte.
        bool wave_form_just_read;
        bool pulsed;
        uint8_t bugged_read_countdown;
    } wave_channel;

    struct {
        uint16_t pulse_length; // Reloaded from NR41 (xorred), in 256Hz DIV ticks
        uint8_t current_volume; // Reloaded from NR42
        uint8_t volume_countdown; // Reloaded from NR42
        uint16_t lfsr;
        bool narrow;

        uint8_t counter_countdown; // Counts from 0-7 to 0 to tick counter (Scaled from 512KHz to 2MHz)
        uint16_t counter; // A bit from this 14-bit register ticks LFSR
        bool length_enabled; // NR44

        uint8_t alignment; // If (NR43 & 7) != 0, samples are aligned to 512KHz clock instead of
                           // 1MHz. This variable keeps track of the alignment.
        bool current_lfsr_sample;
        bool did_step_counter;
        bool countdown_reloaded;
        uint8_t dmg_delayed_start;
        GB_envelope_clock_t envelope_clock;
    } noise_channel;

    enum /*: uint8_t*/ {
        GB_SKIP_DIV_EVENT_INACTIVE,
        GB_SKIP_DIV_EVENT_SKIPPED,
        GB_SKIP_DIV_EVENT_SKIP,
    } skip_div_event;
    uint8_t pcm_mask[2]; // For CGB-0 to CGB-C PCM read glitch
    
    bool apu_cycles_in_2mhz; // For compatibility with 0.16.x save states
    bool pending_envelope_tick;
    
    // Move to noise struct when breaking compat
    bool noise_counter_active;
    bool noise_background_counter_active;
    bool lfsr_stepped_in_narrow;
    bool lfsr_bit_7_before_step; // Used by some corruptions?
    bool noise_started_with_dac_disabled; // TODO: Background counting behaves slightly different this way?
} GB_apu_t;

typedef enum {
    GB_HIGHPASS_OFF, // Do not apply any filter, keep DC offset
    GB_HIGHPASS_ACCURATE, // Apply a highpass filter similar to the one used on hardware
    GB_HIGHPASS_REMOVE_DC_OFFSET, // Remove DC Offset without affecting the waveform
    GB_HIGHPASS_MAX
} GB_highpass_mode_t;

typedef struct {
    unsigned sample_rate;

    unsigned sample_cycles; // Counts by sample_rate until it reaches the clock frequency
    unsigned max_cycles_per_sample;

    uint32_t cycles_since_render;
    uint32_t sample_fraction; // Counter in 1 / sample_rate, in 4.28 fixed format
    uint32_t quick_fraction_multiply_cache[GB_QUICK_MULTIPLY_COUNT];
    
    GB_sample_t channel_output[GB_N_CHANNELS];
    double dac_discharge[GB_N_CHANNELS];
    bool channel_muted[GB_N_CHANNELS];
    bool edge_triggered[GB_N_CHANNELS];

    GB_highpass_mode_t highpass_mode;
    double highpass_rate;
    GB_double_sample_t highpass_diff;
    
    double interference_volume;
    double interference_highpass;
    
    GB_sample_t output;
} GB_apu_output_t;


#define GB_MODEL_FAMILY_MASK 0xF00
#define GB_MODEL_DMG_FAMILY 0x000
#define GB_MODEL_MGB_FAMILY 0x100
#define GB_MODEL_CGB_FAMILY 0x200
#define GB_MODEL_GBP_BIT 0x20
#define GB_MODEL_PAL_BIT 0x40
#define GB_MODEL_NO_SFC_BIT 0x80

typedef enum {
	// GB_MODEL_DMG_0 = 0x000,
	// GB_MODEL_DMG_A = 0x001,
	GB_MODEL_DMG_B = 0x002,
	// GB_MODEL_DMG_C = 0x003,
	GB_MODEL_SGB = 0x004,
	GB_MODEL_SGB_NTSC = GB_MODEL_SGB,
	GB_MODEL_SGB_PAL = GB_MODEL_SGB | GB_MODEL_PAL_BIT,
	GB_MODEL_SGB_NTSC_NO_SFC = GB_MODEL_SGB | GB_MODEL_NO_SFC_BIT,
	GB_MODEL_SGB_NO_SFC = GB_MODEL_SGB_NTSC_NO_SFC,
	GB_MODEL_SGB_PAL_NO_SFC = GB_MODEL_SGB | GB_MODEL_NO_SFC_BIT | GB_MODEL_PAL_BIT,
	GB_MODEL_MGB = 0x100,
	GB_MODEL_SGB2 = 0x101,
	GB_MODEL_SGB2_NO_SFC = GB_MODEL_SGB2 | GB_MODEL_NO_SFC_BIT,
	GB_MODEL_CGB_0 = 0x200,
	GB_MODEL_CGB_A = 0x201,
	GB_MODEL_CGB_B = 0x202,
	GB_MODEL_CGB_C = 0x203,
	GB_MODEL_CGB_D = 0x204,
	GB_MODEL_CGB_E = 0x205,
	// GB_MODEL_AGB_0 = 0x206,
	GB_MODEL_AGB_A = 0x207,
	GB_MODEL_GBP_A = GB_MODEL_AGB_A | GB_MODEL_GBP_BIT, // AGB-A inside a Game Boy Player
	GB_MODEL_AGB = GB_MODEL_AGB_A,
	GB_MODEL_GBP = GB_MODEL_GBP_A,
	//GB_MODEL_AGB_B = 0x208
	//GB_MODEL_AGB_E = 0x209
	//GB_MODEL_GBP_E = GB_MODEL_AGB_E | GB_MODEL_GBP_BIT, // AGB-E inside a Game Boy Player
} GB_model_t;

struct GB_gameboy_s {
    DEV_DATA _devData;

    UINT32 smpl_rate;
    uint32_t clock_rate;

    RATIO_CNTR cycleCntr;

    uint8_t io_registers[0x80];
    int32_t display_cycles, display_state;	// GB_UNIT(display);
    int32_t div_cycles, div_state;	// GB_UNIT(div);
    uint16_t div_counter;

    GB_apu_t apu;
    GB_apu_output_t apu_output;

    /* CPU and General Hardware Flags*/
    GB_model_t model;
    //bool cgb_mode;
    bool cgb_double_speed;
    bool during_div_write;
    bool halted;
    bool stopped;

	bool noWaveCorrupt;
	bool legacyMode;
};
typedef struct GB_gameboy_s GB_gameboy_t;


static void GB_set_channel_muted(GB_gameboy_t *gb, GB_channel_t channel, bool muted);
static bool GB_is_channel_muted(GB_gameboy_t *gb, GB_channel_t channel);
static void GB_set_sample_rate(GB_gameboy_t *gb, unsigned sample_rate);
static unsigned GB_get_sample_rate(GB_gameboy_t *gb);
static void GB_set_highpass_filter_mode(GB_gameboy_t *gb, GB_highpass_mode_t mode);
static void GB_set_interference_volume(GB_gameboy_t *gb, double volume);
internal bool GB_apu_is_DAC_enabled(GB_gameboy_t *gb, GB_channel_t index);
internal void GB_apu_write(GB_gameboy_t *gb, uint8_t reg, uint8_t value);
internal uint8_t GB_apu_read(GB_gameboy_t *gb, uint8_t reg);
internal void GB_apu_div_event(GB_gameboy_t *gb);
internal void GB_apu_div_secondary_event(GB_gameboy_t *gb);
internal void GB_apu_delayed_envelope_tick(GB_gameboy_t *gb);
internal void GB_apu_init(GB_gameboy_t *gb);
internal void GB_apu_run(GB_gameboy_t *gb, bool force);

enum {
    /* Sound */
    GB_IO_NR10       = 0x10, // Channel 1 Sweep register (R/W)
    GB_IO_NR11       = 0x11, // Channel 1 Sound length/Wave pattern duty (R/W)
    GB_IO_NR12       = 0x12, // Channel 1 Volume Envelope (R/W)
    GB_IO_NR13       = 0x13, // Channel 1 Frequency lo (Write Only)
    GB_IO_NR14       = 0x14, // Channel 1 Frequency hi (R/W)
    /* NR20 does not exist */
    GB_IO_NR21       = 0x16, // Channel 2 Sound Length/Wave Pattern Duty (R/W)
    GB_IO_NR22       = 0x17, // Channel 2 Volume Envelope (R/W)
    GB_IO_NR23       = 0x18, // Channel 2 Frequency lo data (W)
    GB_IO_NR24       = 0x19, // Channel 2 Frequency hi data (R/W)
    GB_IO_NR30       = 0x1A, // Channel 3 Sound on/off (R/W)
    GB_IO_NR31       = 0x1B, // Channel 3 Sound Length
    GB_IO_NR32       = 0x1C, // Channel 3 Select output level (R/W)
    GB_IO_NR33       = 0x1D, // Channel 3 Frequency's lower data (W)
    GB_IO_NR34       = 0x1E, // Channel 3 Frequency's higher data (R/W)
    /* NR40 does not exist */
    GB_IO_NR41       = 0x20, // Channel 4 Sound Length (R/W)
    GB_IO_NR42       = 0x21, // Channel 4 Volume Envelope (R/W)
    GB_IO_NR43       = 0x22, // Channel 4 Polynomial Counter (R/W)
    GB_IO_NR44       = 0x23, // Channel 4 Counter/consecutive, Initial (R/W)
    GB_IO_NR50       = 0x24, // Channel control / ON-OFF / Volume (R/W)
    GB_IO_NR51       = 0x25, // Selection of Sound output terminal (R/W)
    GB_IO_NR52       = 0x26, // Sound on/off

    /* Missing */

    GB_IO_WAV_START  = 0x30, // Wave pattern start
    GB_IO_WAV_END    = 0x3F, // Wave pattern end
};

#endif	// __SAMEBOY_APU_PRIVATE_H__
