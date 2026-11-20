// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define HOST_IMPORT(name, ...) __VA_ARGS__
#include "../../apps/iss/iss.c"

static state_t stored, live;
static int32_t stored_size, live_size;
static int writes, redraws;
static const char *values[] = { "51.23", "-4.56", "421.1235", "27580.6", "-23.5", "181.01", "1794955920" };

int32_t kv_get(const char *key, int32_t key_length, void *buffer, int32_t capacity)
{
    assert(key_length == 5 && !memcmp(key, "state", 5) && capacity >= stored_size);
    memcpy(buffer, &stored, (size_t)stored_size);
    return stored_size;
}
int32_t kv_set(const char *key, int32_t key_length, const void *buffer, int32_t length)
{
    assert(key_length == 5 && !memcmp(key, "state", 5) && length == sizeof stored);
    memcpy(&stored, buffer, (size_t)length); stored_size = length; writes++;
    return HOST_OK;
}
int32_t session_get(void *buffer, int32_t capacity)
{
    assert(capacity >= live_size); memcpy(buffer, &live, (size_t)live_size); return live_size;
}
int32_t session_set(const void *buffer, int32_t length)
{
    assert(length == sizeof live); memcpy(&live, buffer, (size_t)length); live_size = length; return HOST_OK;
}
int32_t answer_value(int32_t index, char *buffer, int32_t capacity)
{
    assert(index >= 0 && index < 7);
    int32_t length = (int32_t)strlen(values[index]); assert(length <= capacity);
    memcpy(buffer, values[index], (size_t)length); return length;
}
void screen_redraw(int32_t flags) { assert(flags == HOST_REDRAW_NORMAL); redraws++; }

int main(void)
{
    stored = (state_t){ .version = 1, .has_data = 1, .status = 200, .altitude = 421, .minute = 29915930 };
    orbit_track_add(&stored.track, stored.minute, 1234, -5678);
    stored_size = sizeof stored;
    app_on_load();
    assert(state.version == 2 && state.altitude == 421000 && state.has_data);
    assert(!memcmp(&stored.track, &state.track, sizeof state.track) && writes == 0);
    live = stored; live.minute++; live.altitude = 422; live.status = 503; live_size = sizeof live;
    app_on_focus();
    assert(state.altitude == 422000 && state.status == 200 && state.minute == live.minute);
    app_on_blur();
    assert(live.version == 2 && live.altitude == 422000);
    app_on_focus();
    assert(state.altitude == 422000); /* No second conversion of an already migrated snapshot. */

    stored_size--;
    app_on_load();
    assert(state.version == 2 && !state.has_data && state.track.count == 0);
    app_on_picked(0, 200);
    assert(state.has_data && state.altitude == 421124 && state.velocity == 27581);
    assert(state.solar_longitude == -17899 && state.track.count == 1 && writes == 1);
    values[2] = "421.1234"; values[6] = "1794955980";
    app_on_picked(0, 200);
    assert(state.altitude == 421123 && writes == 1 && state.track.count == 1);
    values[2] = "1999.9995"; values[6] = "1794956040";
    app_on_picked(0, 200);
    assert(state.altitude == 421123 && state.status == HOST_FAILED_NO_ANSWER && state.has_data);
    values[2] = "423.000"; values[6] = "1794956280";
    app_on_picked(0, 200);
    assert(state.altitude == 423000 && state.track.count == 2 && stored.altitude == 423000);
    int32_t minute = state.minute;
    values[6] = "1794955920";
    app_on_picked(0, 200);
    assert(state.minute == minute && state.altitude == 423000);
    app_on_picked(0, 503);
    assert(state.status == 503 && state.has_data && state.altitude == 423000);
    assert(redraws == 6);

    stored.version = 99;
    app_on_load(); assert(!state.has_data);
    stored.version = 1; stored.altitude = 2000;
    app_on_load(); assert(!state.has_data);
    stored.version = 2; stored.altitude = 423000; stored.track.count = TRACK_POINTS + 1;
    app_on_load(); assert(!state.has_data);
    puts("ISS lifecycle: metre rounding, v1 migration, live state, checkpoint cadence and last-good data passed");
}
