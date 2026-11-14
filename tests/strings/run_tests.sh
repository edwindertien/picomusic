#!/bin/bash
# Build and run every host test suite against ../src (override: SRC=/path/to/src ./run_tests.sh).
#   ./run_tests.sh          full suite (includes the 250k-event torture and the two-thread race tests)
#   ./run_tests.sh quick    fast pass
set -e
cd "$(dirname "$0")"
./stage.sh "${SRC:-../src}"
F="-std=gnu++17 -O1 -g -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -Wno-unused-variable
   -fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=float-cast-overflow -I mock -I stage -I ."
G="mock/mock_globals.cpp mock/mock_globals2.cpp"
g++ $F test_engine.cpp stage/wavetable.cpp $G -pthread -o build_test_engine
g++ $F test_lcd.cpp    stage/wavetable.cpp $G -pthread -o build_test_lcd
echo "--- dsp (alias rejection, registration filters, loudness balance, ensemble, envelopes, headroom)"; python3 test_dsp.py | tail -1
echo "--- engine";  ./build_test_engine "${1:-}" | tail -1
echo "--- display"; ./build_test_lcd | tail -1
echo "--- audio driver (both boards)"; python3 test_audio.py | tail -1
