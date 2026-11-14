// CPU cost of the bass synth, x86 -O2, RELATIVE numbers only. The audio output is consumed (the optimiser would delete it otherwise).
#include <Arduino.h>
#include <chrono>
#define private public
#include "bass_manager.h"
#undef private
volatile uint8_t g_cpu_load_pct = 0; volatile int g_sink = 0;
static double run(BassManager& m, long N, int every) { int16_t l, r; auto t0 = std::chrono::steady_clock::now(); for (long i = 0; i < N; i++) { if (every && i % every == 0) { m.noteOff(40); m.noteOn(40, 100); } m.tick(l, r); g_sink += l; } return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count() / N; }
int main() {
    bass_tables_init(); const long N = 4000000; static BassManager a; a.init(); a.keyMode = 0;
    printf("idle (voice silent):                            %6.1f ns/sample\n", run(a, N, 0));
    static BassManager b; b.init(); b.keyMode = 0; b.noteOn(40, 100); printf("Moog Bass, one held note:                       %6.1f ns/sample\n", run(b, N, 0));
    static BassManager c; c.init(); c.loadPatch(2); c.noteOn(40, 100); printf("Acid, one held note (resonance 108):            %6.1f ns/sample\n", run(c, N, 0));
    static BassManager w; w.init(); for (int k : {P_MIX1, P_MIX2, P_MIX3, P_SUB, P_NOISE, P_DRV, P_RES}) w.setParam(k, 127); w.noteOn(40, 100); printf("worst case: all three oscillators + sub + noise: %6.1f ns/sample\n", run(w, N, 0));
    static BassManager s; s.init(); s.play(); printf("demo pattern playing at 120 BPM:                %6.1f ns/sample\n", run(s, N, 0));
}
