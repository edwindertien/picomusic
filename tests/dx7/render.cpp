// Offline demo: the REAL engine rendered to a WAV file (what the MP3 on the project page was made from). Not a test.
#include <Arduino.h>
#include <vector>
#include <cstdio>
#include <algorithm>
#include "dx7_manager.h"
volatile uint8_t g_cpu_load_pct = 0;
struct Ev { double t; int kind; int a, b; };       // kind 0 note on, 1 note off, 2 patch
int main(int argc, char** argv) {
    static Dx7Manager m; m.init();
    m.setParam(P_VOL, 84); m.setParam(P_REV, 38); m.setParam(P_RSIZE, 70); m.setParam(P_CHOR, 40);
    std::vector<Ev> ev;
    auto note = [&](double t, double d, int key, int vel) { ev.push_back({ t, 0, key, vel }); ev.push_back({ t + d, 1, key, 0 }); };
    auto patch = [&](double t, int p) { ev.push_back({ t - 0.001, 2, p, 0 }); };
    // 1  electric piano: a slow progression Am7 - Dm9 - G7 - Cmaj7
    patch(0.0, 0);
    const int ch[4][4] = { { 57, 60, 64, 67 }, { 50, 60, 64, 69 }, { 55, 59, 62, 65 }, { 48, 59, 64, 67 } };
    for (int c = 0; c < 4; c++) for (int k = 0; k < 4; k++) note(c * 2.0 + k * 0.03, 1.9, ch[c][k], 78 + 8 * k);
    // 2  bass walks under the same chords
    patch(8.0, 1); for (int c = 0; c < 4; c++) { const int root[4] = { 33, 38, 31, 36 }; note(8.0 + c * 2.0, 0.45, root[c], 110); note(8.0 + c * 2.0 + 0.5, 0.45, root[c] + 12, 90); note(8.0 + c * 2.0 + 1.0, 0.45, root[c], 105); note(8.0 + c * 2.0 + 1.5, 0.4, root[c] + 7, 90); }
    // 3  bells over the top, then marimba, then brass and strings
    patch(8.0, 4); for (int i = 0; i < 4; i++) note(8.0 + i * 2.0, 1.8, 76 - (i % 2) * 5, 90);
    patch(16.0, 3); { const int mel[8] = { 69, 72, 76, 72, 71, 67, 62, 67 }; for (int i = 0; i < 8; i++) note(16.0 + i * 0.25, 0.22, mel[i], 100); }
    patch(18.0, 2); note(18.0, 1.6, 60, 110); note(18.0, 1.6, 64, 100); note(18.0, 1.6, 67, 100);
    patch(20.0, 9); note(20.0, 3.0, 57, 90); note(20.0, 3.0, 60, 90); note(20.0, 3.0, 64, 90); note(20.0, 3.0, 69, 90);
    patch(24.0, 8); { const int mel[6] = { 72, 74, 76, 79, 76, 72 }; for (int i = 0; i < 6; i++) note(24.0 + i * 0.5, 0.45, mel[i], 105); }
    patch(27.5, 11); note(27.5, 3.0, 64, 95); note(27.5, 3.0, 71, 95);
    std::sort(ev.begin(), ev.end(), [](const Ev& a, const Ev& b) { return a.t < b.t; });
    const long total = (long)(32.0 * SAMPLE_RATE); std::vector<int16_t> pcm; pcm.reserve((size_t)total * 2);
    size_t e = 0; int peak = 0;
    for (long i = 0; i < total; i++) {
        while (e < ev.size() && ev[e].t * SAMPLE_RATE <= (double)i) { const Ev& v = ev[e++]; if (v.kind == 0) m.noteOn((uint8_t)v.a, (uint8_t)v.b); else if (v.kind == 1) m.noteOff((uint8_t)v.a); else m.setPatch((uint8_t)v.a); }
        int16_t l, r; m.tick(l, r); pcm.push_back(l); pcm.push_back(r); peak = std::max(peak, (int)std::abs(l));
    }
    FILE* f = fopen(argc > 1 ? argv[1] : "demo.wav", "wb"); const uint32_t n = (uint32_t)pcm.size() * 2, sr = SAMPLE_RATE, br = sr * 4; const uint16_t ch1 = 2, bps = 16, ba = 4, fmt = 1; const uint32_t sz = 16, riff = 36 + n;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&sz, 4, 1, f); fwrite(&fmt, 2, 1, f); fwrite(&ch1, 2, 1, f); fwrite(&sr, 4, 1, f); fwrite(&br, 4, 1, f); fwrite(&ba, 2, 1, f); fwrite(&bps, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&n, 4, 1, f); fwrite(pcm.data(), 2, pcm.size(), f); fclose(f);
    printf("rendered %.0f s, peak %d / 32767\n", total / (double)SAMPLE_RATE, peak); return 0;
}
