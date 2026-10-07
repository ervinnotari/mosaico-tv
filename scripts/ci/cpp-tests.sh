#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Ervin Notari Junior
#
# C++ tests that need neither network nor the Samsung SDK (MD5, Digest,
# SDP, SPS, depacketizers), compiled natively with the system compiler.
# The same tests run in WASM with scripts\test-wasm.bat.
#
# Optional environment: CXX (compiler), CXXFLAGS (extra flags, e.g.
# "--coverage -O0" for the Sonar analysis) and OUT (test binary path).
set -eu

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
CXX=${CXX:-g++}
CXXFLAGS=${CXXFLAGS:-}
OUT=${OUT:-${TMPDIR:-/tmp}/mosaico_unit_tests}
mkdir -p "$(dirname "$OUT")"

# CXXFLAGS holds several flags: split on purpose.
# shellcheck disable=SC2086
"$CXX" -std=c++14 -O1 -Wall -Wextra -Werror $CXXFLAGS \
  -I"$ROOT/wasm/include" \
  "$ROOT/wasm/src/md5.cpp" \
  "$ROOT/wasm/src/rtsp_protocol.cpp" \
  "$ROOT/wasm/src/rtsp_url.cpp" \
  "$ROOT/wasm/src/video.cpp" \
  "$ROOT/wasm/src/h264.cpp" \
  "$ROOT/wasm/src/h265.cpp" \
  "$ROOT/wasm/tests/unit_tests.cpp" \
  -o "$OUT"
"$OUT"
