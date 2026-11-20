// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
#include "../clock.c"
#include "standin.h"

/* What the minute timer was last set to; the uptime never moves here. */
static int32_t delay(void)
{
    assert(standin_timers() == 1u << MINUTE_TIMER);
    return standin_timer_left(MINUTE_TIMER);
}

int main(void)
{
    int64_t now = 1800000000;
    standin_reset();
    standin_clock(now);
    app_on_focus();
    assert(delay() == 60000);
    now += 50;
    standin_clock(now);
    app_on_intent(HOST_INTENT_TIME_CHANGED);
    assert(delay() == 10000 && standin()->redraws == 1);
    now -= 40;
    standin_clock(now);
    app_on_intent(HOST_INTENT_TIME_CHANGED);
    assert(delay() == 50000);
    now = 0;
    standin_clock(now);
    app_on_focus();
    assert(delay() == RETRY_MS);
    now = 1800000059;
    standin_clock(now);
    app_on_intent(HOST_INTENT_TIME_CHANGED);
    assert(delay() == 1000);
    now++;
    standin_clock(now);
    app_on_timer(MINUTE_TIMER);
    assert(delay() == 60000);
    /* The host counts no timer_set calls: the clock is moved, so one made now would set another delay. */
    standin_clock(now + 7);
    app_on_intent(HOST_INTENT_STARTED);
    assert(delay() == 60000);
    puts("Clock: forward/backward time corrections, initial sync and minute scheduling passed");
    return 0;
}
