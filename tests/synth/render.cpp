// Offline demo: the REAL engine rendered to a WAV file (what the MP3 on the project page was made from). Not a test.
#include <Arduino.h>
#include <vector>
#include <cstdio>
#include <algorithm>
#include "config.h"
#include "wavetable.h"
#include "synth_manager.h"
volatile uint8_t g_cpu_load_pct = 0;
struct Ev { double t; int kind; int a, b; };       // kind 0 note on, 1 note off, 2 setup hook
static void wav(const char* fn, const std::vector<int16_t>& pcm) {
    FILE* f = fopen(fn, "wb"); const uint32_t n = (uint32_t)pcm.size() * 2, sr = SAMPLE_RATE, br = sr * 4; const uint16_t ch1 = 2, bps = 16, ba = 4, fmt = 1; const uint32_t sz = 16, riff = 36 + n;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&sz, 4, 1, f); fwrite(&fmt, 2, 1, f); fwrite(&ch1, 2, 1, f); fwrite(&sr, 4, 1, f); fwrite(&br, 4, 1, f); fwrite(&ba, 2, 1, f); fwrite(&bps, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&n, 4, 1, f); fwrite(pcm.data(), 2, pcm.size(), f); fclose(f);
}

static SynthManager m;
int main(int argc, char** argv) {
    Wavetable::init(); m.init(); m.masterVolume = 200;
    std::vector<Ev> ev;
    auto note = [&](double t, double d, int key, int vel) { ev.push_back({ t, 0, key, vel }); ev.push_back({ t + d, 1, key, 0 }); };
    auto hook = [&](double t, int id) { ev.push_back({ t - 0.001, 2, id, 0 }); };
    // 1 Init patch: a bright arpeggio with the cutoff opened over time
    hook(0.0, 0);
    { const int arp[8] = { 45, 52, 57, 60, 64, 60, 57, 52 }; for (int i = 0; i < 32; i++) { note(0.0 + i * 0.22, 0.2, arp[i % 8] + (i >= 16 ? 5 : 0), 100); } }
    // 2 Pad: slow chords
    hook(8.0, 1);
    { const int ch[3][3] = { { 57, 60, 64 }, { 53, 57, 60 }, { 55, 59, 62 } }; for (int c = 0; c < 3; c++) for (int k = 0; k < 3; k++) note(8.0 + c * 3.0, 2.9, ch[c][k], 90); }
    // 3 Bass: a short groove
    hook(18.0, 2);
    { const int b[8] = { 33, 33, 45, 33, 36, 36, 48, 31 }; for (int i = 0; i < 24; i++) note(18.0 + i * 0.25, 0.22, b[i % 8], i % 4 == 0 ? 120 : 95); }
    // 4 Init again, resonant, glide and a lead melody
    hook(24.0, 3);
    { const int mel[8] = { 69, 72, 76, 74, 72, 69, 67, 69 }; for (int i = 0; i < 8; i++) note(24.0 + i * 0.5, 0.45, mel[i], 105); }
    std::sort(ev.begin(), ev.end(), [](const Ev& a, const Ev& b) { return a.t < b.t; });
    const long total = (long)(29.0 * SAMPLE_RATE); std::vector<int16_t> pcm; pcm.reserve((size_t)total * 2);
    size_t e = 0; int peak = 0; long ctl = 0;
    for (long i = 0; i < total; i++) {
        double t = i / (double)SAMPLE_RATE;
        if (t < 8.0 && i % 2205 == 0) m.setParam(P_CUTOFF, (uint8_t)(40 + t * 10));       // slow filter sweep in the first part
        while (e < ev.size() && ev[e].t * SAMPLE_RATE <= (double)i) {
            const Ev& v = ev[e++];
            if (v.kind == 0) m.noteOn((uint8_t)v.a, (uint8_t)v.b); else if (v.kind == 1) m.noteOff((uint8_t)v.a);
            else if (v.a == 3) { m.loadPatch(0); m.setParam(P_RES, 90); m.setParam(P_CUTOFF, 80); m.setParam(P_GLIDE, 40); m.setGlide(true); }
            else { m.loadPatch((uint8_t)v.a); m.setGlide(false); }
        }
        int16_t l = m.tick(), r = l; pcm.push_back(l); pcm.push_back(r); peak = std::max(peak, (int)std::abs(l)); (void)ctl;
    }
    wav(argc > 1 ? argv[1] : "demo.wav", pcm); printf("rendered %.0f s, peak %d / 32767\n", total / (double)SAMPLE_RATE, peak); return 0;
}
