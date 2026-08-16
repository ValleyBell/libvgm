// license:MIT
// copyright-holders:laoo
// C++17 -> C90 backport by Valley Bell
#include <stdlib.h>

#include "../../stdtype.h"
#include "../../_stdbool.h"
#include "emutypes.h"
#include "../snddef.h"
#include "../EmuStructs.h"
#include "../SoundDevs.h"
#include "../EmuCores.h"
#include "../EmuHelper.h"
#include "mikey.h"

static void mikey_write( void*, uint8_t address, uint8_t value );
static uint8_t mikey_read( void*, uint8_t address );
static void mikey_set_mute_mask( void*, UINT32 mutes );
static UINT8 mikey_start( const DEV_GEN_CFG*, DEV_INFO* );
static void mikey_stop( void* );
static void mikey_reset( void* );
static void mikey_update( void*, UINT32 samples, DEV_SMPL** outputs );

static DEVDEF_RWFUNC devFunc[] =
{
  {RWF_REGISTER | RWF_WRITE, DEVRW_A8D8, 0, (void*)mikey_write},
  {RWF_REGISTER | RWF_READ, DEVRW_A8D8, 0, (void*)mikey_read},
  {RWF_CHN_MUTE | RWF_WRITE, DEVRW_ALL, 0, (void*)mikey_set_mute_mask},
  {0x00, 0x00, 0, NULL}
};

static DEV_DEF devDef =
{
  "MIKEY", "laoo", FCC_LAOO,

  mikey_start,
  mikey_stop,
  mikey_reset,
  mikey_update,

  NULL, // SetOptionBits
  mikey_set_mute_mask,
  NULL, // SetPanning
  NULL, // SetSampleRateChangeCallback
  NULL, // SetLoggingCallback
  NULL, // LinkDevice

  devFunc,  // rwFuncs
};

static const char* DeviceName(const DEV_GEN_CFG* devCfg)
{
	return "Mikey";
}

static UINT16 DeviceChannels(const DEV_GEN_CFG* devCfg)
{
	return 4;
}

static const char** DeviceChannelNames(const DEV_GEN_CFG* devCfg)
{
	return NULL;
}

static const DEVLINK_IDS* DeviceLinkIDs(const DEV_GEN_CFG* devCfg)
{
	return NULL;
}

const DEV_DECL sndDev_Mikey =
{
	DEVID_MIKEY,
	DeviceName,
	DeviceChannels,
	DeviceChannelNames,
	DeviceLinkIDs,
	{	// cores
		&devDef,
		NULL
	}
};


#if defined( _MSC_VER )

#if _MSC_VER >= 1400
#include <intrin.h>
#endif

static uint32_t popcnt_generic( uint32_t x )
{
  int v = 0;
  while ( x != 0 )
  {
    x &= x - 1;
    v++;
  }
  return v;
}

#if (_MSC_VER >= 1400) && (defined( _M_IX86 ) || defined( _M_X64 ))

static uint32_t popcnt_intrinsic( uint32_t x )
{
  return __popcnt( x );
}

static uint32_t( *popcnt )( uint32_t );

//detecting popcnt availability on msvc intel
static void selectPOPCNT(void)
{
  int info[4];
  __cpuid( info, 1 );
  if ( ( info[2] & ( (int)1 << 23 ) ) != 0 )
  {
    popcnt = &popcnt_intrinsic;
  }
  else
  {
    popcnt = &popcnt_generic;
  }
}

#else //defined( _M_IX86 ) || defined( _M_X64 )

//MSVC non INTEL should use generic implementation
static void selectPOPCNT(void)
{
}

#define popcnt popcnt_generic

#endif

#else //defined( _MSC_VER )

//non MVSC should use builtin implementation

static void selectPOPCNT(void)
{
}

#define popcnt __builtin_popcount

#endif

#ifndef INT8_MIN
#define INT8_MIN         (-0x80)
#define INT8_MAX         0x7F
#ifdef _MSC_VER
#define INT64_MAX        0x7FFFFFFFFFFFFFFFuI64
#else
#define INT64_MAX        0x7FFFFFFFFFFFFFFFull
#endif
#endif

#define CNT_MAX (INT64_MAX & ~15)

#define MIKEY_TIMER_STAGE_COUNT 4
#define MIKEY_AUDIO_STAGE_COUNT 4
#define MIKEY_AUDIO_STAGE_BASE  MIKEY_TIMER_STAGE_COUNT
#define MIKEY_STAGE_COUNT       ( MIKEY_TIMER_STAGE_COUNT + MIKEY_AUDIO_STAGE_COUNT )

static int32_t clamp_i32( int32_t v, int32_t lo, int32_t hi )
{
  return v < lo ? lo : ( v > hi ? hi : v );
}

static int64_t min_i64( int64_t v1, int64_t v2 )
{
  return v1 > v2 ? v2 : v1;
}

// mikey_timer_t::CONTROLA : uint8_t
#define TMR_CTRLA_RESET_DONE      0x40  // 0b01000000
#define TMR_CTRLA_ENABLE_RELOAD   0x10  // 0b00010000
#define TMR_CTRLA_ENABLE_COUNT    0x08  // 0b00001000
#define TMR_CTRLA_AUD_CLOCK_MASK  0x07  // 0b00000111
#define TMR_CTRLA_LINKED_CLOCK    0x07  // 0b00000111
// mikey_timer_t::CONTROLB : uint8_t
#define TMR_CTRLB_BORROW_OUT      0x01  // 0b00000001
#define TMR_CTRLB_BORROW_IN       0x02  // 0b00000010
#define TMR_CTRLB_LAST_CLOCK      0x04  // 0b00000100
#define TMR_CTRLB_TIMER_DONE      0x08  // 0b00001000

// mikey_timer_t
typedef struct
{
  int64_t mStatusClearTick;
  int mAudShift;
  uint32_t mPendingClocks;
  bool mEnableReload;
  bool mEnableCount;
  bool mLinked;
  bool mResetDone;
  bool mTimerDone;
  uint8_t mBackup;
  uint8_t mControlA;
  uint8_t mControlB;
  uint8_t mValue;
} mikey_timer_t;

static int64_t mikey_timer_computeTriggerTime( mikey_timer_t* timer, int64_t tick );
static int64_t mikey_timer_computeAction( mikey_timer_t* timer, int64_t tick );
static bool mikey_timer_borrowIn( mikey_timer_t* timer );
static bool mikey_timer_clock( mikey_timer_t* timer );
static void mikey_timer_clearStatus( mikey_timer_t* timer, int64_t tick );
static void mikey_timer_scheduleStatusClear( mikey_timer_t* timer, int64_t tick );

// mikey_timer_t public:
static void mikey_timer_Timer( mikey_timer_t* timer )
{
  timer->mStatusClearTick = CNT_MAX;
  timer->mAudShift = 0;
  timer->mPendingClocks = 0;
  timer->mEnableReload = false;
  timer->mEnableCount = false;
  timer->mLinked = false;
  timer->mResetDone = false;
  timer->mTimerDone = false;
  timer->mBackup = 0;
  timer->mControlA = 0;
  timer->mControlB = 0;
  timer->mValue = 0;
}

static int64_t mikey_timer_setBackup( mikey_timer_t* timer, int64_t tick, uint8_t backup )
{
  timer->mBackup = backup;
  return mikey_timer_computeAction( timer, tick );
}

static int64_t mikey_timer_setControlA( mikey_timer_t* timer, int64_t tick, uint8_t controlA )
{
  int oldAudShift = timer->mAudShift;
  bool oldEnableCount = timer->mEnableCount;

  timer->mControlA = controlA;
  timer->mResetDone = ( controlA & TMR_CTRLA_RESET_DONE ) != 0;
  timer->mEnableReload = ( controlA & TMR_CTRLA_ENABLE_RELOAD ) != 0;
  timer->mEnableCount = ( controlA & TMR_CTRLA_ENABLE_COUNT ) != 0;
  timer->mAudShift = controlA & TMR_CTRLA_AUD_CLOCK_MASK;
  timer->mLinked = timer->mAudShift == TMR_CTRLA_LINKED_CLOCK;

  if ( oldAudShift != timer->mAudShift || ( ! oldEnableCount && timer->mEnableCount ) )
    timer->mPendingClocks = 0;

  if ( timer->mResetDone )
  {
    timer->mTimerDone = false;
    timer->mControlB &= ~TMR_CTRLB_TIMER_DONE;
  }

  return mikey_timer_computeAction( timer, tick );
}

static int64_t mikey_timer_setCount( mikey_timer_t* timer, int64_t tick, uint8_t value )
{
  timer->mValue = value;
  return mikey_timer_computeTriggerTime( timer, tick );
}

static int64_t mikey_timer_setControlB( mikey_timer_t* timer, int64_t tick, uint8_t controlB, bool* borrowOut )
{
  mikey_timer_clearStatus( timer, tick );
  *borrowOut = false;
  if ( ( timer->mControlB & TMR_CTRLB_BORROW_IN ) == 0 && ( controlB & TMR_CTRLB_BORROW_IN ) != 0 )
    *borrowOut = mikey_timer_borrowIn( timer );

  timer->mControlB = controlB & TMR_CTRLB_TIMER_DONE;
  timer->mTimerDone = ( controlB & TMR_CTRLB_TIMER_DONE ) != 0;
  timer->mStatusClearTick = CNT_MAX;
  return mikey_timer_computeAction( timer, tick );
}

static int64_t mikey_timer_fireAction( mikey_timer_t* timer, int64_t tick, bool* borrowOut )
{
  *borrowOut = mikey_timer_clock( timer );

  return mikey_timer_computeAction( timer, tick );
}

static uint8_t mikey_timer_getBackup( const mikey_timer_t* timer )
{
  return timer->mBackup;
}

static uint8_t mikey_timer_getCount( const mikey_timer_t* timer )
{
  return timer->mValue;
}

//mikey_timer_t private:
static bool mikey_timer_borrowIn( mikey_timer_t* timer )
{
  if ( timer->mValue > 0 )
  {
    timer->mValue --;
    if ( ! timer->mLinked )
      timer->mControlB &= ~TMR_CTRLB_LAST_CLOCK;
    return false;
  }

  timer->mControlB |= TMR_CTRLB_BORROW_OUT;
  if ( ! timer->mLinked )
    timer->mControlB |= TMR_CTRLB_LAST_CLOCK;
  timer->mControlB |= TMR_CTRLB_TIMER_DONE;
  timer->mTimerDone = true;
  if ( timer->mEnableReload )
    timer->mValue = timer->mBackup;

  return true;
}

static bool mikey_timer_clock( mikey_timer_t* timer )
{
  timer->mControlB &= ~( TMR_CTRLB_BORROW_OUT | TMR_CTRLB_BORROW_IN );

  if ( timer->mResetDone )
  {
    timer->mControlB &= ~TMR_CTRLB_TIMER_DONE;
    timer->mTimerDone = false;
  }

  if ( ! timer->mEnableReload && timer->mTimerDone )
    return false;

  timer->mControlB |= TMR_CTRLB_BORROW_IN;
  return mikey_timer_borrowIn( timer );
}

static void mikey_timer_clearStatus( mikey_timer_t* timer, int64_t tick )
{
  if ( tick >= timer->mStatusClearTick )
  {
    timer->mControlB &= ~( TMR_CTRLB_BORROW_OUT | TMR_CTRLB_BORROW_IN );
    timer->mStatusClearTick = CNT_MAX;
  }
}

static void mikey_timer_scheduleStatusClear( mikey_timer_t* timer, int64_t tick )
{
  if ( ( timer->mControlB & ( TMR_CTRLB_BORROW_OUT | TMR_CTRLB_BORROW_IN ) ) != 0 )
    timer->mStatusClearTick = tick;
  else
    timer->mStatusClearTick = CNT_MAX;
}

static int64_t mikey_timer_computeTriggerTime( mikey_timer_t* timer, int64_t tick )
{
  if ( timer->mEnableCount && ! timer->mLinked )
  {
    int64_t period = (int64_t)1 << ( timer->mAudShift + 4 );
    return ( tick & ~( period - 1 ) ) + period;
  }
  else
  {
    return CNT_MAX;  //infinite
  }
}

static int64_t mikey_timer_computeAction( mikey_timer_t* timer, int64_t tick )
{
  return mikey_timer_computeTriggerTime( timer, tick );
}
// mikey_timer_t end

// mikey_audio_channel_t
#define AC_FEEDBACK_7       0x80  // 0b10000000
#define AC_ENABLE_INTEGRATE 0x20  // 0b00100000
typedef struct
{
  mikey_timer_t mTimer;
  uint32_t mNumber;

  uint32_t mShiftRegister;
  uint32_t mTapSelector;
  bool mEnableIntegrate;
  int8_t mVolume;
  int8_t mOutput;
  uint8_t mFeedback;
  uint8_t mCtrlA;
} mikey_audio_channel_t;

static void mikey_audio_channel_trigger( mikey_audio_channel_t* ac );

//mikey_audio_channel_t public:
static void mikey_audio_channel_AudioChannel( mikey_audio_channel_t* ac, uint32_t number )
{
  mikey_timer_Timer( &ac->mTimer );
  ac->mNumber = number;
  ac->mShiftRegister = 0;
  ac->mTapSelector = 0;
  ac->mEnableIntegrate = false;
  ac->mVolume = 0;
  ac->mOutput = 0;
  ac->mFeedback = 0;
  ac->mCtrlA = 0;
  return;
}

static int64_t mikey_audio_channel_fireAction( mikey_audio_channel_t* ac, int64_t tick, bool* borrowOut )
{
  int64_t action = mikey_timer_fireAction( &ac->mTimer, tick, borrowOut );
  if ( *borrowOut )
    mikey_audio_channel_trigger( ac );
  return action;
}

static void mikey_audio_channel_setVolume( mikey_audio_channel_t* ac, int8_t value )
{
  ac->mVolume = value;
}

static void mikey_audio_channel_setFeedback( mikey_audio_channel_t* ac, uint8_t value )
{
  ac->mFeedback = value;
  ac->mTapSelector = ( ac->mTapSelector & 0x3c0 ) | ( value & 0x3f ) | ( ( (int)value & 0xc0 ) << 4 );
}

static void mikey_audio_channel_setOutput( mikey_audio_channel_t* ac, uint8_t value )
{
  ac->mOutput = value;
}

static void mikey_audio_channel_setShift( mikey_audio_channel_t* ac, uint8_t value )
{
  ac->mShiftRegister = ( ac->mShiftRegister & 0x0f00 ) | value;
}

static int64_t mikey_audio_channel_setBackup( mikey_audio_channel_t* ac, int64_t tick, uint8_t value )
{
  return mikey_timer_setBackup( &ac->mTimer, tick, value );
}

static int64_t mikey_audio_channel_setControl( mikey_audio_channel_t* ac, int64_t tick, uint8_t value )
{
  ac->mCtrlA = value;

  ac->mTapSelector = ( ac->mTapSelector & 0xf7f ) | ( value & AC_FEEDBACK_7 );
  ac->mEnableIntegrate = ( value & AC_ENABLE_INTEGRATE ) != 0;
  return mikey_timer_setControlA( &ac->mTimer, tick, value & ~( AC_FEEDBACK_7 | AC_ENABLE_INTEGRATE ) );
}

static int64_t mikey_audio_channel_setCounter( mikey_audio_channel_t* ac, int64_t tick, uint8_t value )
{
  return mikey_timer_setCount( &ac->mTimer, tick, value );
}

static int64_t mikey_audio_channel_setOther( mikey_audio_channel_t* ac, int64_t tick, uint8_t value, bool* borrowOut )
{
  int64_t action = mikey_timer_setControlB( &ac->mTimer, tick, value & 0x0f, borrowOut );
  if ( *borrowOut )
    mikey_audio_channel_trigger( ac );
  ac->mShiftRegister = ( ac->mShiftRegister & 0x0ff ) | ( ( (int)value & 0xf0 ) << 4 );
  return action;
}

static uint8_t mikey_audio_channel_readRegister( mikey_audio_channel_t* ac, int64_t tick, int reg )
{
  switch ( reg )
  {
  case 0:
    return ac->mVolume;
  case 1:
    return ac->mFeedback;
  case 2:
    return ac->mOutput;
  case 3:
    return ac->mShiftRegister & 0xff;
  case 4:
    return mikey_timer_getBackup( &ac->mTimer );
  case 5:
    return ac->mCtrlA;
  case 6:
    return mikey_timer_getCount( &ac->mTimer );
  case 7:
    mikey_timer_clearStatus( &ac->mTimer, tick );
    return ( ( ac->mShiftRegister >> 4 ) & 0xf0 ) | ( ac->mTimer.mControlB & 0x0f );
  default:
    return 0xff;
  }
}

// mikey_audio_channel_t private:
static void mikey_audio_channel_trigger( mikey_audio_channel_t* ac )
{
  uint32_t xorGate = ac->mTapSelector & ac->mShiftRegister;
  uint32_t parity = popcnt( xorGate ) & 1;
  uint32_t newShift = ( ( ac->mShiftRegister << 1 ) & 0x0ffe ) | ( parity ^ 1 );
  ac->mShiftRegister = newShift;

  if ( ac->mEnableIntegrate )
  {
    int32_t temp = ac->mOutput + ( ( newShift & 1 ) ? ac->mVolume : -ac->mVolume );
    ac->mOutput = (int8_t)clamp_i32( temp, INT8_MIN, INT8_MAX );
  }
  else
  {
    int32_t temp = ( newShift & 1 ) ? ac->mVolume : -(int32_t)ac->mVolume;
    ac->mOutput = (int8_t)clamp_i32( temp, INT8_MIN, INT8_MAX );
  }
}
// mikey_audio_channel_t end


/*
  "Queue" holding events of timer and audio stages.
  Time is in 16 MHz units.
*/
// mikey_action_queue_t
#define AQ_TAB_SIZE 8
typedef struct
{
  int64_t mTab[AQ_TAB_SIZE];
} mikey_action_queue_t;

// mikey_action_queue_t public:
static void mikey_action_queue_ActionQueue( mikey_action_queue_t* aq )
{
  int i;
  for (i = 0; i < AQ_TAB_SIZE; i ++)
    aq->mTab[i] = CNT_MAX;
}

static void mikey_action_queue_set( mikey_action_queue_t* aq, int stage, int64_t value )
{
  aq->mTab[stage] = value;
}

static void mikey_action_queue_schedule( mikey_action_queue_t* aq, int stage, int64_t value )
{
  aq->mTab[stage] = min_i64( aq->mTab[stage], value );
}

static int64_t mikey_action_queue_pop( mikey_action_queue_t* aq, int* stage )
{
  int i;
  int64_t value = aq->mTab[0];

  *stage = 0;
  for ( i = 1; i < AQ_TAB_SIZE; i ++ )
  {
    if ( aq->mTab[i] < value )
    {
      value = aq->mTab[i];
      *stage = i;
    }
  }

  return value;
}


typedef struct
{
  int16_t left;
  int16_t right;
} mikey_audio_sample_t;

static const uint8_t mikey_timer_numbers[MIKEY_TIMER_STAGE_COUNT] = {1, 3, 5, 7};
static const uint8_t mikey_stage_slots[MIKEY_STAGE_COUNT] = {1, 3, 5, 7, 8, 9, 10, 11};

// mikey_pimpl_t
typedef struct
{
  mikey_timer_t mTimers[MIKEY_TIMER_STAGE_COUNT];
  mikey_audio_channel_t mAudioChannels[4];
  int mAttenuationLeft[4];
  int mAttenuationRight[4];
  bool mMute[4];
  #define MIKEY_REGPOOL_SIZE  (4 * 8 + 4)
  uint8_t mRegisterPool[MIKEY_REGPOOL_SIZE];

  uint8_t mPan;
  uint8_t mStereo;

  mikey_audio_sample_t mSample;
  bool mSampleValid;
  int mWriteStage;
  int mWriteBorrowStage;
} mikey_pimpl_t;

// mikey_pimpl_t public:
#define VOLCNTRL 0x0
#define FEEDBACK 0x1
#define OUTPUT 0x2
#define SHIFT 0x3
#define BACKUP 0x4
#define CONTROL 0x5
#define COUNTER 0x6
#define OTHER 0x7

#define ATTENREG0 0x40
#define ATTENREG1 0x41
#define ATTENREG2 0x42
#define ATTENREG3 0x43
#define MPAN 0x44
#define MSTEREO 0x50

static void mikey_pimpl_reset( mikey_pimpl_t* mikey );
static void mikey_pimpl_MikeyPimpl( mikey_pimpl_t* mikey )
{
  int i;
  mikey_pimpl_reset(mikey);
  for (i = 0; i < 4; i ++)
    mikey->mMute[i] = false;
}

static void mikey_pimpl_reset( mikey_pimpl_t* mikey )
{
  int i;
  for (i = 0; i < MIKEY_TIMER_STAGE_COUNT; i ++)
    mikey_timer_Timer( &mikey->mTimers[i] );
  for (i = 0; i < 4; i ++)
    mikey_audio_channel_AudioChannel( &mikey->mAudioChannels[i], i );
  mikey->mPan = 0;
  mikey->mStereo = 0;
  for (i = 0; i < MIKEY_REGPOOL_SIZE; i ++)
    mikey->mRegisterPool[i] = 0;
  for (i = 0; i < 4; i ++)
  {
    mikey->mAttenuationLeft[i] = 0;
    mikey->mAttenuationRight[i] = 0;
  }
  mikey->mSampleValid = false;
  mikey->mWriteStage = -1;
  mikey->mWriteBorrowStage = -1;
}

static int mikey_pimpl_timerStage( uint8_t timerNumber )
{
  int stage;
  for ( stage = 0; stage < MIKEY_TIMER_STAGE_COUNT; stage ++ )
  {
    if ( mikey_timer_numbers[stage] == timerNumber )
      return stage;
  }
  return -1;
}

static mikey_timer_t* mikey_pimpl_stageTimer( mikey_pimpl_t* mikey, int stage )
{
  if ( stage < MIKEY_TIMER_STAGE_COUNT )
    return &mikey->mTimers[stage];
  return &mikey->mAudioChannels[stage - MIKEY_AUDIO_STAGE_BASE].mTimer;
}

static int64_t mikey_pimpl_stageServiceTick( int stage, int64_t tick )
{
  int64_t action = ( tick & ~(int64_t)15 ) + mikey_stage_slots[stage];
  if ( action <= tick )
    action += 16;
  return action;
}

static void mikey_pimpl_applyWriteAction( mikey_pimpl_t* mikey, mikey_action_queue_t* queue, int64_t tick, int64_t action )
{
  mikey_timer_t* timer;

  if ( mikey->mWriteStage < 0 )
    return;

  timer = mikey_pimpl_stageTimer( mikey, mikey->mWriteStage );
  if ( timer->mLinked && timer->mPendingClocks > 0 )
  {
    mikey_action_queue_schedule( queue, mikey->mWriteStage, mikey_pimpl_stageServiceTick( mikey->mWriteStage, tick ) );
    return;
  }

  mikey_action_queue_set( queue, mikey->mWriteStage, action );
}

static int64_t mikey_pimpl_writeTimer( mikey_pimpl_t* mikey, int64_t tick, uint8_t address, uint8_t value )
{
  int stage = mikey_pimpl_timerStage( ( address >> 2 ) & 7 );
  mikey_timer_t* timer;

  if ( stage < 0 )
    return 0;

  mikey->mWriteStage = stage;
  timer = &mikey->mTimers[stage];
  switch ( address & 3 )
  {
  case 0:
    return mikey_timer_setBackup( timer, tick, value );
  case 1:
    return mikey_timer_setControlA( timer, tick, value );
  case 2:
    return mikey_timer_setCount( timer, tick, value );
  case 3:
  {
    bool borrowOut;
    int64_t action = mikey_timer_setControlB( timer, tick, value, &borrowOut );
    if ( borrowOut )
      mikey->mWriteBorrowStage = stage;
    return action;
  }
  default:
    return 0;
  }
}

static void mikey_pimpl_scheduleLink( mikey_pimpl_t* mikey, mikey_action_queue_t* queue, int stage, int64_t tick )
{
  int nextStage = ( stage + 1 ) & ( MIKEY_STAGE_COUNT - 1 );
  mikey_timer_t* timer = mikey_pimpl_stageTimer( mikey, nextStage );
  int64_t action;

  if ( ! timer->mLinked )
    return;

  mikey_timer_clearStatus( timer, tick );
  timer->mPendingClocks ++;
  timer->mControlB |= TMR_CTRLB_BORROW_IN;
  action = mikey_pimpl_stageServiceTick( nextStage, tick );
  mikey_timer_scheduleStatusClear( timer, action );
  mikey_action_queue_schedule( queue, nextStage, action );
}

static void mikey_pimpl_fireStage( mikey_pimpl_t* mikey, mikey_action_queue_t* queue, int stage, int64_t tick )
{
  mikey_timer_t* timer = mikey_pimpl_stageTimer( mikey, stage );
  bool borrowOut = false;
  int64_t action;

  if ( timer->mLinked )
  {
    uint32_t clocks;
    timer->mControlB &= ~( TMR_CTRLB_BORROW_OUT | TMR_CTRLB_BORROW_IN );

    if ( ! timer->mEnableCount )
    {
      action = CNT_MAX;
    }
    else
    {
      if ( timer->mResetDone )
      {
        timer->mControlB &= ~TMR_CTRLB_TIMER_DONE;
        timer->mTimerDone = false;
      }

      if ( timer->mEnableReload || ! timer->mTimerDone )
      {
        clocks = timer->mPendingClocks;
        timer->mPendingClocks = 0;
        if ( clocks > 0 )
        {
          timer->mControlB |= TMR_CTRLB_BORROW_IN;
          while ( clocks -- > 0 )
          {
            borrowOut = mikey_timer_borrowIn( timer );
            if ( borrowOut )
            {
              if ( stage >= MIKEY_AUDIO_STAGE_BASE )
                mikey_audio_channel_trigger( &mikey->mAudioChannels[stage - MIKEY_AUDIO_STAGE_BASE] );
              mikey_pimpl_scheduleLink( mikey, queue, stage, tick );
              if ( ! timer->mEnableReload )
                break;
            }
          }
        }
      }
      action = CNT_MAX;
    }
    mikey_timer_scheduleStatusClear( timer, tick + 16 );
  }
  else if ( stage < MIKEY_TIMER_STAGE_COUNT )
  {
    action = mikey_timer_fireAction( timer, tick, &borrowOut );
    mikey_timer_scheduleStatusClear( timer, mikey_pimpl_stageServiceTick( stage, tick ) );
    if ( borrowOut )
      mikey_pimpl_scheduleLink( mikey, queue, stage, tick );
  }
  else
  {
    action = mikey_audio_channel_fireAction( &mikey->mAudioChannels[stage - MIKEY_AUDIO_STAGE_BASE], tick, &borrowOut );
    mikey_timer_scheduleStatusClear( timer, mikey_pimpl_stageServiceTick( stage, tick ) );
    if ( borrowOut )
      mikey_pimpl_scheduleLink( mikey, queue, stage, tick );
  }

  mikey_action_queue_set( queue, stage, action );
  mikey->mSampleValid = false;
}

static int64_t mikey_pimpl_write( mikey_pimpl_t* mikey, int64_t tick, uint8_t address, uint8_t value )
{
  mikey->mWriteStage = -1;
  mikey->mWriteBorrowStage = -1;
  if ( address < 0x20 )
    return mikey_pimpl_writeTimer( mikey, tick, address, value );

  mikey->mSampleValid = false;

  if ( address < 0x40 )
  {
    size_t idx = ( address >> 3 ) & 3;
    switch ( address & 0x7 )
    {
    case VOLCNTRL:
      mikey_audio_channel_setVolume( &mikey->mAudioChannels[idx], (int8_t)value );
      break;
    case FEEDBACK:
      mikey_audio_channel_setFeedback( &mikey->mAudioChannels[idx], value );
      break;
    case OUTPUT:
      mikey_audio_channel_setOutput( &mikey->mAudioChannels[idx], value );
      break;
    case SHIFT:
      mikey_audio_channel_setShift( &mikey->mAudioChannels[idx], value );
      break;
    case BACKUP:
      mikey->mWriteStage = MIKEY_AUDIO_STAGE_BASE + idx;
      return mikey_audio_channel_setBackup( &mikey->mAudioChannels[idx], tick, value );
    case CONTROL:
      mikey->mWriteStage = MIKEY_AUDIO_STAGE_BASE + idx;
      return mikey_audio_channel_setControl( &mikey->mAudioChannels[idx], tick, value );
    case COUNTER:
      mikey->mWriteStage = MIKEY_AUDIO_STAGE_BASE + idx;
      return mikey_audio_channel_setCounter( &mikey->mAudioChannels[idx], tick, value );
    case OTHER:
    {
      bool borrowOut;
      int64_t action = mikey_audio_channel_setOther( &mikey->mAudioChannels[idx], tick, value, &borrowOut );
      mikey->mWriteStage = MIKEY_AUDIO_STAGE_BASE + idx;
      if ( borrowOut )
        mikey->mWriteBorrowStage = MIKEY_AUDIO_STAGE_BASE + idx;
      return action;
    }
    }
  }
  else
  {
    int idx = address & 3;
    switch ( address )
    {
    case ATTENREG0:
    case ATTENREG1:
    case ATTENREG2:
    case ATTENREG3:
      mikey->mRegisterPool[8*4+idx] = value;
      mikey->mAttenuationRight[idx] = ( value & 0x0f ) << 2;
      mikey->mAttenuationLeft[idx] = ( value & 0xf0 ) >> 2;
      break;
    case MPAN:
      mikey->mPan = value;
      break;
    case MSTEREO:
      mikey->mStereo = value;
      break;
    default:
      break;
    }
  }
  return 0;
}

static mikey_audio_sample_t mikey_pimpl_sampleAudio( mikey_pimpl_t* mikey )
{
  if ( !mikey->mSampleValid )
  {
    int left = 0;
    int right = 0;

    if ( !mikey->mMute[0] )
    {
      left += mikey->mAudioChannels[0].mOutput * ( ( ( mikey->mStereo & ( 0x10 << 0 ) ) == 0 ) ? ( ( mikey->mPan & ( 0x10 << 0 ) ) != 0 ? mikey->mAttenuationLeft[0] : 0x3c ) : 0 );
      right += mikey->mAudioChannels[0].mOutput * ( ( ( mikey->mStereo & ( 0x01 << 0 ) ) == 0 ) ? ( ( mikey->mPan & ( 0x01 << 0 ) ) != 0 ? mikey->mAttenuationRight[0] : 0x3c ) : 0 );
    }
    if ( !mikey->mMute[1] )
    {
      left += mikey->mAudioChannels[1].mOutput * ( ( ( mikey->mStereo & ( 0x10 << 1 ) ) == 0 ) ? ( ( mikey->mPan & ( 0x10 << 1 ) ) != 0 ? mikey->mAttenuationLeft[1] : 0x3c ) : 0 );
      right += mikey->mAudioChannels[1].mOutput * ( ( ( mikey->mStereo & ( 0x01 << 1 ) ) == 0 ) ? ( ( mikey->mPan & ( 0x01 << 1 ) ) != 0 ? mikey->mAttenuationRight[1] : 0x3c ) : 0 );
    }
    if ( !mikey->mMute[2] )
    {
      left += mikey->mAudioChannels[2].mOutput * ( ( ( mikey->mStereo & ( 0x10 << 2 ) ) == 0 ) ? ( ( mikey->mPan & ( 0x10 << 2 ) ) != 0 ? mikey->mAttenuationLeft[2] : 0x3c ) : 0 );
      right += mikey->mAudioChannels[2].mOutput * ( ( ( mikey->mStereo & ( 0x01 << 2 ) ) == 0 ) ? ( ( mikey->mPan & ( 0x01 << 2 ) ) != 0 ? mikey->mAttenuationRight[2] : 0x3c ) : 0 );
    }
    if ( !mikey->mMute[3] )
    {
      left += mikey->mAudioChannels[3].mOutput * ( ( ( mikey->mStereo & ( 0x10 << 3 ) ) == 0 ) ? ( ( mikey->mPan & ( 0x10 << 3 ) ) != 0 ? mikey->mAttenuationLeft[3] : 0x3c ) : 0 );
      right += mikey->mAudioChannels[3].mOutput * ( ( ( mikey->mStereo & ( 0x01 << 3 ) ) == 0 ) ? ( ( mikey->mPan & ( 0x01 << 3 ) ) != 0 ? mikey->mAttenuationRight[3] : 0x3c ) : 0 );
    }

    mikey->mSample.left = ( int16_t )left;
    mikey->mSample.right = ( int16_t )right;
    mikey->mSampleValid = true;
  }

  return mikey->mSample;
}

static uint8_t mikey_pimpl_read( mikey_pimpl_t* mikey, int64_t tick, int address )
{
  if ( address < 0x20 )
  {
    int stage = mikey_pimpl_timerStage( ( address >> 2 ) & 7 );
    mikey_timer_t* timer;

    if ( stage < 0 )
      return 0xff;

    timer = &mikey->mTimers[stage];
    switch ( address & 3 )
    {
    case 0:
      return mikey_timer_getBackup( timer );
    case 1:
      return timer->mControlA;
    case 2:
      return mikey_timer_getCount( timer );
    case 3:
      mikey_timer_clearStatus( timer, tick );
      return timer->mControlB;
    default:
      return 0xff;
    }
  }

  if ( address < 0x40 )
  {
    size_t i = ( address - 0x20 ) >> 3;
    return mikey_audio_channel_readRegister( &mikey->mAudioChannels[i], tick, address & 7 );
  }

  if ( address >= ATTENREG0 && address <= ATTENREG3 )
    return mikey->mRegisterPool[8 * 4 + ( address & 3 )];
  if ( address == MPAN )
    return mikey->mPan;
  if ( address == MSTEREO )
    return mikey->mStereo;
  return 0xff;
}

static void mikey_pimpl_mute( mikey_pimpl_t* mikey, int channel, bool mute )
{
  mikey->mMute[channel] = mute;
}


// mikey_t
typedef struct
{
  DEV_DATA devData;
  mikey_pimpl_t mMikey;
  mikey_action_queue_t mQueue;
  int64_t mTick;
  int64_t mNextTick;
  uint32_t mSampleRate;
  uint32_t mSamplesRemainder;
  uint32_t mTicksPerSample1;
  uint32_t mTicksPerSample2;
} mikey_t;

static UINT8 mikey_start( const DEV_GEN_CFG* cfg, DEV_INFO* retDevInf )
{
  mikey_t* mikey;

  mikey = (mikey_t*)calloc(1, sizeof(mikey_t));
  if (mikey == NULL)
    return 0xFF;

  mikey_pimpl_MikeyPimpl( &mikey->mMikey );
  mikey_action_queue_ActionQueue( &mikey->mQueue );
  mikey->mSampleRate = cfg->clock / 16;
  SRATE_CUSTOM_HIGHEST(cfg->srMode, mikey->mSampleRate, cfg->smplRate);
  mikey->mTicksPerSample1 = 16000000 / mikey->mSampleRate;
  mikey->mTicksPerSample2 = 16000000 % mikey->mSampleRate;
  selectPOPCNT();

  mikey->devData.chipInf = (void*)mikey;
  INIT_DEVINF( retDevInf, &mikey->devData, mikey->mSampleRate, &devDef );
  return 0x00;
}

static void mikey_reset( void* info )
{
  mikey_t* mikey = (mikey_t*)info;
  mikey_pimpl_reset( &mikey->mMikey );
  mikey_action_queue_ActionQueue( &mikey->mQueue );
  mikey->mTick = 0;
  mikey->mNextTick = mikey->mTicksPerSample1;
  mikey->mSamplesRemainder = mikey->mTicksPerSample2;
}

static void mikey_stop( void* info )
{
  mikey_t* mikey = (mikey_t*)info;

  free( mikey );
}

static void mikey_write( void* info, uint8_t address, uint8_t value )
{
  mikey_t* mikey = (mikey_t*)info;

  int64_t action = mikey_pimpl_write( &mikey->mMikey, mikey->mTick, address, value );
  mikey_pimpl_applyWriteAction( &mikey->mMikey, &mikey->mQueue, mikey->mTick, action );
  if ( mikey->mMikey.mWriteBorrowStage >= 0 )
  {
    mikey_pimpl_scheduleLink( &mikey->mMikey, &mikey->mQueue, mikey->mMikey.mWriteBorrowStage, mikey->mTick );
  }
}

static void mikey_update( void* info, UINT32 samples, DEV_SMPL** outputs )
{
  mikey_t* mikey = (mikey_t*)info;
  UINT32 i = 0;
  for ( ;; )
  {
    int stage;
    int64_t value = mikey_action_queue_pop( &mikey->mQueue, &stage );
    while ( value > mikey->mTick )
    {
      mikey_audio_sample_t sample;

      if ( i >= samples )
        return;

      sample = mikey_pimpl_sampleAudio( &mikey->mMikey );
      outputs[0][i] = sample.left;
      outputs[1][i] = sample.right;

      mikey->mTick = mikey->mNextTick & ~15;
      mikey->mNextTick = mikey->mNextTick + mikey->mTicksPerSample1;
      mikey->mSamplesRemainder += mikey->mTicksPerSample2;
      if ( mikey->mSamplesRemainder >= mikey->mSampleRate )
      {
        mikey->mSamplesRemainder -= mikey->mSampleRate;
        mikey->mNextTick += 1;
      }

      i ++;
    }

    mikey_pimpl_fireStage( &mikey->mMikey, &mikey->mQueue, stage, value );
  }
}

static uint8_t mikey_read( void* info, uint8_t address )
{
  mikey_t* mikey = (mikey_t*)info;

  return mikey_pimpl_read( &mikey->mMikey, mikey->mTick, address );
}

static void mikey_set_mute_mask( void* info, UINT32 mutes )
{
  mikey_t* mikey = (mikey_t*)info;
  int i;

  for ( i = 0; i < 4; ++i )
  {
    mikey_pimpl_mute( &mikey->mMikey, i, ( mutes & ( 1 << i ) ) != 0 );
  }
}
