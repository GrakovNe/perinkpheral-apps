// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../stopwatch.c"
#include "standin.h"

static stopwatch_t stored;
static int32_t seeded; /* writes of the test itself, not the application's */

static int32_t store_writes(void) { return standin()->store_writes - seeded; }
static int32_t redraw_calls(void) { return standin()->redraws; }
static int32_t next_timer_ms(void) { return standin_timer_left(TURN_TIMER); }
static const char *header_text(void) { return standin()->status_text[HOST_HEADER_LEFT]; }

/* What the old stubs refused at each call, read from what the host was left with. */
static void check_host(void)
{
    const standin_t *host = standin();
    int32_t length = 0;
    assert((standin_timers() & ~(1u << TURN_TIMER)) == 0);
    assert((standin_alarms() & ~(1u << MINUTE_ALARM)) == 0);
    /* The status is on the left only, with the play icon or none. */
    assert(host->status_icon[HOST_HEADER_RIGHT] == HOST_ICON_NONE && host->status_text[HOST_HEADER_RIGHT][0] == '\0');
    assert(host->status_icon[HOST_HEADER_LEFT] == HOST_ICON_PLAY || host->status_icon[HOST_HEADER_LEFT] == HOST_ICON_NONE);
    /* A saved state is the whole struct of this version. */
    const uint8_t *bytes = standin_value(STATE_KEY, &length);
    assert(bytes == NULL || (length == sizeof stored && *(const int32_t *)bytes == STATE_VERSION));
    assert(host->logs == 0);
}

/* The host counts no alarm_set_every calls: whether the minutes' alarm waits is what is left of them. */
static bool alarm_waits(void)
{
    const standin_alarm_t *alarm = &standin()->alarms[MINUTE_ALARM];
    check_host();
    if (alarm->kind == STANDIN_ALARM_NONE) {
        return false;
    }
    assert(alarm->kind == STANDIN_ALARM_EVERY && alarm->every == 60);
    assert(standin_alarm_left(MINUTE_ALARM) >= 1 && standin_alarm_left(MINUTE_ALARM) <= 60);
    return true;
}

/* Nor header_set_status calls: a header never set has no icon and no text. */
static bool header_is_set(void)
{
    return standin()->status_icon[HOST_HEADER_LEFT] != HOST_ICON_NONE || header_text()[0] != '\0';
}

/* A device that restarted (boot 2, the state is of boot 1) and does not know the time yet. */
static void reset_host(void)
{
    state = (stopwatch_t){0};
    stored = (stopwatch_t){ .version = STATE_VERSION, .is_running = 1,
        .banked_ms = 2000, .started_uptime_ms = 500, .boot = 1, .started_at = 1800000000 };
    standin_reset();
    standin_handlers.on_updated = app_on_updated;
    standin()->boot_id = 2;
    standin_clock(0);
    standin_pass(1000);
    seeded = 0;
}

/* Puts `stored` where app_state_load() reads it. */
static void keep_stored(void)
{
    assert(kv_set(STATE_KEY, (int32_t)strlen(STATE_KEY), &stored, sizeof stored) == HOST_OK);
    seeded++;
}

static void read_stored(void)
{
    int32_t length = 0;
    const uint8_t *bytes = standin_value(STATE_KEY, &length);
    assert(bytes != NULL && length == sizeof stored);
    memcpy(&stored, bytes, sizeof stored);
}

static void test_cold_boot_then_time_sync(void)
{
    reset_host();
    keep_stored();
    app_on_load();
    app_on_intent(HOST_INTENT_STARTED);
    app_on_focus();
    read_stored();
    assert(state.is_running && stored.is_running && store_writes() == 0);
    assert(!alarm_waits() && !header_is_set() && next_timer_ms() == 0);

    standin_clock(stored.started_at + 65);
    app_on_intent(HOST_INTENT_TIME_CHANGED);
    assert(state.is_running && elapsed_ms() == 67000);
    assert(alarm_waits() && standin()->status_icon[HOST_HEADER_LEFT] == HOST_ICON_PLAY);
    assert(next_timer_ms() > 0 && redraw_calls() == 1);
    assert(strcmp(header_text(), "00:01") == 0 && store_writes() == 0);
}

static void test_background_reload_after_sync(void)
{
    reset_host();
    keep_stored();
    app_on_load();
    app_on_intent(HOST_INTENT_STARTED);
    assert(!alarm_waits());
    state = (stopwatch_t){0}; /* the foreground application displaced this instance */
    standin_clock(stored.started_at + 125);
    app_on_load();
    app_on_intent(HOST_INTENT_TIME_CHANGED);
    assert(state.is_running && elapsed_ms() == 127000);
    assert(alarm_waits() && strcmp(header_text(), "00:02") == 0);
}

static void test_reopen_and_update_restore_background_work(void)
{
    reset_host();
    keep_stored();
    standin_clock(stored.started_at + 60);
    app_on_load();
    app_on_focus();
    assert(alarm_waits() && strcmp(header_text(), "00:01") == 0 && next_timer_ms() > 0);
    /* A minute on, and the old version's alarm gone: the update sets both again. */
    standin_pass(60000);
    standin_updated();
    assert(alarm_waits() && strcmp(header_text(), "00:02") == 0);

    reset_host();
    keep_stored();
    app_on_load();
    standin_updated();
    assert(state.is_running && !alarm_waits() && !header_is_set());
}

static void test_same_boot_offline_then_time_sync(void)
{
    reset_host();
    standin()->boot_id = stored.boot;
    stored.started_at = 0;
    keep_stored();
    standin_pass(64500); /* the uptime is 65500 */
    app_on_load();
    app_on_intent(HOST_INTENT_STARTED);
    assert(state.is_running && elapsed_ms() == 67000 && alarm_waits());
    standin_clock(1800000065);
    app_on_intent(HOST_INTENT_TIME_CHANGED);
    read_stored();
    assert(stored.started_at == 1800000000 && store_writes() == 1);
    app_on_intent(HOST_INTENT_TIME_CHANGED);
    assert(store_writes() == 1); /* repeated sync does not rewrite the saved anchor */
}

static void test_missing_anchor_and_stopped_state(void)
{
    reset_host();
    stored.started_at = 0;
    keep_stored();
    app_on_load();
    app_on_intent(HOST_INTENT_STARTED);
    assert(!state.is_running && elapsed_ms() == stored.banked_ms && !alarm_waits());
    standin_clock(1800000100);
    app_on_intent(HOST_INTENT_TIME_CHANGED);
    assert(!state.is_running && !alarm_waits());

    reset_host();
    stored.is_running = 0;
    keep_stored();
    app_on_load();
    standin_clock(1800000100);
    app_on_intent(HOST_INTENT_TIME_CHANGED);
    app_on_focus();
    standin_updated();
    assert(!state.is_running && elapsed_ms() == stored.banked_ms && !alarm_waits());

    reset_host();
    app_on_load();
    app_on_intent(HOST_INTENT_TIME_CHANGED);
    assert(!state.is_running && elapsed_ms() == 0 && !alarm_waits());
}

static void test_stop_after_resume(void)
{
    reset_host();
    keep_stored();
    app_on_load();
    standin_clock(stored.started_at + 65);
    app_on_intent(HOST_INTENT_TIME_CHANGED);
    assert(alarm_waits());
    app_on_button(BTN_OK_PRESSED);
    read_stored();
    assert(!stored.is_running && stored.banked_ms == 67000 && next_timer_ms() == 0);
    assert(strcmp(header_text(), "") == 0);
    app_on_intent(HOST_INTENT_TIME_CHANGED);
    assert(!state.is_running && !alarm_waits()); /* stopped: the alarm is not set again */
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
