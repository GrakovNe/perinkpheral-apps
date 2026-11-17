#!/bin/sh
# SPDX-License-Identifier: MIT
# Builds every application in apps/ and writes the catalog a device
# installs from. Needs the perinkpheral repository for its SDK and tools
# (PERINKPHERAL, by default a checkout beside this one), wasi-sdk
# (WASI_SDK_PATH) and Java 21 or later.
set -e
here=$(cd "$(dirname "$0")" && pwd)
sdk=${PERINKPHERAL:-$here/../perinkpheral}
if [ ! -x "$sdk/tools/perinkpheral" ]; then
    echo "no perinkpheral checkout at $sdk: set PERINKPHERAL to where it is" >&2
    exit 1
fi
"$sdk/tools/perinkpheral" catalog "$here/apps" --out "$here/catalog"
# The glyph packs - letters beyond ASCII for the device's fonts - are made
# by tools/perinkpheral glyph-packs of the perinkpheral repository; what it
# made is kept in glyphs/ and goes into the catalog as it is.
mkdir -p "$here/catalog/glyphs"
cp "$here"/glyphs/* "$here/catalog/glyphs/"
