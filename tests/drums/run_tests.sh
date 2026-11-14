#!/bin/bash
# Build and run every host test suite against ../src (override: SRC=/path/to/src ./run_tests.sh).
#   ./run_tests.sh          everything (the 250k-event torture and the two-thread race included)
set -e
cd "$(dirname "$0")"
bash ./stage.sh "${SRC:-../src}"
SAN="-fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=float-cast-overflow"
W="-Wall -Wextra -Wno-unused-function -Wno-unused-parameter -Wno-unused-variable -Wno-misleading-indentation"
F="-std=gnu++17 -O1 -g $W $SAN -I mock -I stage -I ."
G="stage/wavetable.cpp mock/mock_globals.cpp mock/mock_globals2.cpp"
g++ $F test_engine.cpp  $G -pthread -o build_engine
g++ $F test_lcd.cpp     $G -pthread -o build_test_lcd
g++ $F test_storage.cpp $G -o build_storage
g++ $F test_hex.cpp     $G -o build_hex
# MIDI parsers: the REAL midi_handler.h, once omni and once with MIDI_CHANNEL 10
rm -rf stage_midi && mkdir stage_midi && cp ../src/midi_handler.h stage_midi/
M="-std=gnu++17 -O1 -g $W -fsanitize=address,undefined -I mock_midi -I mock -I stage_midi"
cp ../src/config.h stage_midi/config.h
g++ $M test_midi.cpp mock/mock_globals.cpp mock/mock_globals2.cpp -o build_midi
sed 's/#define MIDI_CHANNEL        0 /#define MIDI_CHANNEL       10 /' ../src/config.h > stage_midi/config.h
g++ $M -DTEST_CH10 test_midi.cpp mock/mock_globals.cpp mock/mock_globals2.cpp -o build_midi10
echo "--- dsp (pitch, decay, spectra, choke, velocity / level / pan laws, headroom)"; python3 test_dsp.py | tail -1
echo "--- sequencer (timing, swing, tempo, transport, pattern queue, song, record, MIDI clock, pads, CCs)"; python3 test_seq.py | tail -1
echo "--- engine (params, ring, storage image, torture, two-thread race, delivery)"; ./build_engine | tail -1
echo "--- storage wrapper (LittleFS: save / load / power cut / corruption)"; ./build_storage | tail -1
echo "--- serial image transfer (hex helpers, full round trip)"; ./build_hex | tail -1
echo "--- display (4 pages, gestures, fuzz)"; ./build_test_lcd | tail -1
echo "--- MIDI parsers (omni + channel 10)"; ./build_midi | tail -1; ./build_midi10 | tail -1
echo "--- audio driver (both boards)"; AUDIO_SRC=../src python3 test_audio.py | tail -1
echo "--- drum editor (tools/drums: image round trip against the firmware, library, API, audition engine, USB protocol)"; python3 test_editor.py | tail -1
