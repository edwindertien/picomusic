#!/bin/bash
# DX7 host tests: the REAL dx7_manager.h / dx7_patch.h / midi_handler.h / stereo_fx.h, ASan + UBSan + float-cast-overflow.
# Run through ../run_tests.sh dx7 (stages the firmware into ../src), or directly with SRC=/path/to/src.
set -e
cd "$(dirname "$0")"
SRC=${SRC:-../src}
W="-Wall -Wextra -Wno-unused-function -Wno-unused-parameter -Wno-unused-variable -Wno-misleading-indentation"
SAN="-fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=float-cast-overflow"
g++ -std=gnu++17 -O1 -g $W $SAN -I mock -I mock_midi -I "$SRC" test_dx7.cpp mock/mock_globals.cpp mock/mock_globals2.cpp -pthread -o build_dx7
echo "--- DX7 engine (algorithms, packed voices, pitch, FM sidebands vs Bessel, envelopes, velocity, LFO, voices, SysEx, torture, race)"
./build_dx7 | tee build_dx7.log | grep -E "FAIL|PASS$|TESTS" | grep -v "^PASS" | tail -20
grep -q "ALL DX7 ENGINE TESTS PASS" build_dx7.log
echo "--- audio driver (both boards)"
[ -f test_audio.py ] && AUDIO_SRC="$SRC" python3 test_audio.py | tail -1 || true
echo "--- demo render (offline, writes demo.wav)"
g++ -std=gnu++17 -O2 -I mock -I mock_midi -I "$SRC" render.cpp mock/mock_globals.cpp mock/mock_globals2.cpp -o build_render && ./build_render demo.wav | tail -1
