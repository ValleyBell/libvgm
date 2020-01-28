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
#include "vgm2wav.h"

/* see vgm2wav.h for utility functions - packing/unpacking integers,
 * writing a WAVE header, applying a fade, etc.
 * This file is focused on showing how to use the c-api */

/* I use a wrapper struct that allows me to load any kind of file -
 * VGM, S98, or DRO, and use the same method calls via function
 * pointers. Details in vgm2wav.h
 */

static void set_core(player_funcs *f, UINT8 devId, UINT32 coreId) {
    PLR_DEV_OPTS devOpts;
    UINT32 id;

    /* just going to set the first instance */
    id = PLR_DEV_ID(devId,0);
    if(f->GetDeviceOptions(f->player,id,&devOpts)) return;
    devOpts.emuCore = coreId;
    f->SetDeviceOptions(f->player,id,&devOpts);
    return;
}

static void dump_info(player_funcs *f) {
    PLR_DEV_INFO *devInfo;
    PLR_SONG_INFO songInfo;
    const DEV_DEF **devDefList;
    UINT32 devLen;
    UINT32 i;
    UINT8 ret;
    char str[5];

    devLen = 0;
    fprintf(stderr,"PlayerName: %s\n",f->GetPlayerName(f->player));
    f->GetSongInfo(f->player,&songInfo);
    f->GetSongDeviceInfo(f->player,&devInfo,&devLen);

    FCC2Str(str,songInfo.format);
    fprintf(stderr,"SongInfo: %s v%X.%X, Rate %u/%u, Len %u, Loop at %d, devices: %u\n",
      str,
      songInfo.fileVerMaj,
      songInfo.fileVerMin,
      songInfo.tickRateMul,
      songInfo.tickRateDiv,
      songInfo.songLen,
      songInfo.loopTick,
      songInfo.deviceCnt);
      
    for(i=0;i<devLen;i++) {
        FCC2Str(str,devInfo[i].core);
        fprintf(stderr,"  Dev %d: Type 0x%02X #%d, Core %s, Clock %u, Rate %u, Volume 0x%X\n",
          devInfo[i].id,
          devInfo[i].type,
          (INT8)devInfo[i].instance,
          str,
          devInfo[i].clock,
          devInfo[i].smplRate,
          devInfo[i].volume);
        devDefList = SndEmu_GetDevDefList(devInfo[i].id);
        fprintf(stderr,"    Cores:");
        while(*devDefList) {
            FCC2Str(str,(*devDefList)->coreID);
            fprintf(stderr," %s",str);
            devDefList++;
        }
        fprintf(stderr,"\n");

    }
    free(devInfo);
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
        return 1;
    }
    DataLoader_SetPreloadBytes(loader,0x100);
    if(DataLoader_Load(loader)) {
        DataLoader_CancelLoading(loader);
        fprintf(stderr,"failed to load DataLoader\n");
        DataLoader_Deinit(loader);
        return 1;
    }

    /* initialize the wrapper with VGM/S98/DRO function pointers
     * so for example, if using VGMPlayer, a call to:
     * wrapper.LoadFile(wrapper.player,loader)
     * is equivalent to
     * VGMPlayer_LoadFile(player,loader)
     */

    if(VGMPlayer_IsMyFile(loader) == 0) {
        wrapper.player = VGMPlayer_New();
        if(wrapper.player == NULL) {
            fprintf(stderr,"Player_new returned NULL\n");
            return 1;
        }

        WRAPPER_INIT_FUNCS(VGMPlayer)
    }

    else if(S98Player_IsMyFile(loader) == 0) {
        wrapper.player = S98Player_New();
        if(wrapper.player == NULL) {
            fprintf(stderr,"Player_new returned NULL\n");
            return 1;
        }

        WRAPPER_INIT_FUNCS(S98Player)
    }

    else if(DROPlayer_IsMyFile(loader) == 0) {
        wrapper.player = DROPlayer_New();
        if(wrapper.player == NULL) {
            fprintf(stderr,"Player_new returned NULL\n");
            return 1;
        }
        WRAPPER_INIT_FUNCS(DROPlayer)
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

    /* going to ask for the FCC_NUKE core for YM2612 */
    set_core(&wrapper,DEVID_YM2612,FCC_NUKE);

    tags = wrapper.GetTags(wrapper.player);
    while(*tags) {
        fprintf(stderr,"%s: %s\n",tags[0],tags[1]);
        tags += 2;
    }
    wrapper.SetSampleRate(wrapper.player,44100);


    /* call Start to update the sample rate multiplier/divisors */
    wrapper.Start(wrapper.player);

    dump_info(&wrapper);

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
