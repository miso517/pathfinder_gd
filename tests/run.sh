#!/usr/bin/env bash
# Builds and runs the engine independent test suite.
#
# Everything under src/core and src/format compiles without Geometry Dash, Geode
# or cocos2d, so the .slc codecs, the candidate generator and the search can be
# verified on any machine with a C++20 compiler - no game, no SDK, no Windows.
#
#     bash tests/run.sh
#
# Override the compiler with CXX=clang++ bash tests/run.sh
set -euo pipefail

cd "$(dirname "$0")/.."

CXX="${CXX:-g++}"
OUT="${OUT:-build-tests/pathfinder-tests}"

mkdir -p "$(dirname "$OUT")"

echo "compiling the core with $CXX"
"$CXX" -std=c++20 -O1 -Wall -Wextra -o "$OUT" \
    tests/main.cpp \
    src/core/Search.cpp \
    src/core/Plans.cpp \
    src/format/Slc.cpp \
    src/format/SlcV2.cpp \
    src/format/SlcV3.cpp \
    src/format/Dump.cpp

echo "running $OUT"
"$OUT"
