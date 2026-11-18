// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
#define HOST_IMPORT(name, ...) __VA_ARGS__
#include "../../apps/clock/clock.c"

static int64_t now;
static int32_t delay;
static int timer_calls, redraws;
int64_t time_now(void) { return now; }
int32_t timer_set(int32_t id, int32_t milliseconds)
{
    assert(id == MINUTE_TIMER);
    delay = milliseconds;
    timer_calls++;
    return HOST_OK;
}
void screen_redraw(int32_t flags) { (void)flags; redraws++; }

int main(void)
{
    now = 1800000000;
    app_on_focus();
    assert(delay == 60000);
    now += 50;
    app_on_intent(HOST_INTENT_TIME_CHANGED);
    assert(delay == 10000 && redraws == 1);
    now -= 40;
    app_on_intent(HOST_INTENT_TIME_CHANGED);
    assert(delay == 50000);
    now = 0;
    app_on_focus();
    assert(delay == RETRY_MS);
    now = 1800000059;
    app_on_intent(HOST_INTENT_TIME_CHANGED);
    assert(delay == 1000);
    now++;
    app_on_timer(MINUTE_TIMER);
    assert(delay == 60000);
    int previous_calls = timer_calls;
    app_on_intent(HOST_INTENT_STARTED);
    assert(timer_calls == previous_calls);
    puts("Clock: forward/backward time corrections, initial sync and minute scheduling passed");
    return 0;
}
