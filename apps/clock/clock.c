/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * A clock for the whole screen: the time, and under it the day of the
 * week and the date. It lives on a timer - it wakes at the turn of every
 * minute, asks for a redraw and goes back to waiting. Until the device
 * knows the time it says so instead of showing a wrong one.
 */
#include <stdbool.h>

#include "app_text.h"
#include "host_api.h"

#define MINUTE_TIMER       0
#define MS_PER_SECOND      1000
#define SECONDS_PER_MINUTE 60
#define DAYS_PER_WEEK      7
#define RETRY_MS           5000 /* how soon to look again while the time is unknown */

/* Its digits in columns: a minute turning does not move the hours. */
#define TIME_FONT (HOST_FONT_HUGE | HOST_FONT_TABULAR)
#define DATE_FONT HOST_FONT_MEDIUM
/* Where the tops of the digits are, the room between the digits and the
 * date, and between the two lines of the date. */
#define TIME_CAP_TOP     61
#define TIME_TO_DATE_GAP 50
#define DATE_LINE_GAP    16


static void schedule_next_frame(void)
{
    int64_t utc_seconds = time_now();
    if (utc_seconds == 0) {
        timer_set(MINUTE_TIMER, RETRY_MS);
        return;
    }
    int32_t seconds_into_minute = (int32_t)(utc_seconds % SECONDS_PER_MINUTE);
    timer_set(MINUTE_TIMER, (SECONDS_PER_MINUTE - seconds_into_minute) * MS_PER_SECOND);
}

/* Draws the text centered, with the top of its capitals and digits at
 * `cap_top`, and returns where they end: the baseline. */
static int32_t draw_centered(int32_t cap_top, int32_t font, const char *text)
{
    int32_t length = app_text_length(text);
    int32_t width = gfx_text_width(font, text, length);
    int32_t cap_height = gfx_font_cap_height(font);
    gfx_text((gfx_width() - width) / 2, cap_top - (gfx_font_ascent(font) - cap_height), font, text,
             length);
    return cap_top + cap_height;
}

void app_on_focus(void)
{
    schedule_next_frame();
}

void app_on_timer(int32_t id)
{
    (void)id;
    request_redraw(HOST_REDRAW_NORMAL);
    schedule_next_frame();
}

/* The clock or the time zone was set to another: what is on the screen is
 * the wrong time. Asked for in the manifest; off the screen there is
 * nothing to do about it. */
void app_on_intent(int32_t intent)
{
    if (intent == HOST_INTENT_TIME_CHANGED) {
        request_redraw(HOST_REDRAW_SOON);
    }
}

void app_on_draw(void)
{
    char time_line[8], weekday_line[24], date_line[24];
    app_text_t text;
    int64_t now = time_now();
    if (now != 0) {
        /* The host says a moment in words: no names of days here. */
        time_line[time_text(now, HOST_TIME_CLOCK, time_line, sizeof time_line - 1)] = '\0';
        weekday_line[time_text(now, HOST_TIME_WEEKDAY, weekday_line, sizeof weekday_line - 1)] = '\0';
        date_line[time_text(now, HOST_TIME_LONG_DATE, date_line, sizeof date_line - 1)] = '\0';
    } else {
        app_text_start(&text, time_line, sizeof time_line);
        app_text_add(&text, "--:--");
        app_text_start(&text, weekday_line, sizeof weekday_line);
        app_text_add(&text, "No time");
        app_text_start(&text, date_line, sizeof date_line);
        app_text_add(&text, "Waiting for network");
    }

    int32_t time_bottom = draw_centered(TIME_CAP_TOP, TIME_FONT, time_line);
    int32_t weekday_bottom = draw_centered(time_bottom + TIME_TO_DATE_GAP, DATE_FONT, weekday_line);
    draw_centered(weekday_bottom + DATE_LINE_GAP, DATE_FONT, date_line);
}
