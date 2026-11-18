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
    VERSION = 1, SECONDS_PER_MINUTE = 60, MINUTES_PER_HOUR = 60,
    TRACK_SAMPLE_MINUTES = 5, AXIS_DASH = 4, AXIS_PERIOD = 9, TERMINATOR_SPACING = 5,
    TRACK_DOT_SPACING = 6, MARKER_RADIUS = 9, TELEMETRY_TOP = 204, VALUE_TOP = 218, FRESHNESS_TOP = 242
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
        track_dot(x, y);
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

    gfx_set_color(HOST_PAPER);
    gfx_draw_rect(x - 6, y - 6, 13, 13, HOST_RECT_FILLED);
    gfx_set_color(HOST_INK);
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

static void coordinate(app_text_t *text, int32_t value, int32_t positive, int32_t negative)
{
    app_text_add_fixed(text, value < 0 ? -value : value, 2, 0);
    app_text_add(text, APP_TEXT_DEGREE " ");
    app_text_add(text, app_string(value < 0 ? negative : positive));
}
static void draw_coordinate(int32_t center, int32_t top, int32_t value, int32_t positive, int32_t negative)
{
    char line[24];
    app_text_t text;
    app_text_start(&text, line, sizeof line);
    coordinate(&text, value, positive, negative);
    app_draw_centered(center, top, HOST_FONT_SMALL, line);
}

static void draw_measure(int32_t center, int32_t label, int32_t number, int32_t unit, bool group_digits)
{
    char value[16];
    app_text_t text;
    app_text_start(&text, value, sizeof value);
    if (group_digits && number >= 1000) {
        app_text_add_number(&text, number / 1000);
        app_text_add(&text, " ");
        app_text_add_padded(&text, number % 1000, 3);
    } else {
        app_text_add_number(&text, number);
    }

    app_draw_centered(center, TELEMETRY_TOP, HOST_FONT_SMALL, app_string(label));
    const char *suffix = app_string(unit);
    int32_t length = app_text_length(value), unit_length = app_text_length(suffix);
    int32_t font = HOST_FONT_TEXT | HOST_FONT_TABULAR;
    int32_t width = ui_value_width(font, value, length, suffix, unit_length);
    ui_draw_value(center - width / 2, VALUE_TOP, font, value, length, suffix, unit_length);
}

static void draw_freshness(void)
{
    int64_t now = time_now();
    int64_t age = now - ((int64_t)state.minute * SECONDS_PER_MINUTE + state.second);
    bool failed = state.status < 200 || state.status >= 300;
    char line[64];
    app_text_t text;
    app_text_start(&text, line, sizeof line);
    app_text_add(&text, app_string(failed ? STR_UPDATE_FAILED : STR_UPDATED));
    if (now > 0) {
        int32_t minutes = age > 0 ? (int32_t)(age / SECONDS_PER_MINUTE) : 0;
        app_text_add(&text, failed ? " · " : " ");
        if (minutes >= MINUTES_PER_HOUR) {
            app_text_add_number(&text, minutes / MINUTES_PER_HOUR);
            app_text_add(&text, " ");
            app_text_add(&text, app_string(STR_HOUR));
        } else {
            if (minutes == 0) { app_text_add(&text, "<"); }
            app_text_add_number(&text, minutes > 0 ? minutes : 1);
            app_text_add(&text, " ");
            app_text_add(&text, app_string(STR_MINUTE));
        }
    } else if (!failed) {
        return;
    }
    app_draw_centered(MAP_WIDTH / 2, FRESHNESS_TOP, HOST_FONT_SMALL, line);
}

void app_on_draw(void)
{
    if (!state.has_data) { ui_draw_reason(state.status); return; }
    gfx_draw_bitmap_file(0, 0, MAP_WIDTH, MAP_HEIGHT, "land.bin", 8, 0);
    draw_terminator();
    draw_track();
    draw_station();
    draw_coordinate(53, TELEMETRY_TOP + 5, state.latitude, STR_NORTH, STR_SOUTH);
    draw_coordinate(53, VALUE_TOP + 6, state.longitude, STR_EAST, STR_WEST);
    draw_measure(172, STR_ALTITUDE, state.altitude, STR_KM, false);
    draw_measure(315, STR_VELOCITY, state.velocity, STR_KMH, true);
    draw_freshness();
}
