#!/bin/bash
# Fetches "Stevie Clavinet" (Jeff Learman's cleaned-up version of an anonymous Hohner clavinet from musical-artifacts.com #646; 255 FLAC files, about 61 MB)
# into samples/stevie-clavinet/.   LICENCE: the README says "License is unknown". Use it for yourself, do not redistribute it or a bank built from it.
set -e
cd "$(dirname "$0")"   # tools/piano: samples/ lives here (next to banks/)
mkdir -p samples
if [ -f "samples/stevie-clavinet/Stevie Clavinet.sfz" ]; then echo "already there: samples/stevie-clavinet"; exit 0; fi
rm -rf samples/stevie-clavinet
git clone --depth 1 https://github.com/jlearman/stevie-clavinet.git samples/stevie-clavinet
rm -rf samples/stevie-clavinet/.git
echo "$(ls samples/stevie-clavinet/flac/*.flac | wc -l) FLAC files in samples/stevie-clavinet/flac"
echo "licence: UNKNOWN (see samples/stevie-clavinet/README.md): private use only"
