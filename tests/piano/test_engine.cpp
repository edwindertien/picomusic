// Engine tests on the REAL PianoManager: parameters, zone map, event ring, torture, two-thread race, delivery, extremes. ASan + UBSan + float-cast-overflow.
#include <Arduino.h>
#include <thread>
#include <atomic>
#include <chrono>
#include <vector>
#include <random>
#include <cmath>
#define PIANO_TRACE 1
#define private public
#include "piano_manager.h"
#undef private
#define GRAND (PIANO_N_INSTRUMENTS == 1 && PIANO_ZONES == 30)       // the shipped bank; the same tests also run against a synthetic multi-instrument bank
volatile uint8_t g_cpu_load_pct = 0;
static int fails = 0;
#define CHECK(c, ...) do { if (c) printf("PASS %s\n", #c); else { printf("FAIL %s   ", #c); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)
static void run(PianoManager& m, long n) { for (long i = 0; i < n; i++) { int16_t l, r; m.tick(l, r); } }
static bool voicesSane(PianoManager& m) {
    for (int i = 0; i < N_VOICES; i++) { const PianoVoice& v = m._v[i]; if (!v.active) continue;
        if (v.key < KEY_LO || v.key > KEY_HI || v.pos > v.n || v.p == nullptr || !std::isfinite(v.e) || !std::isfinite(v.amp) || !std::isfinite(v.inc) || v.inc <= 0.0f || v.inc >= 8.0f || v.frac < 0.0f || v.frac >= 1.0f + 1e-3f) return false; }   // the shipped bank stays below 1; a user bank may go higher (tools/bankconf warns), 8 is the sanity ceiling
    return true;
}

static void test_params() {
    static PianoManager m; m.init();
#if GRAND
    for (int i = 0; i < N_PARAMS; i++) CHECK(m.pv[i] == PARAM_DEFAULT[i], "default of param %d", i);
#endif
    for (int i = 0; i < N_PARAMS; i++) CHECK(m.pv[i] == PIANO_INSTRUMENT[0].def[i], "param %d starts at the instrument 0 default", i);
    m.setParam(P_TONE, 200); CHECK(m.pv[P_TONE] == 127, "values clamp to 127");
    CHECK(fabsf(toneMulOf(64) - 1.0f) < 1e-6f && fabsf(toneMulOf(0) - 1.0f / 16.0f) < 1e-5f && fabsf(toneMulOf(127) - 8.0f) < 1e-3f, "tone: 0 -> /16, 64 -> 1, 127 -> x8");
    CHECK(fabsf(hardExpOf(64) - 1.4f) < 0.01f && hardExpOf(0) == 0.9f && fabsf(hardExpOf(127) - 1.9f) < 1e-5f, "hardness exponent 0.9 / 1.4 / 1.9");
    CHECK(decayOffOf(127) && decayOffOf(124) && !decayOffOf(123), "decay off from 124 up");
    CHECK(fabsf(tuneCentsOf(0) + 50.0f) < 1e-4f && tuneCentsOf(64) == 0.0f && fabsf(tuneCentsOf(127) - 49.22f) < 0.01f, "tune -50 .. +49.2 cents");
    m.setParam(P_TUNE, 127); CHECK(fabsf(m.pitchMul - powf(2.0f, 49.2188f / 1200.0f)) < 1e-5f, "pitchMul follows tune"); m.pitchBend(8191); CHECK(fabsf(m.pitchMul - powf(2.0f, (49.2188f / 100.0f + 1.99976f) / 12.0f)) < 1e-4f, "...and bend");
    CHECK(m.handleCC(120, 0) && m.handleCC(123, 0) && m.handleCC(64, 100) && m.handleCC(67, 100) && !m.handleCC(99, 1), "CC120/123/64/67 handled, unknown not");
    int seen[128] = {0}; bool dup = false; for (int i = 0; i < N_PARAMS; i++) { if (seen[PARAM_INFO[i].cc]++) dup = true; CHECK(paramForCC(PARAM_INFO[i].cc) != 0xFF, "cc of param %d", i); } CHECK(!dup, "all CC numbers distinct");
    CHECK(paramForCC(1) == P_TONE && paramForCC(7) == P_VOL && paramForCC(8) == P_SOFT, "AKAI K1 = tone, K7 = volume, K8 = una corda");
    int whites = 0; for (int k = KEY_LO; k <= KEY_HI; k++) whites += !keyIsBlack(k); CHECK(whites == N_WHITE, "88 keys have %d white keys", whites);
    CHECK(whiteIndexOf(21) == 0 && whiteIndexOf(108) == 51 && keyIsBlack(22) && !keyIsBlack(60), "keyboard geometry");
}

static void test_zones() {
    static PianoManager m; m.init(); bool near1 = true, incok = true; float lo = 9, hi = 0; int exact = 0; bool inrange = true;
    for (int inst = 0; inst < PIANO_N_INSTRUMENTS; inst++) for (int key = KEY_LO; key <= KEY_HI; key++) for (int vel : { 1, 40, 80, 127 }) {
        const int z = PianoManager::selectZone(inst, key, vel); if (z < PIANO_INSTRUMENT[inst].zoneFirst || z >= PIANO_INSTRUMENT[inst].zoneFirst + PIANO_INSTRUMENT[inst].zoneCount) { inrange = false; continue; }
        const PianoZone& Z = PIANO_ZONE[z]; const int d = abs(key - (int)Z.root);
#if GRAND
        if (d > 1) near1 = false;
#endif
        if (vel == 80 && d == 0) exact++;
        const float inc = ((float)Z.rate / (float)SAMPLE_RATE) * powf(2.0f, (key - (int)Z.root) / 12.0f); if (inc <= 0) incok = false; if (inc < lo) lo = inc; if (inc > hi) hi = inc;
    }
    CHECK(inrange, "selectZone always returns a zone of the instrument asked for");
#if GRAND
    CHECK(near1, "every key is within ONE semitone of its recorded sample"); CHECK(exact == PIANO_ZONES, "%d keys play at the recorded pitch (one per zone)", exact);
    CHECK(hi < 1.0f, "playback increment stays below 1: at most one ADPCM sample is decoded per output sample (range %.3f .. %.3f)", lo, hi);
    CHECK(incok, "increments positive");
#endif
    for (int z = 0; z < PIANO_ZONES; z++) { const PianoZone& Z = PIANO_ZONE[z]; if (Z.off + 2u * Z.head + (Z.n - Z.head + 1) / 2 > PIANO_DATA_BYTES || Z.head > Z.n || (Z.loopEnd && Z.head >= Z.loopStart) || (Z.loopEnd && (Z.loopStart >= Z.loopEnd || Z.loopEnd != Z.n)) || Z.vlo > Z.vhi || Z.lo > Z.hi) { CHECK(false, "zone %d is inconsistent", z); break; } }
    CHECK(true, "all zones lie inside the data, loops are inside their zone, ranges are ordered");
    CHECK(PianoManager::selectZone(0, 60, 0) >= 0 && PianoManager::selectZone(0, 0, 127) >= 0 && PianoManager::selectZone(0, 127, 1) >= 0, "keys and velocities outside every range still find the nearest zone (never silence)");
}

static void test_limiter() {
    bool lin = true, odd = true, mono = true, bounded = true; float prev = -2.0f;
    for (float x = -20.0f; x <= 20.0f; x += 0.001f) { const float y = softLimit(x); if (fabsf(x) <= LIMIT_KNEE && y != x) lin = false; if (y != -softLimit(-x)) odd = false; if (y < prev) mono = false; prev = y; if (!(fabsf(y) < 0.995f + 1e-6f)) bounded = false; }
    CHECK(lin, "the limiter is EXACTLY linear (y == x, bit for bit) up to the knee at %.2f", LIMIT_KNEE); CHECK(odd, "odd symmetric"); CHECK(mono, "monotonic: a louder input is never quieter"); CHECK(bounded, "never reaches full scale (< 0.995) for any input up to 20");
    const float k = LIMIT_KNEE, h = 1e-3f; CHECK(fabsf((softLimit(k + h) - softLimit(k)) / h - 1.0f) < 0.01f && fabsf((softLimit(k) - softLimit(k - h)) / h - 1.0f) < 1e-3f, "slope 1 on both sides of the knee: no kink");
    CHECK(softLimit(1.0f) > 0.85f && softLimit(1.0f) < 0.95f && softLimit(2.0f) < softLimit(4.0f), "above the knee it compresses smoothly (1.0 -> %.3f, 2.0 -> %.3f)", softLimit(1.0f), softLimit(2.0f));
    const float inf = 1e30f, nan_ = nanf(""); CHECK(softLimit(inf) < 0.995f && softLimit(-inf) > -0.995f && fabsf(softLimit(nan_)) < 1.0f, "huge and NaN inputs stay inside the 16-bit range (the audio core has no libm to catch them)");
    CHECK((int16_t)(softLimit(inf) * 32767.0f) < 32767 && (int16_t)(softLimit(-inf) * 32767.0f) > -32768, "...and convert to int16 without overflow");
}

static void test_limiter_notes() {
    static PianoManager m; m.init(); m.setParam(P_VOL, 127); for (int k = 24; k < 100; k += 3) m.noteOn((uint8_t)k, 127); int16_t l, r; long over = 0;
    for (long i = 0; i < 60000; i++) { m.tick(l, r); if (l == -32768 || r == -32768 || l == 32767 || r == 32767) over++; }
    CHECK(over == 0, "26 notes at velocity 127 and full volume never reach an int16 rail (%ld samples did)", over);
}

static void test_instruments_fx() {
    static PianoManager m; m.init();
    m.setInstrument(200); CHECK(m.instrument == 0, "an out-of-range instrument is ignored");
    m.setParam(P_VOL, 5); m.setParam(P_REV, 99); m.setInstrument(0); CHECK(m.pv[P_VOL] == PIANO_INSTRUMENT[0].def[P_VOL] && m.pv[P_REV] == PIANO_INSTRUMENT[0].def[P_REV], "selecting an instrument loads ALL its knob defaults, effects included");
    CHECK(m.fx.on == 0 || !GRAND, "the effects are off at start");
    m.setParam(P_REV, 10); CHECK((m.fx.on & 16) && m.fx.revWet > 0.0f, "REV switches the reverb on");
    m.setParam(P_REV, 0); CHECK(!(m.fx.on & 16), "REV 0 switches it off");
    m.setParam(P_TREM, 20); CHECK((m.fx.on & 2) && !(m.fx.on & 4) && m.fx.tremDepth > 0.6f, "TREM below 64 = tremolo"); m.setParam(P_TREM, 100); CHECK(!(m.fx.on & 2) && (m.fx.on & 4) && m.fx.panDepth > 0.5f, "TREM above 64 = auto-pan"); m.setParam(P_TREM, 64); CHECK((m.fx.on & 6) == 0, "TREM 64 = off");
    m.setParam(P_WAH, 100); CHECK(m.fx.on & 1, "WAH"); m.setParam(P_WLFO, 50); CHECK(m.fx.wahLfoInc > 0, "WLFO > 0 starts the auto-sweep"); m.setParam(P_WLFO, 0); CHECK(m.fx.wahLfoInc == 0, "WLFO 0 = manual");
    m.setParam(P_CHOR, 9); CHECK(m.fx.on & 8, "CHOR");
    CHECK(m.handleCC(11, 90) && m.pv[P_WPOS] == 90 && fabsf(m.fx.wahPos - 90 / 127.0f) < 1e-6f, "CC11 (expression) is the wah pedal"); CHECK(m.handleCC(91, 55) && m.pv[P_REV] == 55 && m.handleCC(93, 44) && m.pv[P_CHOR] == 44, "CC91 reverb, CC93 chorus (the standard numbers)");
    CHECK(fabsf(PianoFx::wahRateHz(0) - 0.2f) < 1e-4f && fabsf(PianoFx::wahRateHz(127) - 6.4f) < 0.01f && fabsf(PianoFx::tremRateHz(0) - 0.5f) < 1e-4f && fabsf(PianoFx::tremRateHz(127) - 9.85f) < 0.05f, "LFO rate ranges 0.2..6.4 Hz and 0.5..9.9 Hz");
    CHECK(fabsf(PianoFx::lfo(0)) < 1e-6f && fabsf(PianoFx::lfo(0x40000000u) - 1.0f) < 1e-6f && fabsf(PianoFx::lfo(0xC0000000u) + 1.0f) < 1e-6f, "the LFO approximation hits 0, +1, -1 at 0, a quarter and three quarters of a cycle");
    float worst = 0; for (float x = 0.0f; x < 3.99f; x += 0.01f) { const float e = fabsf(PianoFx::exp2x(x) / powf(2.0f, x) - 1.0f); if (e > worst) worst = e; } CHECK(worst < 3e-4f, "exp2 polynomial: worst relative error %.1e", worst);
    CHECK(sizeof(PianoFx) < 70000, "the effects state is %zu bytes of RAM", sizeof(PianoFx));
}

static void test_ring() {
    static PianoManager m; m.init(); m.keyMask[0] = 0; int accepted = 0;
    for (int i = 0; i < 100; i++) { const uint8_t before = m._rw; m.noteOn(40 + i % 40, 80); if (m._rw != before) accepted++; }
    CHECK(accepted == EVT_RING - 1, "ring accepts %d of 100 events when nothing is consuming (capacity - 1)", accepted);
    run(m, 20); CHECK(m._rr == m._rw, "one control tick drains the ring"); CHECK(m.activeVoices() >= N_POLY && m.activeVoices() <= N_VOICES, "63 note-ons: %d of the %d slots used (limit %d), no crash", m.activeVoices(), N_VOICES, N_POLY);
    CHECK(voicesSane(m), "voice state sane");
    static PianoManager n; n.init(); n.noteOn(60, 80); n.noteOn(64, 80); n.noteOff(60); run(n, 20);
    CHECK(n.activeVoices() == 2 && n._v[0].key == 60 && n._v[1].key == 64 && n._v[0].rel && !n._v[1].rel, "events are applied in order (60 on, 64 on, 60 off)");
    n.noteOn(10, 80); n.noteOn(120, 80); n.noteOff(10); run(n, 20); CHECK(n.activeVoices() == 2, "notes outside A0..C8 are ignored");
    static PianoManager f; f.init(); for (int k = KEY_LO; k < KEY_LO + 20; k++) f.noteOn((uint8_t)k, 80); run(f, 40);
    for (int i = 0; i < 100; i++) f.noteOn(60 + i % 10, 80);                                          // jam the ring full ...
    f.panic(); f.allNotesOff();                                                                       // ... panic and all-notes-off still get through
    run(f, 44100); CHECK(f.activeVoices() == 0 && !f.pedalDown, "panic is never lost, even with the ring full: every voice gone after 1 s (%d left)", f.activeVoices());
    static PianoManager g; g.init(); g.setPedal(true); for (int k = 40; k < 60; k++) g.noteOn((uint8_t)k, 80); run(g, 40); for (int k = 40; k < 60; k++) g.noteOff((uint8_t)k); run(g, 22050);
    CHECK(g.activeVoices() == 20, "pedal down: 20 released keys still ring"); g.allNotesOff(); run(g, 44100 * 2); CHECK(g.activeVoices() == 0, "all-notes-off (CC123) damps them even under the pedal (%d left)", g.activeVoices());
    static PianoManager o; o.init(); o.noteOn(60, 0); run(o, 20); CHECK(o.activeVoices() == 0, "note-on with velocity 0 is a note-off (nothing starts)");
}

static void test_steal_fades() {
    static PianoManager m; m.init(); for (int i = 0; i < N_POLY; i++) { m.noteOn((uint8_t)(40 + i), 80); run(m, 200); }
    CHECK(m.activeVoices() == N_POLY, "%d notes held: %d voices", N_POLY, m.activeVoices());
    uint32_t before = m.traceN; m.noteOn(100, 80); run(m, 20);
    int victim = -1, fresh = -1; for (int i = 0; i < N_VOICES; i++) { if (m._v[i].active && m._v[i].key == 40) victim = i; if (m._v[i].active && m._v[i].key == 100) fresh = i; }
    CHECK(victim >= 0 && m._v[victim].fast && m._v[victim].rel, "the oldest voice (key 40) is still there right after the steal, fading, not cut");
    CHECK(fresh >= 0 && fresh != victim && m.activeVoices() == N_POLY + 1, "the new note sits in a spare slot (%d voices now)", m.activeVoices());
    int hard = 0; for (uint32_t i = before; i < m.traceN; i++) hard += m.trace[i].kind == 6; CHECK(hard == 0, "no hard cut for a single steal");
    run(m, 2000); CHECK(m.activeVoices() == N_POLY && !m._v[victim].active, "15 ms later the victim is gone and %d voices sound", m.activeVoices());
    // a flood: 200 note-ons, 5 ms apart. Hard cuts are allowed only when every spare slot is busy fading.
    static PianoManager f; f.init(); for (int i = 0; i < 200; i++) { f.noteOn((uint8_t)(30 + i % 70), 80); run(f, 220); }
    int cuts = 0; for (uint32_t i = 0; i < f.traceN; i++) cuts += f.trace[i].kind == 6; CHECK(cuts == 0, "200 notes 5 ms apart: %d hard cuts (a victim fades in 15 ms, so 4 spare slots suffice at this rate)", cuts);
    CHECK(voicesSane(f), "voices sane after the flood");
}

static void test_keymask() {
    static PianoManager m; m.init(); m.noteOn(60, 80); m.noteOn(100, 80); run(m, 40);
    CHECK(m.keySounding(60) && m.keySounding(100) && !m.keySounding(61) && m.voicesNow == 2, "key mask shows the sounding keys");
    m.noteOff(60); m.noteOff(100); run(m, 44100 * 2); CHECK(!m.keySounding(60) && !m.keySounding(100) && m.voicesNow == 0 && m.keyMask[0] == 0 && m.keyMask[1] == 0 && m.keyMask[2] == 0, "...and clears when they have died away");
    CHECK(!m.keySounding(5) && !m.keySounding(200), "out-of-range keys are never 'sounding'");
}

static void test_torture() {
    static PianoManager m; m.init(); std::mt19937 rng(7); uint64_t ticks = 0; int bad = 0;
    for (int ev = 0; ev < 250000; ev++) {
        switch (rng() % 15) {
            case 14: m.setInstrument(rng() % PIANO_N_INSTRUMENTS); break;
            case 0: case 1: case 2: case 3: m.noteOn(rng() % 128, rng() % 128); break;
            case 4: case 5: case 6: m.noteOff(rng() % 128); break;
            case 7: m.setPedal(rng() & 1); break;
            case 8: m.setParam(rng() % N_PARAMS, rng() % 140); break;
            case 9: m.handleCC(rng() % 128, rng() % 128); break;
            case 10: m.pitchBend((int16_t)(rng() % 16384) - 8192); break;
            case 11: if (rng() % 30 == 0) m.panic(); break;
            case 12: if (rng() % 60 == 0) m.allNotesOff(); break;
            case 13: m.softPedal = rng() & 1; break;
        }
        const long n = rng() % 400; run(m, n); ticks += n;
        if (!voicesSane(m) || m.activeVoices() > N_VOICES) bad++;
    }
    CHECK(bad == 0, "voice invariants held over 250000 random events / %llu samples (%u voice events logged)", (unsigned long long)ticks, m.traceN);
}

// two threads: core 0 (this thread) plays and turns knobs; "core 1" renders flat out
static void test_threads() {
    static PianoManager m; m.init(); std::atomic<bool> stop{false}; std::atomic<long> samples{0}; std::atomic<int> bad{0};
    std::thread core1([&] { int16_t l, r; while (!stop) { for (int i = 0; i < 64; i++) m.tick(l, r); samples += 64; if (!std::isfinite(m._v[0].amp)) bad++; } });
    while (samples == 0) std::this_thread::yield();
    std::mt19937 rng(11);
    for (int ev = 0; ev < 60000; ev++) {
        if ((ev & 7) == 0) std::this_thread::yield();
        switch (rng() % 8) {
            case 0: case 1: m.noteOn(21 + rng() % 88, 1 + rng() % 127); break;
            case 2: case 3: m.noteOff(21 + rng() % 88); break;
            case 4: m.setPedal(rng() & 1); break;
            case 5: m.setParam(rng() % N_PARAMS, rng() % 128); break;
            case 6: m.pitchBend((int16_t)(rng() % 16384) - 8192); break;
            case 7: if (rng() % 200 == 0) m.panic(); break;
        }
    }
    while (samples < 2000000) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    stop = true; core1.join();
    CHECK(bad == 0 && samples >= 2000000 && m.activeVoices() <= N_VOICES, "two threads: 60000 control-side events against %ld rendered samples, no invalid state", (long)samples);
}

// every note-on must start exactly one voice. Hand-off: wait until the audio side has CONSUMED each event before the next.
static void test_delivery() {
    static PianoManager m; m.init(); std::atomic<bool> stop{false};
    std::thread core1([&] { int16_t l, r; while (!stop) for (int i = 0; i < 16; i++) m.tick(l, r); });
    auto consumed = [&] { for (int k = 0; k < 2000000; k++) { if (*(volatile uint8_t*)&m._rr == m._rw) return true; std::this_thread::yield(); } return false; };
    const int N = 1500; bool ok = true;
    for (int i = 0; i < N; i++) { m.noteOn(30 + i % 60, 80); ok &= consumed(); m.noteOff(30 + i % 60); ok &= consumed(); }
    stop = true; core1.join(); int starts = 0; for (uint32_t i = 0; i < m.traceN; i++) starts += m.trace[i].kind == 1;
    CHECK(ok && starts == N, "%d voice starts for %d note-ons, every event consumed", starts, N);
}

static void test_extremes() {
    static PianoManager m; m.init(); m.setParam(P_VOL, 127); m.setPedal(true); int16_t l, r; int peak = 0; bool fin = true;
    for (int rep = 0; rep < 4; rep++) { for (int k = KEY_LO; k <= KEY_HI; k++) m.noteOn((uint8_t)k, 127); for (int n = 0; n < 30000; n++) { m.tick(l, r); if (abs((int)l) > peak) peak = abs((int)l); if (abs((int)r) > peak) peak = abs((int)r); } }
    fin = voicesSane(m);
    CHECK(peak <= 32767 && fin, "88 notes at velocity 127, volume 127, pedal down, four times over: peak %d, stays within int16, voices sane", peak);
}

int main() {
    test_params(); test_zones(); test_limiter(); test_limiter_notes(); test_instruments_fx(); test_ring(); test_steal_fades(); test_keymask(); test_torture(); test_threads(); test_delivery(); test_extremes();
    printf(fails ? "\n%d ENGINE TESTS FAILED\n" : "\nALL ENGINE TESTS PASS\n", fails); return fails != 0;
}
