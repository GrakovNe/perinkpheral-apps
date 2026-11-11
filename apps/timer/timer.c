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
#define STATE_VERSION 1
typedef struct {
    int32_t version;
    int32_t minutes; /* what the user set */
    int32_t ends_at; /* 0: not running */
    int32_t is_up;   /* the alarm came and nobody has answered it yet */
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
    bool is_stale = is_stored && state.ends_at != 0 &&
                    state.ends_at - uptime_seconds() > state.minutes * SECONDS_PER_MINUTE;
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
static void wake_at_the_next_minute(void)
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
    wake_at_the_next_minute();
}

void app_on_timer(int32_t id)
{
    (void)id;
    request_redraw(HOST_REDRAW_NORMAL);
    wake_at_the_next_minute();
}

/* Loaded for this one call when the application is not on the screen. */
void app_on_alarm(void)
{
    state.ends_at = 0;
    state.is_up = 1;
    save();
    request_focus();
    request_redraw(HOST_REDRAW_SOON);
}

static void start(void)
{
    state.ends_at = uptime_seconds() + state.minutes * SECONDS_PER_MINUTE;
    state.is_up = 0;
    alarm_set(state.minutes * SECONDS_PER_MINUTE);
    wake_at_the_next_minute();
}

static void stop(void)
{
    state.ends_at = 0;
    state.is_up = 0;
    alarm_set(0);
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
    state.is_up = 0;
}

void app_on_button(int32_t event)
{
    switch (event) {
    case BTN_BACK_PRESSED:
        request_exit();
        return;
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
        if (state.ends_at != 0 || state.is_up) {
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

static void draw_centered(int32_t top, int32_t font, const char *text)
{
    int32_t length = app_text_length(text);
    gfx_text((gfx_width() - gfx_text_width(font, text, length)) / 2, top, font, text, length);
}

#define NUMBER_TOP  30
#define WORDS_TOP   150
#define HINT_TOP    216
#define BAR_INSET   16
#define BAR_HEIGHT  40

void app_on_draw(void)
{
    char buffer[24];
    app_text_t text;
    int32_t left = seconds_left();
    bool is_running = left > 0;

    if (state.is_up) {
        /* White on a black bar: the one thing on this screen that is news. */
        gfx_rect(BAR_INSET, WORDS_TOP - 6, gfx_width() - 2 * BAR_INSET, BAR_HEIGHT,
                 HOST_RECT_FILLED);
        gfx_color(HOST_PAPER);
        draw_centered(WORDS_TOP, HOST_FONT_TEXT, "Time is up");
        gfx_color(HOST_INK);
    }
    app_text_start(&text, buffer, sizeof buffer);
    /* The minute in hand counts: 61 seconds left reads 2, the last second 1. */
    app_text_add_number(&text, is_running ? (left + SECONDS_PER_MINUTE - 1) / SECONDS_PER_MINUTE
                                          : state.minutes);
    draw_centered(NUMBER_TOP, HOST_FONT_HUGE, buffer);
    if (!state.is_up) {
        bool is_one = (is_running ? (left + SECONDS_PER_MINUTE - 1) / SECONDS_PER_MINUTE
                                  : state.minutes) == 1;
        draw_centered(WORDS_TOP, HOST_FONT_MEDIUM,
                      is_running ? (is_one ? "minute left" : "minutes left")
                                 : (is_one ? "minute" : "minutes"));
    }
    draw_centered(HINT_TOP, HOST_FONT_TEXT,
                  is_running || state.is_up ? "Ok stops" : "Up, Down set - Ok starts");
}
