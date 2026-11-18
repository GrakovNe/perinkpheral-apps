// SPDX-License-Identifier: MIT
/*
 * Equirectangular map with a dotted solar terminator and sampled ground track.
 * Keep each live fix in memory and checkpoint the track at five-minute intervals.
 */
#include "app_draw.h"
#include "app_fixed.h"
#include "app_state.h"
#include "app_text.h"
#include "app_values.h"
#include "host_api.h"
#include "orbit.h"
#include "strings.h"

enum {
    VERSION = 1, SECONDS_PER_MINUTE = 60,
    TRACK_SAMPLE_MINUTES = 5, AXIS_DASH = 4, AXIS_PERIOD = 9, TERMINATOR_SPACING = 5,
    TRACK_DOT_SPACING = 6, MARKER_RADIUS = 9, TELEMETRY_TOP = 212, VALUE_TOP = 228
};
typedef struct {
    int32_t version, has_data, status, latitude, longitude, altitude, velocity;
    int32_t solar_latitude, solar_longitude;
    int32_t second, minute; /* Older snapshots stored 0 or 1 in the seconds slot. */
    orbit_track_t track;
} state_t;
static state_t state;
static int32_t saved_minute, saved_status;
_Static_assert(sizeof(state_t) <= HOST_SCENE_STATE_MAX, "ISS snapshot fits scene storage");
_Static_assert(sizeof(state_t) <= HOST_KV_VALUE_MAX, "ISS snapshot fits one store value");
void app_on_load(void)
{
    if (!app_state_load("state", &state, sizeof state, VERSION) ||
        state.track.count < 0 || state.track.count > TRACK_POINTS ||
        state.track.next < 0 || state.track.next >= TRACK_POINTS) {
        state = (state_t){ .version = VERSION };
    }
    saved_minute = state.minute;
    saved_status = state.status;
}

void app_on_focus(void)
{
    state_t live;
    if (scene_state_get(&live, sizeof live) == sizeof live && live.version == VERSION &&
        (live.minute > state.minute || (live.minute == state.minute && live.second > state.second))) {
        live.status = state.status;
        state = live;
    }
}

void app_on_blur(void)
{
    if (state.has_data) { scene_state_set(&state, sizeof state); }
}

void app_on_button(int32_t event)
{
    if (event == BTN_OK_PRESSED) { poll_run(0); }
}

void app_on_picked(int32_t request, int32_t http_status)
{
    if (request) { return; }
    state_t fresh = state;
    int64_t timestamp;
    bool success = http_status >= 200 && http_status < 300;
    bool valid = success &&
        app_value_number(0, 2, &fresh.latitude) && fresh.latitude >= -9000 && fresh.latitude <= 9000 &&
        app_value_number(1, 2, &fresh.longitude) && fresh.longitude >= -18000 && fresh.longitude <= 18000 &&
        app_value_number(2, 0, &fresh.altitude) && fresh.altitude >= 0 && fresh.altitude < 2000 &&
        app_value_number(3, 0, &fresh.velocity) && fresh.velocity >= 0 && fresh.velocity < 100000 &&
        app_value_number(4, 2, &fresh.solar_latitude) && fresh.solar_latitude >= -9000 && fresh.solar_latitude <= 9000 &&
        app_value_number(5, 2, &fresh.solar_longitude) && fresh.solar_longitude >= -36000 && fresh.solar_longitude <= 36000 &&
        app_value_fixed(6, 0, &timestamp) && timestamp >= HOST_TIME_MIN && timestamp <= HOST_TIME_MAX;
    if (valid && timestamp >= (int64_t)state.minute * SECONDS_PER_MINUTE + state.second) {
        if (fresh.solar_longitude > 18000) { fresh.solar_longitude -= 36000; }
        if (fresh.solar_longitude < -18000) { fresh.solar_longitude += 36000; }
        fresh.has_data = 1;
        fresh.status = 200;
        fresh.minute = (int32_t)(timestamp / SECONDS_PER_MINUTE);
        fresh.second = (int32_t)(timestamp % SECONDS_PER_MINUTE);
        orbit_track_sample(&fresh.track, fresh.minute, fresh.latitude, fresh.longitude, TRACK_SAMPLE_MINUTES);
        state = fresh;
    } else {
        state.status = success ? HOST_FAILED_NO_ANSWER : http_status;
    }
    if ((state.status != saved_status || state.minute - saved_minute >= TRACK_SAMPLE_MINUTES) &&
        app_state_save("state", &state, sizeof state)) {
        saved_minute = state.minute;
        saved_status = state.status;
    }
    screen_redraw(HOST_REDRAW_NORMAL);
}

static void dot(int32_t x, int32_t y)
{
    gfx_draw_rect(x, y, 1, 1, HOST_RECT_FILLED);
}

static void draw_terminator(void)
{
    bool previous_row[(MAP_WIDTH - 1) / TERMINATOR_SPACING + 1];
    for (int32_t y = 0; y < MAP_HEIGHT; y += TERMINATOR_SPACING) {
        int32_t latitude = 9000 - y * 18000 / MAP_HEIGHT;
        bool previous = false;
        for (int32_t x = 0; x < MAP_WIDTH; x += TERMINATOR_SPACING) {
            int32_t longitude = x * 36000 / MAP_WIDTH - 18000;
            int32_t column = x / TERMINATOR_SPACING;
            bool night = orbit_night(latitude, longitude, state.solar_latitude, state.solar_longitude);
            int32_t point_x = x, point_y = y;
            if (x > 0 && night != previous) {
                point_x -= TERMINATOR_SPACING / 2;
            } else if (y > 0 && night != previous_row[column]) {
                point_y -= TERMINATOR_SPACING / 2;
            }
            if (point_x != x || point_y != y) {
                gfx_set_color(HOST_PAPER);
                gfx_draw_rect(point_x - 1, point_y - 1, 3, 3, HOST_RECT_FILLED);
                gfx_set_color(HOST_INK);
                dot(point_x, point_y);
            }
            previous_row[column] = previous = night;
        }
    }
}

static void track_dot(int32_t x, int32_t y)
{
    gfx_set_color(HOST_PAPER);
    gfx_draw_rect(x - 1, y - 1, 4, 4, HOST_RECT_FILLED);
    gfx_set_color(HOST_INK);
    gfx_draw_rect(x, y, 2, 2, HOST_RECT_FILLED);
}

static void draw_track(void)
{
    int32_t prior_x = -1, prior_y = 0, prior_minute = 0;
    int64_t now = time_now();
    int32_t minute = now > 0 ? (int32_t)(now / SECONDS_PER_MINUTE) : state.minute;
    for (int32_t index = 0; index < state.track.count; index++) {
        const orbit_point_t *point = &state.track.points[(state.track.next + TRACK_POINTS - state.track.count + index) % TRACK_POINTS];
        if (point->minute <= minute - TRACK_POINTS) { prior_x = -1; continue; }
        int32_t x = orbit_x(point->longitude), y = orbit_y(point->latitude);
        int32_t dx = x - prior_x, dy = y - prior_y;
        int32_t distance_x = dx < 0 ? -dx : dx, distance_y = dy < 0 ? -dy : dy;
        int32_t steps = distance_x > distance_y ? distance_x : distance_y;
        /* Do not join across the date line or across an unobserved gap. */
        if (prior_x >= 0 && distance_x < MAP_WIDTH / 2 && point->minute - prior_minute <= TRACK_SAMPLE_MINUTES * 2 && steps) {
            for (int32_t step = TRACK_DOT_SPACING; step < steps; step += TRACK_DOT_SPACING) {
                track_dot(prior_x + dx * step / steps, prior_y + dy * step / steps);
            }
        }
        if (x != orbit_x(state.longitude) || y != orbit_y(state.latitude)) {
            track_dot(x, y);
        }
        prior_x = x; prior_y = y; prior_minute = point->minute;
    }
}
static void draw_station(void)
{
    int32_t x = orbit_x(state.longitude), y = orbit_y(state.latitude);
    for (int32_t dash = 0; dash < MAP_WIDTH; dash += AXIS_PERIOD) {
        if (dash + AXIS_DASH < x - MARKER_RADIUS || dash > x + MARKER_RADIUS) {
            gfx_draw_rect(dash, y, AXIS_DASH, 1, HOST_RECT_FILLED);
        }
    }
    for (int32_t dash = 0; dash < MAP_HEIGHT - AXIS_DASH; dash += AXIS_PERIOD) {
        if (dash + AXIS_DASH < y - MARKER_RADIUS || dash > y + MARKER_RADIUS) {
            gfx_draw_rect(x, dash, 1, AXIS_DASH, HOST_RECT_FILLED);
        }
    }

    static const uint8_t bitmap[18] = {
        0x1c, 0, 0x22, 0, 0x41, 0, 0x80, 0x80, 0x88, 0x80,
        0x80, 0x80, 0x41, 0, 0x22, 0, 0x1c, 0
    };
    gfx_draw_bitmap(x - 4, y - 4, 9, 9, bitmap);
    gfx_draw_rect(x - MARKER_RADIUS, y - 1, MARKER_RADIUS - 4, 3, HOST_RECT_FILLED);
    gfx_draw_rect(x + 5, y - 1, MARKER_RADIUS - 4, 3, HOST_RECT_FILLED);
    gfx_draw_rect(x - 1, y - MARKER_RADIUS, 3, MARKER_RADIUS - 4, HOST_RECT_FILLED);
    gfx_draw_rect(x - 1, y + 5, 3, MARKER_RADIUS - 4, HOST_RECT_FILLED);
}

static void coordinate(char line[24], int32_t value, int32_t positive, int32_t negative)
{
    app_text_t text;
    app_text_start(&text, line, 24);
    app_text_add_fixed(&text, value < 0 ? -value : value, 2, 0);
    app_text_add(&text, APP_TEXT_DEGREE " ");
    app_text_add(&text, app_string(value < 0 ? negative : positive));
}

typedef struct {
    char value[16];
    const char *unit, *label;
    int32_t width;
} measure_t;

static void measure_init(measure_t *measure, int32_t number, int32_t unit, int32_t label)
{
    app_text_t text;
    app_text_start(&text, measure->value, sizeof measure->value);
    app_text_add_number(&text, number);
    measure->unit = app_string(unit);
    measure->label = app_string(label);
    measure->width = ui_value_width(HOST_FONT_TEXT | HOST_FONT_TABULAR, measure->value,
                                    app_text_length(measure->value), measure->unit,
                                    app_text_length(measure->unit));
}

static void draw_measure(int32_t left, const measure_t *measure)
{
    int32_t digit_width = ui_value_width(HOST_FONT_TEXT | HOST_FONT_TABULAR, measure->value,
                                         app_text_length(measure->value), "", 0);
    int32_t label_inset = gfx_font_ascent(HOST_FONT_SMALL) - gfx_font_cap_height(HOST_FONT_SMALL);
    int32_t line_spacing = VALUE_TOP - TELEMETRY_TOP;
    int32_t block_height = line_spacing + gfx_font_ascent(HOST_FONT_TEXT) - label_inset;
    int32_t label_top = MAP_HEIGHT + (gfx_height() - MAP_HEIGHT - block_height) / 2 - label_inset;
    app_draw_centered(left + digit_width / 2, label_top, HOST_FONT_SMALL, measure->label);
    ui_draw_value(left, label_top + line_spacing, HOST_FONT_TEXT | HOST_FONT_TABULAR, measure->value,
                  app_text_length(measure->value), measure->unit, app_text_length(measure->unit));
}

void app_on_draw(void)
{
    if (!state.has_data) { ui_draw_reason(state.status); return; }
    gfx_draw_bitmap_file(0, 0, MAP_WIDTH, MAP_HEIGHT, "land.bin", 8, 0);
    draw_terminator();
    draw_track();
    draw_station();
    char latitude[24], longitude[24];
    coordinate(latitude, state.latitude, STR_NORTH, STR_SOUTH);
    coordinate(longitude, state.longitude, STR_EAST, STR_WEST);
    int32_t latitude_width = app_text_width(HOST_FONT_SMALL, latitude);
    int32_t longitude_width = app_text_width(HOST_FONT_SMALL, longitude);
    int32_t coordinates_width = latitude_width > longitude_width ? latitude_width : longitude_width;

    measure_t altitude, speed;
    measure_init(&altitude, state.altitude, STR_KM, STR_ALTITUDE);
    measure_init(&speed, state.velocity, STR_KMH, STR_VELOCITY);
    int32_t free_width = MAP_WIDTH - coordinates_width - altitude.width - speed.width;
    int32_t right = free_width / 6 + coordinates_width;
    app_draw_right(right, TELEMETRY_TOP + 3, HOST_FONT_SMALL, latitude);
    app_draw_right(right, VALUE_TOP + 3, HOST_FONT_SMALL, longitude);
    draw_measure(right + free_width / 3, &speed);
    draw_measure(MAP_WIDTH - free_width / 6 - altitude.width, &altitude);
}
