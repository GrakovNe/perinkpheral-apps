/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * A stopwatch: one number, large, in the middle. For its first hour it
 * shows minutes and seconds and turns every second; after that, hours and
 * minutes, turning once a minute. OK starts it, stops it and lets it go
 * on; a long press of OK on a stopped one sets it back to nothing. BACK
 * leaves, and it goes on counting wherever the user has gone, with the
 * time in the header.
 *
 * Nothing counts in here. What is kept is the moment the run began and
 * what earlier runs added up to; the time on the screen is worked out
 * from the two whenever it is drawn.
 */
#include <stdbool.h>

#include "app_text.h"
#include "host_api.h"

#define MS_PER_SECOND      1000
#define SECONDS_PER_MINUTE 60
#define MINUTES_PER_HOUR   60
#define SECONDS_PER_HOUR   (SECONDS_PER_MINUTE * MINUTES_PER_HOUR)

#define TURN_TIMER   0 /* the picture, while the application is on the screen */
#define MINUTE_ALARM 0 /* the header, wherever the user is */

/* The two clocks tell the same run apart by no more than this when the
 * device has not restarted in between. */
#define SAME_START_SLACK_S 2
/* A little past the turn, so that the second has turned when it is drawn. */
#define TURN_SLACK_MS 20

/* What survives being unloaded. The run's start is kept twice: on the
 * time since the device started, which is exact but begins anew at a
 * restart, and on the clock, which goes on through one. */
#define STATE_KEY     "state"
#define STATE_VERSION 2
typedef struct {
    int32_t version;
    int32_t is_running;
    int64_t banked_ms;         /* what the runs before this one added up to */
    int64_t started_uptime_ms; /* of the run in hand */
    int64_t started_at;        /* the same moment in unix seconds; 0: the clock was not known */
} stopwatch_t;

static stopwatch_t state;

static void save(void)
{
    kv_set(STATE_KEY, sizeof STATE_KEY - 1, &state, sizeof state);
}

/* How long the run in hand has lasted, or -1 when that cannot be known
 * any more: it began without a clock, and the device has restarted. */
static int64_t run_ms(void)
{
    int64_t by_uptime_ms = time_uptime_ms() - state.started_uptime_ms;
    int64_t now = time_now();
    if (state.started_at == 0 || now == 0) {
        return by_uptime_ms;
    }
    int64_t by_clock_s = now - state.started_at;
    int64_t apart_s = by_clock_s - by_uptime_ms / MS_PER_SECOND;
    bool is_same_start = by_uptime_ms >= 0 && apart_s >= -SAME_START_SLACK_S &&
                         apart_s <= SAME_START_SLACK_S;
    return is_same_start ? by_uptime_ms : by_clock_s * MS_PER_SECOND;
}

static int64_t elapsed_ms(void)
{
    if (!state.is_running) {
        return state.banked_ms;
    }
    int64_t run = run_ms();
    return state.banked_ms + (run > 0 ? run : 0);
}

static int32_t elapsed_seconds(void)
{
    return (int32_t)(elapsed_ms() / MS_PER_SECOND);
}

static bool shows_seconds(void)
{
    return elapsed_seconds() < SECONDS_PER_HOUR;
}

static void add_two_parts(app_text_t *text, int32_t first, int32_t second)
{
    app_text_add_padded(text, first, 2);
    app_text_add(text, ":");
    app_text_add_padded(text, second, 2);
}

/* "07:42" in the first hour - minutes and seconds - and "01:07" after
 * it: hours and minutes. */
static void format_elapsed(char *line, int32_t size)
{
    int32_t seconds = elapsed_seconds();
    app_text_t text;
    app_text_start(&text, line, size);
    if (seconds < SECONDS_PER_HOUR) {
        add_two_parts(&text, seconds / SECONDS_PER_MINUTE, seconds % SECONDS_PER_MINUTE);
    } else {
        add_two_parts(&text, seconds / SECONDS_PER_HOUR,
                      seconds % SECONDS_PER_HOUR / SECONDS_PER_MINUTE);
    }
}

/* The header turns once a minute at best: hours and minutes, always. */
static void show_in_header(void)
{
    int32_t minutes = elapsed_seconds() / SECONDS_PER_MINUTE;
    char line[HOST_STATUS_TEXT_MAX + 1];
    app_text_t text;
    app_text_start(&text, line, sizeof line);
    add_two_parts(&text, minutes / MINUTES_PER_HOUR, minutes % MINUTES_PER_HOUR);
    header_status(HOST_HEADER_LEFT, HOST_ICON_PLAY, line, app_text_length(line));
}

/* When the number on the screen is next another: at the next second in
 * the first hour, at the next minute after it. */
static void wake_at_the_next_turn(void)
{
    if (!state.is_running) {
        return;
    }
    int64_t turn_ms = shows_seconds() ? MS_PER_SECOND : SECONDS_PER_MINUTE * MS_PER_SECOND;
    int64_t into_turn_ms = elapsed_ms() % turn_ms;
    timer_set(TURN_TIMER, (int32_t)(turn_ms - into_turn_ms) + TURN_SLACK_MS);
}

static void start(void)
{
    state.is_running = 1;
    state.started_uptime_ms = time_uptime_ms();
    state.started_at = time_now();
    int32_t into_minute_s = elapsed_seconds() % SECONDS_PER_MINUTE;
    alarm_set_every(MINUTE_ALARM, SECONDS_PER_MINUTE, SECONDS_PER_MINUTE - into_minute_s);
    show_in_header();
    wake_at_the_next_turn();
}

static void stop(void)
{
    state.banked_ms = elapsed_ms();
    state.is_running = 0;
    alarm_set(MINUTE_ALARM, 0);
    timer_set(TURN_TIMER, 0);
    header_status(HOST_HEADER_LEFT, HOST_ICON_NONE, "", 0);
}

static void reset(void)
{
    state.banked_ms = 0;
}

void app_init(void)
{
    bool is_stored = kv_get(STATE_KEY, sizeof STATE_KEY - 1, &state, sizeof state) ==
                         sizeof state &&
                     state.version == STATE_VERSION;
    if (!is_stored) {
        state = (stopwatch_t){ .version = STATE_VERSION };
    }
    if (state.is_running && run_ms() < 0) {
        /* The run began without a clock and the device has restarted: how
         * long it lasted is not to be had. What was counted before it is. */
        state.is_running = 0;
    }
}

void app_on_focus(void)
{
    wake_at_the_next_turn();
}

void app_on_timer(int32_t id)
{
    (void)id;
    /* A second after a second: one picture of a row, with no full refresh
     * between them. A minute is an ordinary change. */
    request_redraw(shows_seconds() ? HOST_REDRAW_NO_BLINK : HOST_REDRAW_NORMAL);
    wake_at_the_next_turn();
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
    if (event == BTN_OK_PRESSED) {
        if (state.is_running) {
            stop();
        } else {
            start();
        }
    } else if (event == BTN_OK_LONG_PRESSED && !state.is_running) {
        reset();
    } else {
        return;
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
    int32_t cap = gfx_font_cap_height(font);
    int32_t top = (body - cap) / 2 - (gfx_font_ascent(font) - cap);
    gfx_text((gfx_width() - gfx_text_width(font, line, length)) / 2, top, font, line, length);

    bool is_stopped_with_a_time = !state.is_running && state.banked_ms > 0;
    if (is_stopped_with_a_time) {
        static const char HINT[] = "HOLD OK TO RESET";
        int32_t hint_top = body - ui_text_height(HOST_UI_CAPTION) - HOST_UI_MARGIN / 2;
        ui_text((gfx_width() - ui_text_width(HOST_UI_CAPTION, HINT, sizeof HINT - 1)) / 2, hint_top,
                HOST_UI_CAPTION, HINT, sizeof HINT - 1, 0);
    }
    const char *ok = state.is_running ? "Stop" : is_stopped_with_a_time ? "Continue" : "Start";
    ui_actions(ok, app_text_length(ok), "Close", 5);
}
