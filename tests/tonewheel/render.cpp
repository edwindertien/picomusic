// Offline demo: the REAL engine rendered to a WAV file (what the MP3 on the project page was made from). Not a test.
#include <Arduino.h>
#include <vector>
#include <cstdio>
#include <algorithm>
#include "tonewheel_manager.h"
volatile uint8_t g_cpu_load_pct = 0;
struct Ev { double t; int kind; int a, b; };       // kind 0 note on, 1 note off, 2 setup hook
static void wav(const char* fn, const std::vector<int16_t>& pcm) {
    FILE* f = fopen(fn, "wb"); const uint32_t n = (uint32_t)pcm.size() * 2, sr = SAMPLE_RATE, br = sr * 4; const uint16_t ch1 = 2, bps = 16, ba = 4, fmt = 1; const uint32_t sz = 16, riff = 36 + n;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&sz, 4, 1, f); fwrite(&fmt, 2, 1, f); fwrite(&ch1, 2, 1, f); fwrite(&sr, 4, 1, f); fwrite(&br, 4, 1, f); fwrite(&ba, 2, 1, f); fwrite(&bps, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&n, 4, 1, f); fwrite(pcm.data(), 2, pcm.size(), f); fclose(f);
}

int16_t sineTable[WAVETABLE_SIZE];
static TonewheelManager m;
int main(int argc, char** argv) {
    wavetable_init(); m.init(); m.masterVolume = 230;
    std::vector<Ev> ev;
    auto note = [&](double t, double d, int key, int vel) { ev.push_back({ t, 0, key, vel }); ev.push_back({ t + d, 1, key, 0 }); };
    auto hook = [&](double t, int id) { ev.push_back({ t - 0.001, 2, id, 0 }); };
    auto chord = [&](double t, double d, int a, int b, int c, int e) { note(t, d, a, 100); note(t, d, b, 96); note(t, d, c, 96); if (e) note(t, d, e, 92); };
    // 1 jazz registration, slow Leslie: ii-V-I-vi
    hook(0.0, 0);
    chord(0.0, 1.9, 50, 60, 65, 69); chord(2.0, 1.9, 43, 59, 65, 69); chord(4.0, 1.9, 48, 59, 64, 67); chord(6.0, 3.8, 45, 60, 64, 67);
    // 2 fast Leslie, full organ, percussion, chorus: a rock chord riff
    hook(10.0, 1);
    for (int i = 0; i < 4; i++) { chord(10.0 + i * 1.0, 0.45, 45, 57, 64, 69); chord(10.5 + i * 1.0, 0.45, 45, 57, 64, 69); }
    // 3 overdriven lead line over a held chord
    hook(14.0, 2);
    { const int mel[8] = { 72, 74, 76, 79, 76, 74, 72, 67 }; for (int i = 0; i < 8; i++) note(14.0 + i * 0.5, 0.45, mel[i], 110); }
    note(14.0, 4.0, 48, 90); note(14.0, 4.0, 55, 90);
    // 4 flute registration, Leslie stopped, a quiet tune
    hook(19.0, 3);
    { const int mel[8] = { 76, 72, 74, 71, 72, 69, 71, 67 }; for (int i = 0; i < 8; i++) note(19.0 + i * 0.6, 0.55, mel[i], 85); }
    chord(19.0, 5.0, 40, 52, 0, 0);
    // 5 final chord, everything on
    hook(25.0, 1); chord(25.0, 3.5, 45, 57, 64, 72);
    std::sort(ev.begin(), ev.end(), [](const Ev& a, const Ev& b) { return a.t < b.t; });
    const long total = (long)(30.0 * SAMPLE_RATE); std::vector<int16_t> pcm; pcm.reserve((size_t)total * 2);
    size_t e = 0; int peak = 0;
    for (long i = 0; i < total; i++) {
        while (e < ev.size() && ev[e].t * SAMPLE_RATE <= (double)i) {
            const Ev& v = ev[e++];
            if (v.kind == 0) m.noteOn((uint8_t)v.a, (uint8_t)v.b); else if (v.kind == 1) m.noteOff((uint8_t)v.a);
            else switch (v.a) {
                case 0: m.drawbars.presetJazz(); m.propagateDrawbars(); m.handleFxCC(MIDI_CC_LESLIE, 62); m.overdrive.drive = 0; m.chorus.depth = 0; m.chorus.mix = 0; break;
                case 1: m.drawbars.presetFullOrgan(); m.propagateDrawbars(); m.handleCC(MIDI_CC_PERC_SWITCH, 0); m.handleFxCC(MIDI_CC_LESLIE, 126); m.overdrive.drive = 30; m.handleFxCC(MIDI_CC_CHORUS_AMOUNT, 40); break;
                case 2: m.drawbars.presetFullOrgan(); m.propagateDrawbars(); m.handleFxCC(MIDI_CC_LESLIE, 126); m.overdrive.drive = 90; break;
                default: m.drawbars.presetFlute(); m.propagateDrawbars(); m.handleFxCC(MIDI_CC_LESLIE, 62); m.overdrive.drive = 0; m.handleFxCC(MIDI_CC_CHORUS_AMOUNT, 0); break;
            }
        }
        int16_t l, r; m.tick(l, r); pcm.push_back(l); pcm.push_back(r); peak = std::max(peak, (int)std::abs(l));
    }
    wav(argc > 1 ? argv[1] : "demo.wav", pcm); printf("rendered %.0f s, peak %d / 32767\n", total / (double)SAMPLE_RATE, peak); return 0;
}
