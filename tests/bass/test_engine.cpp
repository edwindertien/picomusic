// Engine tests on the REAL BassManager: parameters, key stack (model-based), patterns, storage image, torture, two-thread race.
// Built with ASan + UBSan (+ float-cast-overflow).
#include <Arduino.h>
#include <thread>
#include <atomic>
#include <chrono>
#include <vector>
#include <random>
#include <cmath>
#define BASS_TRACE 1
#define private public
#include "bass_manager.h"
#undef private
volatile uint8_t g_cpu_load_pct = 0;
static int fails = 0;
#define CHECK(c, ...) do { if (c) printf("PASS %s\n", #c); else { printf("FAIL %s   ", #c); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)
static void run(BassManager& m, long n) { for (long i = 0; i < n; i++) { int16_t l, r; m.tick(l, r); } }

static void test_params() {
    static BassManager m; m.init();
    CHECK(m.patch == 0 && !strcmp(m.patchName(), "Moog Bass"), "starts on patch 0");
    for (int k = 0; k < N_PARAMS; k++) if (k != P_TEMPO && k != P_SWING) CHECK(m.pv[k] == PATCHES[0].v[k], "param %d", k);
    m.setParam(P_CUT, 200); CHECK(m.pv[P_CUT] == 127, "values clamp to 127");
    m.setBpm(1000); CHECK(m.bpm == 300.0f && m.pv[P_TEMPO] == 127, "bpm clamps at 300, knob saturates");
    m.setBpm(10);   CHECK(m.bpm == 30.0f && m.pv[P_TEMPO] == 0, "bpm clamps at 30");
    m.setBpm(123.5f); CHECK(m.bpm == 123.5f && m.pv[P_TEMPO] == 64, "fractional bpm kept; knob mirrors");
    CHECK(fabsf(m.stepLen - 44100.0f * 15.0f / 123.5f) < 0.01f, "step length follows bpm");
    m.setParam(P_SWING, 127); CHECK(fabsf(m.swing - 0.75f) < 1e-6f, "swing 127 = 75 %%");
    m.setParam(P_GATE, 0); CHECK(fabsf(m.gateFrac - 0.10f) < 1e-6f, "gate 0 = 10 %%");
    m.setBpm(123.5f); m.loadPatch(1); CHECK(m.bpm == 123.5f, "loading a patch leaves a fractional BPM alone (%.2f)", m.bpm);
    m.setParam(P_TEMPO, 60); m.setParam(P_SWING, 0); m.loadPatch(2);
    CHECK(m.bpm == 120.0f && m.swing == 0.5f && m.pv[P_RES] == PATCHES[2].v[P_RES], "a patch keeps tempo and swing");
    m.loadPatch(9); CHECK(m.patch == 2, "invalid patch ignored");
    CHECK(m.handleCC(120, 0) && m.handleCC(123, 0) && m.handleCC(64, 100) && !m.handleCC(99, 1), "CC120/123/64 handled, unknown CC not");
    for (int i = 0; i < N_PARAMS; i++) { const uint8_t id = paramForCC(PARAM_INFO[i].cc); CHECK(id != 0xFF, "cc of param %d", i); }
    // every CC number is unique and never collides with the AKAI knobs 1..8 except where intended
    int seen[128] = {0}; bool dup = false; for (int i = 0; i < N_PARAMS; i++) { if (seen[PARAM_INFO[i].cc]++) dup = true; }
    CHECK(!dup, "all parameter CC numbers are distinct");
    char b[8]; noteName(36, b); CHECK(!strcmp(b, "C2"), "MIDI 36 = C2 (%s)", b); noteName(61, b); CHECK(!strcmp(b, "C#4"), "61 = C#4 (%s)", b); noteName(24, b); CHECK(!strcmp(b, "C1"), "24 = C1");
    uint8_t n, f; CHECK(parseStep("C2", n, f) && n == 36 && f == F_GATE, "parse C2"); CHECK(parseStep("A1s", n, f) && n == 33 && f == (F_GATE | F_SLIDE), "parse A1s");
    CHECK(parseStep("D#3sa", n, f) && n == 51 && f == (F_GATE | F_SLIDE | F_ACCENT), "parse D#3sa"); CHECK(parseStep(".", n, f) && f == 0, "rest"); CHECK(!parseStep("H2", n, f) && !parseStep("C", n, f) && !parseStep("C2x", n, f), "bad tokens rejected");
}

// ---- the mono key stack against a reference model ----
struct Model {
    struct K { int note; bool rel; }; std::vector<K> st; bool pedal = false; int sounding = -1; bool gate = false;
    void on(int n) { for (size_t i = 0; i < st.size(); i++) if (st[i].note == n) { st.erase(st.begin() + i); break; } if (st.size() == STACK_N) st.erase(st.begin()); st.push_back({ n, false }); sounding = n; gate = true; }
    void resolve() { if (st.empty()) { sounding = -1; gate = false; } else if (st.back().note != sounding) { sounding = st.back().note; gate = true; } }
    void off(int n) { for (size_t i = 0; i < st.size(); i++) if (st[i].note == n) { if (pedal) { st[i].rel = true; return; } st.erase(st.begin() + i); resolve(); return; } }
    void ped(bool d) { if (d == pedal) return; pedal = d; if (d) return; for (int i = (int)st.size() - 1; i >= 0; i--) if (st[i].rel) st.erase(st.begin() + i); resolve(); }
    void all() { st.clear(); pedal = false; sounding = -1; gate = false; }
};
static void test_keystack() {
    static BassManager m; m.init(); m.keyMode = 0; Model mod; std::mt19937 rng(3); int bad = 0, ops = 0;
    for (int it = 0; it < 40000; it++) {
        const int op = rng() % 10, n = 30 + rng() % 14;
        if (op < 4) { m.noteOn(n, 100); mod.on(n); } else if (op < 8) { m.noteOff(n); mod.off(n); }
        else if (op == 8) { const bool d = rng() & 1; m.setPedal(d); mod.ped(d); } else if (rng() % 8 == 0) { m.allNotesOff(); mod.all(); } else continue;
        ops++; run(m, 20);
        const bool g = m.gateNow != 0; const int note = g ? m.noteNow : -1;
        if (g != mod.gate || (g && note != mod.sounding) || m.heldKeys() != mod.st.size()) { if (bad < 5) printf("   mismatch op %d: engine gate=%d note=%d held=%d, model gate=%d note=%d held=%zu\n", it, g, note, m.heldKeys(), mod.gate, mod.sounding, mod.st.size()); bad++; }
    }
    CHECK(bad == 0, "%d random key operations: the engine matches the reference model (%d mismatches)", ops, bad);
}

static void test_patterns() {
    static BassManager m; m.init(); m.setPattern(5);
    m.setStep(3, 40, F_GATE); CHECK(m.stepNote[5][3] == 40 && m.stepFlg[5][3] == F_GATE, "setStep");
    m.cycleFlags(3); CHECK(m.stepFlg[5][3] == (F_GATE | F_ACCENT), "note -> accent"); m.cycleFlags(3); CHECK(m.stepFlg[5][3] == (F_GATE | F_SLIDE), "accent -> slide");
    m.cycleFlags(3); CHECK(m.stepFlg[5][3] == (F_GATE | F_SLIDE | F_ACCENT), "slide -> slide+accent"); m.cycleFlags(3); CHECK(m.stepFlg[5][3] == 0, "slide+accent -> rest"); m.cycleFlags(3); CHECK(m.stepFlg[5][3] == F_GATE, "rest -> note (full cycle of five)");
    m.setStep(99, 40, 1); m.setStep(0, 40, 9); CHECK(m.stepFlg[5][0] == 0, "out-of-range edits ignored");
    m.setStep(1, 5, F_GATE); CHECK(m.stepNote[5][1] == NOTE_MIN, "note clamps to %d", NOTE_MIN); m.setStep(1, 120, F_GATE); CHECK(m.stepNote[5][1] == NOTE_MAX, "note clamps to %d", NOTE_MAX);
    m.setStep(2, 60, 0); m.shiftNote(2, +1); CHECK(m.stepNote[5][2] == 61 && (m.stepFlg[5][2] & F_GATE), "shiftNote on a rest switches the step on");
    for (int i = 0; i < 200; i++) { m.shiftNote(2, -1); }
    CHECK(m.stepNote[5][2] == NOTE_MIN, "shiftNote stops at the floor");
    m.setLength(0); m.setLength(17); CHECK(m.patLen[5] == 16, "length 0 / 17 rejected"); m.cursor = 15; m.setLength(9); CHECK(m.patLen[5] == 9 && m.cursor == 8, "shortening pulls the cursor in");
    m.copyPattern(5, 6); CHECK(m.patLen[6] == 9 && m.stepNote[6][3] == 40, "copy keeps length and steps");
    m.clearPattern(5); CHECK(m.stepFlg[5][3] == 0 && m.patLen[5] == 16, "clear");
    int on = 0; for (int s = 0; s < N_STEPS; s++) on += m.stepFlg[1][s] & F_GATE; CHECK(on == 11, "demo 'Acid' has 11 notes (16 steps, 5 rests), found %d", on);
    CHECK(m.stepFlg[1][5] == (F_GATE | F_SLIDE) && m.stepNote[1][5] == 39, "demo Acid step 6 = D#2 slide");
}

static void test_storage() {
    static BassManager a, b; a.init(); b.init();
    a.setBpm(137.5f); a.loadPatch(2); a.setParam(P_CUT, 33); a.setPattern(7); a.setStep(4, 50, F_GATE | F_SLIDE); a.setLength(11); a.extSync = true; a.keyMode = 0;
    static uint8_t buf[4096]; const size_t n = a.serialize(buf, sizeof buf);
    CHECK(n == BassManager::SAVE_SIZE, "save image is %zu bytes", n); CHECK(a.serialize(buf, 100) == 0, "too-small buffer refused");
    CHECK(b.deserialize(buf, n), "load a good image");
    CHECK(b.bpm == 137.5f && b.patch == 2 && b.pv[P_CUT] == 33 && b.stepNote[7][4] == 50 && b.stepFlg[7][4] == (F_GATE | F_SLIDE) && b.patLen[7] == 11 && b.selPat == 7 && b.extSync && b.keyMode == 0, "everything restored");
    CHECK(memcmp(b.stepNote, a.stepNote, sizeof a.stepNote) == 0 && memcmp(b.stepFlg, a.stepFlg, sizeof a.stepFlg) == 0 && memcmp(b.pv, a.pv, sizeof a.pv) == 0, "memory identical");
    static uint8_t bad[4096]; int rejected = 0, accepted = 0; static BassManager c; c.init(); c.setBpm(99); static uint8_t pre[sizeof c.stepNote]; memcpy(pre, c.stepNote, sizeof pre); const float before = c.bpm;
    for (size_t i = 0; i < n; i += 5) { memcpy(bad, buf, n); bad[i] ^= 0x10; if (c.deserialize(bad, n)) accepted++; else rejected++; }
    CHECK(accepted == 0, "%d single-byte corruptions all rejected (accepted %d)", rejected, accepted);
    CHECK(c.bpm == before && memcmp(pre, c.stepNote, sizeof pre) == 0, "a rejected load changes NOTHING");
    CHECK(!c.deserialize(buf, n - 1) && !c.deserialize(buf, 0) && !c.deserialize(buf, n + 1), "truncated / empty / long images rejected");
    memcpy(bad, buf, n); bad[3] = 2; uint32_t crc = BassManager::crc32(bad, n - 4); memcpy(bad + n - 4, &crc, 4); CHECK(!c.deserialize(bad, n), "future version rejected even with a valid CRC");
    memcpy(bad, buf, n); const size_t off_len = 8 + N_PARAMS + 1 + 4 + 2; bad[off_len] = 0; bad[off_len + N_PATTERNS] = 3; bad[off_len + N_PATTERNS + 256] = 0xFF; bad[8] = 200;
    crc = BassManager::crc32(bad, n - 4); memcpy(bad + n - 4, &crc, 4);
    CHECK(c.deserialize(bad, n) && c.patLen[0] >= 1 && c.stepNote[0][0] >= NOTE_MIN && c.stepFlg[0][0] <= 7 && c.pv[0] <= 127, "valid-CRC but out-of-range data is sanitised on load");
}

// every public operation, ticks of random length
static void test_torture() {
    static BassManager m; m.init(); std::mt19937 rng(7); uint64_t ticks = 0; int bad = 0;
    for (int ev = 0; ev < 250000; ev++) {
        switch (rng() % 22) {
            case 0: case 1: case 2: m.noteOn(rng() % 128, rng() % 128); break;
            case 3: case 4: m.noteOff(rng() % 128); break;
            case 5: m.setParam(rng() % N_PARAMS, rng() % 140); break;
            case 6: m.handleCC(rng() % 128, rng() % 128); break;
            case 7: m.play(); break; case 8: m.stop(); break; case 9: m.resume(); break;
            case 10: m.setPattern(rng() % 20); break;
            case 11: m.cycleFlags(rng() % 16); break;
            case 12: m.shiftNote(rng() % 16, (int)(rng() % 25) - 12); break;
            case 13: m.setLength(rng() % 20); break;
            case 14: m.extSync = rng() & 1; break;
            case 15: m.midiRealtime(0xF8); break;
            case 16: m.midiRealtime(0xFA + rng() % 3); break;
            case 17: m.setBpm(20 + rng() % 300); break;
            case 18: if (rng() % 50 == 0) m.panic(); break;
            case 19: m.pitchBend((int16_t)(rng() % 16384) - 8192); break;
            case 20: m.keyMode = rng() & 1; m.stepWrite = rng() % 4 == 0; break;
            case 21: m.setPedal(rng() & 1); break;
        }
        const long n = rng() % 300; run(m, n); ticks += n;
        if (m.stepNow >= N_STEPS || m.patNow >= N_PATTERNS || m._step >= N_STEPS || m._patCur >= N_PATTERNS || m.cursor >= N_STEPS) bad++;
        if (!m._voice.lp.sane() || !std::isfinite(m._voice.amp) || !std::isfinite(m._voice.pitch)) bad++;
    }
    CHECK(bad == 0, "invariants held over 250000 random events / %llu samples", (unsigned long long)ticks);
}

// two threads: core 0 (this thread) mutates everything; "core 1" renders audio flat out
static void test_threads() {
    static BassManager m; m.init(); std::atomic<bool> stop{false}; std::atomic<long> samples{0}; std::atomic<int> bad{0};
    std::thread core1([&] { int16_t l, r; while (!stop) { for (int i = 0; i < 64; i++) m.tick(l, r); samples += 64; if (m.stepNow >= N_STEPS || m.patNow >= N_PATTERNS) bad++; if (!std::isfinite(m._voice.amp)) bad++; } });
    while (samples == 0) std::this_thread::yield();                          // the audio thread must be running before the burst starts
    std::mt19937 rng(11); m.play();
    for (int ev = 0; ev < 60000; ev++) {
        if ((ev & 7) == 0) std::this_thread::yield();                         // interleave with the audio thread
        switch (rng() % 12) {
            case 0: case 1: m.noteOn(30 + rng() % 40, 1 + rng() % 127); break;
            case 2: case 3: m.noteOff(30 + rng() % 40); break;
            case 4: m.setParam(rng() % N_PARAMS, rng() % 128); break;
            case 5: m.cycleFlags(rng() % 16); break;
            case 6: m.setPattern(rng() % 16); break;
            case 7: m.midiRealtime(0xF8); break;
            case 8: if (rng() % 40 == 0) m.midiRealtime(0xFA + rng() % 3); break;
            case 9: m.setBpm(40 + rng() % 200); break;
            case 10: m.pitchBend((int16_t)(rng() % 16384) - 8192); break;
            case 11: if (rng() % 500 == 0) { static uint8_t b[4096]; size_t n = m.serialize(b, sizeof b); m.deserialize(b, n); } break;
        }
    }
    while (samples < 2000000) std::this_thread::sleep_for(std::chrono::milliseconds(1));   // the audio side must have rendered well beyond the burst
    stop = true; core1.join();
    CHECK(bad == 0 && samples >= 2000000, "two threads: 60000 control-side events, %ld samples rendered, no invalid state", (long)samples);
}

// every key event must reach the voice exactly once. A hand-off (wait until the audio side has CONSUMED each event before posting the next)
// makes this independent of thread scheduling: two events inside one control tick would legitimately collapse into the newest one.
static void test_delivery() {
    static BassManager m; m.init(); m.keyMode = 0; std::atomic<bool> stop{false};
    std::thread core1([&] { int16_t l, r; while (!stop) for (int i = 0; i < 16; i++) m.tick(l, r); });
    auto consumed = [&] { for (int k = 0; k < 2000000; k++) { if (*(volatile uint8_t*)&m._evtSeen == m._evtSeq) return true; std::this_thread::yield(); } return false; };
    const int N = 2000; bool ok = true;
    for (int i = 0; i < N; i++) { m.noteOn(40 + i % 12, 100); ok &= consumed(); m.noteOff(40 + i % 12); ok &= consumed(); }
    stop = true; core1.join();
    CHECK(ok && (int)m.traceN == 2 * N, "%u voice events for %d note on/off pairs (expected %d), every one consumed", m.traceN, N, 2 * N);
}

// the worst case the hardware can see: everything at 127, every note, all at once
static void test_output_extremes() {
    static BassManager m; m.init(); m.keyMode = 0;
    for (int k = 0; k < N_PARAMS; k++) if (k != P_TEMPO && k != P_SWING && k != P_WAV1 && k != P_WAV2 && k != P_WAV3 && k != P_OCT1 && k != P_OCT2 && k != P_OCT3) m.setParam(k, 127);
    int peak = 0; bool full = false; bool finite = true; int16_t l, r;
    for (int note = 24; note <= 96; note += 3) { m.noteOn(note, 127); for (int n = 0; n < 8000; n++) { m.tick(l, r); if (abs((int)l) > peak) peak = abs((int)l); if (abs((int)l) >= 32766) full = true; } m.noteOff(note); }
    finite = std::isfinite(m._voice.amp) && m._voice.lp.sane();
    CHECK(peak <= 32767 && finite, "every control at 127, notes 24..96: peak %d (%s), filter stable", peak, full ? "reaches full scale, never wraps" : "below full scale");
}

int main() {
    bass_tables_init();
    test_params(); test_keystack(); test_patterns(); test_storage(); test_torture(); test_threads(); test_delivery(); test_output_extremes();
    printf(fails ? "\n%d ENGINE TESTS FAILED\n" : "\nALL ENGINE TESTS PASS\n", fails); return fails != 0;
}
