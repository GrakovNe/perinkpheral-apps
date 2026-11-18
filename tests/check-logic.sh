#!/bin/sh
# SPDX-License-Identifier: MIT
# Pure calculations and the streamed, realistic GraphQL fixture, offline.
set -eu
cd "$(dirname "$0")/.."
sdk=${PERINKPHERAL:-../perinkpheral}
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT HUP INT TERM
${CC:-cc} -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    tests/air/ranking.c -o "$build/air"
"$build/air"
${CC:-cc} -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -include tests/github/host_mocks.h -I"$sdk/sdk" -I"$sdk/sdk/lib" tests/github/picker.c apps/github/calendar_data.c \
    "$sdk/sdk/lib/json_pick.c" "$sdk/sdk/lib/app_days.c" \
    "$sdk/sdk/lib/app_values.c" "$sdk/sdk/lib/app_fixed.c" "$sdk/sdk/lib/app_text.c" \
    -o "$build/github"
"$build/github"
${CC:-cc} -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    tests/iss/orbit.c apps/iss/orbit.c -o "$build/iss"
"$build/iss"
"$sdk/tools/perinkpheral" headers apps/iss "$build"
strip=-Wl,--gc-sections
if [ "$(uname -s)" = Darwin ]; then strip=-Wl,-dead_strip; fi
${CC:-cc} -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -ffunction-sections -fdata-sections "$strip" '-DHOST_IMPORT(name,...)=__VA_ARGS__' \
    -I"$build" -I"$sdk/sdk" -I"$sdk/sdk/lib" \
    tests/iss/lifecycle.c apps/iss/orbit.c "$sdk/sdk/lib/app_state.c" \
    "$sdk/sdk/lib/app_values.c" "$sdk/sdk/lib/app_fixed.c" "$sdk/sdk/lib/app_text.c" \
    -o "$build/iss-lifecycle"
"$build/iss-lifecycle"

sh tests/network/check.sh
