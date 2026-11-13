/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * A countdown that ends wherever the user has gone: set the minutes,
 * start it, switch to the clock or the weather - when the time is up the
 * timer takes the screen back and says so.
 *
 * It is the sample of an application that works while it is not loaded.
 * Nothing counts down in here: the end is a moment in the store, the host
 * is asked for one alarm at that moment, and whatever the screen shows is
 * worked out from the two whenever the application is drawn.
 */
#include <stdbool.h>

#include "app_text.h"
#include "host_api.h"
#include "strings.h" /* made of the `strings` folder by tools/mkapp */

#define MINUTE_TIMER       0
#define SECONDS_PER_MINUTE 60
#define MS_PER_SECOND      1000
#define MINUTES_DEFAULT    25
#define MINUTES_MIN        1
#define MINUTES_MAX        180
#define MINUTES_STEP_LONG  5

/* What survives being unloaded. `ends_at` is uptime in seconds: the wall
 * clock may not be known, and it may jump when it is set. */
#define STATE_KEY     "state"
#define STATE_VERSION 3
typedef struct {
    int32_t version;
    int32_t minutes; /* what the user set */
    int32_t ends_at; /* 0: not running; else in seconds since the device started */
    uint32_t boot;   /* the run of the device `ends_at` is of */
} timer_state_t;

static timer_state_t state;

static int32_t uptime_seconds(void)
{
    return (int32_t)(time_uptime_ms() / MS_PER_SECOND);
}

static void save(void)
{
    kv_set(STATE_KEY, sizeof STATE_KEY - 1, &state, sizeof state);
}

void app_init(void)
{
    bool is_stored = kv_get(STATE_KEY, sizeof STATE_KEY - 1, &state, sizeof state) ==
                         sizeof state &&
                     state.version == STATE_VERSION;
    /* A countdown from before a restart is over: uptime began again. */
    bool is_stale = is_stored && state.ends_at != 0 && state.boot != time_boot_id();
    if (!is_stored || is_stale) {
        state = (timer_state_t){ .version = STATE_VERSION, .minutes = MINUTES_DEFAULT };
    }
}

static int32_t seconds_left(void)
{
    int32_t left = state.ends_at != 0 ? state.ends_at - uptime_seconds() : 0;
    return left > 0 ? left : 0;
}

/* The picture shows whole minutes: it changes when one of them is over. */
static void schedule_minute_tick(void)
{
    int32_t left = seconds_left();
    if (left > 0) {
        int32_t into_minute = left % SECONDS_PER_MINUTE;
        timer_set(MINUTE_TIMER,
                  (into_minute > 0 ? into_minute : SECONDS_PER_MINUTE) * MS_PER_SECOND);
    }
}

void app_on_focus(void)
{
    schedule_minute_tick();
}

void app_on_timer(int32_t id)
{
    (void)id;
    request_redraw(HOST_REDRAW_NORMAL);
    schedule_minute_tick();
}

/* The application has a few alarms: the countdown's end, and one that
 * comes every minute to keep the minutes in the header right. */
#define COUNTDOWN_ALARM 0
#define MINUTE_ALARM    1

static int32_t minutes_left(void)
{
    /* The minute in hand counts: 61 seconds left reads 2, the last second 1. */
    return (seconds_left() + SECONDS_PER_MINUTE - 1) / SECONDS_PER_MINUTE;
}

/* While it counts, the header says so wherever the user has gone: the
 * minutes left and the hourglass, on the right where the date was. */
static void show_in_header(void)
{
    /* "{0} min", the number where the language wants it. */
    char line[HOST_STATUS_TEXT_MAX + 1];
    const char *pattern = app_string(STR_IN_HEADER);
    const host_text_argument_t minutes = { .number = minutes_left() };
    int32_t length = text_fill(pattern, app_text_length(pattern), &minutes, 1, line, sizeof line - 1);
    line[length > 0 ? length : 0] = '\0';
    header_status(HOST_HEADER_RIGHT, HOST_ICON_HOURGLASS, line, app_text_length(line));
}

static void release_header_status(void)
{
    header_status(HOST_HEADER_RIGHT, HOST_ICON_NONE, "", 0);
    alarm_set(MINUTE_ALARM, 0);
}

/* Loaded for this one call when the application is not on the screen. */
void app_on_alarm(int32_t id)
{
    if (id == MINUTE_ALARM) {
        if (seconds_left() > 0) {
            show_in_header();
        }
        return;
    }
    release_header_status();
    state.ends_at = 0;
    save();
    /* The host tells it, over whatever has the screen, until it is seen. */
    host_notice_t time_is_up = { .icon = HOST_ICON_BELL };
    app_text_t    text;
    app_text_start(&text, time_is_up.first, sizeof time_is_up.first);
    app_text_add(&text, app_string(STR_TIME));
    app_text_start(&text, time_is_up.second, sizeof time_is_up.second);
    app_text_add(&text, app_string(STR_IS_UP));
    notify(&time_is_up);
    request_redraw(HOST_REDRAW_NORMAL);
}

static void start(void)
{
    state.ends_at = uptime_seconds() + state.minutes * SECONDS_PER_MINUTE;
    state.boot = time_boot_id();
    alarm_set(COUNTDOWN_ALARM, state.minutes * SECONDS_PER_MINUTE);
    alarm_set_every(MINUTE_ALARM, SECONDS_PER_MINUTE, 0);
    show_in_header();
    schedule_minute_tick();
}

static void stop(void)
{
    state.ends_at = 0;
    alarm_set(COUNTDOWN_ALARM, 0);
    release_header_status();
    timer_set(MINUTE_TIMER, 0);
}

static void change_minutes(int32_t by)
{
    if (state.ends_at != 0) {
        return; /* a running countdown is stopped first */
    }
    state.minutes += by;
    if (state.minutes < MINUTES_MIN) {
        state.minutes = MINUTES_MIN;
    } else if (state.minutes > MINUTES_MAX) {
        state.minutes = MINUTES_MAX;
    }
}

void app_on_button(int32_t event)
{
    switch (event) {
    case BTN_UP_PRESSED:
        change_minutes(1);
        break;
    case BTN_DOWN_PRESSED:
        change_minutes(-1);
        break;
    case BTN_UP_LONG_PRESSED:
        change_minutes(MINUTES_STEP_LONG);
        break;
    case BTN_DOWN_LONG_PRESSED:
        change_minutes(-MINUTES_STEP_LONG);
        break;
    case BTN_OK_PRESSED:
        if (state.ends_at != 0) {
            stop();
        } else {
            start();
        }
        break;
    default:
        return; /* the key going down and up changes nothing by itself */
    }
    save();
    request_redraw(HOST_REDRAW_NORMAL);
}

/* ---- the picture: the host's parts ----
 *
 * The minutes are the page. Being set, they stand in the frame with the
 * arrows UP and DOWN have; counting, over a bar of how much is left. The
 * row of buttons along the bottom names what OK and BACK do. That the
 * time is up is the host's to say (notify). */

#define FRAME_WIDTH   336
#define FRAME_PADDING 16
#define BAR_HEIGHT    14 /* the host's bar */

void app_on_draw(void)
{
    bool    is_running = seconds_left() > 0;
    int32_t font = HOST_FONT_HUGE | HOST_FONT_TABULAR;
    int32_t capitals = gfx_font_cap_height(font);
    int32_t body = gfx_height() - HOST_UI_ACTIONS_HEIGHT;
    int32_t frame_height = capitals + 2 * FRAME_PADDING;
    int32_t frame_left = (gfx_width() - FRAME_WIDTH) / 2;
    int32_t frame_top = (body - frame_height) / 2;

    char       number[8];
    app_text_t text;
    app_text_start(&text, number, sizeof number);
    app_text_add_number(&text, is_running ? minutes_left() : state.minutes);
    const char *unit = app_string(is_running ? STR_MINUTES_LEFT : STR_MINUTES);
    int32_t length = app_text_length(number), unit_length = app_text_length(unit);
    ui_value((gfx_width() - ui_value_width(font, number, length, unit, unit_length)) / 2,
             frame_top + FRAME_PADDING - (gfx_font_ascent(font) - capitals), font, number, length,
             unit, unit_length);

    if (is_running) {
        /* How much is left, halfway from the digits to the row of buttons. */
        int32_t digits_bottom = frame_top + FRAME_PADDING + capitals;
        ui_bar(frame_left, (digits_bottom + body - BAR_HEIGHT) / 2, FRAME_WIDTH, minutes_left(),
               state.minutes);
        ui_actions(app_string(STR_STOP), app_text_length(app_string(STR_STOP)),
                   app_string(STR_CLOSE), app_text_length(app_string(STR_CLOSE)));
    } else {
        ui_frame(frame_left, frame_top, FRAME_WIDTH, frame_height,
                 (state.minutes < MINUTES_MAX ? HOST_UI_ARROW_UP : 0) |
                     (state.minutes > MINUTES_MIN ? HOST_UI_ARROW_DOWN : 0));
        ui_actions(app_string(STR_START), app_text_length(app_string(STR_START)),
                   app_string(STR_CLOSE), app_text_length(app_string(STR_CLOSE)));
    }
}
