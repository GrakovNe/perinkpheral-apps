#!/bin/sh
# SPDX-License-Identifier: MIT
set -eu
cd "$(dirname "$0")/../.."
sdk=${PERINKPHERAL:-../perinkpheral-os}
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT HUP INT TERM
strip=-Wl,--gc-sections
if [ "$(uname -s)" = Darwin ]; then strip=-Wl,-dead_strip; fi
for app in ha weather calendar; do
    mkdir -p "$build/$app"
    "$sdk/tools/perinkpheral" headers "apps/$app" "$build/$app"
    extra=
    if [ "$app" = calendar ]; then extra="apps/calendar/ics.c $sdk/sdk/lib/app_days.c"; fi
    ${CC:-cc} -std=c11 -Wall -Wextra -Werror -Wno-unused-function -fsanitize=address,undefined \
        '-DHOST_IMPORT(name,...)=__VA_ARGS__' -ffunction-sections -fdata-sections "$strip" \
        -I"$build/$app" -I"$sdk/sdk" -I"$sdk/sdk/lib" "tests/network/$app.c" \
        "$sdk/sdk/lib/app_text.c" "$sdk/sdk/lib/app_state.c" "$sdk/sdk/lib/app_values.c" \
        "$sdk/sdk/lib/app_fixed.c" $extra -o "$build/$app/test"
    "$build/$app/test"
done
