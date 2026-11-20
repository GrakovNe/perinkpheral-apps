// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../calendar.c"
#include "../ics.c"
#include "standin.h"

static const char valid[] = "BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\nUID:one\r\nDTSTART:20260115T140000Z\r\nDTEND:20260115T150000Z\r\nSUMMARY:Meeting\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";

static int file_writes(void) { return standin()->file_writes; }

/* The events as the device keeps them: one file, written whole, and no write over the host's limits. */
static const stored_events_t *event_file(void)
{
    int32_t size = 0;
    const uint8_t *bytes = standin_file(EVENTS_FILE, &size);
    assert(bytes != NULL && size == sizeof(stored_events_t) && standin()->refused_what[0] == '\0');
    return (const stored_events_t *)bytes;
}
static void answer(const char *payload, int32_t length, int chunk)
{
    standin_data(0, -1, 200, payload, length, chunk, STANDIN_WHOLE, 0);
}
int main(void)
{
    standin_reset();
    standin_handlers.on_data = app_on_data;
    /* 2026-01-15 12:00 UTC, in UTC: the day of the event. */
    standin_clock(1768478400);
    standin()->zone_offset = 0;
    standin_set("feed", "https://calendar.invalid/feed"); standin_set("remind", "Off");
    app_on_load();
    answer(valid, sizeof valid - 1, 1);
    assert(stored.count == 1 && state.status == 200 && file_writes() == 1);
    assert(event_file()->count == 1);
    int written = file_writes();
    for (int poll = 0; poll < 96; poll++) answer(valid, sizeof valid - 1, 17);
    assert(file_writes() == written);
    const char partial[] = "BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\n";
    answer(partial, sizeof partial - 1, 2);
    assert(stored.count == 1 && state.status == STATUS_NOT_A_CALENDAR && file_writes() == written);
    answer(valid, (int32_t)(strstr(valid, "END:VCALENDAR") - valid), 1);
    assert(stored.count == 1 && state.status == STATUS_NOT_A_CALENDAR && file_writes() == written);
    const char empty[] = "BEGIN:VCALENDAR\r\nEND:VCALENDAR";
    standin()->refuse_writes = HOST_ERR_NO_SPACE; answer(empty, sizeof empty - 1, 3);
    assert(stored.count == 0 && events_dirty);
    standin()->refuse_writes = HOST_OK; answer(empty, sizeof empty - 1, 1);
    assert(!events_dirty && event_file()->count == 0);
    puts("calendar: one-byte streams, 96 unchanged polls, missing footer, valid empty calendar and failed save retry passed");
}
