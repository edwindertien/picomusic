#!/bin/bash
# Build and run every host test suite against ../src (override: SRC=/path/to/src bash run_tests.sh).
set -e
cd "$(dirname "$0")"
bash ./stage.sh "${SRC:-../src}"
SAN="-fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=float-cast-overflow"
W="-Wall -Wextra -Wno-unused-function -Wno-unused-parameter -Wno-unused-variable -Wno-misleading-indentation"
F="-std=gnu++17 -O1 -g $W $SAN -I mock -I stage -I ."
G="stage/bass_tables.cpp mock/mock_globals.cpp mock/mock_globals2.cpp"
g++ $F test_engine.cpp  $G -pthread -o build_engine
g++ $F test_lcd.cpp     $G -pthread -o build_test_lcd
g++ $F test_storage.cpp $G -o build_storage
# MIDI parsers: the REAL midi_handler.h, once omni and once with MIDI_CHANNEL 10
rm -rf stage_midi && mkdir stage_midi && cp ../src/midi_handler.h stage_midi/
M="-std=gnu++17 -O1 -g $W -fsanitize=address,undefined -I mock_midi -I mock -I stage_midi"
cp ../src/config.h stage_midi/config.h
g++ $M test_midi.cpp mock/mock_globals.cpp mock/mock_globals2.cpp -o build_midi
sed 's/#define MIDI_CHANNEL        0 /#define MIDI_CHANNEL       10 /' ../src/config.h > stage_midi/config.h
g++ $M -DTEST_CH10 test_midi.cpp mock/mock_globals.cpp mock/mock_globals2.cpp -o build_midi10
echo "--- dsp (pitch, waveforms, alias, ladder, self-oscillation, tracking, envelopes, glide, mono priority, loudness)"; python3 test_dsp.py | tail -1
echo "--- sequencer (timing, gate, swing, slide, accent, transport, queue, keys, step-write, MIDI clock, CCs)"; python3 test_seq.py | tail -1
echo "--- engine (params, key stack vs model, patterns, save image, torture, two-thread race, delivery)"; ./build_engine | tail -1
echo "--- storage wrapper (LittleFS: save / load / power cut / corruption)"; ./build_storage | tail -1
echo "--- display (4 pages, gestures, fuzz)"; ./build_test_lcd | tail -1
echo "--- MIDI parsers (omni + channel 10)"; ./build_midi | tail -1; ./build_midi10 | tail -1
echo "--- audio driver (both boards)"; AUDIO_SRC=../src python3 test_audio.py | tail -1
