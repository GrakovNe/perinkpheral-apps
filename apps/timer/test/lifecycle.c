// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../timer.c"
#include "standin.h"

static int writes(void) { return standin()->store_writes; }
static int redraws(void) { return standin()->redraws; }
static int32_t countdown_delay(void) { return standin_alarm_left(COUNTDOWN_ALARM); }
static int32_t timer_delay(void) { return standin_timer_left(MINUTE_TIMER); }

/* What the old stubs refused at each call, read from what the host was left with. */
static void check_host(void)
{
    const standin_t *host = standin();
    int32_t length = 0;
    /* Only the minute timer; the minutes' alarm is every 60 s or not at all; nothing else waits. */
    assert((standin_timers() & ~(1u << MINUTE_TIMER)) == 0);
    assert((standin_alarms() & ~(1u << COUNTDOWN_ALARM | 1u << MINUTE_ALARM)) == 0);
    assert(host->alarms[MINUTE_ALARM].kind == STANDIN_ALARM_NONE ||
           (host->alarms[MINUTE_ALARM].kind == STANDIN_ALARM_EVERY && host->alarms[MINUTE_ALARM].every == 60));
    /* The status is on the right only. */
    assert(host->status_icon[HOST_HEADER_LEFT] == HOST_ICON_NONE && host->status_text[HOST_HEADER_LEFT][0] == '\0');
    /* A saved state is the whole struct of this version. */
    const uint8_t *stored = standin_value(STATE_KEY, &length);
    assert(stored == NULL || (length == sizeof(timer_state_t) && *(const int32_t *)stored == STATE_VERSION));
    /* No alarm was refused. */
    assert(host->logs == 0);
}

static void press(int32_t event)
{
    app_on_button(event);
    check_host();
}

int main(void)
{
    standin_reset();
    standin_handlers.on_updated = app_on_updated;
    app_on_load();
    assert(state.minutes == MINUTES_DEFAULT);
    press(BTN_UP_PRESSED);
    assert(state.minutes == 26 && writes() == 1 && redraws() == 1);
    press(BTN_OK_PRESSED);
    assert(countdown_delay() == 1560 && timer_delay() == 60000);
    int before_writes = writes(), before_redraws = redraws();
    press(BTN_UP_PRESSED);
    press(BTN_DOWN_PRESSED);
    press(BTN_UP_LONG_PRESSED);
    press(BTN_DOWN_LONG_PRESSED);
    assert(writes() == before_writes && redraws() == before_redraws);
    standin_pass(17000);
    standin_updated(); /* the old version's alarms are gone: these two are set anew */
    check_host();
    assert(countdown_delay() == 1543 && standin_alarm_left(MINUTE_ALARM) == 43);
    assert(writes() == before_writes);
    standin_pass(43000);
    app_on_alarm(MINUTE_ALARM);
    check_host();
    /* The header's line has one number filled in: the minutes left. */
    assert(minutes_left() == 25 && standin()->status_icon[HOST_HEADER_RIGHT] == HOST_ICON_HOURGLASS);
    assert(strcmp(standin()->status_text[HOST_HEADER_RIGHT], "25 min") == 0);
    press(BTN_OK_PRESSED);
    assert(state.ends_at == 0 && countdown_delay() == 0 && timer_delay() == 0);
    state.minutes = MINUTES_MAX;
    before_writes = writes(); before_redraws = redraws();
    press(BTN_UP_PRESSED);
    press(BTN_UP_LONG_PRESSED);
    assert(writes() == before_writes && redraws() == before_redraws);
    state.minutes = MINUTES_MIN;
    press(BTN_DOWN_PRESSED);
    press(BTN_DOWN_LONG_PRESSED);
    assert(writes() == before_writes && redraws() == before_redraws);
    press(BTN_UP_LONG_PRESSED);
    assert(state.minutes == 6 && writes() == before_writes + 1);
    app_on_load();
    assert(state.minutes == 6);
    puts("Timer: ignored buttons do not write/redraw; update preserves header minute boundary; stop/settings passed");
    return 0;
}
