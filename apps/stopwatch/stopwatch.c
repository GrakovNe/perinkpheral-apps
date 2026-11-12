/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * A stopwatch for a screen that changes once a minute: hours and minutes,
 * large, in the middle. OK starts it and stops it; BACK leaves, and it
 * goes on counting wherever the user has gone, with the time in the
 * header.
 *
 * Nothing counts in here. What is kept is the moment it was started; the
 * time on the screen is worked out from that whenever it is drawn.
 */
#include <stdbool.h>

#include "app_text.h"
#include "host_api.h"

#define SECONDS_PER_MINUTE 60
#define MINUTES_PER_HOUR   60
#define MS_PER_SECOND      1000

#define MINUTE_TIMER 0 /* the picture, while the application is on the screen */
#define MINUTE_ALARM 0 /* the header, wherever the user is */

/* What survives being unloaded. The start is kept on the clock when the
 * device knows the time - that goes on through a restart - and on the
 * time since the device started otherwise. */
#define STATE_KEY     "state"
#define STATE_VERSION 1
typedef struct {
    int32_t version;
    int32_t is_running;
    int64_t started_at;        /* unix seconds; 0: the clock was not known */
    int32_t started_at_uptime; /* seconds since the device started */
    int32_t stopped_at;        /* seconds it showed when it was stopped */
} stopwatch_t;

static stopwatch_t state;

static int32_t uptime_seconds(void)
{
    return (int32_t)(time_uptime_ms() / MS_PER_SECOND);
}

static void save(void)
{
    kv_set(STATE_KEY, sizeof STATE_KEY - 1, &state, sizeof state);
}

static int32_t elapsed_seconds(void)
{
    if (!state.is_running) {
        return state.stopped_at;
    }
    int64_t now = time_now();
    int64_t elapsed = state.started_at != 0 && now != 0 ? now - state.started_at
                                                        : uptime_seconds() - state.started_at_uptime;
    return elapsed > 0 ? (int32_t)elapsed : 0;
}

/* "00:07", "12:40": hours and minutes, the minute in hand not yet counted. */
static void format_elapsed(char *line, int32_t size)
{
    int32_t minutes = elapsed_seconds() / SECONDS_PER_MINUTE;
    app_text_t text;
    app_text_start(&text, line, size);
    app_text_add_padded(&text, minutes / MINUTES_PER_HOUR, 2);
    app_text_add(&text, ":");
    app_text_add_padded(&text, minutes % MINUTES_PER_HOUR, 2);
}

static int32_t seconds_to_the_next_minute(void)
{
    return SECONDS_PER_MINUTE - elapsed_seconds() % SECONDS_PER_MINUTE;
}

/* While it runs the header says so, above whatever is on the screen. */
static void show_in_header(void)
{
    char line[HOST_STATUS_TEXT_MAX + 1];
    format_elapsed(line, sizeof line);
    header_status(HOST_HEADER_LEFT, HOST_ICON_PLAY, line, app_text_length(line));
}

static void wake_at_the_next_minute(void)
{
    if (state.is_running) {
        timer_set(MINUTE_TIMER, seconds_to_the_next_minute() * MS_PER_SECOND);
    }
}

static void start(void)
{
    state.is_running = 1;
    state.started_at = time_now();
    state.started_at_uptime = uptime_seconds();
    state.stopped_at = 0;
    alarm_set_every(MINUTE_ALARM, SECONDS_PER_MINUTE, 0);
    show_in_header();
    wake_at_the_next_minute();
}

static void stop(void)
{
    state.stopped_at = elapsed_seconds();
    state.is_running = 0;
    alarm_set(MINUTE_ALARM, 0);
    timer_set(MINUTE_TIMER, 0);
    header_status(HOST_HEADER_LEFT, HOST_ICON_NONE, "", 0);
}

void app_init(void)
{
    bool is_stored = kv_get(STATE_KEY, sizeof STATE_KEY - 1, &state, sizeof state) ==
                         sizeof state &&
                     state.version == STATE_VERSION;
    if (!is_stored) {
        state = (stopwatch_t){ .version = STATE_VERSION };
    }
    /* Started without a clock, and the device has restarted since: the
     * moment it began is gone, and so is the count. */
    bool is_lost = state.is_running && state.started_at == 0 &&
                   uptime_seconds() < state.started_at_uptime;
    if (is_lost) {
        state.is_running = 0;
        state.stopped_at = 0;
    }
}

void app_on_focus(void)
{
    wake_at_the_next_minute();
}

void app_on_timer(int32_t id)
{
    (void)id;
    request_redraw(HOST_REDRAW_NORMAL);
    wake_at_the_next_minute();
}

/* Every minute, on the screen or off it: the header's line is the one
 * thing that has to be right when nobody has the application open. */
void app_on_alarm(int32_t id)
{
    (void)id;
    if (state.is_running) {
        show_in_header();
    }
}

void app_on_button(int32_t event)
{
    if (event != BTN_OK_PRESSED) {
        return;
    }
    if (state.is_running) {
        stop();
    } else {
        start();
    }
    save();
    request_redraw(HOST_REDRAW_NORMAL);
}

/* The largest of the fonts the time fits in, whichever typeface is chosen. */
static int32_t font_for(const char *line, int32_t length)
{
    static const int32_t LARGEST_FIRST[] = { HOST_FONT_HUGE, HOST_FONT_DISPLAY, HOST_FONT_LARGE };
    int32_t room = gfx_width() - 2 * HOST_UI_MARGIN;
    for (int32_t i = 0; i < 2; i++) {
        if (gfx_text_width(LARGEST_FIRST[i], line, length) <= room) {
            return LARGEST_FIRST[i];
        }
    }
    return LARGEST_FIRST[2];
}

void app_on_draw(void)
{
    char line[16];
    format_elapsed(line, sizeof line);
    int32_t length = app_text_length(line);
    int32_t font = font_for(line, length);
    /* The digits stand in the middle of what the row of buttons leaves. */
    int32_t body = gfx_height() - HOST_UI_ACTIONS_HEIGHT;
    int32_t top = (body - gfx_font_cap_height(font)) / 2 - (gfx_font_ascent(font) -
                                                           gfx_font_cap_height(font));
    gfx_text((gfx_width() - gfx_text_width(font, line, length)) / 2, top, font, line, length);

    if (state.is_running) {
        ui_actions("Stop", 4, "Close", 5);
    } else {
        ui_actions("Start", 5, "Close", 5);
    }
}
