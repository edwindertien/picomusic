// The firmware's own drum engine (DrumManager, the real sources) as a command-line renderer for the editor.
//
//   drum_render <image.bin> <out.wav> pattern <index 0..15> <bars> [loop]   one pattern; with `loop` the tail is mixed back onto the start, so the file loops seamlessly
//   drum_render <image.bin> <out.wav> song                              play the song chain once from its start
//   drum_render <image.bin> <out.wav> hit <inst 0..15> <velocity>       one drum, 1.6 s (with its own kit settings)
//   drum_render <image.bin> <out.wav> kit <velocity>                    all 16 drums one after the other, 0.9 s apart
//
// The image is the 4247-byte machine image (what `dump` / `image` move and what flash holds). Prints one line:  peak <n> secs <s> clipped <n>
#include <Arduino.h>
#include <vector>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#define private public
#include "drum_manager.h"
#undef private
volatile uint8_t g_cpu_load_pct = 0;
static DrumManager m;
static std::vector<int16_t> pcm;
static void runTo(long samples) { for (long i = 0; i < samples; i++) { int16_t l, r; m.tick(l, r); pcm.push_back(l); pcm.push_back(r); } }
static void run(double secs) { const long n = (long)(secs * SAMPLE_RATE); for (long i = 0; i < n; i++) { int16_t l, r; m.tick(l, r); pcm.push_back(l); pcm.push_back(r); } }
static bool wav(const char* fn) {
    FILE* f = fopen(fn, "wb"); if (!f) return false;
    const uint32_t n = (uint32_t)pcm.size() * 2, sr = SAMPLE_RATE, br = sr * 4; const uint16_t ch = 2, bps = 16, ba = 4, fmt = 1; const uint32_t sz = 16, riff = 36 + n;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&sz, 4, 1, f); fwrite(&fmt, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&sr, 4, 1, f); fwrite(&br, 4, 1, f); fwrite(&ba, 2, 1, f); fwrite(&bps, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&n, 4, 1, f); fwrite(pcm.data(), 2, pcm.size(), f); fclose(f); return true;
}
int main(int argc, char** argv) {
    if (argc < 4) { fprintf(stderr, "usage: drum_render image.bin out.wav pattern|song|hit|kit ...\n"); return 2; }
    Wavetable::init(); m.init();
    static uint8_t img[DrumManager::SAVE_SIZE + 1];
    FILE* f = fopen(argv[1], "rb"); if (!f) { fprintf(stderr, "cannot read %s\n", argv[1]); return 2; }
    const size_t n = fread(img, 1, sizeof img, f); fclose(f);
    if (!m.deserialize(img, n)) { fprintf(stderr, "not a valid drum image (size, header or CRC)\n"); return 3; }
    const std::string mode = argv[3];
    const double step = 60.0 / m.bpm / 4.0;                       // average step length; swing only moves the odd steps inside a pair
    if (mode == "pattern" && argc >= 6) {
        const int p = atoi(argv[4]); const int bars = atoi(argv[5]);
        if (p < 0 || p >= N_PATTERNS || bars < 1 || bars > 32) { fprintf(stderr, "bad pattern or bars\n"); return 2; }
        m.songMode = false; m.setPattern((uint8_t)p); m.play();
        const long L = (long)llround(m.patLen[p] * step * bars * SAMPLE_RATE);
        runTo(L - 500); m.stop(); runTo(500); run(1.2);                              // play the bars, stop the sequencer 500 samples before the end (the last step starts earlier, the next round's first step would start AT the end) and let the last hits ring out
        if (argc >= 7 && !strcmp(argv[6], "loop")) {              // fold the tail onto the start: the ring-out of the last bar sounds under the first bar of the next round
            const long tail = (long)(pcm.size() / 2) - L;
            for (long i = 0; i < tail && i < L; i++) for (int c = 0; c < 2; c++) { int v = (int)pcm[2 * i + c] + (int)pcm[2 * (L + i) + c]; pcm[2 * i + c] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); }
            pcm.resize(2 * (size_t)L);
        }
    } else if (mode == "song") {
        const int len = m.songLength(); if (!len) { fprintf(stderr, "the song chain is empty\n"); return 2; }
        double steps = 0; for (int i = 0; i < len; i++) steps += m.patLen[m.song[i]];
        m.songMode = true; m.play();
        { const long T = (long)llround(steps * step * SAMPLE_RATE); runTo(T - 500); m.stop(); runTo(500); run(1.2); }
    } else if (mode == "hit" && argc >= 6) {
        const int i = atoi(argv[4]); const int v = atoi(argv[5]);
        if (i < 0 || i >= N_INST) { fprintf(stderr, "bad instrument\n"); return 2; }
        m.hit((uint8_t)i, (uint8_t)v); run(1.6);
    } else if (mode == "kit" && argc >= 5) {
        const int v = atoi(argv[4]);
        for (int i = 0; i < N_INST; i++) { m.hit((uint8_t)i, (uint8_t)v); run(0.9); }
    } else { fprintf(stderr, "unknown mode or missing arguments\n"); return 2; }
    int peak = 0; long clipped = 0; for (int16_t s : pcm) { const int a = s < 0 ? -(int)s : s; if (a > peak) peak = a; if (a >= 32767) clipped++; }
    if (!wav(argv[2])) { fprintf(stderr, "cannot write %s\n", argv[2]); return 2; }
    printf("peak %d secs %.2f clipped %ld\n", peak, pcm.size() / 2.0 / SAMPLE_RATE, clipped);
    return 0;
}
