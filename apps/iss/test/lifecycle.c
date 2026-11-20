// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../iss.c"
#include "../orbit.c"
#include "standin.h"

static state_t stored, live;
static int32_t seeded; /* writes of the test itself, not the application's */
static const char *values[] = { "51.23", "-4.56", "421.1235", "27580.6", "1794955920" };

/* Puts the first `size` bytes of `stored` where app_state_load() reads them. */
static void keep(int32_t size)
{
    assert(kv_set("state", 5, &stored, size) == HOST_OK);
    seeded++;
}

static int32_t writes(void) { return standin()->store_writes - seeded; }

/* The snapshot as the device holds it: one value, "state", a whole state_t. */
static void read_stored(void)
{
    int32_t length = 0, kept = 0;
    const uint8_t *bytes = standin_value("state", &length);
    assert(bytes != NULL && length == sizeof stored);
    memcpy(&stored, bytes, sizeof stored);
    for (int32_t index = 0; index < STANDIN_STORE_VALUES; index++) { kept += standin()->values[index].is_stored; }
    assert(kept == 1);
}

static void read_live(void)
{
    assert(standin()->session_length == sizeof live);
    memcpy(&live, standin()->session, sizeof live);
}

/* An answer with `values` in the manifest's pick order, through the SDK's picker. */
static void picked(int32_t status)
{
    char body[160];
    int32_t length = snprintf(body, sizeof body,
        "{\"latitude\":%s,\"longitude\":%s,\"altitude\":%s,\"velocity\":%s,\"timestamp\":%s}",
        values[0], values[1], values[2], values[3], values[4]);
    standin_picked(0, -1, status, body, length);
    assert(standin()->redraw_flags == HOST_REDRAW_NORMAL);
}

int main(void)
{
    standin_reset();
    standin_handlers.on_picked = app_on_picked;
    standin()->is_session_writable = true;
    stored = (state_t){ .version = 1, .has_data = 1, .status = 200, .altitude = 421, .minute = 29915930 };
    orbit_track_add(&stored.track, stored.minute, 1234, -5678);
    keep(sizeof stored);
    app_on_load();
    assert(state.version == 2 && state.altitude == 421000 && state.has_data);
    assert(!memcmp(&stored.track, &state.track, sizeof state.track) && writes() == 0);
    live = stored; live.minute++; live.altitude = 422; live.status = 503;
    assert(session_set(&live, sizeof live) == HOST_OK);
    app_on_focus();
    assert(state.altitude == 422000 && state.status == 200 && state.minute == live.minute);
    app_on_blur();
    read_live();
    assert(live.version == 2 && live.altitude == 422000);
    app_on_focus();
    assert(state.altitude == 422000); /* No second conversion of an already migrated snapshot. */

    keep(sizeof stored - 1);
    app_on_load();
    assert(state.version == 2 && !state.has_data && state.track.count == 0);
    picked(200);
    assert(state.has_data && state.altitude == 421124 && state.velocity == 27581);
    assert(state.track.count == 1 && writes() == 1);
    read_stored();
    values[2] = "421.1234"; values[4] = "1794955980";
    picked(200);
    assert(state.altitude == 421123 && writes() == 1 && state.track.count == 1);
    values[2] = "1999.9995"; values[4] = "1794956040";
    picked(200);
    assert(state.altitude == 421123 && state.status == HOST_FAILED_NO_ANSWER && state.has_data);
    values[2] = "423.000"; values[4] = "1794956280";
    picked(200);
    read_stored();
    assert(state.altitude == 423000 && state.track.count == 2 && stored.altitude == 423000);
    int32_t minute = state.minute;
    values[4] = "1794955920";
    picked(200);
    assert(state.minute == minute && state.altitude == 423000);
    picked(503);
    assert(state.status == 503 && state.has_data && state.altitude == 423000);
    assert(standin()->redraws == 6);

    read_stored();
    stored.version = 99; keep(sizeof stored);
    app_on_load(); assert(!state.has_data);
    stored.version = 1; stored.altitude = 2000; keep(sizeof stored);
    app_on_load(); assert(!state.has_data);
    stored.version = 2; stored.altitude = 423000; stored.track.count = TRACK_POINTS + 1; keep(sizeof stored);
    app_on_load(); assert(!state.has_data);
    puts("ISS lifecycle: metre rounding, v1 migration, live state, checkpoint cadence and last-good data passed");
}
