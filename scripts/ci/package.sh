#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Ervin Notari Junior
#
# Signs and packages app/ into out/Mosaico-<version>.wgt (Linux equivalent of
# package.bat, used by the release). The certificates come from environment
# variables (GitHub secrets), never from the repository:
#   TIZEN_CLI                   path of the "tizen" executable
#   TIZEN_AUTHOR_P12_BASE64     author.p12 in base64
#   TIZEN_AUTHOR_PASSWORD       password of author.p12
#   TIZEN_DISTRIBUTOR_P12_BASE64  distributor.p12 in base64
#   TIZEN_DISTRIBUTOR_PASSWORD  password of distributor.p12
set -eu

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
: "${TIZEN_CLI:?}" "${TIZEN_AUTHOR_P12_BASE64:?}" "${TIZEN_AUTHOR_PASSWORD:?}"
: "${TIZEN_DISTRIBUTOR_P12_BASE64:?}" "${TIZEN_DISTRIBUTOR_PASSWORD:?}"

VERSION=$(sed -n 's/.*<widget [^>]*version="\([^"]*\)".*/\1/p' "$ROOT/app/config.xml")
[ -n "$VERSION" ] || { echo "[package] version not found in config.xml" >&2; exit 1; }
[ -f "$ROOT/app/wasm/player.wasm" ] || { echo "[package] app/wasm/player.wasm missing; run scripts/build.sh" >&2; exit 1; }

# Certificates only in a temporary directory, removed at the end.
KEYS=$(mktemp -d)
trap 'rm -rf "$KEYS"' EXIT
printf '%s' "$TIZEN_AUTHOR_P12_BASE64" | base64 -d > "$KEYS/author.p12"
printf '%s' "$TIZEN_DISTRIBUTOR_P12_BASE64" | base64 -d > "$KEYS/distributor.p12"

PROFILE=mosaico-release
# The profile is written by hand: on Linux, "tizen security-profiles add" keeps
# the passwords in the system keyring (gnome-keyring), which does not exist on a
# headless runner, and signing fails with "Invalid password". The CLI accepts the
# password in profiles.xml itself; it stays in the temporary folder, removed at the end.
xml_escape() {
  printf '%s' "$1" | sed -e 's/&/\&amp;/g' -e 's/</\&lt;/g' -e 's/>/\&gt;/g' -e 's/"/\&quot;/g'
}
cat > "$KEYS/profiles.xml" <<EOF
<?xml version="1.0" encoding="UTF-8" standalone="no"?>
<profiles active="$PROFILE" version="3.1">
<profile name="$PROFILE">
<profileitem ca="" distributor="0" key="$KEYS/author.p12" password="$(xml_escape "$TIZEN_AUTHOR_PASSWORD")" rootca=""/>
<profileitem ca="" distributor="1" key="$KEYS/distributor.p12" password="$(xml_escape "$TIZEN_DISTRIBUTOR_PASSWORD")" rootca=""/>
<profileitem ca="" distributor="2" key="" password="" rootca=""/>
</profile>
</profiles>
EOF
chmod 600 "$KEYS/profiles.xml"
"$TIZEN_CLI" cli-config "profiles.path=$KEYS/profiles.xml" >/dev/null

# Licenses and credits go inside the app (About screen).
mkdir -p "$ROOT/app/licenses"
cp "$ROOT/LICENSE" "$ROOT/NOTICE" "$ROOT/THIRD_PARTY_NOTICES.md" "$ROOT/CREDITS.md" "$ROOT/app/licenses/"

OUT="$ROOT/out"
rm -rf "$OUT"
mkdir -p "$OUT"
"$TIZEN_CLI" package -t wgt -s "$PROFILE" -o "$OUT" -- "$ROOT/app"
WGT=$(ls "$OUT"/*.wgt)
FINAL="$OUT/Mosaico-$VERSION.wgt"
mv "$WGT" "$FINAL"

# The CLI produces an unsigned package without an error when something goes
# wrong: require both signatures in the package.
for sig in author-signature.xml signature1.xml; do
  unzip -l "$FINAL" | grep -q " $sig\$" || { echo "[package] package without $sig" >&2; exit 1; }
done
(cd "$OUT" && sha256sum "$(basename "$FINAL")" > "$(basename "$FINAL").sha256")
echo "[package] OK: $FINAL"
