// SPDX-License-Identifier: MIT
#include "support.h"
#include "../../apps/calendar/calendar.c"
static const char valid[] = "BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\nUID:one\r\nDTSTART:20260115T140000Z\r\nDTEND:20260115T150000Z\r\nSUMMARY:Meeting\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";
static void answer(const char *payload, int32_t length, int chunk)
{
    for (int32_t at = 0; at < length; at += chunk) {
        int32_t taken = length - at < chunk ? length - at : chunk;
        app_on_data(0, (const uint8_t *)payload + at, taken,
            (at == 0 ? HOST_DATA_FIRST : 0) | (at + taken == length ? HOST_DATA_LAST : 0), 200);
    }
}
int main(void)
{
    setting("feed", "https://calendar.invalid/feed"); setting("remind", "Off");
    app_on_load();
    answer(valid, sizeof valid - 1, 1);
    assert(stored.count == 1 && state.status == 200 && file_writes == 1);
    int written = file_writes;
    for (int poll = 0; poll < 96; poll++) answer(valid, sizeof valid - 1, 17);
    assert(file_writes == written);
    const char partial[] = "BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\n";
    answer(partial, sizeof partial - 1, 2);
    assert(stored.count == 1 && state.status == STATUS_NOT_A_CALENDAR && file_writes == written);
    answer(valid, (int32_t)(strstr(valid, "END:VCALENDAR") - valid), 1);
    assert(stored.count == 1 && state.status == STATUS_NOT_A_CALENDAR && file_writes == written);
    const char empty[] = "BEGIN:VCALENDAR\r\nEND:VCALENDAR";
    write_result = HOST_ERR_NO_SPACE; answer(empty, sizeof empty - 1, 3);
    assert(stored.count == 0 && events_dirty);
    write_result = HOST_OK; answer(empty, sizeof empty - 1, 1);
    assert(!events_dirty && ((stored_events_t *)event_file)->count == 0);
    puts("calendar: one-byte streams, 96 unchanged polls, missing footer, valid empty calendar and failed save retry passed");
}
