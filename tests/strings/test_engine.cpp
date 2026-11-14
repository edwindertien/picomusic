// Engine tests for the string machine (REAL source files, mocked hardware headers).
#include <Arduino.h>
#include <vector>
#include <set>
#include <random>
#include <thread>
#include <atomic>
#define private public            // test-only: inspect core-0 bookkeeping
#include "strings_manager.h"
#undef private

static StringsManager sm;
static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { fails++; printf("  FAIL (line %d): %s   ", __LINE__, #cond); printf(__VA_ARGS__); printf("\n"); } } while (0)
#define SECTION(name) printf("\n== %s ==\n", name)

struct Stats { double peak = 0, sumsq = 0; long n = 0; double rms() const { return n ? sqrt(sumsq / n) : 0; } };
static Stats run(int n) {
    Stats st;
    for (int i = 0; i < n; i++) {
        int16_t l, r; sm.tick(l, r);
        const double a = fmax(fabs((double)l), fabs((double)r)); if (a > st.peak) st.peak = a;
        st.sumsq += (double)l * l; st.n++;
    }
    return st;
}
static inline int secs(double s) { return (int)(s * SAMPLE_RATE); }
static void delayHook(uint32_t us) { for (int i = 0; i < (int)(us * 44100ull / 1000000ull) + 1; i++) { int16_t l, r; sm.tick(l, r); } }
static void fresh() { sm.init(); sm.setPedal(false); }

static bool invariants(const char* when) {
    bool ok = true; int counted = 0, live = 0;
    for (int i = 0; i < STRINGS_POOL; i++) {
        if (sm._voices[i].live) { live++; if (!sm._dying[i]) counted++; }
        if (sm._voices[i].live && sm._held[i]) {
            if (sm._dying[i] || sm._sust[i])                       { printf("  [%s] V%d held and dying/sustained\n", when, i); ok = false; }
            if (sm._note[i] >= 128 || sm._map[sm._note[i]] != i)   { printf("  [%s] V%d held note %u not mapped to it\n", when, i, sm._note[i]); ok = false; }
        }
        if (sm._sust[i] && (!sm._voices[i].live || sm._held[i] || sm._dying[i])) { printf("  [%s] V%d sustained but not live / held / dying\n", when, i); ok = false; }
    }
    for (int n = 0; n < 128; n++) {
        const uint8_t v = sm._map[n]; if (v == StringsManager::NONE) continue;
        if (v >= STRINGS_POOL || !sm._voices[v].live || !sm._held[v] || sm._note[v] != n) { printf("  [%s] map[%d]=%u invalid\n", when, n, v); ok = false; }
    }
    if (counted > MAX_ACTIVE_VOICES) { printf("  [%s] counted %d > cap\n", when, counted); ok = false; }
    if (live > STRINGS_POOL)         { printf("  [%s] live %d > pool\n", when, live); ok = false; }
    return ok;
}
static int heldCount() { int n = 0; for (int i = 0; i < STRINGS_POOL; i++) n += (sm._voices[i].live && sm._held[i]); return n; }
static int sustCount() { int n = 0; for (int i = 0; i < STRINGS_POOL; i++) n += (sm._voices[i].live && sm._sust[i]); return n; }

static void test_params() {
    SECTION("parameter table");
    for (int p = 0; p < N_PATCHES; p++) {
        CHECK(PATCHES[p][P_VOL] >= 60 && PATCHES[p][P_VOL] <= 120, "patch %d: VOL misaligned (%d)", p, PATCHES[p][P_VOL]);
        for (int i = 0; i < N_PARAMS; i++) CHECK(PATCHES[p][i] <= 127, "patch %d param %d > 127", p, i);
    }
    for (int i = 0; i < N_PARAMS; i++) for (int j = i + 1; j < N_PARAMS; j++)
        CHECK(PARAM_INFO[i].cc != PARAM_INFO[j].cc, "params %s and %s share CC %d", PARAM_INFO[i].cmd, PARAM_INFO[j].cmd, PARAM_INFO[i].cc);
    std::set<int> cells;
    for (int i = 0; i < N_KNOBS; i++) {
        CHECK(PARAM_INFO[i].col < 8 && PARAM_INFO[i].row < 3, "knob %s has no cell", PARAM_INFO[i].cmd);
        CHECK(cells.insert(PARAM_INFO[i].row * 8 + PARAM_INFO[i].col).second, "knob %s shares a grid cell", PARAM_INFO[i].cmd);
        CHECK(!(PARAM_INFO[i].row == 2 && PARAM_INFO[i].col >= 5), "knob %s sits where the status block is", PARAM_INFO[i].cmd);
        CHECK(strlen(PARAM_INFO[i].label) <= 4, "label %s too long for a cell", PARAM_INFO[i].label);
    }
    CHECK(paramForCC(1) == P_TONE && paramForCC(2) == P_ATTACK && paramForCC(3) == P_RELEASE && paramForCC(4) == P_ENS_DEPTH, "AKAI K1-K4");
    CHECK(paramForCC(5) == P_ENS_RATE && paramForCC(6) == P_ENS_MIX && paramForCC(7) == P_VOL && paramForCC(8) == P_VIB_DEPTH, "AKAI K5-K8");
    for (int i = 0; i < N_PARAMS; i++) CHECK(paramForCC(PARAM_INFO[i].cc) == i, "CC %d does not map back to %s", PARAM_INFO[i].cc, PARAM_INFO[i].cmd);
    fresh();
    CHECK(sm.handleCC(11, 50) && sm.pv[P_VOL] == 50,            "CC11 expression -> volume");
    CHECK(sm.handleCC(93, 77) && sm.pv[P_ENS_MIX] == 77,         "CC93 chorus send -> ensemble mix");
    CHECK(sm.handleCC(94, 66) && sm.pv[P_ENS_DEPTH] == 66,       "CC94 celeste depth -> ensemble depth");
    CHECK(sm.handleCC(64, 127) && sm.sustainActive(),            "CC64 down -> sustain");
    CHECK(sm.handleCC(64, 0)   && !sm.sustainActive(),           "CC64 up");
    CHECK(!sm.handleCC(100, 5),                                  "unused CC reported unused");
    sm.loadPatch(1); CHECK(!strcmp(sm.patchName, "Cello"), "patch name"); sm.setParam(P_TONE, 3); CHECK(!strcmp(sm.patchName, "Custom"), "edit -> Custom");
    sm.loadPatch(0);
    CHECK(sm.ep.use16 && sm.ep.use8 && sm.ep.use4 && !sm.ep.usePz, "Ensemble patch uses 16', 8', 4' but not pizz");
    sm.setParam(P_CELLO, 0); sm.setParam(P_VIOLA, 0); CHECK(!sm.ep.use8, "8' bus off when neither cello nor viola is up");
    sm.setParam(P_VIOLA, 5); CHECK(sm.ep.use8, "...and back on");
    sm.setParam(P_PIZZ, 1); CHECK(sm.ep.usePz, "pizz on");
    sm.setParam(P_ATTACK, 100); CHECK(fabsf(1.0f / (sm.ep.atkRate * SAMPLE_RATE) - attackSeconds(100)) < 1e-4f, "attack rate matches attackSeconds");
    sm.setParam(P_DETUNE, 0); CHECK(!sm.ep.unison, "no detune -> no 2nd saw"); sm.setParam(P_DETUNE, 127); CHECK(sm.ep.unison && sm.ep.detuneRatio > 1.009f && sm.ep.detuneRatio < 1.0105f, "18 cents %f", sm.ep.detuneRatio);
    printf("  ok\n");
}

static void test_basic() {
    SECTION("one note: attack, hold, release, voice returned to the pool");
    fresh(); sm.setParam(P_ATTACK, 100); sm.setParam(P_RELEASE, 60); sm.setParam(P_ENS_MIX, 0);
    sm.noteOn(60, 100);
    CHECK(sm.activeCount() == 1 && invariants("on"), "one voice");
    Stats early = run(secs(0.05)); run(secs(0.5)); Stats later = run(secs(0.3)); run(secs(1.5)); Stats hold = run(secs(0.2));
    printf("  rms 0-50 ms %.0f | 0.55-0.85 s %.0f | held %.0f\n", early.rms(), later.rms(), hold.rms());
    CHECK(early.rms() < 0.15 * hold.rms(), "slow attack: first 50 ms is quiet");
    CHECK(later.rms() < hold.rms(), "still rising at 0.7 s with a 0.6 s attack... then full");
    int v = -1; for (int i = 0; i < STRINGS_POOL; i++) if (sm._voices[i].live) v = i;
    CHECK(sm._voices[v].st == StringVoice::HOLD && sm._voices[v].level == 1.0f, "holds at full level (no decay)");
    sm.noteOff(60); CHECK(invariants("off"), "off");
    run(secs(releaseSeconds(60) + 0.3));
    CHECK(sm.activeCount() == 0, "voice handed back after the release (live=%u)", sm.activeCount());
    CHECK(run(500).peak == 0, "silence after release");
}

static void test_cap() {
    SECTION("polyphony cap: the new note wins, oldest are stolen");
    fresh(); g_delay_us_hook = delayHook;
    const int N = MAX_ACTIVE_VOICES + 8;
    for (int n = 30; n < 30 + N; n++) {
        sm.noteOn(n, 100); CHECK(sm._map[n] != StringsManager::NONE, "note %d must start at once", n); CHECK(invariants("burst"), "inv"); run(100);
    }
    run(secs(0.1));
    CHECK(sm.activeCount() == MAX_ACTIVE_VOICES, "settles to the cap (%u)", sm.activeCount());
    for (int n = 30; n < 30 + 8; n++) CHECK(sm._map[n] == StringsManager::NONE, "oldest %d stolen", n);
    for (int n = 38; n < 30 + N; n++) CHECK(sm._map[n] != StringsManager::NONE, "newest %d held", n);
    g_delay_us_hook = nullptr; printf("  ok: %d notes pressed, the newest %d kept\n", N, MAX_ACTIVE_VOICES);
}

static void test_prefers_released() {
    SECTION("stealing prefers key-up voices (tails) and keeps held keys");
    fresh(); sm.setParam(P_RELEASE, 110);
    for (int n = 40; n < 40 + MAX_ACTIVE_VOICES; n++) { sm.noteOn(n, 100); run(40); }
    run(secs(0.2));
    for (int n = 40; n < 48; n++) sm.noteOff(n);
    run(secs(0.01));
    CHECK(sm.countedVoices() == MAX_ACTIVE_VOICES, "held + tails = cap");
    for (int n = 90; n < 98; n++) { sm.noteOn(n, 100); run(20); CHECK(invariants("steal"), "inv"); }
    for (int n = 48; n < 40 + MAX_ACTIVE_VOICES; n++) CHECK(sm._map[n] != StringsManager::NONE, "held key %d survives", n);
    for (int n = 90; n < 98; n++) CHECK(sm._map[n] != StringsManager::NONE, "new key %d plays", n);
    printf("  ok\n");
}

static void test_pool_exhaustion() {
    SECTION("burst larger than the spare slots: waits for a fading voice, never drops the note");
    fresh(); g_delay_us_hook = delayHook;
    for (int n = 40; n < 40 + MAX_ACTIVE_VOICES; n++) { sm.noteOn(n, 100); run(40); }
    run(secs(0.1));
    const int spare = STRINGS_POOL - MAX_ACTIVE_VOICES; uint64_t t0 = g_mock_us;
    for (int n = 100; n < 100 + spare + 1; n++) { sm.noteOn(n, 100); CHECK(sm._map[n] != StringsManager::NONE, "burst note %d dropped", n); CHECK(invariants("burst"), "inv"); }
    printf("  %d simultaneous steals with %d spare slots: core 0 waited %.2f ms\n", spare + 1, spare, (g_mock_us - t0) / 1000.0);
    CHECK(g_mock_us - t0 < 12000, "wait stays bounded");
    run(secs(0.1)); g_delay_us_hook = nullptr;
}

static void test_pedal() {
    SECTION("sustain pedal");
    fresh(); sm.setParam(P_RELEASE, 40);
    sm.setPedal(true); sm.noteOn(60, 100); sm.noteOn(64, 100); run(secs(0.3));
    sm.noteOff(60); sm.noteOff(64);
    CHECK(sm._map[60] == StringsManager::NONE && heldCount() == 0 && sustCount() == 2, "keys up, both voices sustained (held %d, sust %d)", heldCount(), sustCount());
    run(secs(2.0)); CHECK(sm.activeCount() == 2 && invariants("pedal"), "still sounding 2 s after key-up while the pedal is down");
    Stats s = run(2000); CHECK(s.rms() > 100, "and audible");
    sm.setPedal(false); CHECK(sustCount() == 0, "pedal up clears the sustain flags");
    run(secs(releaseSeconds(40) + 0.3)); CHECK(sm.activeCount() == 0, "voices released after the pedal goes up (live=%u)", sm.activeCount());

    fresh(); sm.setParam(P_RELEASE, 40);
    sm.noteOn(60, 100); sm.setPedal(true); sm.noteOff(60); run(secs(1.0));                   // pedal pressed AFTER the key
    CHECK(sm.activeCount() == 1, "pedal pressed after the key still catches the release"); sm.setPedal(false); run(secs(2.0)); CHECK(sm.activeCount() == 0, "released");

    fresh(); sm.setParam(P_RELEASE, 40);
    sm.noteOn(60, 100); sm.noteOff(60); run(secs(releaseSeconds(40) + 0.3)); sm.setPedal(true); sm.setPedal(false);   // already gone: nothing to hold
    CHECK(sm.activeCount() == 0 && invariants("late pedal"), "a pedal press after the release does nothing");

    fresh(); sm.setParam(P_RELEASE, 40); sm.setPedal(true);
    sm.noteOn(60, 100); sm.noteOff(60); sm.noteOn(60, 100); run(secs(0.3));                  // same key twice under the pedal
    CHECK(sm.activeCount() == 2 && heldCount() == 1 && sustCount() == 1 && invariants("retrigger"), "retrigger under the pedal: 1 held + 1 sustained");
    sm.noteOff(60); sm.setPedal(false); run(secs(releaseSeconds(40) + 0.3)); CHECK(sm.activeCount() == 0, "all released");
    printf("  ok\n");

    SECTION("stealing with the pedal: sustained (key-up) voices go before held keys");
    fresh(); g_delay_us_hook = delayHook; sm.setPedal(true);
    const int H = MAX_ACTIVE_VOICES / 2;
    for (int n = 40; n < 40 + H; n++) { sm.noteOn(n, 100); run(30); }
    for (int n = 40; n < 40 + H; n++) sm.noteOff(n);                                          // H sustained
    for (int n = 80; n < 80 + H; n++) { sm.noteOn(n, 100); run(30); }                          // H held keys: at the cap
    run(secs(0.2)); CHECK(sm.countedVoices() == MAX_ACTIVE_VOICES && sustCount() == H && heldCount() == H, "%d sustained + %d held = cap", H, H);
    for (int n = 100; n < 100 + H / 2; n++) { sm.noteOn(n, 100); run(20); CHECK(invariants("steal"), "inv"); }
    for (int n = 80; n < 80 + H; n++) CHECK(sm._map[n] != StringsManager::NONE, "held key %d survives", n);
    CHECK(sustCount() == H - H / 2, "%d of the %d sustained voices were stolen (%d left)", H / 2, H, sustCount());
    sm.allNotesOff(); run(secs(0.1)); CHECK(sustCount() == 0 && heldCount() == 0, "allNotesOff releases sustained voices too");
    sm.setPedal(false); run(secs(7)); CHECK(sm.activeCount() == 0, "everything ends (live=%u)", sm.activeCount());
    g_delay_us_hook = nullptr; printf("  ok\n");
}

static void test_same_note() {
    SECTION("same note repeatedly / duplicates / stale note-offs");
    fresh(); g_delay_us_hook = delayHook; sm.setParam(P_RELEASE, 30); std::mt19937 rng(1);
    for (int i = 0; i < 3000; i++) {
        sm.noteOn(60, 100); CHECK(sm._map[60] != StringsManager::NONE, "on"); run(rng() % 400);
        sm.noteOff(60);     CHECK(sm._map[60] == StringsManager::NONE, "off"); run(rng() % 400);
        if (!invariants("repeat")) break;
    }
    run(secs(3)); CHECK(sm.activeCount() == 0, "no stuck voices after 3000 retriggers (live=%u)", sm.activeCount());
    sm.noteOn(64, 100); sm.noteOn(64, 100); run(secs(0.05)); CHECK(heldCount() == 1, "duplicate note-on keeps one held voice");
    sm.noteOff(64); run(secs(3)); CHECK(sm.activeCount() == 0, "released");
    fresh(); for (int n = 0; n < MAX_ACTIVE_VOICES + 1; n++) { sm.noteOn(n, 100); run(40); }
    run(secs(0.1)); const int before = heldCount(); sm.noteOff(0); CHECK(before == heldCount(), "stale note-off for a stolen note changes nothing");
    g_delay_us_hook = nullptr; printf("  ok\n");
}

static void test_modulation() {
    SECTION("pitch bend and vibrato");
    fresh();
    sm.pitchBend(8191); run(CONTROL_DIV * 2); CHECK(fabsf(sm.modRatio() - powf(2.0f, (8191.0f * 2.0f / 8192.0f) / 12.0f)) < 2e-6f, "bend +2 st %.7f", sm.modRatio());
    sm.pitchBend(-8192); run(CONTROL_DIV * 2); CHECK(fabsf(sm.modRatio() - powf(2.0f, -2.0f / 12.0f)) < 2e-6f, "bend -2 st");
    sm.pitchBend(0); sm.setParam(P_VIB_DEPTH, 127); sm.setParam(P_VIB_RATE, 90);
    float mn = 9, mx = 0; for (int i = 0; i < secs(1.0); i++) { int16_t l, r; sm.tick(l, r); mn = fminf(mn, sm.modRatio()); mx = fmaxf(mx, sm.modRatio()); }
    const float e = powf(2.0f, VIB_MAX_SEMITONES / 12.0f);
    printf("  vibrato ratio %.5f .. %.5f (expect %.5f .. %.5f)\n", mn, mx, 1.0f / e, e);
    CHECK(mx > 0.995f * e && mn < 1.005f / e && mx < 1.001f * e, "vibrato swings +-%.2f semitones", VIB_MAX_SEMITONES);
    // a note's pitch really follows the bend: zero-crossing frequency of a bass note
    fresh(); sm.setParam(P_BASS, 127); sm.setParam(P_CELLO, 0); sm.setParam(P_VIOLA, 0); sm.setParam(P_VIOLIN, 0); sm.setParam(P_ENS_MIX, 0); sm.setParam(P_ATTACK, 0); sm.setParam(P_TONE, 127); sm.setParam(P_DETUNE, 0);
    sm.noteOn(72, 100); run(secs(0.3));                       // 16' of note 72 = 261.6 Hz: fine zero-crossing resolution
    auto freq = [&]() { int zc = 0; int16_t prev = 0; const int n = secs(2.0); for (int i = 0; i < n; i++) { int16_t l, r; sm.tick(l, r); if (prev < 0 && l >= 0) zc++; prev = l; } return zc / 2.0; };
    const double f0 = freq(); sm.pitchBend(8191); run(secs(0.1)); const double f1 = freq();
    const double want = powf(2.0f, 2.0f * 8191 / 8192 / 12.0f);
    printf("  bass footage of note 72: %.1f Hz (expect 261.6) -> bent up: %.1f Hz (ratio %.4f, expect %.4f)\n", f0, f1, f1 / f0, want);
    CHECK(fabs(f1 / f0 - want) < 0.005, "pitch follows the bend");
    CHECK(fabs(f0 - 261.63) < 1.5, "16' footage sounds an octave below the key (note 72 = 523.3 Hz -> 261.6 Hz)");
}

static void test_extremes() {
    SECTION("extremes: every parameter swept while 24 notes play (UBSan float-cast + ASan watch)");
    fresh(); g_delay_us_hook = delayHook;
    for (int n : {0, 12, 24, 60, 96, 120, 127}) sm.noteOn(n, 127);
    sm.pitchBend(8191); sm.setPedal(true);
    for (int round = 0; round < 3; round++)
        for (int p = 0; p < N_PARAMS; p++) for (int v : {0, 1, 64, 126, 127}) { sm.setParam(p, v); Stats s = run(700); CHECK(s.peak <= 32768, "p=%d v=%d peak %f", p, v, s.peak); }
    std::mt19937 rng(7);
    for (int i = 0; i < 60; i++) { sm.noteOn(rng() % 128, 127); run(secs(0.02)); }
    Stats s = run(secs(1)); printf("  stress: peak %.0f rms %.0f\n", s.peak, s.rms());
    sm.setPedal(false); sm.panic(); run(secs(0.2)); CHECK(sm.activeCount() == 0, "panic silences within 200 ms (live=%u)", sm.activeCount());
    g_delay_us_hook = nullptr;
}

static void test_torture() {
    SECTION("random torture (single thread): 250k events incl. pedal, panic, patches, bend, CC floods");
    fresh(); g_delay_us_hook = delayHook; std::mt19937 rng(12345); std::set<int> down; long steals = 0, maxLive = 0;
    for (int ev = 0; ev < 250000; ev++) {
        const int r = rng() % 1000;
        if (r < 300)      { int n = 24 + rng() % 90; const int cb = sm.countedVoices(); sm.noteOn(n, 1 + rng() % 127); down.insert(n); if (cb >= MAX_ACTIVE_VOICES) steals++; CHECK(sm._map[n] != StringsManager::NONE, "note %d not started", n); }
        else if (r < 580) { if (!down.empty()) { auto it = down.begin(); std::advance(it, rng() % down.size()); sm.noteOff(*it); down.erase(it); } }
        else if (r < 640) { sm.noteOff(rng() % 128); }
        else if (r < 800) { sm.setParam(rng() % N_PARAMS, rng() % 128); }
        else if (r < 860) { sm.handleCC(rng() % 128, rng() % 128); }
        else if (r < 910) { sm.pitchBend((int16_t)((int)(rng() % 16384) - 8192)); }
        else if (r < 920) { sm.loadPatch(rng() % N_PATCHES); }
        else if (r < 950) { sm.setPedal(rng() & 1); }
        else if (r < 954) { sm.panic(); down.clear(); }
        else if (r < 962) { sm.allNotesOff(); down.clear(); }
        if (!invariants("torture")) { printf("  stopped at event %d\n", ev); fails++; break; }
        const long live = sm.activeCount(); if (live > maxLive) maxLive = live;
        run((rng() % 8 == 0) ? rng() % 6000 : rng() % 700);
    }
    sm.setPedal(false); sm.allNotesOff(); down.clear(); run(secs(10));
    CHECK(sm.activeCount() == 0, "all voices returned (live=%u)", sm.activeCount()); CHECK(invariants("end"), "final invariants");
    printf("  250000 events, %ld steals, max live voices %ld (pool %d)\n", steals, maxLive, STRINGS_POOL);
    g_delay_us_hook = nullptr;
}

static std::atomic<bool> g_audio_run{false}; static std::atomic<long> g_audio_samples{0};
static void audioThread(bool paced) {
    while (g_audio_run) { for (int i = 0; i < 64; i++) { int16_t l, r; sm.tick(l, r); } g_audio_samples += 64; if (paced) std::this_thread::sleep_for(std::chrono::microseconds(1450)); }
}
static void test_threads(bool paced, double seconds) {
    SECTION(paced ? "two threads, real-time paced audio" : "two threads, audio flat out (worst-case race windows)");
    fresh(); g_mock_real = true; g_delay_us_hook = nullptr; g_audio_run = true; g_audio_samples = 0;
    std::thread audio(audioThread, paced); std::mt19937 rng(paced ? 99 : 4242); std::set<int> down; long events = 0; bool inv_ok = true;
    const auto t_end = std::chrono::steady_clock::now() + std::chrono::milliseconds((int)(seconds * 1000));
    while (std::chrono::steady_clock::now() < t_end) {
        const int r = rng() % 100;
        if (r < 35)       { int n = 30 + rng() % 80; sm.noteOn(n, 100); down.insert(n); }
        else if (r < 70)  { if (!down.empty()) { auto it = down.begin(); std::advance(it, rng() % down.size()); sm.noteOff(*it); down.erase(it); } }
        else if (r < 82)  { sm.setParam(rng() % N_PARAMS, rng() % 128); }
        else if (r < 88)  { sm.pitchBend((int16_t)((int)(rng() % 16384) - 8192)); }
        else if (r < 90)  { sm.loadPatch(rng() % N_PATCHES); }
        else if (r < 95)  { sm.setPedal(rng() & 1); }
        else if (r < 96)  { sm.panic(); down.clear(); }
        else if (r < 98)  { sm.allNotesOff(); down.clear(); }
        events++;
        for (int n = 0; n < 128; n++) { uint8_t v = sm._map[n]; if (v != StringsManager::NONE && (v >= STRINGS_POOL || sm._note[v] != n || !sm._held[v])) inv_ok = false; }
        if (paced) std::this_thread::sleep_for(std::chrono::microseconds(rng() % 3000));
    }
    sm.setPedal(false); sm.allNotesOff(); down.clear();
    const auto t0 = std::chrono::steady_clock::now(); bool idle = false;
    while (std::chrono::steady_clock::now() - t0 < std::chrono::seconds(15)) { if (sm.activeCount() == 0) { idle = true; break; } std::this_thread::sleep_for(std::chrono::milliseconds(20)); }
    g_audio_run = false; audio.join(); g_mock_real = false;
    printf("  %ld control events, %ld audio samples (%.1fx real time)\n", events, g_audio_samples.load(), g_audio_samples.load() / (seconds * 44100.0));
    CHECK(inv_ok, "map invariants violated under concurrency"); CHECK(idle, "voices stuck after the race test (live=%u)", sm.activeCount());
}

// Regression: softSat must never exceed +-1 (the output stage converts to int16; > 1 would wrap into a loud click).
// An earlier clamp at |x| = 4 let the curve overshoot to 1.0058 for 3 < |x| < 4.
static void test_softsat_bound() {
    // Near |x| = 3 the curve is cubic-flat, so float rounding gives ~1e-7 wiggles: allow 1e-6. The old bug overshot by 5.8e-3.
    float worst = 0.0f, prev = -2.0f, worst_drop = 0.0f; bool conv_ok = true;
    for (int i = -200000; i <= 200000; i++) {
        const float x = i * 5e-5f, y = softSat(x);
        if (fabsf(y) > worst) worst = fabsf(y);
        if (prev - y > worst_drop) worst_drop = prev - y;
        if (fabsf(y) * 32767.0f >= 32768.0f) conv_ok = false;            // the int16 conversion would leave its range
        prev = y;
    }
    CHECK(worst <= 1.000001f, "softSat overshoots: max |y| = %.7f", worst);
    CHECK(worst_drop <= 1e-6f, "softSat is not monotonic: largest drop %.2e", worst_drop);
    CHECK(conv_ok, "int16 conversion of softSat() output can leave its range");
    CHECK(softSat(0.0f) == 0.0f && softSat(1e6f) == 1.0f && softSat(-1e6f) == -1.0f, "softSat limits");
    CHECK((int16_t)(softSat(3.5f) * 32767.0f) == 32767, "softSat(3.5) converts to full scale without wrapping");
}

int main(int argc, char** argv) {
    const bool quick = (argc > 1 && !strcmp(argv[1], "quick"));
    Wavetable::init();
    if (argc > 1 && !strcmp(argv[1], "threads")) { test_threads(false, 3.0); printf("done\n"); return fails ? 1 : 0; }
    test_softsat_bound(); test_params(); test_basic(); test_cap(); test_prefers_released(); test_pool_exhaustion(); test_pedal(); test_same_note(); test_modulation(); test_extremes();
    if (!quick) test_torture();
    test_threads(false, quick ? 1.0 : 3.0); test_threads(true, quick ? 1.0 : 4.0);
    printf("\nENGINE TESTS: %s (%d failures)\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}
