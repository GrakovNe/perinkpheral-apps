#!/bin/sh
# SPDX-License-Identifier: MIT
set -eu
cd "$(dirname "$0")/../.."
sdk=${PERINKPHERAL:-../perinkpheral-os}
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT HUP INT TERM
"$sdk/tools/perinkpheral" headers apps/pomodoro "$build"
strip=-Wl,--gc-sections
if [ "$(uname -s)" = Darwin ]; then strip=-Wl,-dead_strip; fi
${CC:-cc} -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -ffunction-sections -fdata-sections "$strip" -I"$build" -I"$sdk/sdk" -I"$sdk/sdk/lib" \
    tests/pomodoro/stop.c -o "$build/test_pomodoro_stop"
"$build/test_pomodoro_stop"
