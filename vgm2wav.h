#ifndef VGM2WAV_H
#define VGM2WAV_H

#include "player/vgmplayer.h"
#include "player/s98player.h"
#include "player/droplayer.h"
#include "utils/DataLoader.h"
#include "utils/FileLoader.h"
#include "emu/SoundDevs.h"
#include "emu/EmuCores.h"
#include "emu/SoundEmu.h"

/* lots of boiler-plate to keep vgm2wav.c understandable */

#define PASTE(a,b) a ## b
#define UNDERSCORE(a,b) a ## _ ## b

#define SAMPLE_RATE 44100
#define BUFFER_LEN 4096
#define FADE_LEN 8

#define WRAPPER_INIT_FUNC(FUNC,TYPE) \
  wrapper.FUNC = (PASTE(FUNC,Func)) &UNDERSCORE(TYPE,FUNC);

#define WRAPPER_INIT_FUNCS(TYPE) \
  WRAPPER_INIT_FUNC(Delete,TYPE) \
  WRAPPER_INIT_FUNC(GetPlayerType,TYPE) \
  WRAPPER_INIT_FUNC(GetPlayerName,TYPE) \
  WRAPPER_INIT_FUNC(LoadFile,TYPE) \
  WRAPPER_INIT_FUNC(UnloadFile,TYPE) \
  WRAPPER_INIT_FUNC(GetTags,TYPE) \
  WRAPPER_INIT_FUNC(GetSongInfo,TYPE) \
  WRAPPER_INIT_FUNC(GetSongDeviceInfo,TYPE) \
  WRAPPER_INIT_FUNC(InitDeviceOptions,TYPE) \
  WRAPPER_INIT_FUNC(SetDeviceOptions,TYPE) \
  WRAPPER_INIT_FUNC(GetDeviceOptions,TYPE) \
  WRAPPER_INIT_FUNC(SetDeviceMuting,TYPE) \
  WRAPPER_INIT_FUNC(GetDeviceMuting,TYPE) \
  WRAPPER_INIT_FUNC(SetSampleRate,TYPE) \
  WRAPPER_INIT_FUNC(GetSampleRate,TYPE) \
  WRAPPER_INIT_FUNC(SetPlaybackSpeed,TYPE) \
  WRAPPER_INIT_FUNC(SetCallback,TYPE) \
  WRAPPER_INIT_FUNC(Tick2Sample,TYPE) \
  WRAPPER_INIT_FUNC(Sample2Tick,TYPE) \
  WRAPPER_INIT_FUNC(Tick2Second,TYPE) \
  WRAPPER_INIT_FUNC(Sample2Second,TYPE) \
  WRAPPER_INIT_FUNC(GetState,TYPE) \
  WRAPPER_INIT_FUNC(GetCurPos,TYPE) \
  WRAPPER_INIT_FUNC(GetCurLoop,TYPE) \
  WRAPPER_INIT_FUNC(GetTotalTicks,TYPE) \
  WRAPPER_INIT_FUNC(GetLoopTicks,TYPE) \
  WRAPPER_INIT_FUNC(GetTotalPlayTicks,TYPE) \
  WRAPPER_INIT_FUNC(Start,TYPE) \
  WRAPPER_INIT_FUNC(Stop,TYPE) \
  WRAPPER_INIT_FUNC(Reset,TYPE) \
  WRAPPER_INIT_FUNC(Seek,TYPE) \
  WRAPPER_INIT_FUNC(Render,TYPE)

typedef void  (*DeleteFunc)(void *);
typedef UINT32 (*GetPlayerTypeFunc)(void *);
typedef const char * (*GetPlayerNameFunc)(void *);
typedef UINT8 (*LoadFileFunc)(void *, DATA_LOADER *);
typedef UINT8 (*UnloadFileFunc)(void *);
typedef const char * const * (*GetTagsFunc)(void *);
typedef UINT8 (*GetSongInfoFunc)(void *, PLR_SONG_INFO *);
typedef UINT8 (*GetSongDeviceInfoFunc)(void *, PLR_DEV_INFO **, UINT32 *);
typedef UINT8 (*InitDeviceOptionsFunc)(PLR_DEV_OPTS *);
typedef UINT8 (*SetDeviceOptionsFunc)(void *, UINT32, PLR_DEV_OPTS *);
typedef UINT8 (*GetDeviceOptionsFunc)(void *, UINT32, PLR_DEV_OPTS *);
typedef UINT8 (*SetDeviceMutingFunc)(void *, UINT32, PLR_MUTE_OPTS *);
typedef UINT8 (*GetDeviceMutingFunc)(void *, UINT32, PLR_MUTE_OPTS *);
typedef UINT32 (*GetSampleRateFunc)(void *);
typedef UINT8 (*SetSampleRateFunc)(void *, UINT32);
typedef UINT8 (*SetPlaybackSpeedFunc)(void *, double);
typedef void (*SetCallbackFunc)(void *, UINT8 (*)(void *, void *, UINT8, void *),void *);
typedef UINT32 (*Tick2SampleFunc)(void *, UINT32);
typedef UINT32 (*Sample2TickFunc)(void *, UINT32);
typedef double (*Tick2SecondFunc)(void *, UINT32);
typedef double (*Sample2SecondFunc)(void *, UINT32);

typedef UINT8 (*GetStateFunc)(void *);
typedef UINT32 (*GetCurPosFunc)(void *, UINT8);
typedef UINT32 (*GetCurLoopFunc)(void *);
typedef UINT32 (*GetTotalTicksFunc)(void *);
typedef UINT32 (*GetLoopTicksFunc)(void *);
typedef UINT32 (*GetTotalPlayTicksFunc)(void *,UINT32);

typedef UINT8 (*StartFunc)(void *);
typedef UINT8 (*StopFunc)(void *);
typedef UINT8 (*ResetFunc)(void *);
typedef UINT8 (*SeekFunc)(void *,UINT8,UINT32);
typedef UINT8 (*RenderFunc)(void *, UINT32, WAVE_32BS *);

struct player_funcs_s {
    void *player;
    GetPlayerTypeFunc GetPlayerType;
    GetPlayerNameFunc GetPlayerName;
    DeleteFunc Delete;
    LoadFileFunc LoadFile;
    UnloadFileFunc UnloadFile;
    GetTagsFunc GetTags;
    GetSongInfoFunc GetSongInfo;
    GetSongDeviceInfoFunc GetSongDeviceInfo;
    InitDeviceOptionsFunc InitDeviceOptions;
    SetDeviceOptionsFunc SetDeviceOptions;
    GetDeviceOptionsFunc GetDeviceOptions;
    SetDeviceMutingFunc SetDeviceMuting;
    GetDeviceMutingFunc GetDeviceMuting;
    GetSampleRateFunc GetSampleRate;
    SetSampleRateFunc SetSampleRate;
    SetPlaybackSpeedFunc SetPlaybackSpeed;
    SetCallbackFunc SetCallback;
    Tick2SampleFunc Tick2Sample;
    Sample2TickFunc Sample2Tick;
    Tick2SecondFunc Tick2Second;
    Sample2SecondFunc Sample2Second;
    GetStateFunc GetState;
    GetCurPosFunc GetCurPos;
    GetCurLoopFunc GetCurLoop;
    GetTotalTicksFunc GetTotalTicks;
    GetLoopTicksFunc GetLoopTicks;
    GetTotalPlayTicksFunc GetTotalPlayTicks;
    StartFunc Start;
    StopFunc Stop;
    ResetFunc Reset;
    SeekFunc Seek;
    RenderFunc Render;
};

typedef struct player_funcs_s player_funcs;

/* utility functions */
static void FCC2Str(char *str, UINT32 fcc) {
    str[4] = '\0';
    str[0] = (char)((fcc >> 24) & 0xFF);
    str[1] = (char)((fcc >> 16) & 0xFF);
    str[2] = (char)((fcc >>  8) & 0xFF);
    str[3] = (char)((fcc >>  0) & 0xFF);
}

static void pack_int16le(UINT8 *d, INT16 n) {
    d[0] = (UINT8)(n      );
    d[1] = (UINT8)(n >> 8 );
}

static void pack_uint16le(UINT8 *d, UINT16 n) {
    d[0] = (UINT8)(n      );
    d[1] = (UINT8)(n >> 8 );
}

static void pack_uint32le(UINT8 *d, UINT32 n) {
    d[0] = (UINT8)(n      );
    d[1] = (UINT8)(n >> 8 );
    d[2] = (UINT8)(n >> 16);
    d[3] = (UINT8)(n >> 24);
}

static int write_header(FILE *f, unsigned int totalSamples) {
    unsigned int dataSize = totalSamples * sizeof(INT16) * 2;
    UINT8 tmp[4];
    if(fwrite("RIFF",1,4,f) != 4) return 0;
    pack_uint32le(tmp,dataSize + 44 - 8);
    if(fwrite(tmp,1,4,f) != 4) return 0;

    if(fwrite("WAVE",1,4,f) != 4) return 0;
    if(fwrite("fmt ",1,4,f) != 4) return 0;

    pack_uint32le(tmp,16); /*fmtSize */
    if(fwrite(tmp,1,4,f) != 4) return 0;

    pack_uint16le(tmp,1); /* audioFormat */
    if(fwrite(tmp,1,2,f) != 2) return 0;

    pack_uint16le(tmp,2); /* numChannels */
    if(fwrite(tmp,1,2,f) != 2) return 0;

    pack_uint32le(tmp,SAMPLE_RATE);
    if(fwrite(tmp,1,4,f) != 4) return 0;

    pack_uint32le(tmp,SAMPLE_RATE * 2 * sizeof(INT16));
    if(fwrite(tmp,1,4,f) != 4) return 0;

    pack_uint16le(tmp,2 * sizeof(INT16));
    if(fwrite(tmp,1,2,f) != 2) return 0;

    pack_uint16le(tmp,sizeof(INT16) * 8);
    if(fwrite(tmp,1,2,f) != 2) return 0;

    if(fwrite("data",1,4,f) != 4) return 0;

    pack_uint32le(tmp,dataSize);
    if(fwrite(tmp,1,4,f) != 4) return 0;

    return 1;
}

static void pack_samples(UINT8 *d, unsigned int sample_count, WAVE_32BS *data) {
    unsigned int i = 0;
    while(i<sample_count) {
        data[i].L >>= 8;
        data[i].R >>= 8;
        if(data[i].L < -0x8000) {
            data[i].L = -0x8000;
        } else if(data[i].L > 0x7FFF) {
            data[i].L = 0x7FFFF;
        }
        if(data[i].R < -0x8000) {
            data[i].R = -0x8000;
        } else if(data[i].R > 0x7FFF) {
            data[i].R = 0x7FFFF;
        }
        pack_int16le(&d[ 0 ], (INT16)data[i].L);
        pack_int16le(&d[ sizeof(INT16) ], (INT16)data[i].R);
        i++;
        d += (sizeof(INT16) * 2);
    }
}

static int write_samples(FILE *f, unsigned int sample_count, UINT8 *d) {
    return fwrite(d,sizeof(INT16) * 2,sample_count,f) == sample_count;
}


/* apply a fade to a buffer of samples */
/* samples_rem - remaining total samples, includes fade samples */
/* samples_fade - total number of fade sampes */
/* sample_count - number of samples being rendered right now */
static void fade_samples(unsigned int samples_rem, unsigned int samples_fade, unsigned int sample_count, WAVE_32BS *data) {
    unsigned int i = 0;
    unsigned int f = samples_fade;
    UINT64 fade_vol;
    double fade;

    if(samples_rem - sample_count > samples_fade) return;
    if(samples_rem > samples_fade) {
        i = samples_rem - samples_fade;
        f += i;
    } else {
        f = samples_rem;
    }
    while(i<sample_count) {
        fade = (double)(f-i) / (double)samples_fade;
        fade *= fade;
        fade_vol = (UINT64)((f - i)) * (1 << 16);
        fade_vol /= samples_fade;
        fade_vol *= fade_vol;
        fade_vol >>= 16;

        data[i].L = (INT32)(((UINT64)data[i].L * fade_vol) >> 16);
        data[i].R = (INT32)(((UINT64)data[i].R * fade_vol) >> 16);
        i++;
    }
    return;
}


#endif
