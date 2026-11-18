// SPDX-License-Identifier: MIT
#include <assert.h>
#include <string.h>

#define HOST_IMPORT(name, ...) __VA_ARGS__
#include "../../apps/life/life.c"

static uint8_t scene_bytes[HOST_SCENE_STATE_MAX];
static int32_t scene_length, timer_ms;

bool app_setting_is(const char *key, const char *value)
{
    return (strcmp(key, "cell") == 0 && strcmp(value, "4 px") == 0) ||
           (strcmp(key, "speed") == 0 && strcmp(value, "Fast") == 0);
}

bool app_setting_number(const char *key, int32_t decimals, int32_t *value)
{
    (void)key;
    (void)decimals;
    *value = 1;
    return true;
}

int64_t time_uptime_ms(void) { return 0; }
uint32_t time_boot_id(void) { return 1; }

int32_t timer_set(int32_t id, int32_t milliseconds)
{
    assert(id == STEP_TIMER);
    timer_ms = milliseconds;
    return HOST_OK;
}

void screen_redraw(int32_t reason_flags) { (void)reason_flags; }

int32_t scene_state_set(const void *data, int32_t length)
{
    assert(length <= (int32_t)sizeof scene_bytes);
    memcpy(scene_bytes, data, (size_t)length);
    scene_length = length;
    return HOST_OK;
}

int32_t scene_state_get(void *data, int32_t capacity)
{
    assert(scene_length <= capacity);
    memcpy(data, scene_bytes, (size_t)scene_length);
    return scene_length;
}

int main(void)
{
    app_on_focus();
    app_on_button(BTN_DOWN_PRESSED);
    app_on_button(BTN_OK_PRESSED);
    assert(running && generation == 1 && timer_ms == 600 && cell == 4);
    uint8_t expected[FIELD_BYTES];
    memcpy(expected, fields[current], sizeof expected);
    uint32_t expected_random = random_state;
    app_on_blur();

    /* Reconstruct a fresh instance's globals without touching the scene-owned bytes. */
    clear_fields();
    cell = columns = rows = 0;
    random_state = 0;
    app_on_focus();
    assert(running && generation == 1 && timer_ms == 600 && cell == 4);
    assert(memcmp(fields[current], expected, sizeof expected) == 0 && random_state == expected_random);
    app_on_timer(STEP_TIMER);
    assert(generation == 2 && memcmp(fields[current], expected, sizeof expected) != 0);

    scene_length = 0;
    app_on_focus();
    assert(!running && generation == 0 && timer_ms == 0);
    return 0;
}
