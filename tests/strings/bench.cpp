#include <Arduino.h>
#include <chrono>
#include "strings_manager.h"
StringsManager m;
int main() {
    Wavetable::init(); m.init();
    auto run = [&](const char* label, int notes, bool all) {
        m.panic(); for (int i = 0; i < 4000; i++) { int16_t l, r; m.tick(l, r); }
        m.loadPatch(0);
        if (all) { m.setParam(P_BASS, 127); m.setParam(P_CELLO, 127); m.setParam(P_VIOLA, 127); m.setParam(P_VIOLIN, 127); m.setParam(P_PIZZ, 127); m.setParam(P_DETUNE, 127); }
        for (int n = 0; n < notes; n++) m.noteOn(36 + n * 2, 100);
        for (int i = 0; i < 44100; i++) { int16_t l, r; m.tick(l, r); }
        const int N = 2000000; volatile int32_t sink = 0; auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < N; i++) { int16_t l, r; m.tick(l, r); sink += l + r; }
        double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count() / N;
        printf("  %-46s %6.1f ns/sample  (%u voices)\n", label, ns, m.activeCount());
    };
    run("idle (no notes, shared chain only)", 0, false);
    run("6 notes, Ensemble patch", 6, false);
    run("16 notes (the default cap), Ensemble patch", MAX_ACTIVE_VOICES, false);
    
    run("16 notes, ALL registrations + unison (worst case)", MAX_ACTIVE_VOICES, true);
    printf("  sizeof(StringVoice)=%zu  sizeof(StringsManager)=%zu  pool bytes=%zu  ensemble buffer=%zu  sine table=%zu\n", sizeof(StringVoice), sizeof(StringsManager), sizeof(StringVoice) * STRINGS_POOL, sizeof(Ensemble), sizeof(int16_t) * WAVETABLE_SIZE);
}
