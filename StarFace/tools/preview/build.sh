#!/bin/sh
# Builds the desktop preview against the real sketch sources.
set -e
cd "$(dirname "$0")"
SRC=../../StarFace
g++ -std=gnu++17 -O2 -Wall -Wno-unused-function -Istubs -I$SRC \
  preview.cpp $SRC/EyeRenderer.cpp $SRC/CreatureAnimator.cpp -o preview
