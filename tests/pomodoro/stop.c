// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define HOST_IMPORT(name, ...) __VA_ARGS__
#include "../../apps/pomodoro/pomodoro.c"
#include "app_text.c"

/* The lifecycle test does not draw translated text. All string offsets
 * point to one nonempty label so header clearing is still observable. */
uint8_t app_strings[APP_STRINGS_BYTES] = { 'X', 0 };

static state_t stored;
static bool has_stored;
static int32_t alarms[2], timer_ms, header_length, notices;
static int64_t now_ms = 1000;
static uint32_t boot = 1;

bool app_state_load(const char *key, void *value, int32_t size, int32_t version)
{
    assert(strcmp(key, "state") == 0 && size == sizeof stored && version == STATE_VERSION);
    if (!has_stored) return false;
    memcpy(value, &stored, sizeof stored);
    return true;
}
bool app_state_save(const char *key, const void *value, int32_t size)
{
    assert(strcmp(key, "state") == 0 && size == sizeof stored);
    memcpy(&stored, value, sizeof stored);
    has_stored = true;
    return true;
}
bool app_setting_number(const char *key, int32_t decimals, int32_t *number)
{
    assert(strcmp(key, "rounds") == 0 && decimals == 0);
    *number = 2;
    return true;
}
int32_t app_setting_seconds(const char *key, int32_t otherwise)
{
    (void)otherwise;
    if (strcmp(key, "work") == 0) return 3000;
    if (strcmp(key, "break") == 0) return 600;
    assert(strcmp(key, "long") == 0);
    return 1200;
}
int64_t time_uptime_ms(void) { return now_ms; }
uint32_t time_boot_id(void) { return boot; }
int32_t alarm_set(int32_t id, int32_t seconds)
{
    assert(id >= 0 && id < 2);
    alarms[id] = seconds;
    return HOST_OK;
}
int32_t alarm_set_every(int32_t id, int32_t seconds, int32_t first_in)
{
    assert(id == MINUTE_ALARM && seconds == 60 && first_in > 0);
    alarms[id] = seconds;
    return HOST_OK;
}
int32_t timer_set(int32_t id, int32_t milliseconds)
{
    assert(id == MINUTE_TIMER);
    timer_ms = milliseconds;
    return HOST_OK;
}
int32_t header_set_status(int32_t side, int32_t icon, const char *text, int32_t length)
{
    (void)icon; (void)text;
    assert(side == HOST_HEADER_RIGHT);
    header_length = length;
    return HOST_OK;
}
int32_t text_fill(const char *pattern, int32_t length, const host_text_argument_t *args,
                  int32_t count, char *out, int32_t capacity)
{
    (void)pattern; (void)length; (void)args; (void)count;
    assert(capacity > 1);
    out[0] = '1';
    return 1;
}
int32_t notification_post(const host_notice_t *notice) { (void)notice; notices++; return HOST_OK; }
void screen_redraw(int32_t flags) { (void)flags; }
void log_write(int32_t level, const char *text, int32_t length)
{
    (void)level; (void)text; (void)length;
    assert(!"unexpected host failure");
}

static void assert_stopped(void)
{
    assert(state.phase == WORK && state.round == 1 && state.rounds == 2);
    assert(state.phase_length == 3000 && !state.ends_at && !state.paused_left);
    assert(!stored.ends_at && !stored.paused_left);
    assert(alarms[END_ALARM] == 0 && alarms[MINUTE_ALARM] == 0);
    assert(timer_ms == 0 && header_length == 0);
}

int main(void)
{
    app_on_load();
    app_on_button(BTN_OK_PRESSED);
    assert(state.ends_at && alarms[END_ALARM] == 3000 && timer_ms > 0);
    now_ms += 30000;
    app_on_button(BTN_UP_PRESSED);
    assert_stopped();
    app_on_alarm(END_ALARM);
    app_on_alarm(MINUTE_ALARM);
    assert_stopped();
    assert(notices == 0);

    app_on_button(BTN_OK_PRESSED);
    assert(seconds_left() == 3000);
    app_on_button(BTN_DOWN_PRESSED);
    assert(state.phase == BREAK && seconds_left() == 600);
    app_on_button(BTN_OK_PRESSED);
    assert(state.paused_left == 600);
    app_on_button(BTN_UP_PRESSED);
    assert_stopped();

    state = (state_t){0};
    has_saved = false;
    app_on_load();
    app_on_focus();
    app_on_updated();
    assert_stopped();
    boot++;
    now_ms = 1000;
    state = (state_t){0};
    app_on_load();
    app_on_focus();
    app_on_updated();
    assert_stopped();
    app_on_button(BTN_OK_PRESSED);
    assert(seconds_left() == 3000 && state.rounds == 2);
    puts("Pomodoro stop, pause, reload and reboot checks passed");
    return 0;
}
