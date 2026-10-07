#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Ervin Notari Junior
#
# Installs the Tizen CLI (Tizen Studio web-cli) for Linux, used to sign and
# package the .wgt. Needs Java in the PATH.
# Usage: setup-tizen-cli.sh <destination folder>
# The installer only accepts a destination inside the home folder and does not run as root.
# Prints the path of the "tizen" executable.
set -eu

DEST=${1:?usage: setup-tizen-cli.sh <destination-folder>}
case "$DEST" in
  "$HOME"/*) ;;
  *) echo "[setup-tizen-cli] the destination must be inside $HOME" >&2; exit 1 ;;
esac
VERSION=6.1
URL=https://download.tizen.org/sdk/Installer/tizen-studio_$VERSION/web-cli_Tizen_Studio_${VERSION}_ubuntu-64.bin
TIZEN="$DEST/tools/ide/bin/tizen"

if [ ! -x "$TIZEN" ]; then
  INSTALLER=$(mktemp)
  echo "[setup-tizen-cli] downloading Tizen Studio $VERSION (web-cli)" >&2
  curl -fsSL -o "$INSTALLER" "$URL"
  chmod +x "$INSTALLER"
  # The installer is a bash script and exits successfully even when it refuses
  # the destination: hence the check right below.
  bash "$INSTALLER" --accept-license "$DEST" >&2
  rm -f "$INSTALLER"
fi
[ -x "$TIZEN" ] || { echo "[setup-tizen-cli] installation failed: $TIZEN does not exist" >&2; exit 1; }
echo "$TIZEN"
