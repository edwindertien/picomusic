// adpcm_tool for codec 2 (src/piano_adpcm.h: IMA quantiser + 4-tap sign-error LMS predictor)
//   enc   <pred0> <idx0> [h1 h2 h3]         : raw int16 LE on stdin -> ADPCM bytes on stdout (greedy)
//   (every mode takes the optional history h1 h2 h3 = the three samples before pred0, newest first, after its last fixed argument: a stream that continues after a PCM head)
//   encb  <pred0> <idx0> <beam>   : a beam search over code sequences: minimises the squared error of the whole stream (beam 1 = greedy nearest level)
//   dec   <pred0> <idx0> <n>      : ADPCM bytes on stdin -> n raw int16 LE samples on stdout
//   state <pred0> <idx0> <n>      : decode n samples, print the decoder state "h0 h1 h2 h3 w0 w1 w2 w3 idx" (what a loop jump must restore)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include "../src/piano_adpcm.h"
int main(int argc, char** argv) {
    if (argc < 4) return 1;
    AdpcmState st; st.init(atoi(argv[2]), atoi(argv[3]));
    // optional history for a stream that continues after a PCM head: the 3 samples before pred0 (newest first), at argv[hist..hist+2]
    const std::string mode0 = argv[1]; const int hist = (mode0 == "enc") ? 4 : 5;
    if (argc >= hist + 3) { st.h[1] = atoi(argv[hist]); st.h[2] = atoi(argv[hist + 1]); st.h[3] = atoi(argv[hist + 2]); }
    std::vector<uint8_t> in; uint8_t buf[65536]; size_t n; while ((n = fread(buf, 1, sizeof buf, stdin)) > 0) in.insert(in.end(), buf, buf + n);
    const std::string mode = argv[1];
    if (mode == "enc") {
        const size_t ns = in.size() / 2; std::vector<uint8_t> out((ns + 1) / 2, 0);
        for (size_t i = 0; i < ns; i++) { int16_t x; memcpy(&x, &in[2 * i], 2); const uint8_t c = st.encode(x); out[i >> 1] |= (i & 1) ? (uint8_t)(c << 4) : c; }
        fwrite(out.data(), 1, out.size(), stdout);
    } else if (mode == "encb") {
        const int B = argc > 4 ? atoi(argv[4]) : 8; const size_t ns = in.size() / 2; std::vector<uint8_t> out((ns + 1) / 2, 0);
        struct Cand { AdpcmState s; double cost; int32_t parent; uint8_t nib; };
        std::vector<AdpcmState> beam{ st }; std::vector<double> bcost{ 0.0 }; std::vector<Cand> cand; std::vector<std::vector<int32_t>> par(ns); std::vector<std::vector<uint8_t>> nb(ns);
        for (size_t i = 0; i < ns; i++) {
            int16_t x; memcpy(&x, &in[2 * i], 2); cand.clear();
            for (size_t b = 0; b < beam.size(); b++) for (int code = 0; code < 16; code++) {
                Cand c; c.s = beam[b]; const int16_t y = c.s.decode((uint8_t)code); const double e = (double)x - (double)y; c.cost = bcost[b] + e * e; c.parent = (int32_t)b; c.nib = (uint8_t)code; cand.push_back(c);
            }
            if ((int)cand.size() > B) { std::nth_element(cand.begin(), cand.begin() + B, cand.end(), [](const Cand& a, const Cand& b) { return a.cost < b.cost; }); cand.resize(B); }
            beam.resize(cand.size()); bcost.resize(cand.size()); par[i].resize(cand.size()); nb[i].resize(cand.size());
            for (size_t k = 0; k < cand.size(); k++) { beam[k] = cand[k].s; bcost[k] = cand[k].cost; par[i][k] = cand[k].parent; nb[i][k] = cand[k].nib; }
        }
        size_t best = 0; for (size_t k = 1; k < bcost.size(); k++) if (bcost[k] < bcost[best]) best = k;
        for (size_t i = ns; i-- > 0;) { const uint8_t c = nb[i][best]; out[i >> 1] |= (i & 1) ? (uint8_t)(c << 4) : c; best = (size_t)par[i][best]; }
        fwrite(out.data(), 1, out.size(), stdout);
    } else if (mode == "state") {
        const size_t ns = (size_t)atol(argv[4]); for (size_t i = 0; i < ns; i++) { const uint8_t b = in[i >> 1]; st.decode((i & 1) ? (b >> 4) : (b & 15)); }
        printf("%d %d %d %d %d %d %d %d %d\n", st.h[0], st.h[1], st.h[2], st.h[3], st.w[0], st.w[1], st.w[2], st.w[3], st.idx);
    } else {
        const size_t ns = (size_t)atol(argv[4]); std::vector<int16_t> out(ns);
        for (size_t i = 0; i < ns; i++) { const uint8_t b = in[i >> 1]; out[i] = st.decode((i & 1) ? (b >> 4) : (b & 15)); }
        fwrite(out.data(), 2, ns, stdout);
    }
    return 0;
}
