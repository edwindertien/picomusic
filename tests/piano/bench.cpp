// CPU cost of the piano, x86 -O2, RELATIVE numbers only. The audio output is consumed (the optimiser would delete it otherwise).
#include <Arduino.h>
#include <chrono>
#define private public
#include "piano_manager.h"
#undef private
volatile uint8_t g_cpu_load_pct = 0; volatile int g_sink = 0;
static double run(PianoManager& m, long N) { int16_t l, r; auto t0 = std::chrono::steady_clock::now(); for (long i = 0; i < N; i++) { m.tick(l, r); g_sink += l; } return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count() / N; }
int main() {
    const long N = 3000000;
    static PianoManager a; a.init(); printf("idle (no voices):                                  %6.1f ns/sample\n", run(a, N));
    static PianoManager b; b.init(); b.noteOn(60, 80); run(b, 100); printf("one note (bass-zone rate 16 kHz, key 36):          ");
    static PianoManager c; c.init(); c.noteOn(36, 80); run(c, 100); printf("%6.1f ns/sample\n", run(c, 700000));
    static PianoManager d; d.init(); d.noteOn(60, 80); run(d, 100); printf("one note (key 60, 24 kHz zone):                    %6.1f ns/sample\n", run(d, 700000));
    static PianoManager e; e.init(); e.noteOn(96, 80); run(e, 100); printf("one note (key 96, 32 kHz zone):                    %6.1f ns/sample\n", run(e, 400000));
    static PianoManager f; f.init(); for (int i = 0; i < 24; i++) f.noteOn(36 + i * 3, 80); run(f, 100); int n = f.activeVoices(); printf("24 voices at once (%d active), velocity filter on: %6.1f ns/sample\n", n, run(f, 400000));
    static PianoManager g; g.init(); g.setParam(P_TONE, 127); g.setParam(P_HARD, 64); for (int i = 0; i < 24; i++) g.noteOn(36 + i * 3, 127); run(g, 100); printf("24 voices at once, filter bypassed (velocity 127): %6.1f ns/sample\n", run(g, 400000));
}
