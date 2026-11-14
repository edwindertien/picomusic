#!/bin/bash
# The tonewheel organ's host test: its audio driver (both Pico-Audio versions) against a mocked pico-sdk, replayed through a PIO emulator.
# (The organ's engine suites live in the project history; this one keeps the shared audio driver honest for every board setting.)
set -e
cd "$(dirname "$0")"
echo "--- audio driver (both boards)"; python3 test_audio.py | tail -1
