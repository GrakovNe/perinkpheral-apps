// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define HOST_IMPORT(name, ...) __VA_ARGS__
#include "../../apps/github/github.c"

static uint8_t storage[sizeof(state_t)];
static int32_t storage_length;
static int writes;
bool app_state_load(const char *key, void *value, int32_t size, int32_t version)
{
    assert(strcmp(key, "state") == 0);
    if (storage_length != size) return false;
    memcpy(value, storage, (size_t)size);
    return *(int32_t *)value == version;
}
bool app_state_save(const char *key, const void *value, int32_t size)
{
    assert(strcmp(key, "state") == 0 && size == sizeof state);
    memcpy(storage, value, (size_t)size);
    storage_length = size;
    writes++;
    return true;
}
int main(void)
{
    state_v1_t old = { .version = 1, .has_data = 1, .status = 200, .login_hash = 123,
        .calendar = { .first_day = 20000, .count = 366, .total = 42 } };
    for (size_t byte = 0; byte < sizeof old.calendar.levels; byte++) old.calendar.levels[byte] = (uint8_t)byte;
    memcpy(storage, &old, sizeof old);
    storage_length = sizeof old;
    app_on_load();
    assert(state.version == VERSION && state.has_data == 1 && state.status == 200 && state.login_hash == 123);
    assert(state.calendar.first_day == 20000 && state.calendar.count == 366 && state.calendar.total == 42);
    assert(memcmp(state.calendar.levels, old.calendar.levels, sizeof old.calendar.levels) == 0);
    for (size_t byte = sizeof old.calendar.levels; byte < sizeof state.calendar.levels; byte++) {
        assert(state.calendar.levels[byte] == 0);
    }
    assert(writes == 1 && storage_length == sizeof state);
    memset(&state, 0, sizeof state);
    app_on_load();
    assert(state.has_data && state.calendar.count == 366 && writes == 1);
    storage_length = 0;
    app_on_load();
    assert(state.version == VERSION && !state.has_data && writes == 1);
    puts("GitHub: legacy calendar snapshot preserved and migration writes only once");
    return 0;
}
