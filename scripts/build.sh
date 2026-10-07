#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Ervin Notari Junior
#
# Linux/macOS equivalent of build-openh264.bat + build-wasm.bat (used by CI).
# Builds OpenH264 (once) and the player into app/wasm/.
# Usage: SAMSUNG_EMSDK=<emsdk folder> scripts/build.sh
# Keep the source list and flags the same as in build-wasm.bat.
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
: "${SAMSUNG_EMSDK:?set SAMSUNG_EMSDK (emsdk folder of the Samsung SDK)}"
EM="$SAMSUNG_EMSDK/fastcomp/emscripten"
SRC="$ROOT/wasm/third_party/openh264/codec"
[ -d "$SRC/decoder" ] || { echo "[build] wasm/third_party/openh264 is empty: run \"git submodule update --init\" first" >&2; exit 1; }
LIBDIR="$ROOT/build/openh264"
LIB="$LIBDIR/libopenh264dec.a"
OUT="$ROOT/app/wasm"
# The SDK's em++/emar wrappers call "python", which recent Ubuntu lacks;
# call the .py files directly with python3.
PYTHON=${PYTHON:-python3}
EMPP="$PYTHON $EM/em++.py"
EMAR="$PYTHON $EM/emar.py"

# ---- OpenH264 (decoder) -------------------------------------------------
if [ ! -f "$LIB" ]; then
  mkdir -p "$LIBDIR/obj"
  rm -f "$LIBDIR"/obj/*.o
  for dir in common/src decoder/core/src decoder/plus/src; do
    for f in "$SRC/$dir"/*.cpp; do
      echo "[openh264] $(basename "$f")"
      $EMPP -O3 -DNDEBUG -pthread -s USE_PTHREADS=1 -Wno-everything \
        -I"$SRC/api/wels" -I"$SRC/common/inc" \
        -I"$SRC/decoder/core/inc" -I"$SRC/decoder/plus/inc" \
        -c "$f" -o "$LIBDIR/obj/$(basename "$f" .cpp).o"
    done
  done
  $EMAR rcs "$LIB" "$LIBDIR"/obj/*.o
fi

# ---- Player ----------------------------------------------------------------
mkdir -p "$OUT"
$EMPP -std=gnu++14 -O3 -Wall \
  -I"$ROOT/wasm/include" \
  -I"$SRC/api/wels" \
  "$ROOT/wasm/src/md5.cpp" \
  "$ROOT/wasm/src/rtsp_url.cpp" \
  "$ROOT/wasm/src/rtsp_protocol.cpp" \
  "$ROOT/wasm/src/rtsp_connection.cpp" \
  "$ROOT/wasm/src/video.cpp" \
  "$ROOT/wasm/src/h264.cpp" \
  "$ROOT/wasm/src/h265.cpp" \
  "$ROOT/wasm/src/events.cpp" \
  "$ROOT/wasm/src/native_player.cpp" \
  "$ROOT/wasm/src/soft_decoder.cpp" \
  "$ROOT/wasm/src/soft_renderer.cpp" \
  "$ROOT/wasm/src/discovery.cpp" \
  "$ROOT/wasm/src/bench.cpp" \
  "$ROOT/wasm/src/player_main.cpp" \
  "$LIB" \
  -s ENVIRONMENT_MAY_BE_TIZEN \
  -pthread -s USE_PTHREADS=1 -s PTHREAD_POOL_SIZE=18 \
  -s TOTAL_MEMORY=268435456 \
  -s NO_EXIT_RUNTIME=1 \
  -s "EXPORTED_FUNCTIONS=['_main','_malloc','_free']" \
  -s "EXTRA_EXPORTED_RUNTIME_METHODS=['ccall','UTF8ToString']" \
  -o "$OUT/player.js"
echo "[build] OK: $OUT"
ls "$OUT"
