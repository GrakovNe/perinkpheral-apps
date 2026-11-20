// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../pomodoro.c"
#include "standin.h"

static state_t stored;

static int32_t timer_ms(void) { return standin_timer_left(MINUTE_TIMER); }
static int32_t header_length(void) { return (int32_t)strlen(standin()->status_text[HOST_HEADER_RIGHT]); }

/* What the old stubs refused at each call, read from what the host was left with. */
static void check_host(void)
{
    const standin_t *host = standin();
    int32_t length = 0;
    assert((standin_timers() & ~(1u << MINUTE_TIMER)) == 0);
    assert((standin_alarms() & ~(1u << END_ALARM | 1u << MINUTE_ALARM)) == 0);
    /* The minutes' alarm is every 60 s, due within the minute, or not at all. */
    assert(host->alarms[MINUTE_ALARM].kind == STANDIN_ALARM_NONE ||
           (host->alarms[MINUTE_ALARM].kind == STANDIN_ALARM_EVERY && host->alarms[MINUTE_ALARM].every == 60 &&
            standin_alarm_left(MINUTE_ALARM) >= 1 && standin_alarm_left(MINUTE_ALARM) <= 60));
    assert(host->status_icon[HOST_HEADER_LEFT] == HOST_ICON_NONE && host->status_text[HOST_HEADER_LEFT][0] == '\0');
    /* The state is saved whole, under "state", in this version. */
    const uint8_t *bytes = standin_value("state", &length);
    assert(bytes != NULL && length == sizeof stored);
    memcpy(&stored, bytes, sizeof stored);
    assert(stored.version == STATE_VERSION);
    assert(host->logs == 0);
}

static void assert_stopped(void)
{
    check_host();
    assert(state.phase == WORK && state.round == 1 && state.rounds == 2);
    assert(state.phase_length == 3000 && !state.ends_at && !state.paused_left);
    assert(!stored.ends_at && !stored.paused_left);
    assert(standin_alarms() == 0);
    assert(timer_ms() == 0 && header_length() == 0);
}

int main(void)
{
    standin_reset();
    standin_set("rounds", "2");
    standin_set("work", "50 min");
    standin_set("break", "10 min");
    standin_set("long", "20 min");
    standin_handlers.on_updated = app_on_updated;
    standin_pass(1000);
    app_on_load();
    app_on_button(BTN_OK_PRESSED);
    check_host();
    assert(state.ends_at && standin_alarm_left(END_ALARM) == 3000 && timer_ms() > 0);
    assert(standin()->alarms[MINUTE_ALARM].kind == STANDIN_ALARM_EVERY);
    standin_pass(30000);
    app_on_button(BTN_UP_PRESSED);
    assert_stopped();
    app_on_alarm(END_ALARM);
    app_on_alarm(MINUTE_ALARM);
    assert_stopped();
    assert(standin()->notices == 0);

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
    standin_updated();
    assert_stopped();
    standin_restart(); /* another boot id, the uptime from nothing */
    standin_pass(1000);
    state = (state_t){0};
    app_on_load();
    app_on_focus();
    standin_updated();
    assert_stopped();
    app_on_button(BTN_OK_PRESSED);
    check_host();
    assert(seconds_left() == 3000 && state.rounds == 2);
    puts("Pomodoro stop, pause, reload and reboot checks passed");
    return 0;
}
