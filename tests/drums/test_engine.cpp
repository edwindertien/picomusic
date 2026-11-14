// Engine tests on the REAL DrumManager: parameters, ring, storage, torture, two-thread race. Built with ASan+UBSan (+float-cast-overflow).
#include <Arduino.h>
#include <thread>
#include <atomic>
#include <chrono>
#include <vector>
#include <random>
#include <cmath>
#define DRUM_TRACE 1
#define private public
#include "drum_manager.h"
#undef private
volatile uint8_t g_cpu_load_pct = 0;
static int fails = 0;
#define CHECK(c, ...) do { if (c) printf("PASS %s\n", #c); else { printf("FAIL %s   ", #c); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)
static void run(DrumManager& m, long n) { for (long i = 0; i < n; i++) { int16_t l, r; m.tick(l, r); } }

static void test_params() {
    static DrumManager m; m.init();
    m.setParam(P_INST, 24);                                   // 24/8 = instrument 3
    CHECK(m.sel == 3 && m.pv[P_INST] == 24 && m.pv[P_LEVEL] == KIT[3][0], "sel=%d", m.sel);
    m.setParam(P_TUNE, 90); m.setParam(P_PAN, 10);
    CHECK(m.kit[3][1] == 90 && m.kit[3][4] == 10 && m.kit[2][1] == KIT[2][1] && m.kit[4][1] == KIT[4][1], "edit stays on the selected instrument");
    m.selectInst(0); m.selectInst(3);
    CHECK(m.pv[P_TUNE] == 90 && m.pv[P_PAN] == 10, "values follow the selection");
    m.setParam(P_LEVEL, 200); CHECK(m.pv[P_LEVEL] == 127, "values clamp to 127");
    m.setBpm(1000); CHECK(m.bpm == 300.0f && m.pv[P_TEMPO] == 127, "bpm clamps at 300, knob saturates");
    m.setBpm(10);   CHECK(m.bpm == 30.0f && m.pv[P_TEMPO] == 0, "bpm clamps at 30");
    m.setBpm(123.5f); CHECK(m.bpm == 123.5f && m.pv[P_TEMPO] == 64, "fractional bpm kept; knob mirrors");
    CHECK(fabsf(m.stepLen - 44100.0f * 15.0f / 123.5f) < 0.01f, "step length follows bpm");
    m.setParam(P_TEMPO, 60); CHECK(m.bpm == 120.0f, "tempo knob 60 -> 120 BPM");
    m.setParam(P_SWING, 127); CHECK(fabsf(m.swing - 0.75f) < 1e-6f, "swing knob 127 -> 75 %%");
    m.setParam(P_SWING, 0);   CHECK(m.swing == 0.5f, "swing knob 0 -> straight");
    CHECK(m.handleCC(120, 0) && !m.handleCC(99, 1), "CC120 handled, unknown CC not");
    for (int i = 0; i < N_PARAMS; i++) { const uint8_t id = paramForCC(PARAM_INFO[i].cc); CHECK(id != 0xFF, "cc of param %d", i); }
}

static void test_patterns() {
    static DrumManager m; m.init();
    m.setPattern(5); m.setStep(2, 3, 2);
    m.cycleStep(2, 3); CHECK(m.getStep(2, 3) == 3, "normal -> accent");
    m.cycleStep(2, 3); CHECK(m.getStep(2, 3) == 1, "accent -> ghost");
    m.cycleStep(2, 3); CHECK(m.getStep(2, 3) == 0, "ghost -> off");
    m.cycleStep(2, 3); CHECK(m.getStep(2, 3) == 2, "off -> normal (full cycle of four)");
    m.setStep(2, 3, 1);
    m.setStep(99, 0, 1); m.setStep(0, 99, 1); m.setStep(0, 0, 9); CHECK(m.getStep(0, 0) == 0, "out-of-range edits ignored");
    m.setLength(0); m.setLength(17); CHECK(m.patLen[5] == 16, "length 0 / 17 rejected");
    m.setLength(9); m.copyPattern(5, 6); CHECK(m.patLen[6] == 9 && m.pat[6][2][3] == 1, "copy keeps length and steps");
    m.clearPattern(5); CHECK(m.pat[5][2][3] == 0 && m.patLen[5] == 16, "clear");
    m.setSongSlot(0, 3); m.setSongSlot(1, 200); CHECK(m.song[0] == 3 && m.song[1] == 0xFF, "song slot range");
    m.loadDemo(); CHECK(m.songLength() == sizeof DEMO_SONG, "demo song length %d", m.songLength());
    int on = 0; for (int i = 0; i < N_INST; i++) for (int s = 0; s < N_STEPS; s++) on += m.pat[0][i][s] != 0;
    CHECK(on == 4 + 2 + 7 + 1, "demo 'House' has 14 hits (kick 4, clap 2, closed hat 7, open hat 1), found %d", on);
}

static void test_ring() {
    static DrumManager m; m.init();
    for (int i = 0; i < 100; i++) {                           // producer overruns the ring: no overflow, drops, order kept
        const uint8_t w = m._rw, nw = (uint8_t)((w + 1) % REC_RING); if (nw == m._rr) break;
        m._ring[w] = { 0, (uint8_t)(i % 16), (uint8_t)(i % 16), (uint8_t)(i % 2 ? 127 : 60) }; m._rw = nw;
    }
    CHECK(((m._rw - m._rr + REC_RING) % REC_RING) == REC_RING - 1, "ring holds %d events (capacity-1)", REC_RING - 1);
    m.service(); CHECK(m._rr == m._rw, "service drains the ring");
    CHECK(m.pat[0][0][0] == 1 && m.pat[0][1][1] == 3 && m.pat[0][2][2] == 1, "events applied: 60->ghost, 127->accent");
    m._ring[m._rw] = { 200, 0, 0, 100 }; m._rw = (uint8_t)((m._rw + 1) % REC_RING); m.service(); CHECK(true, "out-of-range event ignored (no crash)");
}

static void test_storage() {
    static DrumManager a, b; a.init(); b.init();
    a.setBpm(137.5f); a.setParam(P_SWING, 70); a.setParam(P_VOL, 80); a.setParam(P_DRIVE, 33); a.selectInst(5); a.setParam(P_TUNE, 11);
    a.setPattern(7); a.setStep(4, 9, 3); a.setLength(11); a.setSongSlot(5, 7); a.songMode = true; a.extSync = true; a.padGm = true; a.padBase = 44;
    static uint8_t buf[8192]; const size_t n = a.serialize(buf, sizeof buf);
    CHECK(n == DrumManager::SAVE_SIZE && n == 4247, "save image is %zu bytes", n);
    CHECK(a.serialize(buf, 100) == 0, "too-small buffer refused");
    CHECK(b.deserialize(buf, n), "load a good image");
    CHECK(b.bpm == 137.5f && b.pv[P_SWING] == 70 && b.pv[P_VOL] == 80 && b.pv[P_DRIVE] == 33 && b.kit[5][1] == 11, "kit and machine settings restored");
    CHECK(b.pat[7][4][9] == 3 && b.patLen[7] == 11 && b.song[5] == 7 && b.selPat == 7, "patterns, length, song, selection restored");
    CHECK(b.songMode && b.extSync && b.padGm && b.padBase == 44, "flags and pad map restored");
    CHECK(memcmp(b.pat, a.pat, sizeof a.pat) == 0 && memcmp(b.kit, a.kit, sizeof a.kit) == 0, "memory identical");
    static uint8_t bad[8192]; int rejected = 0, accepted_corrupt = 0;
    static DrumManager c; c.init(); c.setBpm(99); const float before = c.bpm; static uint8_t pre[sizeof c.pat]; memcpy(pre, c.pat, sizeof pre);
    for (size_t i = 0; i < n; i += 7) { memcpy(bad, buf, n); bad[i] ^= 0x10; if (c.deserialize(bad, n)) accepted_corrupt++; else rejected++; }
    CHECK(accepted_corrupt == 0, "%d single-byte corruptions all rejected (accepted %d)", rejected, accepted_corrupt);
    CHECK(c.bpm == before && memcmp(pre, c.pat, sizeof pre) == 0, "a rejected load changes NOTHING");
    CHECK(!c.deserialize(buf, n - 1) && !c.deserialize(buf, 0) && !c.deserialize(buf, n + 1), "truncated / empty / long images rejected");
    memcpy(bad, buf, n); bad[3] = 2; const uint32_t crc = DrumManager::crc32(bad, n - 4); memcpy(bad + n - 4, &crc, 4);
    CHECK(!c.deserialize(bad, n), "future version rejected even with a valid CRC");
    memcpy(bad, buf, n); const size_t pat0 = 9 + sizeof a.kit + 4 + 6 + N_PATTERNS; bad[pat0] = 77; bad[9 + sizeof a.kit + 4 + 6] = 0;        // out-of-range step value and length 0, valid CRC
    const uint32_t crc2 = DrumManager::crc32(bad, n - 4); memcpy(bad + n - 4, &crc2, 4);
    CHECK(c.deserialize(bad, n) && c.pat[0][0][0] <= 3 && c.patLen[0] >= 1, "valid-CRC but out-of-range data is sanitised on load");
}

// deterministic random torture on one thread: every public operation, ticks of random length
static void test_torture() {
    static DrumManager m; m.init(); std::mt19937 rng(7); uint64_t ticks = 0; int bad = 0;
    for (int ev = 0; ev < 250000; ev++) {
        switch (rng() % 22) {
            case 0: case 1: case 2: case 3: m.hit(rng() % 16, rng() % 128); break;
            case 4: m.padHit(30 + rng() % 40, rng() % 128); break;
            case 5: m.setParam(rng() % N_PARAMS, rng() % 140); break;
            case 6: m.handleCC(rng() % 128, rng() % 128); break;
            case 7: m.play(); break;
            case 8: m.stop(); break;
            case 9: m.resume(); break;
            case 10: m.setRecord(rng() & 1); break;
            case 11: m.setPattern(rng() % 20); break;
            case 12: m.cycleStep(rng() % 16, rng() % 16); break;
            case 13: m.setLength(rng() % 20); break;
            case 14: m.setSongSlot(rng() % 34, rng() % 20); break;
            case 15: m.songMode = rng() & 1; break;
            case 16: m.extSync = rng() & 1; break;
            case 17: m.midiRealtime(0xF8); break;
            case 18: m.midiRealtime(0xFA + rng() % 3); break;
            case 19: m.setBpm(20 + rng() % 300); break;
            case 20: if (rng() % 50 == 0) m.panic(); break;
            case 21: m.service(); break;
        }
        const long n = rng() % 300; run(m, n); ticks += n;
        if (m.stepNow >= N_STEPS || m.patNow >= N_PATTERNS || m.songPosNow >= SONG_MAX) bad++;
        if (m._step >= N_STEPS || m._patCur >= N_PATTERNS) bad++;
    }
    CHECK(bad == 0, "invariants held over 250000 random events / %llu samples", (unsigned long long)ticks);
}

// two threads: core 0 (this thread) mutates everything; "core 1" renders audio flat out
static void test_threads() {
    static DrumManager m; m.init(); std::atomic<bool> stop{false}; std::atomic<long> samples{0}; std::atomic<int> bad{0};
    std::thread core1([&] { int16_t l, r; while (!stop) { for (int i = 0; i < 64; i++) { m.tick(l, r); } samples += 64;
        if (m.stepNow >= N_STEPS || m.patNow >= N_PATTERNS || m.songPosNow >= SONG_MAX) bad++; } });
    while (samples == 0) std::this_thread::yield();                          // the audio thread must be running before the burst starts
    std::mt19937 rng(11); long hits = 0;
    m.play();
    for (int ev = 0; ev < 60000; ev++) {
        if ((ev & 7) == 0) std::this_thread::yield();                         // interleave with the audio thread
        switch (rng() % 14) {
            case 0: case 1: case 2: m.hit(rng() % 16, 1 + rng() % 127); hits++; std::this_thread::sleep_for(std::chrono::microseconds(30)); break;
            case 3: m.setParam(rng() % N_PARAMS, rng() % 128); break;
            case 4: m.cycleStep(rng() % 16, rng() % 16); break;
            case 5: m.setPattern(rng() % 16); break;
            case 6: m.setRecord(rng() & 1); break;
            case 7: m.midiRealtime(0xF8); break;
            case 8: if (rng() % 40 == 0) { m.midiRealtime(0xFA + rng() % 3); } break;
            case 9: m.setLength(1 + rng() % 16); break;
            case 10: m.songMode = rng() & 1; break;
            case 11: m.setBpm(40 + rng() % 200); break;
            case 12: m.service(); break;
            case 13: if (rng() % 500 == 0) { static uint8_t b[8192]; size_t n = m.serialize(b, sizeof b); m.deserialize(b, n); } break;
        }
    }
    while (samples < 2000000) std::this_thread::sleep_for(std::chrono::milliseconds(1));   // the audio side must have rendered well beyond the burst
    stop = true; core1.join();
    CHECK(bad == 0 && samples >= 2000000, "two threads: %ld control-side events, %ld samples rendered, no invalid state", 60000L, (long)samples);
    (void)hits;
}

// every pad hit must produce exactly one voice start. A hand-off (wait until the audio side has CONSUMED each hit before the next) makes this
// independent of thread scheduling: two hits on one instrument inside one control tick legitimately collapse into one.
static void test_hit_delivery() {
    static DrumManager m; m.init(); std::atomic<bool> stop{false};
    std::thread core1([&] { int16_t l, r; while (!stop) for (int i = 0; i < 16; i++) m.tick(l, r); });
    const int N = 3000; bool ok = true;
    for (int i = 0; i < N; i++) {
        const int inst = i % 16; m.hit(inst, 100); bool got = false;
        for (int k = 0; k < 2000000 && !got; k++) { got = (*(volatile uint8_t*)&m._v[inst].trigSeen == m._v[inst].trigSeq); if (!got) std::this_thread::yield(); }
        ok &= got;
    }
    stop = true; core1.join();
    CHECK(ok && (int)m.traceN == N, "%u voice starts for %d hits, every one consumed", m.traceN, N);
}

// Regression: softSat must never leave +-1 (the output converts to int16; an overshoot would wrap into a loud click).
// Near |x| = 3 the curve is cubic-flat, so float rounding gives ~1e-7 wiggles: allow 1e-6. The old clamp at 4 overshot by 5.8e-3.
static void test_softsat_bound() {
    float worst = 0.0f, prev = -2.0f, worst_drop = 0.0f; bool conv_ok = true;
    for (int i = -200000; i <= 200000; i++) {
        const float x = i * 5e-5f, y = softSat(x);
        if (fabsf(y) > worst) worst = fabsf(y);
        if (prev - y > worst_drop) worst_drop = prev - y;
        if (fabsf(y) * 32767.0f >= 32768.0f) conv_ok = false;
        prev = y;
    }
    CHECK(worst <= 1.000001f, "softSat overshoots: max |y| = %.7f", worst);
    CHECK(worst_drop <= 1e-6f, "softSat is not monotonic: largest drop %.2e", worst_drop);
    CHECK(conv_ok, "int16 conversion of softSat() output can leave its range");
}

// the worst case the hardware can see: every voice at full level, full drive, full volume, hit together
static void test_output_extremes() {
    static DrumManager m; m.init();
    for (int i = 0; i < N_INST; i++) { m.selectInst(i); m.setParam(P_LEVEL, 127); m.setParam(P_PAN, i & 1 ? 127 : 0); }
    m.setParam(P_DRIVE, 127); m.setParam(P_VOL, 127);
    int16_t l, r; int peak = 0; bool full = false;
    for (int rep = 0; rep < 8; rep++) { for (int i = 0; i < N_INST; i++) m.hit(i, 127); for (int n = 0; n < 20000; n++) { m.tick(l, r); if (abs((int)l) > peak) peak = abs((int)l); if (abs((int)r) > peak) peak = abs((int)r); if (abs((int)l) >= 32766) full = true; } }
    CHECK(peak <= 32767 && full, "all 16 hard-panned at max level+drive+volume: peak %d (reaches full scale, never wraps)", peak);
}

int main() {
    Wavetable::init();
    test_softsat_bound(); test_output_extremes(); test_params(); test_patterns(); test_ring(); test_storage(); test_torture(); test_threads(); test_hit_delivery();
    printf(fails ? "\n%d ENGINE TESTS FAILED\n" : "\nALL ENGINE TESTS PASS\n", fails); return fails != 0;
}
