/* demo application to render frames to a WAVE file */
/* meant to show a simple app that just needs to render audio */
/* need to link with:
 * vgm-player
 * vgm-emu
 * vmg-util
 * iconv (depending on system)
 * z
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "player/playerbase.hpp"
#include "player/vgmplayer.hpp"
#include "player/s98player.hpp"
#include "player/droplayer.hpp"
#include "utils/DataLoader.h"
#include "utils/FileLoader.h"
#include "emu/SoundDevs.h"
#include "emu/EmuCores.h"
#include "emu/SoundEmu.h"

#define str_equals(s1,s2) (strcmp(s1,s2) == 0)
#define str_istarts(s1,s2) (strncasecmp(s1,s2,strlen(s2)) == 0)

#define BUFFER_LEN 2048

/* fade length, in seconds */
static unsigned int
fade_len = 8;

static unsigned int
sample_rate = 44100;

static unsigned int
bit_depth = 16;

static unsigned int
loops = 2;

/* vgm-specific functions */
static void
FCC2STR(char *str, UINT32 fcc);

static UINT32
STR2FCC(const char *str);

static void
set_core(PlayerBase *player, UINT8 devId, UINT32 coreId);

static void
dump_info(PlayerBase *player);

/* generic utility/wave functions */
static void
pack_int16le(UINT8 *d, INT16 n);

static void
pack_uint16le(UINT8 *d, UINT16 n);

static void
pack_int24le(UINT8 *d, INT32 n);

static void
pack_uint32le(UINT8 *d, UINT32 n);

static int
write_wav_header(FILE *f, unsigned int totalFrames);

static void
pack_frames(UINT8 *d, unsigned int frame_count, WAVE_32BS *data);

static int
write_frames(FILE *f, unsigned int frame_count, UINT8 *d);

static void
fade_frames(unsigned int frames_rem, unsigned int frames_fade, unsigned int frame_count, WAVE_32BS *data);

static unsigned int
scan_uint(const char *str);

static const char *
fmt_time(double ts);

static const char *
extensible_guid_trailer= "\x00\x00\x00\x00\x10\x00\x80\x00\x00\xAA\x00\x38\x9B\x71";

int main(int argc, const char *argv[]) {
    PlayerBase *player;

    unsigned int i;
    unsigned int totalFrames;
    unsigned int fadeFrames;
    unsigned int curFrames;
    const char *const *tags;
    const char *f_input;
    const char *f_output;
    const char *self;
    const char *c;
    const char *s;
    FILE *f;
    DATA_LOADER *loader;
    WAVE_32BS *buffer;
    UINT8 *packed;
    double complete;
    double inc;

    fadeFrames = 0;
    complete = 0.0;
    inc = 0.0;

    self = *argv++;
    argc--;

    while(argc > 0) {
        if(str_equals(*argv,"--")) {
            argv++;
            argc--;
            break;
        }
        else if(str_istarts(*argv,"--loops")) {
            c = strchr(*argv,'=');
            if(c != NULL) {
                s = &c[1];
            } else {
                argv++;
                argc--;
                s = *argv;
            }
            loops = scan_uint(s);
            argv++;
            argc--;
        }
        else if(str_istarts(*argv,"--samplerate")) {
            c = strchr(*argv,'=');
            if(c != NULL) {
                s = &c[1];
            } else {
                argv++;
                argc--;
                s = *argv;
            }
            sample_rate = scan_uint(s);
            argv++;
            argc--;
        }
        else if(str_istarts(*argv,"--bps")) {
            c = strchr(*argv,'=');
            if(c != NULL) {
                s = &c[1];
            } else {
                argv++;
                argc--;
                s = *argv;
            }
            bit_depth = scan_uint(s);
            argv++;
            argc--;
        }
        else if(str_istarts(*argv,"--fade")) {
            c = strchr(*argv,'=');
            if(c != NULL) {
                s = &c[1];
            } else {
                argv++;
                argc--;
                s = *argv;
            }
            fade_len = scan_uint(s);
            argv++;
            argc--;
        }
        else {
            break;
        }
    }

    if(loops == 0) {
        loops = 2;
    }

    if(sample_rate == 0) {
        sample_rate = 44100;
    }

    switch(bit_depth) {
        case 16: break;
        case 24: break;
        default: bit_depth = 16;
    }

    if(argc < 2) {
        fprintf(stderr,"Usage: %s [options] /path/to/vgm-file /path/to/out.wav\n",self);
        fprintf(stderr,"Available options:\n");
        fprintf(stderr,"    --samplerate\n");
        fprintf(stderr,"    --bps\n");
        fprintf(stderr,"    --fade\n");
        fprintf(stderr,"    --loops\n");
        return 1;
    }

    /* if we were writing a library that uses libvgm, we'd want
     * to have way better clean-up of resources when we see an error
     * (free all our allocated memory, close files, etc).
     * Since this is just a CLI app, we can just quit and let
     * the OS handle everything. */

    /* libvgm renders sames to a WAVE_32BS object - a struct
     * representing left and right samples (in that order) of
     * a single PCM frame */
    buffer = (WAVE_32BS *)malloc(sizeof(WAVE_32BS) * BUFFER_LEN);
    if(buffer == NULL) {
        fprintf(stderr,"out of memory\n");
        return 1;
    }

    /* we'll want to make sure to pack our audio samples
     * into little-endian, interleaved format.
     * If we only supported 16-bit samples this could be
     * malloc(sizeof(INT16) * 2 * BUFFER_LEN) - but in
     * this case we're using INT32 to ensure we can pack
     * 16 and 24-bit frames */
    packed = (UINT8 *)malloc(sizeof(INT32) * 2 * BUFFER_LEN);
    if(packed == NULL) {
        fprintf(stderr,"out of memory\n");
        return 1;
    }

    f = fopen(argv[1],"wb");
    if(f == NULL) {
        fprintf(stderr,"unable to open output file\n");
        return 1;
    }

    /* past all the boilerplate now!
     * create a FileLoader object - able to read gzip'd
     * files on-the-fly */

    loader = FileLoader_Init(argv[0]);
    if(loader == NULL) {
        fprintf(stderr,"failed to create FileLoader\n");
        return 1;
    }

    /* attempt to load 256 bytes, bail if not possible */
    DataLoader_SetPreloadBytes(loader,0x100);
    if(DataLoader_Load(loader)) {
        DataLoader_CancelLoading(loader);
        fprintf(stderr,"failed to load DataLoader\n");
        DataLoader_Deinit(loader);
        return 1;
    }

    /* figure out a player */
    if(VGMPlayer::PlayerCanLoadFile(loader) == 0) {
        player = new VGMPlayer();
    }
    else if(S98Player::PlayerCanLoadFile(loader) == 0) {
        player = new S98Player();
    }
    else if(DROPlayer::PlayerCanLoadFile(loader) == 0) {
        player = new DROPlayer();
    }
    else {
        fprintf(stderr,"Unsupported file\n");
        return 1;
    }

    /* associate the fileloader to the player -
     * automatically reads the rest of the file */
    if(player->LoadFile(loader)) {
        fprintf(stderr,"failed to load file\n");
        return 1;
    }

    /* example for setting cores */
    /* TODO provide interface for user to specify cores
     * for devices, like:
     *   --core=ym2612=nuke ?
     *   --ym2612=nuke ?
     *   ???
     */
    /* commented-out since NUKE uses a lot of CPU */
    // set_core(player,DEVID_YM2612,FCC_NUKE);

    /* let's get some tags! just printing for now.
     * if we wanted to get *really* fancy we could add
     * an "id3 " chunk or "LIST" "INFO" chunk to the
     * wave file. */
    tags = player->GetTags();
    while(*tags) {
        fprintf(stderr,"%s: %s\n",tags[0],tags[1]);
        tags += 2;
    }

    /* set our desired sample rate */
    player->SetSampleRate(sample_rate);

    /* need to call Start before calls like Tick2Sample or
     * checking any kind of timing info, because
     * Start updates the sample rate multiplier/divisors */
    player->Start();

    dump_info(player);

    /* libvgm uses the term "Sample" but its' really a PCM frame! */
    /* In a mono configuration, 1 frame = 1 sample, in a stereo
     * configuration, 1 frame = (left sample + right sample) */

    /* figure out how many total frames we're going to render */
    totalFrames = player->Tick2Sample(player->GetTotalPlayTicks(loops));

    /* we only want to fade if there's a looping section. Assumption is
     * if the VGM doesn't specify a loop, it's a song with an actual ending */
    if(player->GetLoopTicks()) {
        fadeFrames = sample_rate * fade_len;
        totalFrames += fadeFrames;
    }

    /* Let's tell the user what we're doing */
    fprintf(stderr,"Rendering %s to %s\n",argv[0],argv[1]);
    fprintf(stderr,"Samplerate: %u\n",sample_rate);
    fprintf(stderr,"BPS: %u\n",bit_depth);
    fprintf(stderr,"Channels: 2\n");
    fprintf(stderr,"Length: %s\n",fmt_time(player->Sample2Second(totalFrames)));

    write_wav_header(f,totalFrames);

    /* figure out an incrementor for showing a progress bar */
    inc = (double)BUFFER_LEN / totalFrames;

    /* we'll just print a '-' character each time we've hit the
     * next 10% of the file */
    fprintf(stderr,"[");
    fflush(stderr);

    while(totalFrames) {

        memset(buffer,0,sizeof(WAVE_32BS) * BUFFER_LEN);
        memset(packed,0,sizeof(INT32)     * BUFFER_LEN * 2);

        /* default to BUFFER_LEN PCM frames unless we have under BUFFER_LEN remaining */
        curFrames = (BUFFER_LEN > totalFrames ? totalFrames : BUFFER_LEN);

        player->Render(curFrames,buffer);

        /* apply a fade if we've entered the fade section, nothing otherwise */
        fade_frames(totalFrames, fadeFrames, curFrames, buffer);

        /* back our WAVE_32BS frames into little-endian bytes */
        /* if this were a plugin in a music player, we likely wouldn't
         * want to pack into bytes like this - presumably, the host
         * application would handle converting machine-native PCM frames
         * into whatever's needed. We could have to "pack" into machine-native
         * samples, like INT16, or maybe de-interleave into separate buffers
         * for the left and right channels. */
        pack_frames(packed, curFrames, buffer);

        /* write out to disk */
        write_frames(f, curFrames, packed);

        totalFrames -= curFrames;

        /* if we've done the next 10% of rendering, update the progress bar */
        complete += inc;
        if(complete >= 0.10) {
            complete -= 0.10;
            fprintf(stderr,"-");
            fflush(stderr);
        }
    }
    fprintf(stderr,"]\n");

    free(buffer);
    free(packed);
    DataLoader_Deinit(loader);
    fclose(f);

    delete player;
    return 0;
}

static void set_core(PlayerBase *player, UINT8 devId, UINT32 coreId) {
    PLR_DEV_OPTS devOpts;
    UINT32 id;

    /* just going to set the first instance */
    id = PLR_DEV_ID(devId,0);
    if(player->GetDeviceOptions(id,devOpts)) return;
    devOpts.emuCore = coreId;
    player->SetDeviceOptions(id,devOpts);
    return;
}

static void dump_info(PlayerBase *player) {
    std::vector<PLR_DEV_INFO> devInfList;
    PLR_SONG_INFO songInfo;
    const DEV_DEF **devDefList;
    UINT32 i;
    UINT8 ret;
    char str[5];

    fprintf(stderr,"PlayerName: %s\n",player->GetPlayerName());
    player->GetSongInfo(songInfo);
    player->GetSongDeviceInfo(devInfList);

    FCC2STR(str,songInfo.format);
    fprintf(stderr,"SongInfo: %s v%X.%X, Rate %u/%u, Len %u, Loop at %d, devices: %u\n",
      str,
      songInfo.fileVerMaj,
      songInfo.fileVerMin,
      songInfo.tickRateMul,
      songInfo.tickRateDiv,
      songInfo.songLen,
      songInfo.loopTick,
      songInfo.deviceCnt);

    for(i=0;i<devInfList.size();i++) {
        FCC2STR(str,devInfList[i].core);
        fprintf(stderr,"  Dev %d: Type 0x%02X #%d, Core %s, Clock %u, Rate %u, Volume 0x%X\n",
          devInfList[i].id,
          devInfList[i].type,
          (INT8)devInfList[i].instance,
          str,
          devInfList[i].clock,
          devInfList[i].smplRate,
          devInfList[i].volume);
        devDefList = SndEmu_GetDevDefList(devInfList[i].type);
        fprintf(stderr,"    Cores:");
        while(*devDefList) {
            FCC2STR(str,(*devDefList)->coreID);
            fprintf(stderr," %s",str);
            devDefList++;
        }
        fprintf(stderr,"\n");

    }
    fprintf(stderr,"\n");
}

static const char *
fmt_time(double sec) {
    static char ts[256];
    unsigned int i_sec;
    unsigned int i_min;
    unsigned int i_hour;

    ts[0] = '\0';

    i_sec = (unsigned int)sec;
    i_min = i_sec / 60;
    i_sec = i_sec % 60;
    i_hour = i_min / 60;
    i_min = i_min % 60;

    if(i_hour > 0) {
        snprintf(ts,4,"%02u:",i_hour);
    }
    if(i_min > 0) {
        snprintf(&ts[strlen(ts)],4,"%02u:",i_min);
    }
    snprintf(&ts[strlen(ts)],7,"%02u.%03u",i_sec, (unsigned int)((sec - (unsigned int)sec) * 1000));

    return (const char *)ts;
}

static void FCC2STR(char *str, UINT32 fcc) {
    str[4] = '\0';
    str[0] = (char)((fcc >> 24) & 0xFF);
    str[1] = (char)((fcc >> 16) & 0xFF);
    str[2] = (char)((fcc >>  8) & 0xFF);
    str[3] = (char)((fcc >>  0) & 0xFF);
}

static UINT32 STR2FCC(const char *str) {
    UINT32 fcc = 0;
    fcc += (UINT8)str[0] << 24;
    fcc += (UINT8)str[1] << 16;
    fcc += (UINT8)str[2] << 8;
    fcc += (UINT8)str[3];
    return fcc;
}

static void pack_int16le(UINT8 *d, INT16 n) {
    d[0] = (UINT8)((UINT16) n      );
    d[1] = (UINT8)((UINT16) n >> 8 );
}

static void pack_uint16le(UINT8 *d, UINT16 n) {
    d[0] = (UINT8)((UINT16) n      );
    d[1] = (UINT8)((UINT16) n >> 8 );
}

static void pack_int24le(UINT8 *d, INT32 n) {
    d[0] = (UINT8)((UINT32)n       );
    d[1] = (UINT8)((UINT32)n >> 8  );
    d[2] = (UINT8)((UINT32)n >> 16 );
}

static void pack_uint32le(UINT8 *d, UINT32 n) {
    d[0] = (UINT8)(n      );
    d[1] = (UINT8)(n >> 8 );
    d[2] = (UINT8)(n >> 16);
    d[3] = (UINT8)(n >> 24);
}

static int write_wav_header(FILE *f, unsigned int totalFrames) {
    unsigned int dataSize = totalFrames * (bit_depth / 8) * 2;
    UINT8 tmp[4];
    if(fwrite("RIFF",1,4,f) != 4) return 0;
    pack_uint32le(tmp, 4 + ( 8 + dataSize ) + (8 + 40) );
    if(fwrite(tmp,1,4,f) != 4) return 0;

    if(fwrite("WAVE",1,4,f) != 4) return 0;
    if(fwrite("fmt ",1,4,f) != 4) return 0;

    /* fmtSize
     * 16 = standard wave
     * 40 = extensible
     */
    pack_uint32le(tmp,40);
    if(fwrite(tmp,1,4,f) != 4) return 0;

    /* audioFormat:
     * 1 = PCM
     * 3 = float
     * 6 = alaw
     * 7 = ulaw
     * 0xfffe = extensible */
    pack_uint16le(tmp,0xFFFE);
    if(fwrite(tmp,1,2,f) != 2) return 0;

    /* numChannels */
    pack_uint16le(tmp,2);
    if(fwrite(tmp,1,2,f) != 2) return 0;

    /* sampleRate */
    pack_uint32le(tmp,sample_rate);
    if(fwrite(tmp,1,4,f) != 4) return 0;

    /* dataRate (bytes per second) */
    pack_uint32le(tmp,sample_rate * 2 * (bit_depth / 8));
    if(fwrite(tmp,1,4,f) != 4) return 0;

    /* block alignment (channels * sample size) */
    pack_uint16le(tmp,2 * (bit_depth / 8));
    if(fwrite(tmp,1,2,f) != 2) return 0;

    /* bits per sample */
    pack_uint16le(tmp,bit_depth);
    if(fwrite(tmp,1,2,f) != 2) return 0;

    /* size of extended header */
    pack_uint16le(tmp,22);
    if(fwrite(tmp,1,2,f) != 2) return 0;

    /* number of "valid" bits per sample? */
    pack_uint16le(tmp,bit_depth);
    if(fwrite(tmp,1,2,f) != 2) return 0;

    /* speaker position mask */
    /* 3 = normal stereo */
    pack_uint32le(tmp,3);
    if(fwrite(tmp,1,4,f) != 4) return 0;

    /* subformatcode - same as above audioFormat */
    pack_uint16le(tmp,1);
    if(fwrite(tmp,1,2,f) != 2) return 0;

    /* rest of the GUID */
    if(fwrite(extensible_guid_trailer,1,14,f) != 14) return 0;

    if(fwrite("data",1,4,f) != 4) return 0;

    pack_uint32le(tmp,dataSize);
    if(fwrite(tmp,1,4,f) != 4) return 0;

    return 1;
}

static void pack_frames(UINT8 *d, unsigned int frame_count, WAVE_32BS *data) {
    unsigned int i = 0;
    while(i<frame_count) {
        if(data[i].L < -8388608) {
            data[i].L = -8388608;
        } else if(data[i].L > 8388607) {
            data[i].L = 8388607;
        }
        if(data[i].R < -8388608) {
            data[i].R = -8388608;
        } else if(data[i].R > 8388607) {
            data[i].R = 8388607;
        }
        if(bit_depth == 16) {
            data[i].L = (INT16)((UINT32)data[i].L >> 8);
            data[i].R = (INT16)((UINT32)data[i].R >> 8);
            pack_int16le(&d[ 0 ], (INT16)data[i].L);
            pack_int16le(&d[ 2 ], (INT16)data[i].R);
        } else {
            pack_int24le(&d[ 0 ], data[i].L);
            pack_int24le(&d[ 3 ], data[i].R);
        }
        i++;
        d += ((bit_depth / 8) * 2);
    }
}

static int write_frames(FILE *f, unsigned int frame_count, UINT8 *d) {
    return fwrite(d,(bit_depth / 8) * 2,frame_count,f) == frame_count;
}


/* apply a fade to a buffer of frames */
/* frames_rem - remaining total frames, includes fade frames */
/* frames_fade - total number of fade sampes */
/* frame_count - number of frames being rendered right now */
static void fade_frames(unsigned int frames_rem, unsigned int frames_fade, unsigned int frame_count, WAVE_32BS *data) {
    unsigned int i = 0;
    unsigned int f = frames_fade;
    UINT64 fade_vol;
    double fade;

    if(frames_rem - frame_count > frames_fade) return;
    if(frames_rem > frames_fade) {
        i = frames_rem - frames_fade;
        f += i;
    } else {
        f = frames_rem;
    }
    while(i<frame_count) {
        fade = (double)(f-i) / (double)frames_fade;
        fade *= fade;
        fade_vol = (UINT64)((f - i)) * (1 << 16);
        fade_vol /= frames_fade;
        fade_vol *= fade_vol;
        fade_vol >>= 16;

        data[i].L = (INT32)(((UINT64)data[i].L * fade_vol) >> 16);
        data[i].R = (INT32)(((UINT64)data[i].R * fade_vol) >> 16);
        i++;
    }
    return;
}

static unsigned int scan_uint(const char *str) {
    const char *s = str;
    unsigned int num = 0;
    while(*s) {
        if(*s < 48 || *s > 57) break;
        num *= 10;
        num += (*s - 48);
        s++;
    }

    return num;
}

