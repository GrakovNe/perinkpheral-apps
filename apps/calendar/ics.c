/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "ics.h"

#include <stddef.h>

#define SECONDS_PER_DAY 86400
#define DAYS_PER_WEEK   7
/* The interpreter needs about four bytes for every byte of code that runs:
 * what is called from many places is kept one function, not a copy in each. */
#define KEPT_ONE __attribute__((noinline))
#define NOT_AN_ORIGINAL INT64_MIN /* an occurrence that was moved is nobody's original */

enum { FREQ_NONE, FREQ_DAILY, FREQ_WEEKLY, FREQ_MONTHLY, FREQ_YEARLY };

/* ---- dates ---- */

/* Days since 1970-01-01 of a date of the calendar everybody uses. */
KEPT_ONE static int32_t days_of(int32_t year, int32_t month, int32_t day)
{
    year -= month <= 2;
    int32_t era = (year >= 0 ? year : year - 399) / 400;
    int32_t year_of_era = year - era * 400;
    int32_t day_of_year = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    int32_t day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    return era * 146097 + day_of_era - 719468;
}

static void date_of(int32_t days, int32_t *year, int32_t *month, int32_t *day)
{
    days += 719468;
    int32_t era = (days >= 0 ? days : days - 146096) / 146097;
    int32_t day_of_era = days - era * 146097;
    int32_t year_of_era =
        (day_of_era - day_of_era / 1460 + day_of_era / 36524 - day_of_era / 146096) / 365;
    int32_t day_of_year = day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
    int32_t shifted_month = (5 * day_of_year + 2) / 153;
    *day = day_of_year - (153 * shifted_month + 2) / 5 + 1;
    *month = shifted_month + (shifted_month < 10 ? 3 : -9);
    *year = year_of_era + era * 400 + (*month <= 2);
}

/* Monday is 0. */
KEPT_ONE static int32_t weekday_of(int32_t days)
{
    int32_t weekday = (days + 3) % DAYS_PER_WEEK;
    return weekday < 0 ? weekday + DAYS_PER_WEEK : weekday;
}

KEPT_ONE static int32_t days_in_month(int32_t year, int32_t month)
{
    return days_of(month == 12 ? year + 1 : year, month == 12 ? 1 : month + 1, 1) -
           days_of(year, month, 1);
}

/* The `nth` such day of the week in a month - 1 the first, -1 the last -
 * as days since 1970, or -1 when the month has no such day. */
KEPT_ONE static int32_t nth_weekday_of(int32_t year, int32_t month, int32_t nth, int32_t weekday)
{
    int32_t first = days_of(year, month, 1), length = days_in_month(year, month);
    if (nth > 0) {
        int32_t day = first + (weekday - weekday_of(first) + DAYS_PER_WEEK) % DAYS_PER_WEEK +
                      (nth - 1) * DAYS_PER_WEEK;
        return day < first + length ? day : -1;
    }
    int32_t last = first + length - 1;
    int32_t day = last - (weekday_of(last) - weekday + DAYS_PER_WEEK) % DAYS_PER_WEEK +
                  (nth + 1) * DAYS_PER_WEEK;
    return day >= first ? day : -1;
}

/* ---- text ---- */

KEPT_ONE static bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

KEPT_ONE static bool begins(const char *text, const char *word)
{
    while (*word != '\0') {
        if (*text++ != *word++) {
            return false;
        }
    }
    return true;
}

KEPT_ONE static bool is(const char *text, const char *word)
{
    while (*word != '\0' && *text == *word) {
        text++;
        word++;
    }
    return *text == *word;
}

KEPT_ONE static uint32_t number_of(const char *text, int32_t length)
{
    uint32_t hash = 2166136261u;
    for (int32_t i = 0; i < length; i++) {
        hash = (hash ^ (uint8_t)text[i]) * 16777619u;
    }
    return hash != 0 ? hash : 1;
}

KEPT_ONE static int32_t digits(const char *text, int32_t count)
{
    int32_t value = 0;
    for (int32_t i = 0; i < count; i++) {
        if (!is_digit(text[i])) {
            return -1;
        }
        value = value * 10 + (text[i] - '0');
    }
    return value;
}

/* A number with its sign, and where it ends. */
KEPT_ONE static int32_t signed_number(const char **text)
{
    const char *at = *text;
    bool        is_negative = *at == '-';
    if (*at == '-' || *at == '+') {
        at++;
    }
    int32_t value = 0;
    while (is_digit(*at)) {
        value = value * 10 + (*at++ - '0');
    }
    *text = at;
    return is_negative ? -value : value;
}

/* "MO".."SU" as 0..6, or -1. */
KEPT_ONE static int32_t weekday_named(const char *two)
{
    static const char NAMES[] = "MOTUWETHFRSASU";
    for (int32_t i = 0; i < DAYS_PER_WEEK; i++) {
        if (two[0] == NAMES[i * 2] && two[1] == NAMES[i * 2 + 1]) {
            return i;
        }
    }
    return -1;
}

/* The value of `name=` among parameters or rule parts separated by ';',
 * up to the next ';' or the text's end; NULL when it is not there. */
static const char *part_named(const char *text, const char *name, int32_t *length)
{
    while (*text != '\0') {
        const char *end = text;
        bool        in_quotes = false;
        while (*end != '\0' && (*end != ';' || in_quotes)) {
            in_quotes ^= *end == '"';
            end++;
        }
        if (begins(text, name)) {
            const char *value = text;
            while (*name != '\0') {
                value++;
                name++;
            }
            if (*value == '"') {
                value++;
                *length = (int32_t)(end - value) - (end[-1] == '"' ? 1 : 0);
            } else {
                *length = (int32_t)(end - value);
            }
            return value;
        }
        text = *end == ';' ? end + 1 : end;
    }
    return NULL;
}

/* ---- moments ---- */

/* "20261113", "20261113T150000" or "20261113T150000Z". */
KEPT_ONE static ics_moment_t moment_of(const char *value, const char *parameters)
{
    ics_moment_t moment = { 0 };
    int32_t      year = digits(value, 4), month = digits(value + 4, 2), day = digits(value + 6, 2);
    if (year < 0 || month < 1 || month > 12 || day < 1 || day > 31) {
        return moment;
    }
    moment.days = days_of(year, month, day);
    moment.is_date = value[8] != 'T';
    if (!moment.is_date) {
        int32_t hour = digits(value + 9, 2), minute = digits(value + 11, 2);
        int32_t second = digits(value + 13, 2);
        if (hour < 0 || minute < 0) {
            return moment;
        }
        moment.seconds = hour * 3600 + minute * 60 + (second > 0 ? second : 0);
        moment.is_utc = value[15] == 'Z';
    }
    int32_t     length;
    const char *zone = part_named(parameters, "TZID=", &length);
    if (zone != NULL && !moment.is_utc) {
        moment.zone = number_of(zone, length);
    }
    moment.is_set = true;
    return moment;
}

/* Whether the zone keeps summer time on that day of its own calendar. */
static bool is_in_daylight(const ics_zone_t *zone, int32_t days)
{
    if (!zone->has_daylight || zone->daylight_month == 0 || zone->standard_month == 0) {
        return false;
    }
    int32_t year, month, day;
    date_of(days, &year, &month, &day);
    /* With no rule the day a zone last changed stands in for every year. */
    int32_t begins_at = zone->daylight_nth != 0
                            ? nth_weekday_of(year, zone->daylight_month, zone->daylight_nth,
                                             zone->daylight_weekday)
                            : days_of(year, zone->daylight_month, zone->daylight_weekday);
    int32_t ends_at = zone->standard_nth != 0
                          ? nth_weekday_of(year, zone->standard_month, zone->standard_nth,
                                           zone->standard_weekday)
                          : days_of(year, zone->standard_month, zone->standard_weekday);
    return begins_at < ends_at ? days >= begins_at && days < ends_at
                               : days >= begins_at || days < ends_at;
}

KEPT_ONE static int64_t unix_of(const ics_t *ics, const ics_moment_t *moment)
{
    int64_t local = (int64_t)moment->days * SECONDS_PER_DAY + moment->seconds;
    if (moment->is_utc) {
        return local;
    }
    if (moment->zone != 0 && !moment->is_date) {
        for (int32_t i = 0; i < ics->zone_count; i++) {
            const ics_zone_t *zone = &ics->zones[i];
            if (zone->name == moment->zone) {
                return local -
                       (is_in_daylight(zone, moment->days) ? zone->daylight_s : zone->standard_s);
            }
        }
    }
    /* No zone, a zone the file does not describe, or a whole day: the
     * device's own. */
    return local - ics->device_offset_s;
}

/* ---- the events kept ---- */

static void keep(ics_t *ics, int64_t start, int64_t end, int64_t original, bool is_all_day)
{
    if (end <= ics->from || start >= ics->until) {
        return;
    }
    int32_t at = ics->count;
    while (at > 0 && ics->events[at - 1].start > start) {
        at--;
    }
    if (at >= ICS_EVENTS_MAX) {
        return;
    }
    int32_t last = ics->count < ICS_EVENTS_MAX ? ics->count : ICS_EVENTS_MAX - 1;
    for (int32_t i = last; i > at; i--) {
        ics->events[i] = ics->events[i - 1];
    }
    ics_event_t *event = &ics->events[at];
    event->start = start;
    event->end = end;
    event->original = original;
    event->uid = ics->uid;
    event->is_all_day = is_all_day;
    for (int32_t i = 0; i <= ICS_TITLE_MAX; i++) {
        event->title[i] = ics->title[i];
    }
    if (ics->count < ICS_EVENTS_MAX) {
        ics->count++;
    }
}

static bool is_taken_out(const ics_t *ics, int64_t start)
{
    for (int32_t i = 0; i < ics->exdate_count; i++) {
        if (unix_of(ics, &ics->exdates[i]) == start) {
            return true;
        }
    }
    return false;
}

/* ---- an event that repeats ---- */

#define STEPS_MAX 40000 /* a rule that makes more than this before the window is somebody's mistake */

typedef struct {
    ics_t  *ics;
    int64_t duration;
    int64_t until;  /* of the rule; INT64_MAX: none */
    int32_t made;   /* occurrences so far, for COUNT */
    bool    is_over;
} run_t;

/* One occurrence, on the day `days` of the event's own calendar. */
static void occur(run_t *run, int32_t days)
{
    ics_t *ics = run->ics;
    if (run->is_over || days < ics->start.days) {
        return;
    }
    ics_moment_t moment = ics->start;
    moment.days = days;
    int64_t start = unix_of(ics, &moment);
    if (start > run->until || (ics->rule_count > 0 && run->made >= ics->rule_count) ||
        start >= ics->until) {
        run->is_over = true;
        return;
    }
    run->made++;
    if (!is_taken_out(ics, start)) {
        keep(ics, start, start + run->duration, start, ics->start.is_date);
    }
}

static void repeat(ics_t *ics, int64_t duration)
{
    run_t run = { .ics = ics, .duration = duration, .until = INT64_MAX };
    if (ics->rule_until.is_set) {
        ics_moment_t until = ics->rule_until;
        if (until.is_date) {
            until.seconds = SECONDS_PER_DAY - 1; /* the whole of that day */
        }
        run.until = unix_of(ics, &until);
    }
    int32_t interval = ics->interval > 0 ? ics->interval : 1;
    int32_t first = ics->start.days;
    /* With nothing to count, the days long before the window are stepped
     * over at once: a meeting every day for ten years is still one event. */
    int32_t window_day = (int32_t)((ics->from - duration) / SECONDS_PER_DAY) - 2;
    int32_t year, month, day;
    date_of(first, &year, &month, &day);

    switch (ics->frequency) {
    case FREQ_DAILY: {
        int32_t step = 0;
        if (ics->rule_count == 0 && window_day > first) {
            step = (window_day - first) / interval;
        }
        for (int32_t i = 0; i < STEPS_MAX && !run.is_over; i++, step++) {
            occur(&run, first + step * interval);
        }
        break;
    }
    case FREQ_WEEKLY: {
        uint8_t weekdays = ics->weekdays != 0 ? ics->weekdays : (uint8_t)(1 << weekday_of(first));
        int32_t week_start = first - weekday_of(first);
        int32_t step = 0;
        if (ics->rule_count == 0 && window_day > first) {
            step = (window_day - week_start) / (interval * DAYS_PER_WEEK);
        }
        for (int32_t i = 0; i < STEPS_MAX && !run.is_over; i++, step++) {
            for (int32_t weekday = 0; weekday < DAYS_PER_WEEK; weekday++) {
                if (weekdays & (1 << weekday)) {
                    occur(&run, week_start + step * interval * DAYS_PER_WEEK + weekday);
                }
            }
        }
        break;
    }
    case FREQ_MONTHLY:
    case FREQ_YEARLY: {
        int32_t months = ics->frequency == FREQ_YEARLY ? 12 * interval : interval;
        int32_t wanted_day = ics->month_day != 0 ? ics->month_day : day;
        for (int32_t i = 0; i < STEPS_MAX / 10 && !run.is_over; i++) {
            int32_t month_index = (month - 1) + i * months;
            int32_t its_year = year + month_index / 12, its_month = month_index % 12 + 1;
            int32_t length = days_in_month(its_year, its_month);
            if (ics->nth_weekday != 0 && ics->weekdays != 0) {
                int32_t weekday = 0;
                while (!(ics->weekdays & (1 << weekday))) {
                    weekday++;
                }
                int32_t found = nth_weekday_of(its_year, its_month, ics->nth_weekday, weekday);
                if (found >= 0) {
                    occur(&run, found);
                }
            } else {
                int32_t its_day = wanted_day > 0 ? wanted_day : length + 1 + wanted_day;
                if (its_day >= 1 && its_day <= length) { /* no 31st in a month of thirty */
                    occur(&run, days_of(its_year, its_month, its_day));
                }
            }
        }
        break;
    }
    default:
        break;
    }
}

static void finish_event(ics_t *ics)
{
    if (!ics->start.is_set) {
        return;
    }
    int64_t start = unix_of(ics, &ics->start);
    int64_t duration = ics->end.is_set     ? unix_of(ics, &ics->end) - start
                       : ics->duration_s > 0 ? ics->duration_s
                       : ics->start.is_date  ? SECONDS_PER_DAY
                                             : 0;
    if (duration < 0) {
        duration = 0;
    }
    if (ics->recurrence_id.is_set) {
        /* One occurrence of a repeating event, moved or cancelled: where
         * the rule had it is remembered, and what it is now is an event
         * of its own. */
        int64_t original = unix_of(ics, &ics->recurrence_id);
        bool    is_near = original > ics->from - 2 * SECONDS_PER_DAY && original < ics->until;
        if (is_near && ics->override_count < ICS_OVERRIDES_MAX) {
            ics->overrides[ics->override_count].uid = ics->uid;
            ics->overrides[ics->override_count].original = original;
            ics->override_count++;
        }
        if (!ics->is_cancelled) {
            keep(ics, start, start + duration, NOT_AN_ORIGINAL, ics->start.is_date);
        }
        return;
    }
    if (ics->is_cancelled) {
        return;
    }
    if (ics->has_rule && ics->frequency != FREQ_NONE) {
        repeat(ics, duration);
    } else {
        keep(ics, start, start + duration, start, ics->start.is_date);
    }
}

/* ---- lines ---- */

/* SUMMARY's value with its escapes undone, as it came: the host draws what
 * is not ASCII as best it can. Cut between characters, never inside one. */
static void take_title(ics_t *ics, const char *value)
{
    int32_t length = 0;
    while (*value != '\0' && length < ICS_TITLE_MAX) {
        char c = *value++;
        if (c == '\\' && *value != '\0') {
            c = *value == 'n' || *value == 'N' ? ' ' : *value;
            value++;
        }
        ics->title[length++] = (uint8_t)c < ' ' ? ' ' : c;
    }
    /* A character of several bytes that did not fit whole goes whole. */
    if (*value != '\0') {
        while (length > 0 && ((uint8_t)*value & 0xC0) == 0x80) {
            value--;
            length--;
        }
    }
    ics->title[length] = '\0';
}

/* "FREQ=WEEKLY;INTERVAL=2;BYDAY=MO,WE;UNTIL=20270101T000000Z" */
static void take_rule(ics_t *ics, const char *rule)
{
    int32_t     length;
    const char *part = part_named(rule, "FREQ=", &length);
    ics->has_rule = true;
    ics->frequency = part == NULL            ? FREQ_NONE
                     : begins(part, "DAILY")   ? FREQ_DAILY
                     : begins(part, "WEEKLY")  ? FREQ_WEEKLY
                     : begins(part, "MONTHLY") ? FREQ_MONTHLY
                     : begins(part, "YEARLY")  ? FREQ_YEARLY
                                               : FREQ_NONE;
    if ((part = part_named(rule, "INTERVAL=", &length)) != NULL) {
        ics->interval = signed_number(&part);
    }
    if ((part = part_named(rule, "COUNT=", &length)) != NULL) {
        ics->rule_count = signed_number(&part);
    }
    if ((part = part_named(rule, "UNTIL=", &length)) != NULL) {
        ics->rule_until = moment_of(part, "");
        /* An end written without a zone is in the event's own. */
        ics->rule_until.zone = ics->start.zone;
    }
    if ((part = part_named(rule, "BYMONTHDAY=", &length)) != NULL) {
        ics->month_day = (int8_t)signed_number(&part);
    }
    if ((part = part_named(rule, "BYDAY=", &length)) != NULL) {
        const char *end = part + length;
        while (part < end) {
            int32_t nth = signed_number(&part);
            int32_t weekday = part + 1 < end ? weekday_named(part) : -1;
            if (weekday >= 0) {
                ics->weekdays |= (uint8_t)(1 << weekday);
                if (nth != 0) {
                    ics->nth_weekday = (int8_t)nth;
                }
            }
            while (part < end && *part != ',') {
                part++;
            }
            part++;
        }
    }
}

/* "+0300", "-0530", "+013000" as seconds east of UTC. */
static int32_t offset_of(const char *value)
{
    int32_t sign = *value == '-' ? -1 : 1;
    if (*value == '-' || *value == '+') {
        value++;
    }
    int32_t hours = digits(value, 2), minutes = digits(value + 2, 2);
    return hours < 0 || minutes < 0 ? 0 : sign * (hours * 3600 + minutes * 60);
}

static void zone_line(ics_t *ics, const char *name, const char *value)
{
    if (ics->zone_count >= ICS_ZONES_MAX && !is(name, "END")) {
        return;
    }
    ics_zone_t *zone = &ics->zones[ics->zone_count < ICS_ZONES_MAX ? ics->zone_count : 0];
    bool        is_daylight = ics->in_daylight;
    if (is(name, "TZID")) {
        int32_t length = 0;
        while (value[length] != '\0') {
            length++;
        }
        zone->name = number_of(value, length);
    } else if (is(name, "BEGIN")) {
        ics->in_daylight = is(value, "DAYLIGHT");
        ics->in_standard = is(value, "STANDARD");
    } else if (is(name, "TZOFFSETTO") && (ics->in_daylight || ics->in_standard)) {
        if (is_daylight) {
            zone->daylight_s = offset_of(value);
            zone->has_daylight = true;
        } else {
            zone->standard_s = offset_of(value);
        }
    } else if (is(name, "DTSTART") && (ics->in_daylight || ics->in_standard)) {
        /* The day it changed, in case no rule says when it changes. */
        int32_t month = digits(value + 4, 2), day = digits(value + 6, 2);
        if (month >= 1 && day >= 1) {
            if (is_daylight && zone->daylight_nth == 0) {
                zone->daylight_month = (int8_t)month;
                zone->daylight_weekday = (int8_t)day;
            } else if (!is_daylight && zone->standard_nth == 0) {
                zone->standard_month = (int8_t)month;
                zone->standard_weekday = (int8_t)day;
            }
        }
    } else if (is(name, "RRULE") && (ics->in_daylight || ics->in_standard)) {
        int32_t     length;
        const char *month = part_named(value, "BYMONTH=", &length);
        const char *day = part_named(value, "BYDAY=", &length);
        if (month != NULL && day != NULL) {
            int32_t its_month = signed_number(&month);
            int32_t nth = signed_number(&day);
            int32_t weekday = weekday_named(day);
            if (its_month >= 1 && its_month <= 12 && nth != 0 && weekday >= 0) {
                if (is_daylight) {
                    zone->daylight_month = (int8_t)its_month;
                    zone->daylight_nth = (int8_t)nth;
                    zone->daylight_weekday = (int8_t)weekday;
                } else {
                    zone->standard_month = (int8_t)its_month;
                    zone->standard_nth = (int8_t)nth;
                    zone->standard_weekday = (int8_t)weekday;
                }
            }
        }
    } else if (is(name, "END")) {
        if (is(value, "VTIMEZONE")) {
            ics->in_zone = false;
            if (ics->zone_count < ICS_ZONES_MAX) {
                if (!zone->has_daylight) {
                    zone->daylight_s = zone->standard_s;
                }
                ics->zone_count++;
            }
        }
        ics->in_daylight = ics->in_standard = false;
    }
}

static void begin_event(ics_t *ics)
{
    ics->in_event = true;
    ics->nested = 0;
    ics->start = ics->end = ics->recurrence_id = ics->rule_until = (ics_moment_t){ 0 };
    ics->exdate_count = 0;
    ics->duration_s = 0;
    ics->title[0] = '\0';
    ics->uid = 0;
    ics->is_cancelled = ics->has_rule = false;
    ics->frequency = FREQ_NONE;
    ics->interval = ics->rule_count = 0;
    ics->weekdays = 0;
    ics->nth_weekday = ics->month_day = 0;
}

/* "P1D", "PT1H30M", "P1W": what an event lasts when it has no end. */
static int32_t duration_of(const char *value)
{
    int32_t seconds = 0;
    bool    in_time = false;
    for (const char *at = value; *at != '\0';) {
        if (*at == 'T') {
            in_time = true;
            at++;
        } else if (is_digit(*at)) {
            int32_t amount = signed_number(&at);
            seconds += amount * (*at == 'W'             ? 7 * SECONDS_PER_DAY
                                 : *at == 'D'           ? SECONDS_PER_DAY
                                 : *at == 'H'           ? 3600
                                 : *at == 'M' && in_time ? 60
                                 : *at == 'S'           ? 1
                                                        : 0);
        } else {
            at++;
        }
    }
    return seconds;
}

static void event_line(ics_t *ics, const char *name, const char *parameters, const char *value)
{
    if (is(name, "BEGIN")) {
        ics->nested++;
        return;
    }
    if (is(name, "END")) {
        if (ics->nested > 0) {
            ics->nested--;
        } else {
            ics->in_event = false;
            finish_event(ics);
        }
        return;
    }
    if (ics->nested > 0) {
        return; /* an alarm's own lines */
    }
    if (is(name, "DTSTART")) {
        ics->start = moment_of(value, parameters);
    } else if (is(name, "DTEND")) {
        ics->end = moment_of(value, parameters);
    } else if (is(name, "DURATION")) {
        ics->duration_s = duration_of(value);
    } else if (is(name, "SUMMARY")) {
        take_title(ics, value);
    } else if (is(name, "UID")) {
        int32_t length = 0;
        while (value[length] != '\0') {
            length++;
        }
        ics->uid = number_of(value, length);
    } else if (is(name, "RRULE")) {
        take_rule(ics, value);
    } else if (is(name, "RECURRENCE-ID")) {
        ics->recurrence_id = moment_of(value, parameters);
    } else if (is(name, "STATUS")) {
        ics->is_cancelled = is(value, "CANCELLED");
    } else if (is(name, "EXDATE")) {
        /* One or several, separated by commas. */
        for (const char *at = value; *at != '\0' && ics->exdate_count < ICS_EXDATES_MAX;) {
            ics_moment_t moment = moment_of(at, parameters);
            /* Only the ones that could matter: a meeting of some years has
             * many, nearly all of them long past. */
            int64_t taken_out = moment.is_set ? unix_of(ics, &moment) : 0;
            if (moment.is_set && taken_out > ics->from - 2 * SECONDS_PER_DAY &&
                taken_out < ics->until) {
                ics->exdates[ics->exdate_count++] = moment;
            }
            while (*at != '\0' && *at != ',') {
                at++;
            }
            at += *at == ',';
        }
    }
}

/* One whole line: "NAME;PARAMETERS:VALUE". */
static void take_line(ics_t *ics)
{
    char *line = ics->line;
    line[ics->line_length] = '\0';
    ics->line_length = 0;
    /* The name ends at the first ';' or ':'; the value begins after the
     * first ':' that is not inside quotes. */
    char *parameters = line;
    while (*parameters != '\0' && *parameters != ';' && *parameters != ':') {
        parameters++;
    }
    char *value = parameters;
    bool  in_quotes = false;
    while (*value != '\0' && (*value != ':' || in_quotes)) {
        in_quotes ^= *value == '"';
        value++;
    }
    if (*value != ':') {
        return;
    }
    bool has_parameters = *parameters == ';';
    *parameters = '\0';
    *value++ = '\0';
    const char *parameter_text = has_parameters ? parameters + 1 : "";

    if (ics->in_event) {
        event_line(ics, line, parameter_text, value);
    } else if (ics->in_zone) {
        zone_line(ics, line, value);
    } else if (is(line, "BEGIN") && is(value, "VEVENT")) {
        begin_event(ics);
    } else if (is(line, "BEGIN") && is(value, "VTIMEZONE")) {
        ics->in_zone = true;
        if (ics->zone_count < ICS_ZONES_MAX) {
            ics->zones[ics->zone_count] = (ics_zone_t){ 0 };
        }
    }
}

void ics_begin(ics_t *ics, ics_event_t *events, int64_t from, int64_t until,
               int32_t device_offset_s)
{
    *ics = (ics_t){ .from = from, .until = until, .device_offset_s = device_offset_s,
                    .events = events };
}

void ics_feed(ics_t *ics, const char *data, int32_t length)
{
    for (int32_t i = 0; i < length; i++) {
        char c = data[i];
        if (ics->after_break) {
            ics->after_break = false;
            if (c == ' ' || c == '\t') {
                continue; /* the line goes on: a long one was folded */
            }
            take_line(ics);
        }
        if (c == '\n') {
            ics->after_break = true;
        } else if (c != '\r' && ics->line_length < ICS_LINE_MAX) {
            ics->line[ics->line_length++] = c;
        }
    }
}

int32_t ics_end(ics_t *ics)
{
    if (ics->line_length > 0) {
        take_line(ics);
    }
    /* What somebody moved or cancelled is not where the rule had it. */
    int32_t kept = 0;
    for (int32_t i = 0; i < ics->count; i++) {
        bool is_replaced = false;
        for (int32_t j = 0; j < ics->override_count && !is_replaced; j++) {
            is_replaced = ics->events[i].uid == ics->overrides[j].uid &&
                          ics->events[i].original == ics->overrides[j].original;
        }
        if (!is_replaced) {
            ics->events[kept++] = ics->events[i];
        }
    }
    ics->count = kept;
    return kept;
}
