#!/bin/bash
# Regenerates every figure and table used by README.md. Needs g++, python3 with matplotlib + Pillow.
# Run from anywhere:  tools/make_figures.sh
set -e
cd "$(dirname "$0")/.."
mkdir -p docs tools/.gen
python3 tools/gen_tables.py src tools/.gen > /dev/null
(cd host_test && ./stage.sh ../src && g++ -std=gnu++17 -O1 -Wno-unused -I mock -I stage render_panel.cpp stage/wavetable.cpp \
   mock/mock_globals.cpp mock/mock_globals2.cpp -pthread -o build_render_panel && ./build_render_panel)
python3 tools/panel_images.py tools/.gen
python3 tools/diagrams.py
python3 tools/audio_timing.py
echo "figures written to docs/;  tables in tools/.gen/t_*.md"
