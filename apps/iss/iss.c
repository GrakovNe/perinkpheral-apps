// SPDX-License-Identifier: MIT
/*
 * ISS over an equirectangular Earth: stippled land, a solar terminator
 * and ninety minutes of observed positions. The 400x200 map fits below
 * the header with sixty pixels left for telemetry. Natural Earth land
 * is public domain; its pre-stippled 10 KB bitmap is read from the package
 * by the host, keeping it out of the application's 64 KB memory page.
 */
#include "app_draw.h"
#include "app_fixed.h"
#include "app_state.h"
#include "app_text.h"
#include "app_values.h"
#include "host_api.h"
#include "orbit.h"
#include "strings.h"

enum { VERSION = 1 };
typedef struct {
    int32_t version, has_data, status, latitude, longitude, altitude, velocity;
    int32_t solar_latitude, solar_longitude, daylight, minute;
    orbit_track_t track;
} state_t;
static state_t state;
_Static_assert(sizeof(state_t) <= HOST_KV_VALUE_MAX, "ISS snapshot fits one store value");
void app_on_load(void)
{
    if (!app_state_load("state", &state, sizeof state, VERSION)) { state = (state_t){ .version = VERSION }; }
}
void app_on_button(int32_t event) { if (event == BTN_OK_PRESSED) { poll_run(0); } }
void app_on_picked(int32_t request, int32_t http_status)
{
    if (request) { return; }
    state_t fresh = state;
    fresh.status = http_status;
    char visibility[16];
    int64_t timestamp;
    bool valid = http_status >= 200 && http_status < 300 &&
        app_value_number(0, 2, &fresh.latitude) && fresh.latitude >= -9000 && fresh.latitude <= 9000 &&
        app_value_number(1, 2, &fresh.longitude) && fresh.longitude >= -18000 && fresh.longitude <= 18000 &&
        app_value_number(2, 0, &fresh.altitude) && fresh.altitude >= 0 && fresh.altitude < 2000 &&
        app_value_number(3, 0, &fresh.velocity) && fresh.velocity >= 0 && fresh.velocity < 100000 &&
        app_value_text(4, visibility, sizeof visibility) &&
        (app_text_equal(visibility, "daylight") || app_text_equal(visibility, "eclipsed")) &&
        app_value_number(5, 2, &fresh.solar_latitude) && fresh.solar_latitude >= -9000 && fresh.solar_latitude <= 9000 &&
        app_value_number(6, 2, &fresh.solar_longitude) && fresh.solar_longitude >= -36000 && fresh.solar_longitude <= 36000 &&
        app_value_fixed(7, 0, &timestamp) && timestamp >= HOST_TIME_MIN && timestamp <= HOST_TIME_MAX;
    if (valid && (!state.has_data || timestamp / 60 >= state.minute)) {
        while (fresh.solar_longitude > 18000) { fresh.solar_longitude -= 36000; }
        while (fresh.solar_longitude < -18000) { fresh.solar_longitude += 36000; }
        fresh.has_data = 1;
        fresh.daylight = app_text_equal(visibility, "daylight");
        fresh.minute = (int32_t)(timestamp / 60);
        orbit_track_add(&fresh.track, fresh.minute, fresh.latitude, fresh.longitude);
    } else {
        fresh = state;
        fresh.status = valid ? state.status : http_status >= 200 && http_status < 300 ? HOST_FAILED_NO_ANSWER : http_status;
    }
    bool changed = false;
    for (int32_t byte = 0; byte < (int32_t)sizeof fresh; byte++) {
        changed |= ((const uint8_t *)&fresh)[byte] != ((const uint8_t *)&state)[byte];
    }
    state = fresh;
    if (changed) { app_state_save("state", &state, sizeof state); }
    screen_redraw(HOST_REDRAW_NORMAL);
}
static void dot(int32_t x, int32_t y) { gfx_draw_rect(x, y, 1, 1, HOST_RECT_FILLED); }
static void draw_night(void)
{
    /* One row of sparse night dots, distinct from the land's 1-in-4
     * stipple. The sun's longitude may arrive in the range 0..360. */
    for (int32_t y = 1; y < MAP_HEIGHT; y += 4) {
        uint8_t row[MAP_WIDTH / 8] = {0};
        int32_t latitude = 9000 - (y * 18000 + 9000) / MAP_HEIGHT;
        for (int32_t x = (y % 8 == 1 ? 1 : 3); x < MAP_WIDTH; x += 4) {
            int32_t longitude = (x * 36000 + 18000) / MAP_WIDTH - 18000;
            if (orbit_night(latitude, longitude, state.solar_latitude, state.solar_longitude)) {
                row[x / 8] |= 128u >> (x % 8);
            }
        }
        gfx_draw_bitmap(0, y, MAP_WIDTH, 1, row);
    }
}
static void draw_track(void)
{
    int32_t prior_x = -1, prior_y = 0, prior_minute = 0;
    int32_t minute = time_now() > 0 ? (int32_t)(time_now() / 60) : state.minute;
    for (int32_t index = 0; index < state.track.count; index++) {
        const orbit_point_t *point = &state.track.points[(state.track.next + TRACK_POINTS - state.track.count + index) % TRACK_POINTS];
        if (point->minute <= minute - TRACK_POINTS) { prior_x = -1; continue; }
        int32_t x = orbit_x(point->longitude), y = orbit_y(point->latitude);
        int32_t dx = x - prior_x, dy = y - prior_y;
        int32_t distance_x = dx < 0 ? -dx : dx, distance_y = dy < 0 ? -dy : dy;
        int32_t steps = distance_x > distance_y ? distance_x : distance_y;
        /* Do not join across the date line or across an unobserved gap. */
        if (prior_x >= 0 && distance_x < MAP_WIDTH / 2 && point->minute - prior_minute <= 2 && steps) {
            for (int32_t step = 0; step <= steps; step += 3) {
                int32_t dot_x = prior_x + dx * step / steps, dot_y = prior_y + dy * step / steps;
                gfx_set_color(HOST_PAPER);
                gfx_draw_rect(dot_x - 1, dot_y - 1, 3, 3, HOST_RECT_FILLED);
                gfx_set_color(HOST_INK);
                dot(dot_x, dot_y);
            }
        } else { dot(x, y); }
        prior_x = x; prior_y = y; prior_minute = point->minute;
    }
}
static void draw_station(void)
{
    int32_t x = orbit_x(state.longitude), y = orbit_y(state.latitude);
    gfx_set_color(HOST_PAPER);
    gfx_draw_rect(x - 6, y - 6, 13, 13, HOST_RECT_FILLED);
    gfx_set_color(HOST_INK);
    static const uint8_t bitmap[18] = {
        0x1c, 0, 0x22, 0, 0x41, 0, 0x80, 0x80, 0x88, 0x80,
        0x80, 0x80, 0x41, 0, 0x22, 0, 0x1c, 0
    };
    gfx_draw_bitmap(x - 4, y - 4, 9, 9, bitmap);
    gfx_draw_line(x - 9, y, x - 6, y);
    gfx_draw_line(x + 6, y, x + 9, y);
    gfx_draw_line(x, y - 9, x, y - 6);
    gfx_draw_line(x, y + 6, x, y + 9);
}
static void coordinate(app_text_t *text, int32_t value, int32_t positive, int32_t negative)
{
    app_text_add_fixed(text, value < 0 ? -value : value, 2, 0);
    app_text_add(text, APP_TEXT_DEGREE " ");
    app_text_add(text, app_string(value < 0 ? negative : positive));
}
void app_on_draw(void)
{
    if (!state.has_data) { ui_draw_reason(state.status); return; }
    gfx_draw_bitmap_file(0, 0, MAP_WIDTH, MAP_HEIGHT, "land.bin", 8, 0);
    draw_night();
    for (int32_t longitude = -15000; longitude <= 15000; longitude += 3000) {
        for (int32_t y = 0; y < MAP_HEIGHT; y += 6) { dot(orbit_x(longitude), y); }
    }
    for (int32_t latitude = -6000; latitude <= 6000; latitude += 3000) {
        for (int32_t x = 0; x < MAP_WIDTH; x += 6) { dot(x, orbit_y(latitude)); }
    }
    draw_track();
    draw_station();
    char line[128];
    app_text_t text;
    app_text_start(&text, line, sizeof line);
    coordinate(&text, state.latitude, STR_NORTH, STR_SOUTH);
    app_text_add(&text, " ");
    coordinate(&text, state.longitude, STR_EAST, STR_WEST);
    app_text_add(&text, "  ");
    app_text_add_number(&text, state.altitude);
    app_text_add(&text, " "); app_text_add(&text, app_string(STR_KM));
    app_text_add(&text, "  ");
    app_text_add_number(&text, state.velocity / 1000);
    app_text_add(&text, " "); app_text_add_padded(&text, state.velocity % 1000, 3);
    app_text_add(&text, " "); app_text_add(&text, app_string(STR_SPEED));
    app_text_add(&text, "  "); app_text_add(&text, app_string(state.daylight ? STR_DAY : STR_NIGHT));
    /* Russian direction abbreviations are wider; draw a compact one-line
     * value with measured spacing, using the full width of the panel. */
    app_draw_centered(200, 220, HOST_FONT_SMALL, line);
}
