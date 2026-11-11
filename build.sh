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
# The SDK's sample goes into the catalog too: a device carries it from the
# start, and one that removed it has to be able to get it back.
exec python3 "$sdk/tools/catalog/mkcatalog.py" "$here/apps" "$sdk/sdk/apps" --out "$here/catalog"
