// SPDX-License-Identifier: MIT
#include <assert.h>
#include <string.h>

#include "../life.c"
#include "standin.h"

/* What the step timer waits; 0 when it does not. No other timer is the application's. */
static int32_t timer_ms(void)
{
    assert((standin_timers() & ~(1u << STEP_TIMER)) == 0);
    return standin_timer_left(STEP_TIMER);
}

int main(void)
{
    standin_reset();
    standin_set("cell", "4 px");
    standin_set("speed", "Fast");
    standin_set("seed", "1");
    standin()->is_session_writable = true; /* the handlers are called as for the instance on the screen */
    app_on_focus();
    app_on_button(BTN_DOWN_PRESSED);
    app_on_button(BTN_OK_PRESSED);
    assert(running && generation == 1 && timer_ms() == 600 && cell == 4);
    uint8_t expected[FIELD_BYTES];
    memcpy(expected, fields[current], sizeof expected);
    uint32_t expected_random = random_state;
    app_on_blur();
    assert(standin()->session_length == sizeof(life_scene_state_t));

    /* Reconstruct a fresh instance's globals without touching the scene-owned bytes. */
    clear_fields();
    cell = columns = rows = 0;
    random_state = 0;
    app_on_focus();
    assert(running && generation == 1 && timer_ms() == 600 && cell == 4);
    assert(memcmp(fields[current], expected, sizeof expected) == 0 && random_state == expected_random);
    app_on_timer(STEP_TIMER);
    assert(generation == 2 && memcmp(fields[current], expected, sizeof expected) != 0);

    standin_forget_session();
    app_on_focus();
    assert(!running && generation == 0 && timer_ms() == 0);
    return 0;
}
