#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Ervin Notari Junior
#
# Downloads and activates the Samsung Emscripten SDK (fastcomp 1.39.4.7, with
# the Tizen extensions: WASM Player and Tizen Sockets) for Linux.
# Usage: setup-emsdk.sh <destination folder>
# Prints the path of the emsdk folder (use it as SAMSUNG_EMSDK).
set -eu

DEST=${1:?usage: setup-emsdk.sh <destination-folder>}
# Public link from the Samsung download page (no login required):
# https://developer.samsung.com/smarttv/develop/extension-libraries/webassembly/download.html
URL=https://developer.samsung.com/smarttv/file/a5013a65-af11-4b59-844f-2d34f14d19a9
EMSDK="$DEST/emscripten-release-bundle/emsdk"

if [ ! -x "$EMSDK/emsdk" ]; then
  mkdir -p "$DEST"
  echo "[setup-emsdk] downloading emscripten-1.39.4.7-linux64.zip" >&2
  curl -fsSL -A "Mozilla/5.0" -o "$DEST/emsdk.zip" "$URL"
  unzip -q -o "$DEST/emsdk.zip" -d "$DEST"
  rm -f "$DEST/emsdk.zip"
fi

# Writes the ~/.emscripten that emcc uses (paths of LLVM fastcomp and Node).
(cd "$EMSDK" && ./emsdk activate latest-fastcomp >&2)
echo "$EMSDK"
