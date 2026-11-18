#!/bin/sh
# SPDX-License-Identifier: MIT
set -eu
cd "$(dirname "$0")/../.."
sdk=${PERINKPHERAL:-../perinkpheral}
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT HUP INT TERM

"$sdk/tools/perinkpheral" headers apps/stopwatch "$build"
strip=-Wl,--gc-sections
if [ "$(uname -s)" = Darwin ]; then
    strip=-Wl,-dead_strip
fi
${CC:-cc} -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -ffunction-sections -fdata-sections "$strip" -I"$build" -I"$sdk/sdk" -I"$sdk/sdk/lib" \
    tests/stopwatch/resume.c -o "$build/test_stopwatch_resume"
"$build/test_stopwatch_resume"
