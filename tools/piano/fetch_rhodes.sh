#!/bin/bash
# Fetches the jRhodes3c Rhodes samples (Jeff Learman: a 1977 Rhodes Mark I Stage 73, 5 velocity layers, looped FLAC set, about 37 MB)
# into samples/jRhodes3c/.   LICENCE: CC BY-NC-SA 4.0 (attribution, NON-COMMERCIAL, SHARE-ALIKE): for your own use; a bank built from it carries the same terms.
set -e
cd "$(dirname "$0")"   # tools/piano: samples/ lives here (next to banks/)
mkdir -p samples
if [ -f samples/jRhodes3c/jRhodes3c-looped-flac-sfz/_jRhodes-mono-looped.sfz ]; then echo "already there: samples/jRhodes3c"; exit 0; fi
rm -rf samples/jRhodes3c
git clone --depth 1 https://github.com/sfzinstruments/jlearman.jRhodes3c.git samples/jRhodes3c
rm -rf samples/jRhodes3c/.git
echo "$(ls samples/jRhodes3c/jRhodes3c-looped-flac-sfz/*.flac | wc -l) FLAC files in samples/jRhodes3c/jRhodes3c-looped-flac-sfz"
echo "licence: CC BY-NC-SA 4.0, see samples/jRhodes3c/LICENSE"
