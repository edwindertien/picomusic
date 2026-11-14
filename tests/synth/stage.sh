#!/bin/bash
# Copy the real sources to stage/, replacing ONLY the three hardware-bound platform headers
# (audio_driver.h, midi_handler.h, usb_host.h) with the mocks from plat/.
# Usage: ./stage.sh [path-to-src]      (default: ../src)
set -e
cd "$(dirname "$0")"
SRC=${1:-../src}
rm -rf stage && mkdir stage
for f in "$SRC"/*.h "$SRC"/*.cpp; do
  b=$(basename "$f")
  case $b in audio_driver.h|midi_handler.h|usb_host.h) continue;; esac
  cp "$f" stage/
done
cp plat/*.h stage/
