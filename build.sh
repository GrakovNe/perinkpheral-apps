#!/bin/sh
# SPDX-License-Identifier: MIT
# Builds every application in apps/ and writes the catalog a device
# installs from. Needs the perinkpheral repository for its SDK and tools
# (PERINKPHERAL, by default a checkout beside this one) and wasi-sdk
# (WASI_SDK_PATH).
set -e
here=$(cd "$(dirname "$0")" && pwd)
sdk=${PERINKPHERAL:-$here/../perinkpheral}
if [ ! -f "$sdk/tools/catalog/mkcatalog.py" ]; then
    echo "no perinkpheral checkout at $sdk: set PERINKPHERAL to where it is" >&2
    exit 1
fi
exec python3 "$sdk/tools/catalog/mkcatalog.py" "$here/apps" --out "$here/catalog"
