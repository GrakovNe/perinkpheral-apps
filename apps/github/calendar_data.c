// SPDX-License-Identifier: MIT
/* GitHub's nested weeks exceed the SDK picker's depth and wildcard
 * limits. This selector follows the calendar path and consumes one day
 * object at a time, regardless of chunk or property order. The SDK's
 * streaming validator checks the complete document before it is kept. */
#include "calendar_data.h"
#include "app_days.h"
#include "app_text.h"
#include "app_values.h"

enum { OTHER, ROOT, DATA, USER, COLLECTION, CALENDAR, WEEKS, WEEK, DAYS, DAY };
enum { BETWEEN, STRING, NUMBER };

int32_t calendar_level(const contribution_calendar_t *calendar, int32_t index)
{
    if (index < 0 || index >= calendar->count) { return 0; }
    int32_t bit = index * 3, byte = bit / 8, shift = bit % 8;
    uint32_t pair = calendar->levels[byte];
    if (byte + 1 < CALENDAR_BYTES) { pair |= (uint32_t)calendar->levels[byte + 1] << 8; }
    return (pair >> shift) & 7;
}
static void level_set(contribution_calendar_t *calendar, int32_t index, int32_t level)
{
    int32_t bit = index * 3;
    for (int32_t part = 0; part < 3; part++) {
        if (level & (1 << part)) { calendar->levels[(bit + part) / 8] |= 1u << ((bit + part) % 8); }
    }
}
int32_t calendar_streak(const contribution_calendar_t *calendar, int32_t today)
{
    int32_t index = today - calendar->first_day, streak = 0;
    if (index < 0 || index >= calendar->count) { return 0; }
    while (index >= 0 && calendar_level(calendar, index--)) { streak++; }
    return streak;
}
static int32_t date_read(const char *date)
{
    if (app_text_length(date) != 10 || date[4] != '-' || date[7] != '-') { return -1; }
    int32_t year = 0, month = 0, day = 0;
    for (int32_t index = 0; index < 10; index++) {
        if (index == 4 || index == 7) { continue; }
        if (date[index] < '0' || date[index] > '9') { return -1; }
        int32_t digit = date[index] - '0';
        if (index < 4) { year = year * 10 + digit; }
        else if (index < 7) { month = month * 10 + digit; }
        else { day = day * 10 + digit; }
    }
    if (year < 1970 || year > 2100 || month < 1 || month > 12 || day < 1 || day > 31) { return -1; }
    int32_t days = app_days_from_date(year, month, day), actual_year, actual_month, actual_day;
    app_date_from_days(days, &actual_year, &actual_month, &actual_day);
    return year == actual_year && month == actual_month && day == actual_day ? days : -1;
}
void calendar_picker_start(calendar_picker_t *picker)
{
    *picker = (calendar_picker_t){0};
    json_pick_init(&picker->validation, 0, 0);
}
static void scalar(calendar_picker_t *picker, bool string)
{
    if (picker->depth <= 0) { return; }
    int32_t top = picker->depth - 1, role = picker->frames[top].role;
    const char *key = picker->frames[top].key;
    picker->token[picker->length] = '\0';
    if (picker->frames[top].key_next && string) {
        app_text_copy(picker->frames[top].key, sizeof picker->frames[top].key, picker->overflow ? "" : picker->token);
        picker->frames[top].key_next = false;
        return;
    }
    if (role == CALENDAR && app_text_equal(key, "totalContributions")) {
        int32_t total;
        if (string || picker->overflow || ++picker->total_fields != 1 ||
            !app_parse_number(picker->token, 0, &total) || total < 0) { picker->broken = true; }
        else { picker->calendar.total = total; }
    } else if (role == USER && string && app_text_equal(key, "login")) {
        if (picker->overflow || picker->length >= (int32_t)sizeof picker->login) { picker->broken = true; }
        app_text_copy(picker->login, sizeof picker->login, picker->token);
    } else if (role == DAY && app_text_equal(key, "date")) {
        if (!string || picker->overflow || (picker->day_fields & 1)) { picker->broken = true; }
        picker->day = date_read(picker->token);
        picker->day_fields |= 1;
    } else if (role == DAY && app_text_equal(key, "contributionLevel")) {
        static const char *const LEVELS[] = { "NONE", "FIRST_QUARTILE", "SECOND_QUARTILE", "THIRD_QUARTILE", "FOURTH_QUARTILE" };
        if (!string || picker->overflow || (picker->day_fields & 2)) { picker->broken = true; }
        picker->level = -1;
        for (int32_t level = 0; level < 5; level++) {
            if (app_text_equal(picker->token, LEVELS[level])) { picker->level = level; }
        }
        picker->day_fields |= 2;
    }
}
static void open_container(calendar_picker_t *picker, bool array)
{
    if (picker->depth == 16) { picker->broken = true; return; }
    int32_t role = OTHER;
    if (!picker->depth && !array) { role = ROOT; }
    else if (picker->depth) {
        int32_t parent = picker->frames[picker->depth - 1].role;
        const char *key = picker->frames[picker->depth - 1].key;
        if (!array) {
            if (parent == ROOT && app_text_equal(key, "data")) { role = DATA; }
            if (parent == DATA && app_text_equal(key, "user")) { role = USER; }
            if (parent == USER && app_text_equal(key, "contributionsCollection")) { role = COLLECTION; }
            if (parent == COLLECTION && app_text_equal(key, "contributionCalendar")) { role = CALENDAR; }
            if (parent == WEEKS) { role = WEEK; }
            if (parent == DAYS) { role = DAY; picker->day_fields = 0; picker->day = -1; picker->level = -1; }
        } else {
            if (parent == CALENDAR && app_text_equal(key, "weeks")) { role = WEEKS; }
            if (parent == WEEK && app_text_equal(key, "contributionDays")) { role = DAYS; }
        }
    }
    picker->frames[picker->depth].role = role;
    picker->frames[picker->depth].array = array;
    picker->frames[picker->depth].key_next = !array;
    picker->frames[picker->depth].key[0] = '\0';
    picker->depth++;
}
static void close_container(calendar_picker_t *picker)
{
    if (picker->depth <= 0) { picker->broken = true; return; }
    if (picker->frames[--picker->depth].role != DAY) { return; }
    int32_t count = picker->calendar.count;
    if (picker->day_fields != 3 || picker->day < 0 || picker->level < 0 || count == CALENDAR_DAYS ||
        (count && picker->day != picker->calendar.first_day + count)) { picker->broken = true; return; }
    if (!count) { picker->calendar.first_day = picker->day; }
    level_set(&picker->calendar, count, picker->level);
    picker->calendar.count++;
}
static void append(calendar_picker_t *picker, uint8_t value)
{
    if (picker->length < (int32_t)sizeof picker->token - 1) { picker->token[picker->length++] = (char)value; }
    else { picker->overflow = true; }
}
void calendar_picker_feed(calendar_picker_t *picker, const uint8_t *bytes, int32_t length)
{
    if (!json_pick_feed(&picker->validation, (const char *)bytes, (size_t)length)) { picker->broken = true; }
    for (int32_t index = 0; index < length && !picker->broken; index++) {
        uint8_t value = bytes[index];
        if (picker->mode == STRING) {
            if (picker->unicode_left) {
                int32_t digit = value <= '9' ? value - '0' : (value | 32) - 'a' + 10;
                picker->unicode_value = picker->unicode_value * 16 + digit;
                if (!--picker->unicode_left) { append(picker, picker->unicode_value < 128 ? picker->unicode_value : 127); }
            } else if (picker->escaped) {
                picker->escaped = false;
                if (value == 'u') { picker->unicode_left = 4; picker->unicode_value = 0; }
                else { append(picker, value == '"' || value == '\\' || value == '/' ? value : 127); }
            } else if (value == '\\') { picker->escaped = true; }
            else if (value == '"') { scalar(picker, true); picker->mode = BETWEEN; }
            else { append(picker, value); }
            continue;
        }
        if (picker->mode == NUMBER) {
            if (value != ',' && value != '}' && value != ']' && value > ' ') { append(picker, value); continue; }
            scalar(picker, false);
            picker->mode = BETWEEN;
        }
        if (value == '"') { picker->mode = STRING; picker->length = 0; picker->overflow = false; }
        else if (value == '{' || value == '[') { open_container(picker, value == '['); }
        else if (value == '}' || value == ']') { close_container(picker); }
        else if (value == ',' && picker->depth) { picker->frames[picker->depth - 1].key_next = !picker->frames[picker->depth - 1].array; }
        else if (value != ':' && value > ' ') { picker->mode = NUMBER; picker->length = 0; picker->overflow = false; append(picker, value); }
    }
}
bool calendar_picker_finish(calendar_picker_t *picker)
{
    return json_pick_finish(&picker->validation) && !picker->broken && !picker->depth &&
        picker->total_fields == 1 && picker->calendar.count >= 365 && picker->calendar.count <= 367 && picker->login[0];
}
