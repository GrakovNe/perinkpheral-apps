#!/bin/sh
# SPDX-License-Identifier: MIT
set -eu
cd "$(dirname "$0")/../../.."
sdk=${PERINKPHERAL:-../perinkpheral}
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT HUP INT TERM
strip=-Wl,--gc-sections
if [ "$(uname -s)" = Darwin ]; then
    strip=-Wl,-dead_strip
fi
${CC:-cc} -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -ffunction-sections -fdata-sections "$strip" -I"$sdk/sdk" -I"$sdk/sdk/lib" \
    apps/badapple/test/player.c -o "$build/player"
"$build/player" apps/badapple/files/frames.bin "$build/frames.raw"
python3 - "$build/frames.raw" <<'PY'
import hashlib
import sys
from pathlib import Path
# All 165 original 200x150 frames, expanded exactly 2x to the 400x300 panel.
expected = '8513888dbd041f2f7a9d7ffb918aef01d8343d91fb0977b18541da0170b434ee'
assert hashlib.sha256(Path(sys.argv[1]).read_bytes()).hexdigest() == expected
print('All 165 rendered frames match the original pixels')
PY
