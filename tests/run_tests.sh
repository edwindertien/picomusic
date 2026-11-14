#!/bin/bash
# Host-side test suites: the REAL firmware sources are compiled for the PC (g++ with AddressSanitizer + UBSan) against mocked Arduino /
# pico-sdk headers, and exercised by C++ and Python tests. Nothing here needs a Pico.
#
#   tests/run_tests.sh                 every instrument
#   tests/run_tests.sh piano           one instrument  (tonewheel synth strings bass drums piano dx7)
#   tests/run_tests.sh strings quick   extra arguments go to that instrument's own run_tests.sh
#
# Each suite was written for a flat project (src/, host_test/, tools/ ...). So for every run the firmware is STAGED into
# tests/.build/<instrument>/: firmware/shared/ + firmware/<instrument>/ flattened into src/, the suite copied to host_test/, and for the
# piano the bank tool copied to tools/ and banks/. The suites run unchanged against the files you edit in firmware/.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
ALL="tonewheel synth strings bass drums piano dx7"

stage() {
  local i=$1 B=$ROOT/tests/.build/$1
  rm -rf "$B"; mkdir -p "$B/src"
  cp -R "$ROOT/firmware/shared/." "$B/src/"
  cp -R "$ROOT/firmware/$i/." "$B/src/"
  cp -R "$ROOT/tests/$i" "$B/host_test"
  if [ "$i" = piano ] || [ "$i" = dx7 -a -d "$ROOT/tools/$i" ]; then
    mkdir -p "$B/tools" "$B/banks" "$B/samples"
    (cd "$ROOT/tools/$i" && for f in $(ls -A); do case $f in banks|samples) ;; *) cp -R "$f" "$B/tools/";; esac; done)
    [ -d "$ROOT/tools/$i/banks" ]   && cp -R "$ROOT/tools/$i/banks/."   "$B/banks/"
    [ -d "$ROOT/tools/$i/samples" ] && cp -R "$ROOT/tools/$i/samples/." "$B/samples/"
  fi
}

run_one() {
  local i=$1; shift
  [ -d "$ROOT/tests/$i" ] || { echo "no tests for $i"; return 0; }
  echo "=================== $i"
  stage "$i"
  (cd "$ROOT/tests/.build/$i/host_test" && bash ./run_tests.sh "$@")
}

if [ -z "$1" ] || [ "$1" = all ]; then for i in $ALL; do run_one "$i"; done
else i=$1; shift; run_one "$i" "$@"; fi
