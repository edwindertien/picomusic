#!/bin/bash
# Build and run every host test suite against ../src (override: SRC=/path/to/src bash run_tests.sh).
set -e
cd "$(dirname "$0")"
bash ./stage.sh "${SRC:-../src}"
SAN="-fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=float-cast-overflow"
W="-Wall -Wextra -Wno-unused-function -Wno-unused-parameter -Wno-unused-variable -Wno-misleading-indentation"
F="-std=gnu++17 -O1 -g $W $SAN -I mock -I stage -I ."
G="stage/piano_data.cpp mock/mock_globals.cpp mock/mock_globals2.cpp"
g++ $F test_engine.cpp $G -pthread -o build_engine
g++ $F test_lcd.cpp    $G -pthread -o build_test_lcd
# MIDI parsers: the REAL midi_handler.h, once omni and once with MIDI_CHANNEL 10
rm -rf stage_midi && mkdir stage_midi && cp ../src/midi_handler.h stage_midi/
M="-std=gnu++17 -O1 -g $W -fsanitize=address,undefined -I mock_midi -I mock -I stage_midi"
cp ../src/config.h stage_midi/config.h
g++ $M test_midi.cpp mock/mock_globals.cpp mock/mock_globals2.cpp -o build_midi
sed 's/#define MIDI_CHANNEL        0 /#define MIDI_CHANNEL       10 /' ../src/config.h > stage_midi/config.h
g++ $M -DTEST_CH10 test_midi.cpp mock/mock_globals.cpp mock/mock_globals2.cpp -o build_midi10
echo "--- dsp (bank, player vs recording, pitch of all 88 keys, dynamics, pedal, damper, decay, stealing, stereo, bend, tune)"; python3 test_dsp.py | tail -1
echo "--- engine (params, zones, ring, key mask, torture, two-thread race, delivery, extremes)"; ./build_engine | tail -1
echo "--- display (knobs, 88-key keyboard, pedals, fuzz)"; ./build_test_lcd | tail -1
echo "--- MIDI parsers (omni + channel 10)"; ./build_midi | tail -1; ./build_midi10 | tail -1
echo "--- audio driver (both boards)"; AUDIO_SRC=../src python3 test_audio.py | tail -1
echo "--- effects (wah, tremolo / auto-pan, chorus, reverb; bypass is bit-exact)"; python3 test_fx.py | tail -1
echo "--- bank tool (config, SFZ, budget, loops through the real engine, shipped bank rebuilt byte-for-byte if samples/ is present)"; python3 test_bank.py $([ -d ../samples/salamander ] && echo --full) | tail -1
echo "--- bank GUI (Flask API, audition through the real engine, frontend script in Node)"; python3 test_gui.py | tail -1
