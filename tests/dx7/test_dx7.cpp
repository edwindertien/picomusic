// Engine tests on the REAL Dx7Manager / dx7_patch.h / midi_handler.h: algorithm table, packed voices, pitch, FM sidebands, envelopes,
// velocity, polyphony and stealing, pedal, SysEx bank, a two-thread race and a torture run. ASan + UBSan + float-cast-overflow.
#include <Arduino.h>
#include <thread>
#include <atomic>
#include <vector>
#include <random>
#include <cmath>
#include <array>
#include <string>
#define private public
#include "dx7_manager.h"
#undef private
#include "midi_handler.h"
volatile uint8_t g_cpu_load_pct = 0;
static int fails = 0;
#define CHECK(c, ...) do { if (c) printf("PASS %s\n", #c); else { printf("FAIL %s   ", #c); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static std::vector<float> run(Dx7Manager& m, long n) { std::vector<float> o((size_t)n); for (long i = 0; i < n; i++) { int16_t l, r; m.tick(l, r); o[(size_t)i] = (float)l / 32768.0f; } return o; }
static double peakOf(const std::vector<float>& v, size_t a, size_t b) { double p = 0; for (size_t i = a; i < b && i < v.size(); i++) p = std::max(p, (double)std::fabs(v[i])); return p; }
// amplitude of one frequency (single-bin DFT)
static double binAmp(const std::vector<float>& v, size_t a, size_t n, double hz) {
    double re = 0, im = 0; for (size_t i = 0; i < n; i++) { const double w = 2 * M_PI * hz * (double)(a + i) / SAMPLE_RATE; re += v[a + i] * std::cos(w); im += v[a + i] * std::sin(w); }
    return 2.0 * std::sqrt(re * re + im * im) / (double)n;
}
// a patch with the effects off, one chosen algorithm, all operators silent, ready to be filled in
static void cleanPatch(Dx7Manager& m, int alg) {
    Dx7Patch& p = m.patch; dx7Preset(0, p); p.alg = (uint8_t)alg; p.fb = 0; p.lpmd = 0; p.lamd = 0; p.lfd = 0;
    for (int i = 0; i < 6; i++) { p.op[i] = dxOff(); p.op[i].l[3] = 0; }
}
static void sustainOp(Dx7Op& o, uint8_t coarse, uint8_t ol) { o = dxOp(coarse, 0, ol, 99, 99, 99, 99, 99, 99, 99, 0); }
static void fresh(Dx7Manager& m) { m.init(); }      // the defaults have every effect off

static void test_tables() {
    const int expect[32] = { 2, 2, 2, 2, 3, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 3, 3, 4, 4, 4, 5, 5, 3, 3, 3, 4, 4, 5, 6 };   // carriers per algorithm on the DX7 chart
    for (int a = 0; a < 32; a++) CHECK(dx7Carriers(a) == expect[a], "algorithm %d carriers %d", a + 1, dx7Carriers(a));
    for (int a = 0; a < 32; a++) {                       // every operator is evaluated after the operators that feed it
        bool ok = true; int fbIn = 0, fbOut = 0;
        for (int o = 0; o < 6; o++) { const uint8_t f = DX7_ALG[a][o]; const int inb = (f >> 4) & 3; if (inb) { bool written = false; for (int q = 0; q < o; q++) if ((DX7_ALG[a][q] & 3) == inb) written = true; if (!written) ok = false; } fbIn += (f & DX_FB_IN) != 0; fbOut += (f & DX_FB_OUT) != 0; }
        CHECK(ok && fbIn >= 1 && fbOut >= 1, "algorithm %d: inputs are written before they are read; one feedback loop", a + 1);
    }
    CHECK(dx7Sol(0) == 0 && dx7Sol(19) == 46 && dx7Sol(20) == 48 && dx7Sol(99) == 127, "output level map");
    CHECK(dx7VelScale(127, 7) > 0 && dx7VelScale(1, 7) < -1000 && dx7VelScale(100, 0) == 0, "velocity scaling: loud adds, soft subtracts, sensitivity 0 = nothing");
    CHECK(dx7KeyScale(60, dxOp(1, 0, 99, 99, 99, 99, 99, 99, 99, 99, 0)) == 0, "no scaling without depths");
    Dx7Op o = dxOp(1, 0, 99, 99, 99, 99, 99, 99, 99, 99, 0); o.bp = 39; o.rd = 50; o.rc = 3; CHECK(dx7KeyScale(96, o) > 0 && dx7KeyScale(60, o) == 0 && dx7KeyScale(36, o) == 0, "+LIN right depth adds above the break point only");
    o.rc = 0; CHECK(dx7KeyScale(96, o) < 0, "-LIN right subtracts");
}
static void test_vmem() {
    std::mt19937 g(5); int bad = 0;
    for (int t = 0; t < 200; t++) {
        uint8_t v[128]; for (int i = 0; i < 128; i++) v[i] = (uint8_t)(g() & 0x7F);
        Dx7Patch p, q; dx7Unpack(v, p); uint8_t w[128]; dx7Pack(p, w); dx7Unpack(w, q);
        if (memcmp(&p, &q, sizeof p) != 0) bad++;
        uint8_t x[128]; dx7Pack(q, x); if (memcmp(w, x, 128) != 0) bad++;
    }
    CHECK(bad == 0, "pack(unpack(x)) is stable for 200 random voices (%d bad)", bad);
    for (int n = 0; n < DX_N_PRESETS; n++) { Dx7Patch p, q; dx7Preset(n, p); uint8_t w[128]; dx7Pack(p, w); dx7Unpack(w, q); CHECK(memcmp(&p, &q, sizeof p) == 0, "factory voice %d round-trips (%s)", n, p.name); }
    Dx7Patch p; dx7Preset(0, p); uint8_t w[128]; dx7Pack(p, w);
    CHECK(w[110] == 4 && (w[111] & 7) == 6 && w[14] == 79 && w[17 + 14] == 88 && w[5 * 17 + 14] == 99, "packed layout: algorithm, feedback, output levels in operator 6..1 order");
}
static void test_pitch() {
    static Dx7Manager m; fresh(m);
    cleanPatch(m, 31); sustainOp(m.patch.op[5], 1, 99);                    // algorithm 32, operator 1 only: a plain sine at the note's pitch
    for (int key : { 33, 45, 57, 69, 81, 93 }) {
        m.panic(); run(m, 1024);
        m.noteOn((uint8_t)key, 100); auto y = run(m, 22050);
        const double f = 440.0 * std::pow(2.0, (key - 69) / 12.0);
        const double a0 = binAmp(y, 4096, 16384, f), a1 = binAmp(y, 4096, 16384, f * 1.25), a2 = binAmp(y, 4096, 16384, f * 2);
        CHECK(a0 > 0.05 && a1 < a0 * 0.1 && a2 < a0 * 0.02, "key %d is a clean sine at %.1f Hz (%.3f, off %.4f, 2nd %.5f)", key, f, a0, a1, a2);
    }
    m.panic(); run(m, 1024); m.setParam(P_TUNE, 127); m.noteOn(69, 100); auto y = run(m, 22050);
    const double ft = 440.0 * std::pow(2.0, tuneCentsOf(127) / 1200.0);
    CHECK(binAmp(y, 4096, 16384, ft) > 4 * binAmp(y, 4096, 16384, 440.0), "tune knob at 127 = +%.1f cents", tuneCentsOf(127));
    m.setParam(P_TUNE, 64);
    m.panic(); run(m, 1024); m.pitchBend(8191); m.noteOn(69, 100); y = run(m, 22050);
    const double fb = 440.0 * std::pow(2.0, 2.0 * 8191 / 8192.0 / 12.0);
    CHECK(binAmp(y, 4096, 16384, fb) > 4 * binAmp(y, 4096, 16384, 440.0), "pitch bend up = +2 semitones (%.1f Hz)", fb);
    m.pitchBend(0);
    Dx7Op& o = m.patch.op[5]; o.coarse = 3; o.fine = 50; m.panic(); run(m, 1024); m.noteOn(69, 100); y = run(m, 22050);
    CHECK(binAmp(y, 4096, 16384, 440.0 * 4.5) > 0.05, "coarse 3 fine 50 = ratio 3 x 1.5 = 4.5 (fine is a fraction of the coarse ratio, as on the DX7)");
    o.coarse = 0; o.fine = 0; m.panic(); run(m, 1024); m.noteOn(69, 100); y = run(m, 22050);
    CHECK(binAmp(y, 4096, 16384, 220.0) > 0.05, "coarse 0 = ratio 0.5");
    o.fixed = 1; o.coarse = 2; o.fine = 0; m.panic(); run(m, 1024); m.noteOn(40, 100); y = run(m, 22050);
    CHECK(binAmp(y, 4096, 16384, 100.0) > 0.05, "fixed frequency 100 Hz, the same for every key");
}
static double J(int n, double x) { double s = 0, t = std::pow(x / 2, n); for (int k = 2; k <= n; k++) t /= k; for (int k = 0; k < 40; k++) { s += t * ((k & 1) ? -1 : 1); t *= (x / 2) * (x / 2) / ((k + 1) * (k + 1 + n)); } return s; }
static void test_fm() {
    static Dx7Manager m; fresh(m);
    // algorithm 5, operator 2 (ratio 1) modulates operator 1 (ratio 1): sidebands at f +- f; |J1| / |J0| of the modulation index.
    //  sin(w t + b sin(w t)) = sum J_k(b) sin((1+k) w t); with equal frequencies the orders fold: harmonic h = J(h-1) + (-1)^h J(h+1).
    for (int olMod : { 40, 50, 60, 70, 80, 90 }) {
        cleanPatch(m, 4); sustainOp(m.patch.op[5], 1, 99); sustainOp(m.patch.op[4], 1, (uint8_t)olMod);
        m.panic(); run(m, 1024); m.noteOn(69, 127); auto y = run(m, 30000);
        const int A = 4032 + (dx7Sol(olMod) << 5) - 4256;                                         // vel sens 0: the level offset is just the output level
        const double beta = 2.0 * std::pow(2.0, (A - 3840) / 256.0) * 2.0 * M_PI;               // amplitude 2^((A-3840)/256) x 2 cycles = index in radians
        const double h1 = J(0, beta) - J(2, beta), h2 = J(1, beta) + J(3, beta), h3 = J(2, beta) - J(4, beta);
        const double a1 = binAmp(y, 8192, 16384, 440), a2 = binAmp(y, 8192, 16384, 880), a3 = binAmp(y, 8192, 16384, 1320);
        const double g1 = 1.0 * 0.186, scale = a1 / (std::fabs(h1) > 1e-3 ? std::fabs(h1) : 1.0);
        (void)g1; (void)scale;
        const double ref = volGainOf(PARAM_DEFAULT[P_VOL]);                                          // full-level carrier amplitude
        CHECK(std::fabs(a1 - ref * std::fabs(h1)) < 0.03 * ref && std::fabs(a2 - ref * std::fabs(h2)) < 0.03 * ref && std::fabs(a3 - ref * std::fabs(h3)) < 0.03 * ref,
              "modulator OL %d: index %.2f rad; harmonics 1..3 expected %.3f %.3f %.3f, measured %.3f %.3f %.3f", olMod, beta, ref * std::fabs(h1), ref * std::fabs(h2), ref * std::fabs(h3), a1, a2, a3);
    }
    // a modulator with OL 0 changes nothing; the carrier alone is a sine
    cleanPatch(m, 4); sustainOp(m.patch.op[5], 1, 99); sustainOp(m.patch.op[4], 1, 0);
    m.panic(); run(m, 1024); m.noteOn(69, 127); auto y = run(m, 22050);
    CHECK(binAmp(y, 4096, 16384, 880) < 0.002, "silent modulator: no sidebands");
    // brightness knob raises the modulation index
    cleanPatch(m, 4); sustainOp(m.patch.op[5], 1, 99); sustainOp(m.patch.op[4], 1, 60);
    m.setParam(P_BRIT, 64); m.panic(); run(m, 1024); m.noteOn(69, 127); y = run(m, 22050); const double s64 = binAmp(y, 4096, 16384, 880);
    m.setParam(P_BRIT, 110); m.panic(); run(m, 1024); m.noteOn(69, 127); y = run(m, 22050); const double s110 = binAmp(y, 4096, 16384, 880);
    CHECK(s110 > 2.0 * s64, "bright 110 gives a brighter tone than 64 (%.4f vs %.4f)", s110, s64); m.setParam(P_BRIT, 64);
    // feedback: operator 6 modulating itself adds harmonics
    cleanPatch(m, 31); m.patch.fb = 7; sustainOp(m.patch.op[0], 1, 99);
    m.panic(); run(m, 1024); m.noteOn(57, 127); y = run(m, 22050); const double f = 220.0;
    CHECK(binAmp(y, 4096, 16384, f * 2) > 0.03 && binAmp(y, 4096, 16384, f * 3) > 0.015, "feedback 7: 2nd and 3rd harmonics (%.3f, %.3f)", binAmp(y, 4096, 16384, f * 2), binAmp(y, 4096, 16384, f * 3));
    m.patch.fb = 0; m.panic(); run(m, 1024); m.noteOn(57, 127); y = run(m, 22050);
    CHECK(binAmp(y, 4096, 16384, f * 2) < 0.002, "feedback 0: pure sine");
}
static void test_envelope() {
    static Dx7Manager m; fresh(m);
    cleanPatch(m, 31); m.patch.op[5] = dxOp(1, 0, 99, 70, 60, 50, 60, 99, 95, 85, 0);                // attack to 99, decay to 95, then 85 sustained (-10 dB), release to 0
    m.noteOn(69, 127); auto y = run(m, 44100 * 3);
    const double p0 = peakOf(y, 0, 200), pa = peakOf(y, 0, 60000), ps = peakOf(y, 110000, 120000);
    CHECK(p0 < pa * 0.5, "the attack rises (start %.3f, later %.3f)", p0, pa);
    CHECK(ps < pa && std::fabs(ps / pa - 0.30) < 0.06, "decays to the sustain level L3 = 85, which is 0.30 of the top (%.3f -> %.3f)", pa, ps);
    const double ps2 = peakOf(y, 120000, 132000); CHECK(std::fabs(ps2 - ps) < 0.03 * ps, "sustain does not drift (%.4f vs %.4f)", ps, ps2);
    m.noteOff(69); auto r = run(m, 44100 * 4);
    CHECK(peakOf(r, 0, 300) > ps * 0.5 && peakOf(r, 44100 * 3, 44100 * 4) < 0.002, "release fades out (end peak %.5f)", peakOf(r, 44100 * 3, 44100 * 4));
    CHECK(m.activeVoices() == 0, "the voice is freed after the release");
    // rate: higher attack rate = faster
    double t90[2]; int rates[2] = { 55, 85 };
    for (int k = 0; k < 2; k++) { cleanPatch(m, 31); m.patch.op[5] = dxOp(1, 0, 99, (uint8_t)rates[k], 99, 99, 99, 99, 99, 99, 0); m.panic(); run(m, 2048); m.noteOn(69, 127); auto z = run(m, 44100 * 2); const double top = peakOf(z, 60000, 80000); size_t i = 0; while (i < z.size() && std::fabs(z[i]) < 0.9 * top) i++; t90[k] = (double)i; }
    CHECK(t90[1] < t90[0], "attack rate 85 reaches 90%% sooner than 55 (%.0f vs %.0f samples)", t90[1], t90[0]);
    // velocity: louder with higher velocity when sensitivity > 0, no effect when 0
    double pk[3];
    for (int k = 0; k < 3; k++) { cleanPatch(m, 31); m.patch.op[5] = dxOp(1, 0, 99, 99, 99, 99, 99, 99, 99, 99, 0, k == 2 ? 0 : 5); m.panic(); run(m, 2048); m.noteOn(69, k == 1 ? 30 : 120); auto z = run(m, 20000); pk[k] = peakOf(z, 8000, 20000); }
    CHECK(pk[0] > 1.5 * pk[1], "velocity 120 is much louder than 30 with sensitivity 5 (%.3f vs %.3f)", pk[0], pk[1]);
    m.patch.op[5].kvs = 0; m.panic(); run(m, 2048); m.noteOn(69, 30); auto z = run(m, 20000); const double a = peakOf(z, 8000, 20000); m.panic(); run(m, 2048); m.noteOn(69, 120); z = run(m, 20000);
    CHECK(std::fabs(peakOf(z, 8000, 20000) - a) < 0.01 * a, "sensitivity 0: velocity ignored");
    // level: one full carrier at default volume
    cleanPatch(m, 31); sustainOp(m.patch.op[5], 1, 99); m.panic(); run(m, 2048); m.noteOn(69, 100); z = run(m, 20000);
    const double want = volGainOf(PARAM_DEFAULT[P_VOL]);
    CHECK(std::fabs(peakOf(z, 8000, 20000) - want) < 0.03 * want, "a full-level carrier peaks at the volume gain %.4f (measured %.4f)", want, peakOf(z, 8000, 20000));
    // output level: 99 -> 79 is a known number of dB (units: 1/256 octave)
    double lv[2]; int ol[2] = { 99, 79 };
    for (int k = 0; k < 2; k++) { cleanPatch(m, 31); sustainOp(m.patch.op[5], 1, (uint8_t)ol[k]); m.panic(); run(m, 2048); m.noteOn(69, 100); z = run(m, 20000); lv[k] = peakOf(z, 8000, 20000); }
    const double wantDb = -6.0206 * ((dx7Sol(99) - dx7Sol(79)) * 32.0 / 256.0), gotDb = 20 * std::log10(lv[1] / lv[0]);
    CHECK(std::fabs(gotDb - wantDb) < 0.3, "OL 99 -> 79 = %.2f dB (expected %.2f)", gotDb, wantDb);
}
static void test_voices() {
    static Dx7Manager m; fresh(m); dx7Preset(0, m.patch);
    for (int k = 0; k < 20; k++) { m.noteOn((uint8_t)(48 + k), 100); run(m, 700); }
    int live = 0; for (int i = 0; i < N_VOICES; i++) if (m._v[i].active && !m._v[i].fast) live++;
    CHECK(live <= N_POLY && m.activeVoices() <= N_VOICES, "20 notes: %d live voices (limit %d), %d in the pool", live, N_POLY, m.activeVoices());
    run(m, 44100); int live2 = 0; for (int i = 0; i < N_VOICES; i++) if (m._v[i].active && !m._v[i].fast) live2++;
    CHECK(live2 == N_POLY, "after the fades exactly N_POLY voices remain (%d)", live2);
    bool oldGone = true; for (int i = 0; i < N_VOICES; i++) if (m._v[i].active && !m._v[i].fast && m._v[i].key < 48 + 20 - N_POLY) oldGone = false;
    CHECK(oldGone, "the oldest notes were stolen");
    m.panic(); run(m, 44100); CHECK(m.activeVoices() == 0, "panic silences everything");
    // sustain pedal
    cleanPatch(m, 31); m.patch.op[5] = dxOp(1, 0, 99, 99, 99, 99, 70, 99, 99, 99, 0);
    m.setPedal(true); m.noteOn(60, 100); run(m, 4000); m.noteOff(60); run(m, 44100);
    CHECK(m.activeVoices() == 1 && peakOf(run(m, 4000), 0, 4000) > 0.05, "pedal down: the note rings after the key is released");
    m.setPedal(false); run(m, 44100 * 3); CHECK(m.activeVoices() == 0, "pedal up releases it");
    // re-striking a key does not stack voices forever
    for (int i = 0; i < 40; i++) { m.noteOn(60, 100); run(m, 500); }
    CHECK(m.activeVoices() <= 3, "40 re-strikes of one key leave %d voices", m.activeVoices());
    m.handleCC(123, 0); run(m, 44100 * 4); CHECK(m.activeVoices() == 0, "CC123 releases all notes");
    // CC map
    CHECK(m.handleCC(1, 100) && m.pv[P_MOD] == 100 && m.handleCC(7, 90) && m.pv[P_VOL] == 90 && m.handleCC(91, 50) && m.pv[P_REV] == 50 && !m.handleCC(99, 1), "CC map (AKAI K1 = mod, K7 = volume, CC91 = reverb, unknown ignored)");
    m.setParam(P_VOL, 100); m.setParam(P_MOD, 0);
}
static void test_lfo() {
    static Dx7Manager m; fresh(m);
    cleanPatch(m, 31); m.patch.op[5] = dxOp(1, 0, 99, 99, 99, 99, 99, 99, 99, 99, 0, 0); m.patch.lfs = 40; m.patch.lfw = 4; m.patch.lpms = 7; m.patch.lpmd = 30; m._pushGlobals();
    m.noteOn(69, 100); auto y = run(m, 44100 * 2);
    // vibrato: the instantaneous frequency wanders, so the energy at exactly 440 Hz is lower than for an unmodulated note
    const double a = binAmp(y, 8192, 32768, 440);
    m.patch.lpmd = 0; m._pushGlobals(); m.panic(); run(m, 4096); m.noteOn(69, 100); y = run(m, 44100 * 2); const double b = binAmp(y, 8192, 32768, 440);
    CHECK(a < 0.85 * b, "pitch LFO smears the tone (%.3f vs %.3f)", a, b);
    // amplitude modulation: the envelope of the signal moves
    m.patch.lamd = 99; m.patch.op[5].ams = 3; m.patch.lfs = 30; m._pushGlobals(); m.panic(); run(m, 4096); m.noteOn(69, 100); y = run(m, 44100 * 2);
    double lo = 1e9, hi = 0; for (size_t i = 10000; i + 1000 < y.size(); i += 1000) { const double p = peakOf(y, i, i + 1000); lo = std::min(lo, p); hi = std::max(hi, p); }
    CHECK(hi > 3.0 * lo, "amplitude LFO (AMS 3, depth 99) swings the level (%.3f .. %.3f)", lo, hi);
    m.patch.lamd = 0; m.patch.op[5].ams = 0; m._pushGlobals();
    for (int w = 0; w < 6; w++) { m.patch.lfw = (uint8_t)w; m.patch.lpmd = 20; m.patch.lpms = 4; m._pushGlobals(); m.panic(); run(m, 4096); m.noteOn(69, 100); auto z = run(m, 44100); CHECK(peakOf(z, 0, z.size()) < 1.0 && std::isfinite(z[1000]), "LFO wave %d runs", w); }
    m.patch.lpmd = 0; m._pushGlobals();
}
static void test_sysex() {
    // a complete bank dump through the REAL parsers: UART bytes and USB-MIDI packets (cin 4 / 7), and the failure cases
    static Dx7Manager m; fresh(m);
    static int got = 0; static uint8_t first[128];
    std::vector<uint8_t> body(4096); std::mt19937 g(9);
    for (int v = 0; v < 32; v++) { Dx7Patch p; dx7Preset(v + 3, p); p.alg = (uint8_t)v; dx7Pack(p, &body[128 * v]); }
    int sum = 0; for (uint8_t b : body) sum += b; const uint8_t cs = (uint8_t)((-sum) & 0x7F);
    std::vector<uint8_t> msg = { 0xF0, 0x43, 0x00, 0x09, 0x20, 0x00 }; msg.insert(msg.end(), body.begin(), body.end()); msg.push_back(cs); msg.push_back(0xF7);
    (void)got; (void)first;
    extern int g_banks; extern uint8_t g_bankFirst[128];
    g_banks = 0; for (uint8_t b : msg) _parseUartByte(b);
    CHECK(g_banks == 1 && memcmp(g_bankFirst, &body[0], 128) == 0, "UART: a 4104-byte bank dump is delivered once and intact");
    g_banks = 0; { std::vector<uint8_t> bad = msg; bad[bad.size() - 2] ^= 1; for (uint8_t b : bad) _parseUartByte(b); } CHECK(g_banks == 0, "a wrong checksum is rejected");
    g_banks = 0; { std::vector<uint8_t> cut(msg.begin(), msg.begin() + 2000); for (uint8_t b : cut) _parseUartByte(b); _parseUartByte(0x90); _parseUartByte(60); _parseUartByte(100); } CHECK(g_banks == 0, "a bank cut short by a note is dropped");
    g_banks = 0; { std::vector<uint8_t> inter; for (size_t i = 0; i < msg.size(); i++) { inter.push_back(msg[i]); if (i % 500 == 7) inter.push_back(0xF8); } for (uint8_t b : inter) _parseUartByte(b); } CHECK(g_banks == 1, "real-time bytes inside the dump do not disturb it");
    g_banks = 0; { std::vector<uint8_t> other = msg; other[3] = 0x00; for (uint8_t b : other) _parseUartByte(b); } CHECK(g_banks == 0, "another SysEx format (not 32 voices) is ignored");
    g_banks = 0; { std::vector<uint8_t> big = msg; big.insert(big.begin() + 100, 5000, 0x11); for (uint8_t b : big) _parseUartByte(b); } CHECK(g_banks == 0, "an oversized message is ignored and the parser recovers");
    uint8_t pk[4]; g_banks = 0;
    for (size_t i = 0; i < msg.size(); i += 3) {
        const size_t n = std::min<size_t>(3, msg.size() - i); pk[0] = (uint8_t)(n == 3 ? (i + 3 >= msg.size() ? 0x07 : 0x04) : n == 2 ? 0x06 : 0x05); pk[1] = msg[i]; pk[2] = n > 1 ? msg[i + 1] : 0; pk[3] = n > 2 ? msg[i + 2] : 0; _dispatchUsbPacket(pk); }
    CHECK(g_banks == 1 && memcmp(g_bankFirst, &body[0], 128) == 0, "USB-MIDI packets (CIN 4 .. 7): the same bank");
    // the engine takes the bank
    m.loadBank(&body[0]);
    CHECK(m.nBank == 32 && m.bank[5].alg == 5 && m.bank[31].alg == 31, "loadBank unpacks 32 voices");
    m.setPatch(7); CHECK(m.patch.alg == 7 && m.patchNo == 7, "program change selects a loaded voice");
    m.setPatch(40); CHECK(m.patchNo == 40 % 32, "program numbers wrap");
}
static void test_torture() {
    static Dx7Manager m; fresh(m); std::mt19937 g(77); long samples = 0;
    for (int round = 0; round < 3000; round++) {
        switch (g() % 9) {
            case 0: case 1: case 2: m.noteOn((uint8_t)(12 + g() % 109), (uint8_t)(1 + g() % 127)); break;
            case 3: case 4: m.noteOff((uint8_t)(12 + g() % 109)); break;
            case 5: m.setParam((uint8_t)(g() % P_WAH), (uint8_t)(g() % 128)); break;
            case 6: m.setPatch((uint8_t)(g() % 12)); break;
            case 7: m.setPedal(g() & 1); break;
            default: m.pitchBend((int16_t)((int)(g() % 16384) - 8192)); break;
        }
        for (int i = 0; i < 500; i++) { int16_t l, r; m.tick(l, r); samples++; }
        if (round % 500 == 499) { m.panic(); }
    }
    bool sane = true; for (int i = 0; i < N_VOICES; i++) { const Dx7Voice& v = m._v[i]; if (!v.active) continue; for (int o = 0; o < 6; o++) if (!std::isfinite(v.amp[o]) || !std::isfinite(v.lv[o]) || v.amp[o] < 0 || v.amp[o] > 8) sane = false; }
    CHECK(sane, "after %ld samples of random play all voice state is finite", samples);
    // every factory voice, every key: finite, bounded, and it ends
    for (int n = 0; n < DX_N_PRESETS; n++) { m.panic(); run(m, 2048); m.setPatch((uint8_t)n); double pk = 0; for (int key : { 24, 48, 72, 96, 120 }) { m.noteOn((uint8_t)key, 127); auto y = run(m, 44100 / 2); pk = std::max(pk, peakOf(y, 0, y.size())); m.noteOff((uint8_t)key); } CHECK(pk > 0.01 && pk <= 1.0, "factory voice %d (%s): audible (%.3f) and below full scale", n, m.patch.name, pk); }
    m.panic(); auto tail = run(m, 44100 * 2); CHECK(peakOf(tail, 44100, tail.size()) < 1e-3 && m.activeVoices() == 0, "everything ends after a panic");
}
static void test_race() {                      // core 0 hammering while "core 1" ticks: no crash, no sanitizer report (TSan runs in the nightly profile; here ASan/UBSan)
    static Dx7Manager m; fresh(m); std::atomic<bool> stop(false);
    std::thread audio([&] { while (!stop) { int16_t l, r; m.tick(l, r); } });
    std::mt19937 g(3);
    for (int i = 0; i < 20000; i++) { m.noteOn((uint8_t)(24 + g() % 80), 100); if (i % 3 == 0) m.noteOff((uint8_t)(24 + g() % 80)); if (i % 50 == 0) m.setParam((uint8_t)(g() % P_WAH), (uint8_t)(g() % 128)); if (i % 700 == 0) m.setPatch((uint8_t)(g() % 12)); if (i % 1000 == 0) std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
    stop = true; audio.join(); CHECK(true, "two-thread hammering finished");
}
int main() {
    test_tables(); test_vmem(); test_pitch(); test_fm(); test_envelope(); test_voices(); test_lfo(); test_sysex(); test_torture(); test_race();
    printf(fails ? "\nDX7 ENGINE TESTS: %d FAILED\n" : "\nALL DX7 ENGINE TESTS PASS\n", fails); return fails ? 1 : 0;
}
int g_banks = 0; uint8_t g_bankFirst[128];
void midi_note_on(uint8_t, uint8_t) {} void midi_note_off(uint8_t) {} void midi_pitch_bend(int16_t) {} void midi_cc(uint8_t, uint8_t) {} void midi_program_change(uint8_t) {} void midi_realtime(uint8_t) {}
void midi_sysex_bank(const uint8_t* d) { g_banks++; memcpy(g_bankFirst, d, 128); }
