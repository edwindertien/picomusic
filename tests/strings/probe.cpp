// Scriptable probe around the REAL StringsManager: reads commands on stdin, records audio, answers queries.
#include <Arduino.h>
#include <vector>
#include <string>
#include <sstream>
#include <iostream>
#define private public
#include "strings_manager.h"
#undef private
volatile uint8_t g_cpu_load_pct = 0;
static StringsManager m;
static std::vector<int16_t> rec;

int main(int argc, char** argv) {
    Wavetable::init(); m.init();
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream is(line); std::string c; is >> c; if (c.empty() || c[0] == '#') continue;
        if (c == "set")      { std::string n; int v; is >> n >> v; int id = -1; for (int i = 0; i < N_PARAMS; i++) if (n == PARAM_INFO[i].cmd) id = i; if (id < 0) { printf("ERR unknown param %s\n", n.c_str()); continue; } m.setParam(id, v); }
        else if (c == "patch")  { int p; is >> p; m.loadPatch(p); }
        else if (c == "on")     { int n; is >> n; m.noteOn(n, 100); }
        else if (c == "off")    { int n; is >> n; m.noteOff(n); }
        else if (c == "pedal")  { int v; is >> v; m.setPedal(v); }
        else if (c == "bend")   { int v; is >> v; m.pitchBend(v); }
        else if (c == "panic")  { m.panic(); }
        else if (c == "all")    { m.allNotesOff(); }
        else if (c == "run")    { long n; is >> n; for (long i = 0; i < n; i++) { int16_t l, r; m.tick(l, r); rec.push_back(l); rec.push_back(r); } }
        else if (c == "skip")   { long n; is >> n; for (long i = 0; i < n; i++) { int16_t l, r; m.tick(l, r); } }
        else if (c == "write")  { std::string p; is >> p; FILE* f = fopen(p.c_str(), "wb"); fwrite(rec.data(), 2, rec.size(), f); fclose(f); rec.clear(); }
        else if (c == "voices") { printf("VOICES counted=%d active=%d\n", m.countedVoices(), m.activeCount()); }
        else if (c == "resp")   {                                   // magnitude of a registration path at one frequency
            std::string n; float hz; is >> n >> hz; Svf s = (n == "bass") ? m._fBass : (n == "cello") ? m._fCello : (n == "viola") ? m._fViola : (n == "violin") ? m._fViolin : m._fPizz;
            s.reset(); const int N = 44100; double acc = 0; int cnt = 0;
            for (int i = 0; i < N; i++) { float x = 0.1f * sinf(6.2831853f * hz * i / SAMPLE_RATE), lp, bp, hp; s.tick(x, lp, bp, hp);
                const float y = (n == "bass" || n == "cello") ? lp : bp; if (i > N / 2) { acc += y * y; cnt++; } }
            printf("RESP %s %.1f %.6f\n", n.c_str(), hz, sqrt(acc / cnt) / (0.1 / sqrt(2.0)));
        }
        else if (c == "ensprobe") {                                 // the Ensemble alone, impulse train -> stereo floats
            float depth, shim, mix; int rateV, n; std::string p; is >> depth >> shim >> mix >> rateV >> n >> p;
            static Ensemble e; e.clear(); e.phS = 0; e.phF = 0; const uint32_t inc = (uint32_t)hzToInc(ensHz(rateV)); std::vector<float> o(2 * n);
            for (int i = 0; i < n; i++) { float L, R; e.process((i % 1000 == 0) ? 1.0f : 0.0f, depth, shim, mix, inc, L, R); o[2 * i] = L; o[2 * i + 1] = R; }
            FILE* f = fopen(p.c_str(), "wb"); fwrite(o.data(), 4, 2 * n, f); fclose(f);
        }
        else if (c == "rawsaw") {                                   // the voice's PolyBLEP saw vs a naive one
            float hz; int n, naive; std::string p; is >> hz >> n >> naive >> p; std::vector<float> o(n); uint32_t ph = 0; const uint32_t inc = (uint32_t)hzToInc(hz); const float dt = hz / SAMPLE_RATE;
            for (int i = 0; i < n; i++) { ph += inc; float t = (float)ph * (1.0f / 4294967296.0f); o[i] = naive ? (2 * t - 1) : sawBlep(t, dt); }
            FILE* f = fopen(p.c_str(), "wb"); fwrite(o.data(), 4, n, f); fclose(f);
        }
        else { printf("ERR unknown command %s\n", c.c_str()); }
    }
    return 0;
}
