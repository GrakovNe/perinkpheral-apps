// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../apps/github/calendar_data.h"
#include "app_days.h"

/* A parser has no picked host response; these imports are deliberately empty. */
int32_t answer_value(int32_t index, char *text, int32_t capacity)
{ (void)index; (void)text; (void)capacity; return HOST_ERR_NOT_FOUND; }
int32_t answer_count(int32_t index) { (void)index; return 0; }
int32_t answer_element(int32_t index, int32_t element, char *text, int32_t capacity)
{ (void)index; (void)element; (void)text; (void)capacity; return HOST_ERR_NOT_FOUND; }
int32_t time_text(int64_t moment, int32_t style, char *text, int32_t capacity)
{ (void)moment; (void)style; (void)text; (void)capacity; return 0; }
static char response[100000];
static calendar_picker_t picker;
static void feed(int32_t length, int32_t piece)
{
    calendar_picker_start(&picker);
    for (int32_t offset = 0; offset < length; offset += piece) {
        calendar_picker_feed(&picker, (uint8_t *)response + offset, length - offset < piece ? length - offset : piece);
    }
}
int main(void)
{
    FILE *file = fopen("tests/github/calendar.json", "rb");
    assert(file);
    int32_t length = (int32_t)fread(response, 1, sizeof response, file);
    fclose(file);
    const int32_t pieces[] = { 1, 2, 7, 63, 511, 1024 };
    for (int32_t test = 0; test < 6; test++) {
        feed(length, pieces[test]);
        assert(calendar_picker_finish(&picker));
        assert(picker.calendar.count == 366);
        assert(picker.calendar.first_day == app_days_from_date(2025, 11, 17));
        assert(picker.calendar.total == 2259);
        assert(calendar_streak(&picker.calendar, app_days_from_date(2026, 11, 17)) == 9);
        assert(calendar_streak(&picker.calendar, app_days_from_date(2026, 11, 18)) == 0);
        assert(calendar_level(&picker.calendar, -1) == 0);
        for (int32_t day = 0; day < picker.calendar.count; day++) {
            int32_t weekday = (picker.calendar.first_day + day + 4) % 7;
            int32_t expected = day % 19 < 3 || weekday == 0 ? 0 : (day * 7 + day / 13) % 4 + 1;
            if (day >= 357) { expected = 2; }
            assert(calendar_level(&picker.calendar, day) == expected);
        }
    }
    feed(length - 1, 7); /* the final newline is optional */
    assert(calendar_picker_finish(&picker));
    feed(length - 4, 7); /* a truncated document must not replace the snapshot */
    assert(!calendar_picker_finish(&picker));
    char *level = strstr(response, "FIRST_QUARTILE");
    assert(level); level[0] = 'X';
    feed(length, 1);
    assert(!calendar_picker_finish(&picker));
    level[0] = 'F';
    char *date = strstr(response, "2025-11-17");
    assert(date);
    date[8] = '3'; date[9] = '2';
    feed(length, 7);
    assert(!calendar_picker_finish(&picker));
    puts("GitHub picker: chunking, packed levels, dates, streaks and incomplete data passed");
}
