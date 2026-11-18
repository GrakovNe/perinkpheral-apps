// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define HOST_IMPORT(name, ...) __VA_ARGS__
#include "../../apps/timer/timer.c"
#include "app_text.c"

static timer_state_t stored;
static bool has_stored;
static int64_t uptime;
static int writes, redraws, minute_first, header_calls;
static int countdown_delay, timer_delay;
bool app_state_load(const char *key, void *value, int32_t size, int32_t version)
{
    (void)key;
    assert(size == sizeof stored && version == STATE_VERSION);
    if (!has_stored) return false;
    memcpy(value, &stored, sizeof stored);
    return true;
}
bool app_state_save(const char *key, const void *value, int32_t size)
{
    (void)key;
    assert(size == sizeof stored);
    memcpy(&stored, value, sizeof stored);
    has_stored = true;
    writes++;
    return true;
}
int64_t time_uptime_ms(void) { return uptime; }
uint32_t time_boot_id(void) { return 1; }
int32_t timer_set(int32_t id, int32_t milliseconds)
{
    assert(id == MINUTE_TIMER);
    timer_delay = milliseconds;
    return HOST_OK;
}
int32_t alarm_set(int32_t id, int32_t seconds)
{
    if (id == COUNTDOWN_ALARM) countdown_delay = seconds;
    else assert(id == MINUTE_ALARM && seconds == 0);
    return HOST_OK;
}
int32_t alarm_set_every(int32_t id, int32_t seconds, int32_t first_in)
{
    assert(id == MINUTE_ALARM && seconds == 60);
    minute_first = first_in;
    return HOST_OK;
}
int32_t header_set_status(int32_t side, int32_t icon, const char *text, int32_t length)
{
    (void)icon; (void)text; (void)length;
    assert(side == HOST_HEADER_RIGHT);
    header_calls++;
    return HOST_OK;
}
int32_t text_fill(const char *pattern, int32_t length, const host_text_argument_t *args,
                  int32_t count, char *out, int32_t capacity)
{
    (void)pattern; (void)length;
    assert(count == 1);
    return snprintf(out, (size_t)capacity, "%d min", args[0].number);
}
uint8_t app_strings[APP_STRINGS_BYTES] = { 'X', 0 };
int32_t notification_post(const host_notice_t *notice) { (void)notice; return HOST_OK; }
void screen_redraw(int32_t flags) { (void)flags; redraws++; }
void log_write(int32_t level, const char *text, int32_t length)
{ (void)level; (void)text; (void)length; assert(!"unexpected alarm failure"); }

int main(void)
{
    app_on_load();
    assert(state.minutes == MINUTES_DEFAULT);
    app_on_button(BTN_UP_PRESSED);
    assert(state.minutes == 26 && writes == 1 && redraws == 1);
    app_on_button(BTN_OK_PRESSED);
    assert(countdown_delay == 1560 && timer_delay == 60000);
    int before_writes = writes, before_redraws = redraws;
    app_on_button(BTN_UP_PRESSED);
    app_on_button(BTN_DOWN_PRESSED);
    app_on_button(BTN_UP_LONG_PRESSED);
    app_on_button(BTN_DOWN_LONG_PRESSED);
    assert(writes == before_writes && redraws == before_redraws);
    uptime = 17000;
    app_on_updated();
    assert(countdown_delay == 1543 && minute_first == 43);
    assert(writes == before_writes);
    uptime = 60000;
    app_on_alarm(MINUTE_ALARM);
    assert(minutes_left() == 25 && header_calls > 0);
    app_on_button(BTN_OK_PRESSED);
    assert(state.ends_at == 0 && countdown_delay == 0 && timer_delay == 0);
    state.minutes = MINUTES_MAX;
    before_writes = writes; before_redraws = redraws;
    app_on_button(BTN_UP_PRESSED);
    app_on_button(BTN_UP_LONG_PRESSED);
    assert(writes == before_writes && redraws == before_redraws);
    state.minutes = MINUTES_MIN;
    app_on_button(BTN_DOWN_PRESSED);
    app_on_button(BTN_DOWN_LONG_PRESSED);
    assert(writes == before_writes && redraws == before_redraws);
    app_on_button(BTN_UP_LONG_PRESSED);
    assert(state.minutes == 6 && writes == before_writes + 1);
    app_on_load();
    assert(state.minutes == 6);
    puts("Timer: ignored buttons do not write/redraw; update preserves header minute boundary; stop/settings passed");
    return 0;
}
