// SPDX-License-Identifier: MIT
// Copyright (c) 2026 GrakovNe
/*
 * Pomodoro: work and breaks follow each other on or off the screen.
 * The end is an uptime deadline from this boot, never a count of ticks.
 * One alarm ends the phase; another keeps the header's minutes current.
 * Only transitions write the store. Phase lengths and the displayed round
 * goal are captured when a phase begins, so settings do not move a running
 * deadline. A restart resets the cycle, including a paused phase.
 * OK starts or pauses, DOWN skips, a long OK resets to the first work.
 */
#include "app_draw.h"
#include "app_state.h"
#include "app_text.h"
#include "app_values.h"
#include "host_api.h"
#include "strings.h"

enum { WORK, BREAK, LONG_BREAK };
enum { END_ALARM, MINUTE_ALARM };
#define MINUTE_TIMER 0
#define STATE_VERSION 1
typedef struct {
    int32_t version, phase, round, rounds;
    int64_t ends_at;
    uint32_t boot;
    int32_t paused_left, phase_length;
} state_t;
static state_t state, saved;
static bool has_saved;

static int64_t uptime_seconds(void) { return time_uptime_ms() / 1000; }
static int32_t configured_rounds(void)
{
    int32_t value;
    return app_setting_number("rounds", 0, &value) && value >= 2 && value <= 4 ? value : 4;
}
static int32_t phase_length(int32_t phase)
{
    static const char *const keys[] = { "work", "break", "long" };
    static const int32_t defaults[] = { 25 * 60, 5 * 60, 15 * 60 };
    int32_t n = app_setting_seconds(keys[phase], defaults[phase]);
    return n > 0 ? n : defaults[phase];
}
static void save(void)
{
    bool changed = !has_saved;
    const uint8_t *a = (const uint8_t *)&state, *b = (const uint8_t *)&saved;
    for (int32_t i = 0; i < (int32_t)sizeof state; i++) { changed |= a[i] != b[i]; }
    if (changed && app_state_save("state", &state, sizeof state)) { saved = state; has_saved = true; }
}
static void reset_state(void)
{
    state = (state_t){ .version = STATE_VERSION, .phase = WORK, .round = 1,
        .rounds = configured_rounds(), .boot = time_boot_id(), .phase_length = phase_length(WORK) };
}
static int32_t seconds_left(void)
{
    if (!state.ends_at) { return state.paused_left; }
    int64_t left = state.ends_at - uptime_seconds();
    return left > 0 ? (int32_t)left : 0;
}
static const char *phase_name(void)
{
    return app_string(state.phase == WORK ? STR_WORK : state.phase == BREAK ? STR_BREAK : STR_LONG_BREAK);
}
static int32_t fill(const char *pattern, const host_text_argument_t *args, int32_t count, char *out, int32_t capacity)
{
    int32_t n = text_fill(pattern, app_text_length(pattern), args, count, out, capacity - 1);
    out[n > 0 ? n : 0] = 0;
    return n;
}
static void minutes_text(int32_t seconds, char *line, int32_t capacity)
{
    if (seconds > 0 && seconds < 60) { app_text_copy(line, capacity, app_string(STR_LAST_MINUTE)); return; }
    host_text_argument_t arg = { .number = (seconds + 59) / 60 };
    fill(app_string(STR_MINUTES), &arg, 1, line, capacity);
}
static void show_header(void)
{
    if (!state.ends_at && !state.paused_left) {
        header_set_status(HOST_HEADER_RIGHT, HOST_ICON_NONE, "", 0); return;
    }
    char line[HOST_STATUS_TEXT_MAX + 1];
    if (state.paused_left) { app_text_copy(line, sizeof line, app_string(STR_PAUSED)); }
    else { minutes_text(seconds_left(), line, sizeof line); }
    header_set_status(HOST_HEADER_RIGHT, state.paused_left ? HOST_ICON_PAUSE : HOST_ICON_HOURGLASS, line, app_text_length(line));
}
static void check_alarm(int32_t result)
{
    if (result != HOST_OK) {
        static const char message[] = "no room for Pomodoro alarm";
        log_write(HOST_LOG_WARN, message, sizeof message - 1);
    }
}
static int32_t next_minute(int32_t left)
{
    /* At 60 seconds the label is "1 min"; it becomes "<1 min" a second later. */
    if (left == 60) { return 1; }
    int32_t into = left % 60;
    return into ? into : 60;
}
static void schedule_timer(void)
{
    int32_t left = seconds_left();
    timer_set(MINUTE_TIMER, state.ends_at && left > 0 ? next_minute(left) * 1000 : 0);
}
static void schedule(void)
{
    if (state.ends_at) {
        int32_t left = seconds_left();
        check_alarm(alarm_set(END_ALARM, left > 0 ? left : 1));
        check_alarm(alarm_set_every(MINUTE_ALARM, 60, left > 0 ? next_minute(left) : 1));
    } else {
        alarm_set(END_ALARM, 0); alarm_set(MINUTE_ALARM, 0);
    }
    show_header(); schedule_timer();
}
static void phase_notice(void)
{
    host_notice_t notice = { .icon = HOST_ICON_BELL };
    app_text_copy(notice.first, sizeof notice.first, phase_name());
    if (state.phase == WORK) {
        host_text_argument_t args[] = { { .number = state.round }, { .number = state.rounds } };
        fill(app_string(STR_ROUND), args, 2, notice.second, sizeof notice.second);
    } else { minutes_text(state.phase_length, notice.second, sizeof notice.second); }
    notification_post(&notice);
}
static void advance(bool running, bool paused, bool announce)
{
    int32_t rounds = configured_rounds();
    if (state.phase == WORK) {
        state.phase = state.round >= rounds ? LONG_BREAK : BREAK;
    } else {
        state.round = state.phase == LONG_BREAK || state.round >= rounds ? 1 : state.round + 1;
        state.phase = WORK;
    }
    state.rounds = rounds;
    if (state.round > rounds) { state.round = rounds; }
    state.phase_length = phase_length(state.phase);
    state.ends_at = running ? uptime_seconds() + state.phase_length : 0;
    state.paused_left = paused ? state.phase_length : 0;
    state.boot = time_boot_id();
    save(); schedule();
    if (announce) { phase_notice(); }
    screen_redraw(HOST_REDRAW_NORMAL);
}
void app_on_load(void)
{
    bool stored = app_state_load("state", &state, sizeof state, STATE_VERSION);
    has_saved = stored;
    if (stored) { saved = state; }
    bool stale = stored && state.boot != time_boot_id();
    if (!stored || stale || state.phase < WORK || state.phase > LONG_BREAK
        || state.round < 1 || state.rounds < 2 || state.rounds > 4 || state.round > state.rounds
        || state.phase_length < 1 || state.phase_length > 3600 || state.paused_left < 0
        || state.paused_left > state.phase_length) {
        reset_state();
        if (stored) { save(); }
    }
}
void app_on_focus(void) { show_header(); schedule_timer(); }
void app_on_updated(void) { schedule(); }
void app_on_settings(void)
{
    /* A stopped preview follows settings; an active or paused phase keeps its length. */
    if (!state.ends_at && !state.paused_left) {
        state.phase_length = phase_length(state.phase); state.rounds = configured_rounds();
        if (state.round > state.rounds) { state.round = state.rounds; }
        save();
    }
    screen_redraw(HOST_REDRAW_NORMAL);
}
void app_on_button(int32_t event)
{
    switch (event) {
    case BTN_OK_LONG_PRESSED:
        reset_state(); save(); schedule(); break;
    case BTN_DOWN_PRESSED:
        advance(state.ends_at != 0, state.paused_left != 0, false); return;
    case BTN_OK_PRESSED:
        if (state.ends_at) {
            int32_t left = seconds_left();
            if (left == 0) { advance(true, false, true); return; }
            state.paused_left = left; state.ends_at = 0;
        } else {
            int32_t left = state.paused_left ? state.paused_left : state.phase_length;
            state.paused_left = 0; state.ends_at = uptime_seconds() + left; state.boot = time_boot_id();
        }
        save(); schedule(); break;
    default: return;
    }
    screen_redraw(HOST_REDRAW_NORMAL);
}
void app_on_alarm(int32_t id)
{
    if (!state.ends_at) { return; }
    if (id == END_ALARM) {
        if (seconds_left() == 0) { advance(true, false, true); }
    } else if (id == MINUTE_ALARM) {
        show_header();
        /* Keep the sub-minute label accurate off the screen as well. */
        if (seconds_left() == 60) { check_alarm(alarm_set_every(MINUTE_ALARM, 60, 1)); }
        screen_redraw(HOST_REDRAW_NORMAL);
    }
}
void app_on_timer(int32_t id)
{
    if (id != MINUTE_TIMER) { return; }
    screen_redraw(HOST_REDRAW_NORMAL); schedule_timer();
}
void app_on_draw(void)
{
    const char *name = phase_name();
    int32_t title_font = app_text_width(HOST_FONT_MEDIUM, name) <= 264 ? HOST_FONT_MEDIUM : HOST_FONT_TEXT;
    app_draw_text(HOST_UI_MARGIN, 4, title_font, name);
    host_text_argument_t args[] = { { .number = state.round }, { .number = state.rounds } };
    char round[32]; fill(app_string(STR_OF), args, 2, round, sizeof round);
    app_draw_right(gfx_width() - HOST_UI_MARGIN, 12, HOST_FONT_SMALL, round);
    int32_t left = state.ends_at || state.paused_left ? seconds_left() : state.phase_length;
    char number[12]; app_text_t text;
    app_text_start(&text, number, sizeof number);
    if (left > 0 && left < 60) { app_text_add(&text, "<1"); }
    else { app_text_add_number(&text, (left + 59) / 60); }
    const char *unit = app_string(STR_MINUTE_UNIT);
    int32_t font = HOST_FONT_HUGE | HOST_FONT_TABULAR;
    int32_t w = ui_value_width(font, number, app_text_length(number), unit, app_text_length(unit));
    int32_t y = 68 - (gfx_font_ascent(font) - gfx_font_cap_height(font));
    ui_draw_value((gfx_width() - w) / 2, y, font, number, app_text_length(number), unit, app_text_length(unit));
    /* The bar follows the displayed minutes, including when a redraw comes late. */
    int32_t total_minutes = (state.phase_length + 59) / 60;
    int32_t remaining_minutes = (left + 59) / 60;
    ui_draw_bar(32, 183, gfx_width() - 64, total_minutes - remaining_minutes, total_minutes);
    app_draw_actions(app_string(state.ends_at ? STR_PAUSE : STR_START), 0);
}
