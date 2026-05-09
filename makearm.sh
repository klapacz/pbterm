#!/bin/sh
set -eu

: "${POCKETBOOK_SDK:=${SDK_ROOT:-../SDK/SDK_6.3.0/SDK-B288}}"
: "${BUILD_DIR:=build}"

cmake -S . -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE=Release \
  -DPOCKETBOOK_SDK="$POCKETBOOK_SDK"
cmake --build "$BUILD_DIR"
