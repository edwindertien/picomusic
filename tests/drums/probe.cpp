// Scriptable probe around the REAL DrumManager: reads commands on stdin, records audio, answers queries.
#include <Arduino.h>
#include <vector>
#include <string>
#include <sstream>
#include <iostream>
#define DRUM_TRACE 1
#define private public
#include "drum_manager.h"
#undef private
volatile uint8_t g_cpu_load_pct = 0;
static DrumManager m;
static std::vector<int16_t> rec;
static void run(long n, bool keep) { for (long i = 0; i < n; i++) { int16_t l, r; m.tick(l, r); if (keep) { rec.push_back(l); rec.push_back(r); } } }

int main() {
    Wavetable::init(); m.init();
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream is(line); std::string c; is >> c; if (c.empty() || c[0] == '#') continue;
        if (c == "set")        { std::string n; int v; is >> n >> v; int id = -1; for (int i = 0; i < N_PARAMS; i++) if (n == PARAM_INFO[i].cmd) id = i; if (id < 0) { printf("ERR unknown param %s\n", n.c_str()); continue; } m.setParam(id, v); }
        else if (c == "inst")  { int i; is >> i; m.selectInst(i); }
        else if (c == "param") { int i, k, v; is >> i >> k >> v; m.selectInst(i); m.setParam(P_LEVEL + k, v); }        // param <inst> <0..4> <value>
        else if (c == "hit")   { int i, v = 100; is >> i >> v; m.hit(i, v); }
        else if (c == "pad")   { int n, v = 100; is >> n >> v; printf("PAD %d\n", m.padHit(n, v)); }
        else if (c == "run")   { long n; is >> n; run(n, true); }
        else if (c == "skip")  { long n; is >> n; run(n, false); }
        else if (c == "write") { std::string p; is >> p; FILE* f = fopen(p.c_str(), "wb"); fwrite(rec.data(), 2, rec.size(), f); fclose(f); rec.clear(); }
        else if (c == "play")  { m.play(); } else if (c == "stop") { m.stop(); } else if (c == "resume") { m.resume(); }
        else if (c == "rec")   { int v; is >> v; m.setRecord(v); }
        else if (c == "bpm")   { float b; is >> b; m.setBpm(b); }
        else if (c == "pat")   { int p; is >> p; m.setPattern(p); }
        else if (c == "len")   { int l; is >> l; m.setLength(l); }
        else if (c == "step")  { int i, s, l; is >> i >> s >> l; m.setStep(i, s, l); }
        else if (c == "clear") { int p; is >> p; m.clearPattern(p); }
        else if (c == "clearall") { for (int p = 0; p < N_PATTERNS; p++) m.clearPattern(p); for (int i = 0; i < SONG_MAX; i++) m.song[i] = 0xFF; }
        else if (c == "song")  { int s = 0, p; while (is >> p) m.setSongSlot(s++, p); for (; s < SONG_MAX; s++) m.song[s] = 0xFF; }
        else if (c == "songmode") { int v; is >> v; m.songMode = v; }
        else if (c == "sync")  { int v; is >> v; m.extSync = v; }
        else if (c == "pads")  { int v; is >> v; m.padGm = v; }
        else if (c == "rt")    { int v; is >> std::hex >> v; m.midiRealtime(v); }
        else if (c == "clkrun"){ int ticks; long spt; is >> ticks >> spt; for (int i = 0; i < ticks; i++) { m.midiRealtime(0xF8); run(spt, false); } }   // MIDI clock ticks, spt samples apart
        else if (c == "serv")  { m.service(); }
        else if (c == "cc")    { int a, b; is >> a >> b; printf("CC %d\n", (int)m.handleCC(a, b)); }
        else if (c == "panic") { m.panic(); }
        else if (c == "traceclear") { m.traceN = 0; }
        else if (c == "trace") { for (uint32_t i = 0; i < m.traceN; i++) printf("T %u %d %d %d %d\n", m.trace[i].t, m.trace[i].inst, m.trace[i].vel, m.trace[i].pat, m.trace[i].step); printf("END\n"); }
        else if (c == "status"){ printf("STATUS playing=%d step=%d pat=%d songpos=%d live=%d bpm=%.2f sel=%d selpat=%d\n", m.playing, m.stepNow, m.patNow, m.songPosNow, m.liveCount(), m.bpm, m.sel, m.selPat); }
        else if (c == "grid")  { for (int i = 0; i < N_INST; i++) { printf("%s ", INST_LABEL[i]); for (int s = 0; s < N_STEPS; s++) putchar(m.pat[m.selPat][i][s] ? '0' + m.pat[m.selPat][i][s] : '.'); putchar('\n'); } }
        else if (c == "get")   { int p, i, s; is >> p >> i >> s; printf("GET %d\n", m.pat[p][i][s]); }
        else if (c == "param?"){ int i; is >> i; printf("PV %d\n", m.pv[i]); }
        else if (c == "save")  { std::string p; is >> p; static uint8_t b[8192]; size_t n = m.serialize(b, sizeof b); FILE* f = fopen(p.c_str(), "wb"); fwrite(b, 1, n, f); fclose(f); printf("SAVED %zu\n", n); }
        else if (c == "load")  { std::string p; is >> p; static uint8_t b[8192]; FILE* f = fopen(p.c_str(), "rb"); size_t n = f ? fread(b, 1, sizeof b, f) : 0; if (f) fclose(f); printf("LOAD %d\n", (int)m.deserialize(b, n)); }
        else if (c == "peek")  { int i; is >> i; printf("PEEK live=%d type=%d\n", m._v[i].live, m._v[i].type); }
        else { printf("ERR unknown command %s\n", c.c_str()); }
    }
    return 0;
}
