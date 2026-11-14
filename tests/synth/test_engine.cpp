// Host tests for the synth engine (the REAL source files, with mocked hardware headers).
#include <Arduino.h>
#include <vector>
#include <set>
#include <random>
#include <thread>
#include <atomic>
#define private public            // test-only: inspect core-0 bookkeeping
#include "synth_manager.h"
#undef private

static SynthManager sm;
static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { fails++; printf("  FAIL (line %d): %s   ", __LINE__, #cond); printf(__VA_ARGS__); printf("\n"); } } while (0)
#define SECTION(name) printf("\n== %s ==\n", name)

struct Stats { double peak = 0, sumsq = 0; long n = 0; double rms() const { return n ? sqrt(sumsq / n) : 0; } };
static Stats run(int n) {
    Stats st;
    for (int i = 0; i < n; i++) {
        int16_t o = sm.tick();
        double a = fabs((double)o); if (a > st.peak) st.peak = a;
        st.sumsq += (double)o * o; st.n++;
    }
    return st;
}
static inline int secs(double s) { return (int)(s * SAMPLE_RATE); }

// single-thread test: while "core 0 waits", core 1 keeps running
static void delayHook(uint32_t us) { for (int i = 0; i < (int)(us * 44100ull / 1000000ull) + 1; i++) sm.tick(); }

static void fresh() { sm.init(); sm.setGlide(false); }

// ---- structural invariants of core-0 bookkeeping ------------------------------
static bool invariants(const char* when) {
    bool ok = true;
    int counted = 0, live = 0;
    for (int i = 0; i < SYNTH_POOL; i++) {
        if (sm._voices[i].live) { live++; if (!sm._dying[i]) counted++; }
        if (sm._voices[i].live && sm._held[i]) {
            if (sm._dying[i])                                   { printf("  [%s] V%d held and dying\n", when, i); ok = false; }
            if (sm._note[i] >= 128 || sm._map[sm._note[i]] != i) { printf("  [%s] V%d held note %u not mapped to it\n", when, i, sm._note[i]); ok = false; }
        }
    }
    for (int n = 0; n < 128; n++) {
        const uint8_t v = sm._map[n];
        if (v == SynthManager::NONE) continue;
        if (v >= SYNTH_POOL || !sm._voices[v].live || !sm._held[v] || sm._note[v] != n) {
            printf("  [%s] map[%d]=%u points at a voice that is not live/held/for that note\n", when, n, v); ok = false; }
    }
    if (counted > MAX_ACTIVE_VOICES) { printf("  [%s] counted %d > cap %d\n", when, counted, MAX_ACTIVE_VOICES); ok = false; }
    if (live > SYNTH_POOL)           { printf("  [%s] live %d > pool\n", when, live); ok = false; }
    return ok;
}

// ============================================================================
static void test_params() {
    SECTION("parameter table");
    for (int p = 0; p < N_PATCHES; p++) {
        CHECK(PATCHES[p][P_VOL] == 80,   "patch %d: VOL misaligned (%d)", p, PATCHES[p][P_VOL]);
        CHECK(PATCHES[p][P_GLIDE] == 20, "patch %d: GLD misaligned (%d)", p, PATCHES[p][P_GLIDE]);
        CHECK(PATCHES[p][P_LFOWAVE] == 0,"patch %d: LFOW misaligned", p);
        for (int i = 0; i < N_PARAMS; i++) CHECK(PATCHES[p][i] <= 127, "patch %d param %d > 127", p, i);
    }
    for (int i = 0; i < N_PARAMS; i++) for (int j = i + 1; j < N_PARAMS; j++)
        CHECK(PARAM_INFO[i].cc != PARAM_INFO[j].cc, "params %s and %s share CC %d", PARAM_INFO[i].cmd, PARAM_INFO[j].cmd, PARAM_INFO[i].cc);
    // every knob has a unique grid cell
    std::set<int> cells;
    for (int i = 0; i < N_KNOBS; i++) {
        CHECK(PARAM_INFO[i].col < 8 && PARAM_INFO[i].row < 3, "knob %d has no cell", i);
        CHECK(cells.insert(PARAM_INFO[i].row * 8 + PARAM_INFO[i].col).second, "knob %s shares a grid cell", PARAM_INFO[i].cmd);
    }
    // AKAI knobs
    CHECK(paramForCC(1) == P_CUTOFF && paramForCC(2) == P_RES && paramForCC(3) == P_FENV && paramForCC(4) == P_AATK, "AKAI K1-K4");
    CHECK(paramForCC(5) == P_AREL && paramForCC(6) == P_DRIVE && paramForCC(7) == P_VOL && paramForCC(8) == P_OSCMIX, "AKAI K5-K8");
    for (int i = 0; i < N_PARAMS; i++) CHECK(paramForCC(PARAM_INFO[i].cc) == i, "CC %d does not map back to %s", PARAM_INFO[i].cc, PARAM_INFO[i].cmd);
    CHECK(paramForCC(0) == 0xFF && paramForCC(100) == 0xFF, "unmapped CCs must return 0xFF");

    fresh();
    CHECK(sm.handleCC(1, 127) && sm.pv[P_CUTOFF] == 127, "CC1 -> cutoff");
    CHECK(sm.handleCC(77, 127) && sm.pv[P_MOD] == 127,   "CC77 -> mod depth (not shadowed by AKAI)");
    CHECK(sm.handleCC(76, 10) && sm.pv[P_LFORATE] == 10, "CC76 -> lfo rate");
    CHECK(sm.handleCC(79, 33) && sm.pv[P_GLIDE] == 33,   "CC79 -> glide time");
    CHECK(sm.handleCC(65, 127) && sm.glide(),            "CC65 -> glide on");
    CHECK(sm.handleCC(65, 0) && !sm.glide(),             "CC65 -> glide off");
    CHECK(!sm.handleCC(100, 5),                          "unused CC reported unused");
    // the old bug: MOD knob went through the AKAI remap to cutoff
    sm.setParam(P_CUTOFF, 55); sm.setParam(P_MOD, 99);
    CHECK(sm.pv[P_CUTOFF] == 55 && sm.pv[P_MOD] == 99,   "setParam(MOD) must not touch cutoff");
    sm.loadPatch(1); CHECK(!strcmp(sm.patchName, "Pad"), "patch name"); sm.setParam(P_RES, 3); CHECK(!strcmp(sm.patchName, "Custom"), "edit -> Custom");
    // derived values
    sm.loadPatch(0);
    CHECK(fabsf(sm.ep.cutHz - 30.0f * powf(2.0f, 60.0f / 18.0f)) < 0.5f, "cutHz derivation");
    CHECK(sm.ep.wave == WAVE_SAW, "init patch is a saw");
    sm.setParam(P_WAVE, 112); CHECK(sm.ep.wave == WAVE_SQUARE, "wave 112 -> square");
    sm.setParam(P_DETUNE, 127); CHECK(sm.ep.detuneRatio > 1.028f && sm.ep.detuneRatio < 1.030f, "+50 cents ratio %f", sm.ep.detuneRatio);
    sm.setParam(P_DETUNE, 64);  CHECK(fabsf(sm.ep.detuneRatio - 1.0f) < 1e-6f, "0 cents");
    printf("  ok\n");
}

static void test_basic() {
    SECTION("single note: attack, sustain, release, voice returned to pool");
    fresh();
    sm.setParam(P_FENV, 0); sm.setParam(P_CUTOFF, 100);       // static filter so we measure the AMP envelope
    sm.noteOn(60, 100);
    CHECK(sm.activeCount() == 1 && invariants("on"), "one voice live");
    Stats early = run(30);                                    // first 0.7 ms
    run(300 - 30);
    Stats late  = run(300);
    Stats sus   = run(secs(0.5));
    printf("  peak |y|: first 30 samples %.0f, samples 300-600 %.0f, sustained %.0f\n", early.peak, late.peak, sus.peak);
    CHECK(early.peak < 0.5 * late.peak, "output must ramp up during the attack");
    CHECK(sus.rms() > 300 && sus.peak < 32767, "audible and not clipping");
    int v = -1; for (int i = 0; i < SYNTH_POOL; i++) if (sm._voices[i].live) v = i;
    CHECK(sm._voices[v].aenv.state == SynthEnv::SUSTAIN && fabsf(sm._voices[v].aenv.level - sm.ep.aSus) < 1e-4f, "envelope sits at the sustain level");
    sm.noteOff(60);
    CHECK(invariants("off"), "after noteOff");
    run(secs(3.5));
    CHECK(sm.activeCount() == 0, "voice must be handed back after release (live=%d)", sm.activeCount());
    Stats z = run(1000);
    CHECK(z.peak == 0, "silence after release (peak %.0f)", z.peak);
}

static void test_cap_newest_wins() {
    SECTION("polyphony cap: the new note always wins; oldest are stolen");
    fresh();
    g_delay_us_hook = delayHook;
    for (int n = 36; n < 56; n++) {                          // 20 notes, cap is 12
        sm.noteOn(n, 100);
        CHECK(sm._map[n] != SynthManager::NONE, "note %d must start immediately", n);
        CHECK(invariants("burst"), "invariants after note %d", n);
        run(100);
    }
    run(secs(0.1));
    printf("  after the burst: %u live voices\n", sm.activeCount());
    CHECK(sm.activeCount() == MAX_ACTIVE_VOICES, "settles to exactly the cap (%u)", sm.activeCount());
    for (int n = 36; n < 44; n++)  CHECK(sm._map[n] == SynthManager::NONE, "oldest note %d should have been stolen", n);
    for (int n = 44; n < 56; n++)  CHECK(sm._map[n] != SynthManager::NONE, "newest note %d must still be held", n);
    g_delay_us_hook = nullptr;
}

static void test_prefers_released() {
    SECTION("stealing prefers voices whose key is already up (and keeps held keys)");
    fresh(); sm.setParam(P_AREL, 110);                       // long release tails
    for (int n = 60; n < 72; n++) { sm.noteOn(n, 100); run(50); }
    run(secs(0.2));
    for (int n = 60; n < 66; n++) sm.noteOff(n);             // 6 keys up -> 6 tails
    run(secs(0.01));
    CHECK(sm.countedVoices() == 12, "6 held + 6 releasing = cap");
    for (int n = 80; n < 86; n++) { sm.noteOn(n, 100); run(20); CHECK(invariants("steal"), "inv"); }
    for (int n = 66; n < 72; n++) CHECK(sm._map[n] != SynthManager::NONE && sm._held[sm._map[n]], "held key %d must survive", n);
    for (int n = 80; n < 86; n++) CHECK(sm._map[n] != SynthManager::NONE, "new key %d plays", n);
    printf("  ok: 6 tails faded, 6 held keys untouched, 6 new notes playing\n");
}

static void test_pool_exhaustion() {
    SECTION("burst larger than the spare slots: waits for a fading voice, never drops the note");
    fresh(); g_delay_us_hook = delayHook;
    for (int n = 40; n < 52; n++) { sm.noteOn(n, 100); run(40); }        // 12 held
    run(secs(0.1));
    uint64_t t0 = g_mock_us;
    for (int n = 70; n < 79; n++) {                                      // 9 new notes in the same instant
        sm.noteOn(n, 100);
        CHECK(sm._map[n] != SynthManager::NONE, "burst note %d was dropped!", n);
        CHECK(invariants("burst"), "inv");
    }
    printf("  9 simultaneous steals with 8 spare slots: core 0 waited %.2f ms for the 9th\n", (g_mock_us - t0) / 1000.0);
    CHECK(g_mock_us - t0 < 8000, "wait must stay within the bound");
    run(secs(0.1));
    CHECK(sm.activeCount() <= MAX_ACTIVE_VOICES, "settles");
    g_delay_us_hook = nullptr;
}

static void test_same_note() {
    SECTION("same note repeatedly / duplicates / stale note-offs");
    fresh(); g_delay_us_hook = delayHook;
    std::mt19937 rng(1);
    for (int i = 0; i < 3000; i++) {
        sm.noteOn(60, 100);
        CHECK(sm._map[60] != SynthManager::NONE, "on");
        run(rng() % 400);
        sm.noteOff(60);
        CHECK(sm._map[60] == SynthManager::NONE, "off");
        run(rng() % 400);
        if (!invariants("repeat")) break;
    }
    run(secs(4));
    CHECK(sm.activeCount() == 0, "no stuck voices after 3000 retriggers (live=%u)", sm.activeCount());

    sm.noteOn(64, 100); sm.noteOn(64, 100);                  // duplicate note-on, no note-off in between
    run(secs(0.05));
    int held = 0; for (int i = 0; i < SYNTH_POOL; i++) if (sm._voices[i].live && sm._held[i]) held++;
    CHECK(held == 1, "duplicate note-on keeps exactly one held voice (%d)", held);
    sm.noteOff(64); run(secs(4)); CHECK(sm.activeCount() == 0, "released");

    // stale note-off for a stolen note must not release anybody else
    fresh();
    for (int n = 0; n < 13; n++) { sm.noteOn(n, 100); run(40); }
    run(secs(0.1));
    int before = 0; for (int i = 0; i < SYNTH_POOL; i++) before += (sm._voices[i].live && sm._held[i]);
    sm.noteOff(0);                                           // note 0 was stolen by note 12
    int after = 0;  for (int i = 0; i < SYNTH_POOL; i++) after  += (sm._voices[i].live && sm._held[i]);
    CHECK(before == after, "stale note-off changed held count %d -> %d", before, after);
    g_delay_us_hook = nullptr;
    printf("  ok\n");
}

static void test_torture() {
    SECTION("random torture (single thread): 250k events incl. glide, panic, patches, bend, CC floods");
    fresh(); g_delay_us_hook = delayHook;
    std::mt19937 rng(12345);
    std::set<int> down;
    long steals = 0, maxLive = 0; uint64_t events = 0;
    for (int ev = 0; ev < 250000; ev++) {
        const int r = rng() % 1000;
        if (r < 300) {                                       // note on
            int n = 24 + rng() % 90;
            const int cb = sm.countedVoices();
            sm.noteOn(n, 1 + rng() % 127); down.insert(n);
            if (cb >= MAX_ACTIVE_VOICES && !sm.ep.glideOn) steals++;
            if (!sm.ep.glideOn) CHECK(sm._map[n] != SynthManager::NONE, "note %d not started", n);
        } else if (r < 580) {                                // note off of a held key
            if (!down.empty()) { auto it = down.begin(); std::advance(it, rng() % down.size()); sm.noteOff(*it); down.erase(it); }
        } else if (r < 640) { sm.noteOff(rng() % 128);       // stray note-off
        } else if (r < 840) { sm.setParam(rng() % N_PARAMS, rng() % 128);
        } else if (r < 900) { sm.handleCC(rng() % 128, rng() % 128);
        } else if (r < 950) { sm.pitchBend((int16_t)((int)(rng() % 16384) - 8192));
        } else if (r < 960) { sm.loadPatch(rng() % 3);
        } else if (r < 975) { sm.setGlide(!sm.glide());
        } else if (r < 978) { sm.panic(); down.clear();
        } else if (r < 984) { sm.allNotesOff(); down.clear();
        } else { /* idle */ }
        events++;
        if (!invariants("torture")) { printf("  stopped at event %d\n", ev); fails++; break; }
        const long live = sm.activeCount(); if (live > maxLive) maxLive = live;
        run((rng() % 8 == 0) ? rng() % 6000 : rng() % 700);
    }
    sm.setGlide(false); sm.allNotesOff(); down.clear();
    run(secs(8));
    CHECK(sm.activeCount() == 0, "all voices returned after the torture (live=%u)", sm.activeCount());
    CHECK(invariants("end"), "final invariants");
    printf("  %llu events, %ld steals, max live voices %ld (pool %d)\n", (unsigned long long)events, steals, maxLive, SYNTH_POOL);
    g_delay_us_hook = nullptr;
}

static void test_modulation() {
    SECTION("pitch bend / vibrato / glide maths");
    fresh();
    sm.pitchBend(8191); run(CONTROL_DIV * 2);
    CHECK(fabsf(sm.modRatio() - powf(2.0f, (8191.0f * 2.0f / 8192.0f) / 12.0f)) < 2e-6f, "bend +2st: %.7f", sm.modRatio());
    sm.pitchBend(-8192); run(CONTROL_DIV * 2);
    CHECK(fabsf(sm.modRatio() - powf(2.0f, -2.0f / 12.0f)) < 2e-6f, "bend -2st: %.7f", sm.modRatio());
    sm.pitchBend(0); sm.setParam(P_MOD, 127); sm.setParam(P_LFORATE, 94);
    float mn = 9, mx = 0;
    for (int i = 0; i < secs(1.0); i++) { sm.tick(); mn = fminf(mn, sm.modRatio()); mx = fmaxf(mx, sm.modRatio()); }
    printf("  vibrato ratio range %.5f .. %.5f (expect %.5f .. %.5f)\n", mn, mx, powf(2.0f, -MOD_MAX_SEMITONES / 12.0f), powf(2.0f, MOD_MAX_SEMITONES / 12.0f));
    CHECK(mx > 1.10f && mn < 0.90f, "vibrato swings both ways");
    sm.setParam(P_LFOWAVE, 127);                             // sample & hold
    std::set<int> levels; for (int i = 0; i < secs(2.0); i++) { sm.tick(); levels.insert((int)(sm.modRatio() * 1e5)); }
    CHECK(levels.size() > 3 && levels.size() < 400, "S&H gives stepped values (%zu distinct)", levels.size());
    sm.setParam(P_MOD, 0); sm.setParam(P_LFOWAVE, 0);

    SECTION("glide (mono legato)");
    fresh(); sm.setGlide(true); sm.setParam(P_GLIDE, 20);
    sm.noteOn(60, 100); run(secs(0.2));
    sm.noteOn(72, 100);
    CHECK(sm.activeCount() == 1, "legato: still ONE voice (%u)", sm.activeCount());
    int v = -1; for (int i = 0; i < SYNTH_POOL; i++) if (sm._voices[i].live) v = i;
    CHECK(sm._voices[v].aenv.state == SynthEnv::SUSTAIN, "legato must not retrigger the envelope");
    const float c0 = sm._voices[v].curInc, tgt = sm._noteInc[72];
    run(secs(0.05)); const float c1 = sm._voices[v].curInc;
    run(secs(1.2));  const float c2 = sm._voices[v].curInc;
    printf("  glide 60 -> 72: start %.3f, +50 ms %.3f, +1.25 s %.3f  (target %.3f, units: x1e6)\n", c0 / 1e6, c1 / 1e6, c2 / 1e6, tgt / 1e6);
    CHECK(c1 > c0 && c1 < tgt, "gliding");
    CHECK(fabsf(c2 - tgt) < 0.02f * tgt, "arrived");
    sm.noteOff(72); run(secs(4)); CHECK(sm.activeCount() == 0, "released");
    sm.noteOn(67, 100);                                      // new note after silence glides from the last pitch
    for (int i = 0; i < SYNTH_POOL; i++) if (sm._voices[i].live) v = i;
    CHECK(fabsf(sm._voices[v].curInc - sm._noteInc[72]) < 1e3f || sm._voices[v].curInc != sm._noteInc[67], "starts at previous pitch");
    sm.setGlide(false); sm.allNotesOff(); run(secs(4));
    printf("  ok\n");
}

static void test_extremes() {
    SECTION("extremes: every parameter swept while notes play (UBSan float-cast + ASan watch)");
    fresh(); g_delay_us_hook = delayHook;
    std::mt19937 rng(7);
    for (int n : {24, 60, 96, 127}) sm.noteOn(n, 127);
    sm.pitchBend(8191);
    for (int round = 0; round < 3; round++)
        for (int p = 0; p < N_PARAMS; p++)
            for (int v : {0, 1, 64, 126, 127}) { sm.setParam(p, v); Stats s = run(700); CHECK(s.peak <= 32768, "p=%d v=%d peak %f", p, v, s.peak); }
    sm.setParam(P_MOD, 127); sm.setParam(P_RES, 127); sm.setParam(P_CUTOFF, 127); sm.setParam(P_FENV, 127); sm.setParam(P_DRIVE, 127); sm.setParam(P_NOISE, 127); sm.setParam(P_OSCMIX, 127);
    for (int i = 0; i < 20; i++) { sm.noteOn(100 + (rng() % 28), 127); run(secs(0.05)); }
    Stats s = run(secs(1)); printf("  all-max patch with 12 notes: peak %.0f rms %.0f\n", s.peak, s.rms());
    sm.panic(); run(secs(0.1)); CHECK(sm.activeCount() == 0, "panic silences within 100 ms (live=%u)", sm.activeCount());
    g_delay_us_hook = nullptr;
}

static void test_levels() {
    SECTION("levels");
    fresh();
    for (int n : {48, 55, 60, 64, 67, 72}) sm.noteOn(n, 100);
    Stats s = run(secs(0.5));
    printf("  6-note chord, default patch: rms %.0f peak %.0f (of 32767)\n", s.rms(), s.peak);
    CHECK(s.peak < 32767, "no clipping at default volume");
    sm.setParam(P_DRIVE, 100); s = run(secs(0.3)); printf("  with drive 100: rms %.0f peak %.0f\n", s.rms(), s.peak);
    CHECK(s.peak < 32768, "drive stays within range");
    sm.panic(); run(secs(0.1));
}

// ---------------------------------------------------------------------------------
//  Two real threads: "core 1" renders audio, "core 0" hammers the API
// ---------------------------------------------------------------------------------
static std::atomic<bool> g_audio_run{false};
static std::atomic<long> g_audio_samples{0};
static void audioThread(bool paced) {
    int16_t last = 0;
    while (g_audio_run) {
        for (int i = 0; i < 64; i++) { last = sm.tick(); }
        g_audio_samples += 64;
        if (paced) std::this_thread::sleep_for(std::chrono::microseconds(1450));   // ~real time
    }
    (void)last;
}

static void test_threads(bool paced, double seconds) {
    SECTION(paced ? "two threads, real-time paced audio" : "two threads, audio flat out (worst-case race windows)");
    fresh(); g_mock_real = true; g_delay_us_hook = nullptr;
    g_audio_run = true; g_audio_samples = 0;
    std::thread audio(audioThread, paced);
    std::mt19937 rng(paced ? 99 : 4242);
    std::set<int> down; long events = 0; bool inv_ok = true;
    const auto t_end = std::chrono::steady_clock::now() + std::chrono::milliseconds((int)(seconds * 1000));
    while (std::chrono::steady_clock::now() < t_end) {
        const int r = rng() % 100;
        if (r < 35)       { int n = 30 + rng() % 80; sm.noteOn(n, 100); down.insert(n); }
        else if (r < 70)  { if (!down.empty()) { auto it = down.begin(); std::advance(it, rng() % down.size()); sm.noteOff(*it); down.erase(it); } }
        else if (r < 85)  { sm.setParam(rng() % N_PARAMS, rng() % 128); }
        else if (r < 92)  { sm.pitchBend((int16_t)((int)(rng() % 16384) - 8192)); }
        else if (r < 94)  { sm.loadPatch(rng() % 3); }
        else if (r < 96)  { sm.setGlide(!sm.glide()); }
        else if (r < 97)  { sm.panic(); down.clear(); }
        else if (r < 99)  { sm.allNotesOff(); down.clear(); }
        events++;
        // core-0 invariants are only meaningful when core 1 isn't retiring voices underneath us for
        // fields we read, so check just the pure core-0 maps here
        for (int n = 0; n < 128; n++) { uint8_t v = sm._map[n]; if (v != SynthManager::NONE && (v >= SYNTH_POOL || sm._note[v] != n || !sm._held[v])) inv_ok = false; }
        if (paced) std::this_thread::sleep_for(std::chrono::microseconds(rng() % 3000));
    }
    sm.setGlide(false); sm.allNotesOff(); down.clear();
    // wait (real time) for every voice to be handed back
    const auto t0 = std::chrono::steady_clock::now(); bool idle = false;
    while (std::chrono::steady_clock::now() - t0 < std::chrono::seconds(10)) {
        if (sm.activeCount() == 0) { idle = true; break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        if (!paced) { /* release times up to 3 s of audio at ~100x realtime */ }
    }
    g_audio_run = false; audio.join(); g_mock_real = false;
    printf("  %ld control events, %ld audio samples (%.1fx real time)\n", events, g_audio_samples.load(), g_audio_samples.load() / (seconds * 44100.0));
    CHECK(inv_ok, "map invariants violated under concurrency");
    CHECK(idle, "voices stuck after the race test (live=%u)", sm.activeCount());
    for (int i = 0; i < SYNTH_POOL; i++) if (sm._voices[i].live) printf("   stuck: V%d state=%s level=%.3f held=%d\n", i, sm._voices[i].aenv.name(), sm._voices[i].level(), sm._held[i]);
}

int main(int argc, char** argv) {
    const bool quick = (argc > 1 && !strcmp(argv[1], "quick"));
    Wavetable::init();
    if (argc > 1 && !strcmp(argv[1], "threads")) { test_threads(false, 3.0); printf("done\n"); return fails ? 1 : 0; }
    test_params();
    test_basic();
    test_cap_newest_wins();
    test_prefers_released();
    test_pool_exhaustion();
    test_same_note();
    test_modulation();
    test_extremes();
    test_levels();
    if (!quick) test_torture();
    test_threads(false, quick ? 1.0 : 3.0);
    test_threads(true,  quick ? 1.0 : 4.0);
    printf("\nENGINE TESTS: %s (%d failures)\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}
