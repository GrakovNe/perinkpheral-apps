/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * A calendar as servers give it by a link (iCalendar, RFC 5545), read as it
 * comes - a piece at a time, never whole - into the few events that are
 * about to happen. Pure: no host calls, so it is tested on a computer.
 *
 * What it understands: events with a start and an end, whole days, times
 * in UTC, in a zone the file describes and in no zone at all (the
 * device's); events that repeat by the day, the week, the month and the
 * year, with an interval, a count, an end, days of the week, a day of the
 * month, and the days taken out; single occurrences moved or cancelled.
 * What it does not: rules by position (BYSETPOS), by week number, by hour.
 * Such an event repeats by its plain frequency.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define ICS_TITLE_MAX  63
#define ICS_EVENTS_MAX 24

typedef struct {
    int64_t  start; /* unix seconds; for a whole day, its midnight on the device */
    int64_t  end;
    int64_t  original; /* where the rule put it, before anybody moved it */
    uint32_t uid;      /* a number made of the event's UID */
    uint8_t  is_all_day;
    char     title[ICS_TITLE_MAX + 1]; /* as it came, UTF-8, cut between characters */
} ics_event_t;

#define ICS_LINE_MAX     200
#define ICS_ZONES_MAX    6
#define ICS_EXDATES_MAX  24
#define ICS_OVERRIDES_MAX 32

typedef struct {
    uint32_t name; /* a number made of the TZID */
    int32_t  standard_s, daylight_s; /* east of UTC */
    /* When each begins: the month, and which day of the week of it -
     * `nth` 1.. from its beginning, -1 the last, 0: not known. */
    int8_t standard_month, standard_nth, standard_weekday;
    int8_t daylight_month, daylight_nth, daylight_weekday;
    bool   has_daylight;
} ics_zone_t;

/* A moment as the file writes it, before it is known what zone means. */
typedef struct {
    int32_t  days;    /* since 1970-01-01, of the date as written */
    int32_t  seconds; /* of that day */
    uint32_t zone;    /* a TZID's number; 0: none */
    bool     is_utc, is_date, is_set;
} ics_moment_t;

typedef struct {
    /* What the caller gives. */
    int64_t      from, until;  /* the window: events that end after `from` and begin before `until` */
    int32_t      device_offset_s; /* the device's zone, east of UTC */
    ics_event_t *events;       /* ICS_EVENTS_MAX of them, the earliest kept, in order */
    int32_t      count;

    /* The line being put together. */
    char    line[ICS_LINE_MAX + 1];
    int32_t line_length;
    bool    after_break;

    /* Where in the file. */
    bool    in_event, in_zone, in_daylight, in_standard;
    int32_t nested; /* an alarm inside an event has lines of its own */

    ics_zone_t zones[ICS_ZONES_MAX];
    int32_t    zone_count;

    /* The event in hand. */
    ics_moment_t start, end, recurrence_id, rule_until;
    ics_moment_t exdates[ICS_EXDATES_MAX];
    int32_t      exdate_count;
    int32_t      duration_s;
    char         title[ICS_TITLE_MAX + 1];
    uint32_t     uid;
    bool         is_cancelled, has_rule;
    int32_t      frequency, interval, rule_count;
    uint8_t      weekdays;     /* a bit a day, Monday the lowest */
    int8_t       nth_weekday;  /* of the month: "2TU" is 2 */
    int8_t       month_day;    /* 0: the start's own */

    /* Occurrences somebody moved or cancelled: taken out at the end. */
    struct {
        uint32_t uid;
        int64_t  original;
    } overrides[ICS_OVERRIDES_MAX];
    int32_t override_count;
} ics_t;

/* Begins a file. `events` is the caller's, ICS_EVENTS_MAX long. */
void ics_begin(ics_t *ics, ics_event_t *events, int64_t from, int64_t until,
               int32_t device_offset_s);
void ics_feed(ics_t *ics, const char *data, int32_t length);
/* The file is over: how many events there are, earliest first. */
int32_t ics_end(ics_t *ics);
