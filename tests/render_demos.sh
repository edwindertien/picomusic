#!/bin/bash
# Renders the tonewheel and synth demo WAVs with the REAL engines (audio/demo_tonewheel.mp3, audio/demo_synth.mp3 were made this way).
#   tests/render_demos.sh [outdir]      -> outdir/tonewheel.wav, outdir/synth.wav   (default /tmp/picomusic_demos)
# (The DX7 demo: tests/dx7/run_tests.sh renders demo.wav; bass / drums / strings / piano demos come from their own tools.)
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd); OUT=${1:-/tmp/picomusic_demos}; M=$ROOT/tests/dx7/mock
mkdir -p "$OUT/tw" "$OUT/sy"
cp -R "$ROOT/firmware/shared/." "$ROOT/firmware/tonewheel/." "$OUT/tw/"; cp -R "$ROOT/firmware/shared/." "$ROOT/firmware/synth/." "$OUT/sy/"
g++ -std=gnu++17 -O2 -include "$ROOT/tests/tonewheel/host_shim.h" -I "$M" -I "$OUT/tw" "$ROOT/tests/tonewheel/render.cpp" "$M/mock_globals.cpp" "$M/mock_globals2.cpp" -o "$OUT/render_tw"
g++ -std=gnu++17 -O2 -I "$M" -I "$OUT/sy" "$ROOT/tests/synth/render.cpp" "$OUT/sy/wavetable.cpp" "$M/mock_globals.cpp" "$M/mock_globals2.cpp" -o "$OUT/render_sy"
"$OUT/render_tw" "$OUT/tonewheel.wav"; "$OUT/render_sy" "$OUT/synth.wav"
echo "MP3: ffmpeg -i $OUT/tonewheel.wav -af loudnorm=I=-18:TP=-1.5 -b:a 160k audio/demo_tonewheel.mp3   (same for synth)"
