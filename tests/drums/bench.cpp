// Per-instrument cost: each instrument alone, kept alive forever (decay frozen). x86, relative only.
#include <Arduino.h>
#include <chrono>
#define private public
#include "drum_manager.h"
#undef private
volatile uint8_t g_cpu_load_pct = 0;
volatile int g_sink = 0;
int main() {
    Wavetable::init(); static DrumManager m; m.init(); int16_t l, r; const long N = 2000000;
    auto t = [&](auto fn) { auto t0 = std::chrono::steady_clock::now(); for (long i = 0; i < N; i++) fn(i); return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count() / N; };
    double idle = t([&](long) { m.tick(l, r); g_sink += l + r; });
    printf("idle (nothing live): %.1f ns/sample\n", idle);
    {   // worst cases
        static DrumManager a; a.init(); for (int i = 0; i < 16; i++) { a._v[i].p.c1 = a._v[i].p.c2 = a._v[i].p.c3 = 0.999999f; a._v[i].p.cp = 0.999999f; }
        auto t0 = std::chrono::steady_clock::now(); for (long n = 0; n < N; n++) { if (n % 20000 == 0) for (int k = 0; k < 16; k++) a.hit(k, 127); a.tick(l, r); g_sink += l + r; }
        printf("ALL 16 voices alive at once (decays frozen): %.1f ns/sample (alive=%d)\n", std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count() / N, a.liveCount());
        static DrumManager s; s.init(); s.play(); t0 = std::chrono::steady_clock::now(); long peak = 0; for (long n = 0; n < N; n++) { s.tick(l, r); g_sink += l + r; peak = std::max<long>(peak, s.liveCount()); }
        printf("demo pattern playing at 120 BPM: %.1f ns/sample average, up to %ld voices live\n", std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count() / N, peak);
        static DrumManager f; f.init(); f.setBpm(300); for (int p = 0; p < 16; p++) for (int i = 0; i < 16; i++) for (int k = 0; k < 16; k++) f.pat[p][i][k] = 2; f.play();
        t0 = std::chrono::steady_clock::now(); peak = 0; for (long n = 0; n < N; n++) { f.tick(l, r); g_sink += l + r; peak = std::max<long>(peak, f.liveCount()); }
        printf("STRESS: every track on every step at 300 BPM: %.1f ns/sample average, up to %ld voices live\n", std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count() / N, peak);
    }
    for (int i = 0; i < 16; i++) {
        static DrumManager k; k.init(); k._v[i].p.c1 = k._v[i].p.c2 = k._v[i].p.c3 = 0.999999f; k._v[i].p.cp = 0.999999f;
        double ns = [&] { auto t0 = std::chrono::steady_clock::now(); for (long n = 0; n < N; n++) { if (n % 20000 == 0) k.hit(i, 127); k.tick(l, r); g_sink += l + r; } return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count() / N; }();
        printf("%s  type %d : %6.1f ns/sample  (+%.1f over idle)  alive=%d\n", INST_LABEL[i], INST_TYPE[i], ns, ns - idle, k.liveCount());
    }
}
