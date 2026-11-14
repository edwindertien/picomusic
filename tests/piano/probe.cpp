// Scriptable probe around the REAL PianoManager (stereo): reads commands on stdin, records audio, answers queries.
#include <Arduino.h>
#include <vector>
#include <string>
#include <sstream>
#include <iostream>
#include <cmath>
#define PIANO_TRACE 1
#define private public
#include "piano_manager.h"
#undef private
volatile uint8_t g_cpu_load_pct = 0;
static PianoManager m;
static std::vector<int16_t> rec;
static void run(long n, bool keep) { for (long i = 0; i < n; i++) { int16_t l, r; m.tick(l, r); if (keep) { rec.push_back(l); rec.push_back(r); } } }
int main() {
    m.init();
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream is(line); std::string c; is >> c; if (c.empty() || c[0] == '#') continue;
        if (c == "set")        { std::string n; int v; is >> n >> v; int id = -1; for (int i = 0; i < N_PARAMS; i++) if (n == PARAM_INFO[i].cmd) id = i; if (id < 0) { printf("ERR unknown param %s\n", n.c_str()); continue; } m.setParam(id, v); }
        else if (c == "on")    { int n, v = 80; is >> n >> v; m.noteOn(n, v); }
        else if (c == "off")   { int n; is >> n; m.noteOff(n); }
        else if (c == "pedal") { int v; is >> v; m.setPedal(v); }
        else if (c == "softped") { int v; is >> v; m.softPedal = v; }
        else if (c == "bend")  { int v; is >> v; m.pitchBend(v); }
        else if (c == "panic") { m.panic(); } else if (c == "all") { m.allNotesOff(); }
        else if (c == "cc")    { int a, b; is >> a >> b; printf("CC %d\n", (int)m.handleCC(a, b)); }
        else if (c == "run")   { long n; is >> n; run(n, true); }
        else if (c == "skip")  { long n; is >> n; run(n, false); }
        else if (c == "write") { std::string p; is >> p; FILE* f = fopen(p.c_str(), "wb"); fwrite(rec.data(), 2, rec.size(), f); fclose(f); rec.clear(); }
        else if (c == "inst") { int i; is >> i; m.setInstrument(i); }
        else if (c == "zone?") { int k, v; is >> k >> v; const int z = PianoManager::selectZone(m.instrument, k, v); printf("ZONE %d root=%d vlo=%d vhi=%d loop=%u\n", z, PIANO_ZONE[z].root, PIANO_ZONE[z].vlo, PIANO_ZONE[z].vhi, PIANO_ZONE[z].loopEnd); }
        else if (c == "fxsine") { float hz, amp; long n; is >> hz >> amp >> n; double ph = 0; for (long i = 0; i < n; i++) { float l = amp * (float)sin(ph), r = l; ph += 6.283185307179586 * hz / SAMPLE_RATE; m.fx.process(l, r); rec.push_back((int16_t)lrintf(std::fmax(-32767.0f, std::fmin(32767.0f, l * 32767.0f)))); rec.push_back((int16_t)lrintf(std::fmax(-32767.0f, std::fmin(32767.0f, r * 32767.0f)))); } }
        else if (c == "fximp")  { long n; is >> n; for (long i = 0; i < n; i++) { float l = i == 0 ? 0.5f : 0.0f, r = l; m.fx.process(l, r); rec.push_back((int16_t)lrintf(l * 32767.0f)); rec.push_back((int16_t)lrintf(r * 32767.0f)); } }
        else if (c == "traceclear") { m.traceN = 0; }
        else if (c == "trace") { for (uint32_t i = 0; i < m.traceN; i++) printf("T %u %d %d %d\n", m.trace[i].t, m.trace[i].kind, m.trace[i].key, m.trace[i].voice); printf("END\n"); }
        else if (c == "status"){ printf("STATUS inst=%d active=%d voices=%d pedal=%d mask=%08x%08x%08x pitchmul=%.6f\n", m.instrument, m.activeVoices(), m.voicesNow, m._pedal, m.keyMask[2], m.keyMask[1], m.keyMask[0], m.pitchMul); }
        else if (c == "param?"){ int i; is >> i; printf("PV %d\n", m.pv[i]); }
        else if (c == "levels"){ for (int i = 0; i < N_VOICES; i++) if (m._v[i].active) printf("V %d key=%d rel=%d sus=%d e=%.5f\n", i, m._v[i].key, m._v[i].rel, m._v[i].sustained, m._v[i].e); printf("END\n"); }
        else { printf("ERR unknown command %s\n", c.c_str()); }
    }
    return 0;
}
