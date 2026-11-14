// Scriptable probe around the REAL BassManager: reads commands on stdin, records audio, answers queries.
#include <Arduino.h>
#include <vector>
#include <string>
#include <sstream>
#include <iostream>
#define BASS_TRACE 1
#define private public
#include "bass_manager.h"
#undef private
volatile uint8_t g_cpu_load_pct = 0;
static BassManager m;
static std::vector<int16_t> rec;
static void run(long n, bool keep) { for (long i = 0; i < n; i++) { int16_t l, r; m.tick(l, r); if (keep) rec.push_back(l); } }

int main() {
    bass_tables_init(); m.init();
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream is(line); std::string c; is >> c; if (c.empty() || c[0] == '#') continue;
        if (c == "set")        { std::string n; int v; is >> n >> v; int id = -1; for (int i = 0; i < N_PARAMS; i++) if (n == PARAM_INFO[i].cmd) id = i; if (id < 0) { printf("ERR unknown param %s\n", n.c_str()); continue; } m.setParam(id, v); }
        else if (c == "patch") { int p; is >> p; m.loadPatch(p); }
        else if (c == "on")    { int n, v = 100; is >> n >> v; m.noteOn(n, v); }
        else if (c == "off")   { int n; is >> n; m.noteOff(n); }
        else if (c == "pedal") { int v; is >> v; m.setPedal(v); }
        else if (c == "bend")  { int v; is >> v; m.pitchBend(v); }
        else if (c == "panic") { m.panic(); } else if (c == "all") { m.allNotesOff(); }
        else if (c == "run")   { long n; is >> n; run(n, true); }
        else if (c == "skip")  { long n; is >> n; run(n, false); }
        else if (c == "write") { std::string p; is >> p; FILE* f = fopen(p.c_str(), "wb"); fwrite(rec.data(), 2, rec.size(), f); fclose(f); rec.clear(); }
        else if (c == "play")  { m.play(); } else if (c == "stop") { m.stop(); } else if (c == "resume") { m.resume(); }
        else if (c == "bpm")   { float b; is >> b; m.setBpm(b); }
        else if (c == "pat")   { int p; is >> p; m.setPattern(p); }
        else if (c == "len")   { int l; is >> l; m.setLength(l); }
        else if (c == "step")  { int s, n, f; is >> s >> n >> f; m.setStep(s, n, f); }                     // step <idx> <midi note> <flags 0-7>
        else if (c == "clear") { int p; is >> p; m.clearPattern(p); }
        else if (c == "clearall") { for (int p = 0; p < N_PATTERNS; p++) m.clearPattern(p); }
        else if (c == "sync")  { int v; is >> v; m.extSync = v; } else if (c == "keymode") { int v; is >> v; m.keyMode = v; }
        else if (c == "write_on") { int v; is >> v; m.stepWrite = v; } else if (c == "cursor") { int v; is >> v; m.cursor = v; }
        else if (c == "rt")    { int v; is >> std::hex >> v; m.midiRealtime(v); }
        else if (c == "clkrun"){ int ticks; long spt; is >> ticks >> spt; for (int i = 0; i < ticks; i++) { m.midiRealtime(0xF8); run(spt, false); } }
        else if (c == "cc")    { int a, b; is >> a >> b; printf("CC %d\n", (int)m.handleCC(a, b)); }
        else if (c == "traceclear") { m.traceN = 0; }
        else if (c == "trace") { for (uint32_t i = 0; i < m.traceN; i++) printf("T %u %d %d %d %d %d %d\n", m.trace[i].t, m.trace[i].kind, m.trace[i].note, m.trace[i].vel, m.trace[i].flags, m.trace[i].pat, m.trace[i].step); printf("END\n"); }
        else if (c == "status"){ printf("STATUS playing=%d step=%d pat=%d gate=%d note=%d active=%d pitch=%.3f bpm=%.2f cursor=%d transpose=%d held=%d astage=%d alevel=%.4f\n", m.playing, m.stepNow, m.patNow, m.gateNow, m.noteNow, m._voice.active(), m._voice.pitch, m.bpm, m.cursor, m.transpose, m.heldKeys(), m._voice.aEnv.stage, m._voice.aEnv.level); }
        else if (c == "grid")  { for (int s = 0; s < N_STEPS; s++) { char b[8]; noteName(m.stepNote[m.selPat][s], b); const uint8_t f = m.stepFlg[m.selPat][s]; printf("%s%s%s%s ", (f & 1) ? b : ".", (f & 2) ? "s" : "", (f & 4) ? "a" : "", ""); } printf("\n"); }
        else if (c == "param?"){ int i; is >> i; printf("PV %d\n", m.pv[i]); }
        else if (c == "save")  { std::string p; is >> p; static uint8_t b[8192]; size_t n = m.serialize(b, sizeof b); FILE* f = fopen(p.c_str(), "wb"); fwrite(b, 1, n, f); fclose(f); printf("SAVED %zu\n", n); }
        else if (c == "load")  { std::string p; is >> p; static uint8_t b[8192]; FILE* f = fopen(p.c_str(), "rb"); size_t n = f ? fread(b, 1, sizeof b, f) : 0; if (f) fclose(f); printf("LOAD %d\n", (int)m.deserialize(b, n)); }
        else if (c == "filtresp") { // magnitude of the ladder alone: sine in, RMS out, at one frequency. filtresp <cutOctOffset hz> <resRatio> <testHz>
            float fc, r, hz; is >> fc >> r >> hz; float oct = std::log2(fc / FILTER_MIN_HZ), g, kc; ladderLookup(oct, g, kc); Ladder L; L.reset(g); L.k = r * kc;
            const int N = 44100; double acc = 0; int cnt = 0; for (int i = 0; i < N; i++) { const float y = L.tick(0.05f * sinf(6.2831853f * hz * i / SAMPLE_RATE)); if (i > N / 2) { acc += y * y; cnt++; } }
            printf("RESP %.6f\n", sqrt(acc / cnt) / (0.05 / sqrt(2.0))); }
        else if (c == "filtsweep") {                                      // magnitude (dB) of the ladder at n log-spaced frequencies: filtsweep <fc> <resRatio> <f0> <f1> <n>
            float fc, r, f0, f1; int n; is >> fc >> r >> f0 >> f1 >> n; float oct = std::log2(fc / FILTER_MIN_HZ), g, kc; ladderLookup(oct, g, kc);
            for (int i = 0; i < n; i++) { const float hz = f0 * powf(f1 / f0, (float)i / (n - 1)); Ladder L; L.reset(g); L.k = r * kc; const int N = 20000; double acc = 0; int cnt = 0;
                for (int k = 0; k < N; k++) { const float y = L.tick(0.02f * sinf(6.2831853f * hz * k / SAMPLE_RATE)); if (k > N / 2) { acc += y * y; cnt++; } }
                printf("%.3f ", 20 * log10(sqrt(acc / cnt) / (0.02 / sqrt(2.0)) + 1e-12)); } printf("\n"); }
        else if (c == "rawosc") {                                        // the raw oscillator, PolyBLEP vs naive: rawosc <wave 0 saw|1 square> <note> <naive 0|1> <n> <path>
            int w, note, naive, n; std::string path; is >> w >> note >> naive >> n >> path; std::vector<float> o(n); uint32_t ph = 0; const float f = 440.0f * powf(2.0f, (note - 69) / 12.0f);
            const uint32_t inc = (uint32_t)(INC_C * exp2fast((note - 69) / 12.0f)); const float dt = (float)inc * (1.0f / 4294967296.0f); (void)f;
            for (int i = 0; i < n; i++) { ph += inc; const float t = (float)ph * (1.0f / 4294967296.0f); o[i] = naive ? (w == 0 ? 2 * t - 1 : (t < 0.5f ? 1.0f : -1.0f)) : (w == 0 ? oscSaw(ph, dt) : w == 1 ? oscSquare(ph, dt) : oscTri(ph)); }
            FILE* fp = fopen(path.c_str(), "wb"); fwrite(o.data(), 4, n, fp); fclose(fp); }
        else { printf("ERR unknown command %s\n", c.c_str()); }
    }
    return 0;
}
