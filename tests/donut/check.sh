#!/bin/sh
# SPDX-License-Identifier: MIT
set -eu
cd "$(dirname "$0")/../.."
sdk=${PERINKPHERAL:-../perinkpheral}
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT HUP INT TERM

"$sdk/tools/perinkpheral" headers apps/donut "$build"
strip=-Wl,--gc-sections
if [ "$(uname -s)" = Darwin ]; then
    strip=-Wl,-dead_strip
fi
${CC:-cc} -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -ffunction-sections -fdata-sections "$strip" '-DHOST_IMPORT(name,...)=__VA_ARGS__' \
    -I"$build" -I"$sdk/sdk" -I"$sdk/sdk/lib" \
    tests/donut/render.c "$sdk/sdk/lib/app_state.c" "$sdk/sdk/lib/app_values.c" \
    "$sdk/sdk/lib/app_fixed.c" "$sdk/sdk/lib/app_text.c" -lm -o "$build/test_donut"
"$build/test_donut"
