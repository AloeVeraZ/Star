#!/bin/sh
# Runs the desktop checks against the real sketch sources:
#   shake_test - simulated shakes, flicks, walking, running and knocks through
#                the sketch's shake/twist detectors (awake and wake-from-sleep)
#   touch_test - finger tracking, taps, flick direction, holds, lost lift reports
#   soak_test  - hours of random input through the animator and renderer,
#                with address/undefined-behaviour sanitizers
set -e
cd "$(dirname "$0")"
SRC=../../StarFace
OUT=${TMPDIR:-/tmp}
g++ -std=gnu++17 -O2 -Wall -I$SRC shake_test.cpp -o "$OUT/starface_shake_test"
"$OUT/starface_shake_test"
g++ -std=gnu++17 -O2 -Wall -I$SRC touch_test.cpp -o "$OUT/starface_touch_test"
"$OUT/starface_touch_test"
g++ -std=gnu++17 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined \
  -Istubs -I$SRC soak_test.cpp $SRC/EyeRenderer.cpp $SRC/Eyes.cpp $SRC/Expressions.cpp $SRC/CreatureAnimator.cpp -o "$OUT/starface_soak_test"
"$OUT/starface_soak_test"
