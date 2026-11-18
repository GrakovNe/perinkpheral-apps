// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define HOST_IMPORT(name, ...) __VA_ARGS__
#include "../../apps/stopwatch/stopwatch.c"
#include "app_text.c"

static stopwatch_t stored;
static bool has_stored;
static int64_t wall_seconds, uptime_ms;
static uint32_t current_boot;
static int alarm_calls, header_calls, redraw_calls, store_writes;
static int32_t next_timer_ms;
static char header_text[HOST_STATUS_TEXT_MAX + 1];

bool app_state_load(const char *key, void *value, int32_t size, int32_t version)
{
    assert(strcmp(key, STATE_KEY) == 0 && size == sizeof stored && version == STATE_VERSION);
    if (!has_stored) { return false; }
    memcpy(value, &stored, sizeof stored);
    return true;
}

bool app_state_save(const char *key, const void *value, int32_t size)
{
    assert(strcmp(key, STATE_KEY) == 0 && size == sizeof stored);
    memcpy(&stored, value, sizeof stored);
    has_stored = true;
    store_writes++;
    return true;
}

int64_t time_now(void) { return wall_seconds; }
int64_t time_uptime_ms(void) { return uptime_ms; }
uint32_t time_boot_id(void) { return current_boot; }

int32_t alarm_set_every(int32_t id, int32_t seconds, int32_t first_in)
{
    assert(id == MINUTE_ALARM && seconds == 60 && first_in > 0 && first_in <= 60);
    alarm_calls++;
    return HOST_OK;
}

int32_t alarm_set(int32_t id, int32_t seconds)
{
    assert(id == MINUTE_ALARM && seconds == 0);
    return HOST_OK;
}

int32_t header_set_status(int32_t side, int32_t icon, const char *text, int32_t length)
{
    assert(side == HOST_HEADER_LEFT && (icon == HOST_ICON_PLAY || icon == HOST_ICON_NONE));
    assert(length >= 0 && length < (int32_t)sizeof header_text);
    memcpy(header_text, text, (size_t)length);
    header_text[length] = 0;
    header_calls++;
    return HOST_OK;
}

int32_t timer_set(int32_t id, int32_t milliseconds)
{
    assert(id == TURN_TIMER);
    next_timer_ms = milliseconds;
    return HOST_OK;
}

void screen_redraw(int32_t flags) { (void)flags; redraw_calls++; }
void log_write(int32_t level, const char *text, int32_t length)
{
    (void)level; (void)text; (void)length;
    assert(!"unexpected host failure");
}

static void reset_host(void)
{
    state = (stopwatch_t){0};
    stored = (stopwatch_t){ .version = STATE_VERSION, .is_running = 1,
        .banked_ms = 2000, .started_uptime_ms = 500, .boot = 1, .started_at = 1800000000 };
    has_stored = true;
    current_boot = 2;
    wall_seconds = 0;
    uptime_ms = 1000;
    alarm_calls = header_calls = redraw_calls = store_writes = next_timer_ms = 0;
    header_text[0] = 0;
}

static void test_cold_boot_then_time_sync(void)
{
    reset_host();
    app_on_load();
    app_on_intent(HOST_INTENT_STARTED);
    app_on_focus();
    assert(state.is_running && stored.is_running && store_writes == 0);
    assert(alarm_calls == 0 && header_calls == 0 && next_timer_ms == 0);

    wall_seconds = stored.started_at + 65;
    app_on_intent(HOST_INTENT_TIME_CHANGED);
    assert(state.is_running && elapsed_ms() == 67000);
    assert(alarm_calls == 1 && header_calls == 1 && next_timer_ms > 0 && redraw_calls == 1);
    assert(strcmp(header_text, "00:01") == 0 && store_writes == 0);
}

static void test_background_reload_after_sync(void)
{
    reset_host();
    app_on_load();
    app_on_intent(HOST_INTENT_STARTED);
    state = (stopwatch_t){0}; /* the foreground application displaced this instance */
    wall_seconds = stored.started_at + 125;
    app_on_load();
    app_on_intent(HOST_INTENT_TIME_CHANGED);
    assert(state.is_running && elapsed_ms() == 127000);
    assert(alarm_calls == 1 && strcmp(header_text, "00:02") == 0);
}

static void test_reopen_and_update_restore_background_work(void)
{
    reset_host();
    wall_seconds = stored.started_at + 60;
    app_on_load();
    app_on_focus();
    assert(alarm_calls == 1 && header_calls == 1 && next_timer_ms > 0);
    app_on_updated();
    assert(alarm_calls == 2 && header_calls == 2);

    reset_host();
    app_on_load();
    app_on_updated();
    assert(state.is_running && alarm_calls == 0 && header_calls == 0);
}

static void test_same_boot_offline_then_time_sync(void)
{
    reset_host();
    current_boot = stored.boot;
    stored.started_at = 0;
    uptime_ms = 65500;
    app_on_load();
    app_on_intent(HOST_INTENT_STARTED);
    assert(state.is_running && elapsed_ms() == 67000 && alarm_calls == 1);
    wall_seconds = 1800000065;
    app_on_intent(HOST_INTENT_TIME_CHANGED);
    assert(stored.started_at == 1800000000 && store_writes == 1);
    app_on_intent(HOST_INTENT_TIME_CHANGED);
    assert(store_writes == 1); /* repeated sync does not rewrite the saved anchor */
}

static void test_missing_anchor_and_stopped_state(void)
{
    reset_host();
    stored.started_at = 0;
    app_on_load();
    app_on_intent(HOST_INTENT_STARTED);
    assert(!state.is_running && elapsed_ms() == stored.banked_ms && alarm_calls == 0);
    wall_seconds = 1800000100;
    app_on_intent(HOST_INTENT_TIME_CHANGED);
    assert(!state.is_running && alarm_calls == 0);

    reset_host();
    stored.is_running = 0;
    app_on_load();
    wall_seconds = 1800000100;
    app_on_intent(HOST_INTENT_TIME_CHANGED);
    app_on_focus();
    app_on_updated();
    assert(!state.is_running && elapsed_ms() == stored.banked_ms && alarm_calls == 0);

    reset_host();
    has_stored = false;
    app_on_load();
    app_on_intent(HOST_INTENT_TIME_CHANGED);
    assert(!state.is_running && elapsed_ms() == 0 && alarm_calls == 0);
}

static void test_stop_after_resume(void)
{
    reset_host();
    app_on_load();
    wall_seconds = stored.started_at + 65;
    app_on_intent(HOST_INTENT_TIME_CHANGED);
    app_on_button(BTN_OK_PRESSED);
    assert(!stored.is_running && stored.banked_ms == 67000 && next_timer_ms == 0);
    assert(strcmp(header_text, "") == 0);
    app_on_intent(HOST_INTENT_TIME_CHANGED);
    assert(!state.is_running && alarm_calls == 1);
}

int main(void)
{
    test_cold_boot_then_time_sync();
    test_background_reload_after_sync();
    test_reopen_and_update_restore_background_work();
    test_same_boot_offline_then_time_sync();
    test_missing_anchor_and_stopped_state();
    test_stop_after_resume();
    puts("Stopwatch lifecycle checks passed");
    return 0;
}
