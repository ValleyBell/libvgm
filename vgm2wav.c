/* demo application that uses c-bindings to libvgm-player */
/* meant to show a simple app that just needs to decode audio */
/* creates a 16-bit, stereo, 44.1kHz WAVE file */
/* need to link with:
 * vgm-emu
 * vmg-util
 * iconv
 * z
 */

#include <stdio.h>
#include <stdlib.h>
#include "player/vgmplayer.h"
#include "player/s98player.h"
#include "player/droplayer.h"
#include "utils/DataLoader.h"
#include "utils/FileLoader.h"

#define SAMPLE_RATE 44100
#define BUFFER_LEN 4096
#define FADE_LEN 8

typedef void  (*DeleteFunc)(void *);
typedef UINT8 (*LoadFileFunc)(void *, DATA_LOADER *);
typedef UINT8 (*UnloadFileFunc)(void *);
typedef UINT8 (*StartFunc)(void *);
typedef const char * const * (*GetTagsFunc)(void *);
typedef UINT8 (*SetSampleRateFunc)(void *, UINT32);
typedef UINT8 (*RenderFunc)(void *, UINT32, WAVE_32BS *);
typedef UINT32 (*GetTotalPlayTicksFunc)(void *,UINT32);
typedef UINT32 (*Tick2SampleFunc)(void *, UINT32);
typedef UINT32 (*GetLoopTicksFunc)(void *);

struct player_funcs_s {
    void *player;
    DeleteFunc Delete;
    LoadFileFunc LoadFile;
    UnloadFileFunc UnloadFile;
    StartFunc Start;
    GetTagsFunc GetTags;
    SetSampleRateFunc SetSampleRate;
    RenderFunc Render;
    GetTotalPlayTicksFunc GetTotalPlayTicks;
    Tick2SampleFunc Tick2Sample;
    GetLoopTicksFunc GetLoopTicks;
};

typedef struct player_funcs_s player_funcs;

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

int main(int argc, const char *argv[]) {
    player_funcs wrapper;

    unsigned int i;
    unsigned int totalSamples;
    unsigned int fadeSamples;
    unsigned int curSamples;
    const char *const *tags;
    FILE *f;
    DATA_LOADER *loader;
    WAVE_32BS *buffer;
    UINT8 *packed;

    if(argc < 3) {
        fprintf(stderr,"Usage: %s /path/to/file /path/to/out.wav\n",argv[0]);
        return 1;
    }

    fadeSamples = 0;
    buffer = (WAVE_32BS *)malloc(sizeof(WAVE_32BS) * BUFFER_LEN);
    if(buffer == NULL) {
        fprintf(stderr,"out of memory\n");
        return 1;
    }
    packed = (UINT8 *)malloc(sizeof(INT32) * 2 * BUFFER_LEN);
    if(packed == NULL) {
        fprintf(stderr,"out of memory\n");
        return 1;
    }

    f = fopen(argv[2],"wb");
    if(f == NULL) {
        fprintf(stderr,"unable to open output file\n");
        return 1;
    }

    loader = FileLoader_Init(argv[1]);
    if(loader == NULL) {
        fprintf(stderr,"failed to create FileLoader\n");
    }
    DataLoader_SetPreloadBytes(loader,0x100);
    if(DataLoader_Load(loader)) {
        DataLoader_CancelLoading(loader);
        fprintf(stderr,"failed to load DataLoader\n");
        DataLoader_Deinit(loader);
        return 1;
    }

    if(VGMPlayer_IsMyFile(loader) == 0) {
        wrapper.player = VGMPlayer_New();
        if(wrapper.player == NULL) {
            fprintf(stderr,"Player_new returned NULL\n");
            return 1;
        }

        wrapper.Delete            = (DeleteFunc) &VGMPlayer_Delete;
        wrapper.LoadFile          = (LoadFileFunc) &VGMPlayer_LoadFile;
        wrapper.UnloadFile        = (UnloadFileFunc) &VGMPlayer_UnloadFile;
        wrapper.Start             = (StartFunc) &VGMPlayer_Start;
        wrapper.GetTags           = (GetTagsFunc) &VGMPlayer_GetTags;
        wrapper.SetSampleRate     = (SetSampleRateFunc) &VGMPlayer_SetSampleRate;
        wrapper.Render            = (RenderFunc) &VGMPlayer_Render;
        wrapper.GetTotalPlayTicks = (GetTotalPlayTicksFunc) &VGMPlayer_GetTotalPlayTicks;
        wrapper.Tick2Sample       = (Tick2SampleFunc) &VGMPlayer_Tick2Sample;
        wrapper.GetLoopTicks      = (GetLoopTicksFunc) &VGMPlayer_GetLoopTicks;
    }

    else if(S98Player_IsMyFile(loader) == 0) {
        wrapper.player = S98Player_New();
        if(wrapper.player == NULL) {
            fprintf(stderr,"Player_new returned NULL\n");
            return 1;
        }

        wrapper.Delete            = (DeleteFunc) &S98Player_Delete;
        wrapper.LoadFile          = (LoadFileFunc) &S98Player_LoadFile;
        wrapper.UnloadFile        = (UnloadFileFunc) &S98Player_UnloadFile;
        wrapper.Start             = (StartFunc) &S98Player_Start;
        wrapper.GetTags           = (GetTagsFunc) &S98Player_GetTags;
        wrapper.SetSampleRate     = (SetSampleRateFunc) &S98Player_SetSampleRate;
        wrapper.Render            = (RenderFunc) &S98Player_Render;
        wrapper.GetTotalPlayTicks = (GetTotalPlayTicksFunc) &S98Player_GetTotalPlayTicks;
        wrapper.Tick2Sample       = (Tick2SampleFunc) &S98Player_Tick2Sample;
        wrapper.GetLoopTicks      = (GetLoopTicksFunc) &S98Player_GetLoopTicks;
    }

    else if(DROPlayer_IsMyFile(loader) == 0) {
        wrapper.player = DROPlayer_New();
        if(wrapper.player == NULL) {
            fprintf(stderr,"Player_new returned NULL\n");
            return 1;
        }

        wrapper.Delete            = (DeleteFunc) &DROPlayer_Delete;
        wrapper.LoadFile          = (LoadFileFunc) &DROPlayer_LoadFile;
        wrapper.UnloadFile        = (UnloadFileFunc) &DROPlayer_UnloadFile;
        wrapper.Start             = (StartFunc) &DROPlayer_Start;
        wrapper.GetTags           = (GetTagsFunc) &DROPlayer_GetTags;
        wrapper.SetSampleRate     = (SetSampleRateFunc) &DROPlayer_SetSampleRate;
        wrapper.Render            = (RenderFunc) &DROPlayer_Render;
        wrapper.GetTotalPlayTicks = (GetTotalPlayTicksFunc) &DROPlayer_GetTotalPlayTicks;
        wrapper.Tick2Sample       = (Tick2SampleFunc) &DROPlayer_Tick2Sample;
        wrapper.GetLoopTicks      = (GetLoopTicksFunc) &DROPlayer_GetLoopTicks;
    }

    else {
        fprintf(stderr,"no module available to load\n");
        return 1;
    }


    if(wrapper.LoadFile(wrapper.player,loader)) {
        fprintf(stderr,"failed to load file\n");
        wrapper.Delete(wrapper.player);
        return 1;
    }

    tags = wrapper.GetTags(wrapper.player);
    while(*tags) {
        fprintf(stderr,"%s: %s\n",tags[0],tags[1]);
        tags += 2;
    }

    wrapper.SetSampleRate(wrapper.player,44100);

    /* call Start to update the sample rate multiplier/divisors */
    wrapper.Start(wrapper.player);

    totalSamples = wrapper.Tick2Sample(wrapper.player,wrapper.GetTotalPlayTicks(wrapper.player,2));

    /* only apply fade if there's a looping section */
    if(wrapper.GetLoopTicks(wrapper.player)) {
        fadeSamples = SAMPLE_RATE * FADE_LEN;
        totalSamples += fadeSamples;
    }

    write_header(f,totalSamples);

    while(totalSamples) {
        curSamples = (BUFFER_LEN > totalSamples ? totalSamples : BUFFER_LEN);
        wrapper.Render(wrapper.player,curSamples,buffer);
        fade_samples(totalSamples, fadeSamples, curSamples, buffer);
        pack_samples(packed, curSamples, buffer);
        write_samples(f, curSamples, packed);
        totalSamples -= curSamples;
    }

    wrapper.UnloadFile(wrapper.player);
    wrapper.Delete(wrapper.player);
    DataLoader_Deinit(loader);
    free(buffer);
    free(packed);
    return 0;
}
