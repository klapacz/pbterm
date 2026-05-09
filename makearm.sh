#!/bin/sh
set -eu

: "${POCKETBOOK_SDK:=${SDK_ROOT:-../SDK/SDK_6.3.0/SDK-B288}}"
: "${GHOSTTY_VT_ROOT:=result-ghostty}"
: "${BUILD_DIR:=build}"

if [ ! -d "$GHOSTTY_VT_ROOT" ]; then
  echo "GHOSTTY_VT_ROOT '$GHOSTTY_VT_ROOT' does not exist." >&2
  echo "Build it first with: nix build .#ghostty-vt-arm --out-link result-ghostty" >&2
  exit 1
fi

cmake -S . -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE=Release \
  -DPOCKETBOOK_SDK="$POCKETBOOK_SDK" \
  -DGHOSTTY_VT_ROOT="$GHOSTTY_VT_ROOT"
cmake --build "$BUILD_DIR"
