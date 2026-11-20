#!/bin/sh
# SPDX-License-Identifier: MIT
set -eu
cd "$(dirname "$0")/../../.."
sdk=${PERINKPHERAL:-../perinkpheral-os}
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT HUP INT TERM
"$sdk/tools/perinkpheral" headers apps/rates "$build"
strip=-Wl,--gc-sections
if [ "$(uname -s)" = Darwin ]; then strip=-Wl,-dead_strip; fi
for test in history lifecycle; do
    ${CC:-cc} -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
        -ffunction-sections -fdata-sections "$strip" '-DHOST_IMPORT(name,...)=__VA_ARGS__' \
        -I"$build" -I"$sdk/sdk" -I"$sdk/sdk/lib" \
        "apps/rates/test/$test.c" apps/rates/history.c "$sdk/sdk/lib/app_days.c" \
        "$sdk/sdk/lib/app_text.c" "$sdk/sdk/lib/app_state.c" "$sdk/sdk/lib/json_pick.c" \
        -o "$build/$test"
done
python3 - "$build" <<'PY'
import json
import sys
from pathlib import Path
out = Path(sys.argv[1])
mocks = json.loads(Path('tests/rates/mocks.json').read_text())
(out / 'daily.json').write_text(mocks['https://www.cbr-xml-daily.ru/daily_json.js']['body'])
for url, response in mocks.items():
    for identifier, name in [('R01235', 'usd'), ('R01820', 'jpy')]:
        if url.endswith('VAL_NM_RQ=' + identifier):
            (out / (name + '.xml')).write_text(response['body'])
PY
"$build/history"
"$build/lifecycle" "$build/daily.json" "$build/usd.xml" "$build/jpy.xml"
