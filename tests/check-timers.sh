#!/bin/sh
# SPDX-License-Identifier: MIT
set -eu
cd "$(dirname "$0")/.."
sdk=${PERINKPHERAL:-../perinkpheral}
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT HUP INT TERM
strip=-Wl,--gc-sections
if [ "$(uname -s)" = Darwin ]; then strip=-Wl,-dead_strip; fi
"$sdk/tools/perinkpheral" headers apps/timer "$build"
for test in clock/scheduling timer/lifecycle; do
    ${CC:-cc} -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
        -ffunction-sections -fdata-sections "$strip" \
        -I"$build" -I"$sdk/sdk" -I"$sdk/sdk/lib" \
        "tests/$test.c" -o "$build/test"
    "$build/test"
done
