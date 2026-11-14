#!/bin/bash
# Fetches the LOSSLESS Salamander Grand Piano V3 (Yamaha C5, Alexander Holm, CC BY 3.0) from github.com/sfzinstruments/SalamanderGrandPiano:
# the 30 notes (A0 .. C8, every minor third) of ONE velocity layer, 48 kHz / 24 bit FLAC, about 45 MB, into samples/salamander-flac/ (named A0.flac, Ds1.flac ...).
# The repository has 16 velocity layers (v1 .. v16, 748 MB in all); layer 10 covers velocities 73..80, the velocity the engine plays "as recorded".
#   tools/piano/fetch_salamander.sh          layer 10
#   tools/piano/fetch_salamander.sh 13       another layer (v13 = velocities 97..104: a brighter, harder grand)
# (An earlier version of this script fetched 38 kbit/s MP3s from the Tone.js repository: they have no content above ~5 kHz and are not worth using.)
set -e
cd "$(dirname "$0")"   # tools/piano: samples/ lives here (next to banks/)
LAYER="${1:-10}"
mkdir -p samples && cd samples
if ls salamander-flac/A0.flac >/dev/null 2>&1 && [ "$(ls salamander-flac | wc -l)" -ge 30 ] && [ -z "$1" ]; then echo "already there: samples/salamander-flac ($(ls salamander-flac | wc -l) files)"; exit 0; fi
rm -rf _sg salamander-flac && mkdir salamander-flac
git -c gc.auto=0 clone --quiet --depth 1 --filter=blob:none --no-checkout https://github.com/sfzinstruments/SalamanderGrandPiano.git _sg
cd _sg
FILES=$(git -c gc.auto=0 ls-tree -r --name-only HEAD | grep -E "^Samples/[A-G][#s]?[0-9]v${LAYER}\.flac$" || true)
N=$(echo "$FILES" | grep -c flac || true)
if [ "$N" -lt 30 ]; then echo "found $N files for layer $LAYER (expected 30): wrong layer number? (1..16)"; cd ..; rm -rf _sg; exit 1; fi
git -c gc.auto=0 checkout --quiet HEAD -- $FILES
for f in Samples/*v${LAYER}.flac; do b=$(basename "$f" "v${LAYER}.flac"); cp "$f" "../salamander-flac/$(echo "$b" | sed 's/#/s/').flac"; done
cd .. && rm -rf _sg
echo "$(ls salamander-flac | wc -l) lossless files (layer $LAYER) in samples/salamander-flac, $(du -sh salamander-flac | cut -f1)"
echo "licence: Creative Commons Attribution 3.0 (Alexander Holm): see NOTICE.md"
