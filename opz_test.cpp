// YM2414 (OPZ) Test Program
// Generates some test tones and saves to WAV file

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

extern "C" {
#include "emu/EmuStructs.h"
#include "emu/SoundDevs.h"
#include "emu/cores/ymfmintf.h"
}

// WAV file header structure
#pragma pack(push, 1)
struct WAVHeader {
    char riff[4];           // "RIFF"
    uint32_t fileSize;      // File size - 8
    char wave[4];           // "WAVE"
    char fmt[4];            // "fmt "
    uint32_t fmtSize;       // 16 for PCM
    uint16_t audioFormat;   // 1 for PCM
    uint16_t numChannels;   // 2 for stereo
    uint32_t sampleRate;    // Sample rate
    uint32_t byteRate;      // SampleRate * NumChannels * BitsPerSample/8
    uint16_t blockAlign;    // NumChannels * BitsPerSample/8
    uint16_t bitsPerSample; // 16
    char data[4];           // "data"
    uint32_t dataSize;      // Size of audio data
};
#pragma pack(pop)

void WriteWAVHeader(FILE* f, uint32_t sampleRate, uint32_t numSamples) {
    WAVHeader header;
    memcpy(header.riff, "RIFF", 4);
    memcpy(header.wave, "WAVE", 4);
    memcpy(header.fmt, "fmt ", 4);
    memcpy(header.data, "data", 4);

    header.fmtSize = 16;
    header.audioFormat = 1;  // PCM
    header.numChannels = 2;  // Stereo
    header.sampleRate = sampleRate;
    header.bitsPerSample = 16;
    header.blockAlign = header.numChannels * header.bitsPerSample / 8;
    header.byteRate = sampleRate * header.blockAlign;
    header.dataSize = numSamples * header.blockAlign;
    header.fileSize = 36 + header.dataSize;

    fwrite(&header, sizeof(header), 1, f);
}

void YM2414_Write(const DEV_DEF* devDef, DEV_INFO* devInf, uint8_t reg, uint8_t data) {
    typedef uint8_t (*WriteFn)(void*, uint8_t, uint8_t);
    WriteFn writeFunc = (WriteFn)devDef->rwFuncs[0].funcPtr;

    // Write address
    writeFunc(devInf->dataPtr, 0, reg);
    // Write data
    writeFunc(devInf->dataPtr, 1, data);
}

int main(int argc, char* argv[]) {
    printf("YM2414 (OPZ) Test - Generating test tones...\n");

    // Initialize YM2414
    DEV_GEN_CFG devCfg;
    // Clock calculation: sample_rate = clock / (prescale * operators)
    // For 44100 Hz: clock = 44100 * 16 * 4 = 2,822,400 Hz
    devCfg.clock = 2822400;  // ~2.82 MHz for 44100 Hz output
    devCfg.flags = 0;
    devCfg.emuCore = 0;

    DEV_INFO devInf;
    const DEV_DEF* devDef = sndDev_YM2414.cores[0];  // Get first core
    uint8_t result = devDef->Start(&devCfg, &devInf);
    if (result != 0) {
        printf("Failed to initialize YM2414: error %d\n", result);
        return 1;
    }

    printf("YM2414 initialized at %d Hz\n", devInf.sampleRate);

    // Reset chip
    devDef->Reset(devInf.dataPtr);

    // Program a basic FM voice on channel 0
    // Algorithm 0 (operators in series): Op1->Op2->Op3->Op4->Out

    // Channel 0 setup
    YM2414_Write(devDef, &devInf, 0x20, 0x80);  // CH0: Pan right ON, Key OFF, Feedback=0, Algorithm=0
    YM2414_Write(devDef, &devInf, 0x28, 0x40);  // CH0: Key code (note)
    YM2414_Write(devDef, &devInf, 0x30, 0x01);  // CH0: Key fraction + Mono/Left OUT
    YM2414_Write(devDef, &devInf, 0x38, 0x00);  // CH0: LFO sensitivity

    // Operator 1 (Modulator) - offset 0x00
    YM2414_Write(devDef, &devInf, 0x40, 0x31);  // OP1: DT=3, MUL=1
    YM2414_Write(devDef, &devInf, 0x60, 0x1F);  // OP1: TL=31 (moderate level)
    YM2414_Write(devDef, &devInf, 0x80, 0x9F);  // OP1: KSR=2, AR=31 (fast attack)
    YM2414_Write(devDef, &devInf, 0xA0, 0x85);  // OP1: AM=1, DR=5
    YM2414_Write(devDef, &devInf, 0xC0, 0x05);  // OP1: DT2=0, SR=5
    YM2414_Write(devDef, &devInf, 0xE0, 0xA3);  // OP1: SL=10, RR=3

    // Operator 2 (Modulator) - offset 0x08
    YM2414_Write(devDef, &devInf, 0x48, 0x31);  // OP2: DT=3, MUL=1
    YM2414_Write(devDef, &devInf, 0x68, 0x20);  // OP2: TL=32
    YM2414_Write(devDef, &devInf, 0x88, 0x9F);  // OP2: KSR=2, AR=31
    YM2414_Write(devDef, &devInf, 0xA8, 0x85);  // OP2: AM=1, DR=5
    YM2414_Write(devDef, &devInf, 0xC8, 0x05);  // OP2: DT2=0, SR=5
    YM2414_Write(devDef, &devInf, 0xE8, 0xA3);  // OP2: SL=10, RR=3

    // Operator 3 (Modulator) - offset 0x10
    YM2414_Write(devDef, &devInf, 0x50, 0x31);  // OP3: DT=3, MUL=1
    YM2414_Write(devDef, &devInf, 0x70, 0x20);  // OP3: TL=32
    YM2414_Write(devDef, &devInf, 0x90, 0x9F);  // OP3: KSR=2, AR=31
    YM2414_Write(devDef, &devInf, 0xB0, 0x85);  // OP3: AM=1, DR=5
    YM2414_Write(devDef, &devInf, 0xD0, 0x05);  // OP3: DT2=0, SR=5
    YM2414_Write(devDef, &devInf, 0xF0, 0xA3);  // OP3: SL=10, RR=3

    // Operator 4 (Carrier) - offset 0x18
    YM2414_Write(devDef, &devInf, 0x58, 0x31);  // OP4: DT=3, MUL=1
    YM2414_Write(devDef, &devInf, 0x78, 0x00);  // OP4: TL=0 (full volume)
    YM2414_Write(devDef, &devInf, 0x98, 0x9F);  // OP4: KSR=2, AR=31
    YM2414_Write(devDef, &devInf, 0xB8, 0x85);  // OP4: AM=1, DR=5
    YM2414_Write(devDef, &devInf, 0xD8, 0x05);  // OP4: DT2=0, SR=5
    YM2414_Write(devDef, &devInf, 0xF8, 0xA3);  // OP4: SL=10, RR=3

    // Set volume (channel output level)
    YM2414_Write(devDef, &devInf, 0x00, 0x7F);  // CH0: Volume=127 (max)

    // Prepare output buffer
    const uint32_t duration = 3;  // 3 seconds
    const uint32_t totalSamples = devInf.sampleRate * duration;
    int16_t* wavBuffer = (int16_t*)malloc(totalSamples * 2 * sizeof(int16_t));

    const uint32_t chunkSize = 1024;
    int32_t* tempL = (int32_t*)malloc(chunkSize * sizeof(int32_t));
    int32_t* tempR = (int32_t*)malloc(chunkSize * sizeof(int32_t));
    int32_t* outputs[2] = { tempL, tempR };

    printf("Generating audio...\n");

    // Note sequence: C, E, G, C (major arpeggio)
    uint8_t notes[] = { 0x40, 0x44, 0x47, 0x4C };  // MIDI-like note codes
    uint32_t noteLength = devInf.sampleRate / 2;  // 0.5 seconds per note

    uint32_t samplePos = 0;
    for (int note = 0; note < 4; note++) {
        printf("Playing note %d...\n", note);

        // Set note frequency
        YM2414_Write(devDef, &devInf, 0x28, notes[note]);

        // Key ON (bit 6 = 1 for key on)
        YM2414_Write(devDef, &devInf, 0x20, 0xC0);  // Key ON, Both pans, Feedback=0, Alg=0

        // Generate note
        uint32_t remaining = noteLength;
        while (remaining > 0 && samplePos < totalSamples) {
            uint32_t toRender = (remaining < chunkSize) ? remaining : chunkSize;

            devDef->Update(devInf.dataPtr, toRender, outputs);

            // Convert to 16-bit and interleave
            for (uint32_t i = 0; i < toRender && samplePos < totalSamples; i++) {
                wavBuffer[samplePos * 2 + 0] = (int16_t)tempL[i];  // Left
                wavBuffer[samplePos * 2 + 1] = (int16_t)tempR[i];  // Right
                samplePos++;
            }

            remaining -= toRender;
        }

        // Key OFF (bit 6 = 0 for key off)
        YM2414_Write(devDef, &devInf, 0x20, 0x80);  // Key OFF, Both pans

        // Brief silence between notes
        uint32_t silence = devInf.sampleRate / 10;  // 0.1 seconds
        remaining = silence;
        while (remaining > 0 && samplePos < totalSamples) {
            uint32_t toRender = (remaining < chunkSize) ? remaining : chunkSize;

            devDef->Update(devInf.dataPtr, toRender, outputs);

            for (uint32_t i = 0; i < toRender && samplePos < totalSamples; i++) {
                wavBuffer[samplePos * 2 + 0] = (int16_t)tempL[i];
                wavBuffer[samplePos * 2 + 1] = (int16_t)tempR[i];
                samplePos++;
            }

            remaining -= toRender;
        }
    }

    // Fill remaining with silence
    while (samplePos < totalSamples) {
        uint32_t toRender = ((totalSamples - samplePos) < chunkSize) ? (totalSamples - samplePos) : chunkSize;

        devDef->Update(devInf.dataPtr, toRender, outputs);

        for (uint32_t i = 0; i < toRender; i++) {
            wavBuffer[samplePos * 2 + 0] = (int16_t)(tempL[i] >> 8);
            wavBuffer[samplePos * 2 + 1] = (int16_t)(tempR[i] >> 8);
            samplePos++;
        }
    }

    // Write WAV file
    printf("Writing opz_test.wav...\n");
    FILE* f = fopen("opz_test.wav", "wb");
    if (!f) {
        printf("Failed to create output file\n");
        return 1;
    }

    WriteWAVHeader(f, devInf.sampleRate, totalSamples);
    fwrite(wavBuffer, sizeof(int16_t) * 2, totalSamples, f);
    fclose(f);

    printf("Done! Created opz_test.wav (%d Hz, %d samples, %d seconds)\n",
           devInf.sampleRate, totalSamples, duration);

    // Cleanup
    free(wavBuffer);
    free(tempL);
    free(tempR);
    devDef->Stop(devInf.dataPtr);

    return 0;
}
