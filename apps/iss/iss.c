// SPDX-License-Identifier: MIT
/*
 * Equirectangular map with a day/night theme and sampled ground track.
 * Keep each live fix in memory and checkpoint the track at five-minute intervals.
 */
#include "app_draw.h"
#include "app_state.h"
#include "app_text.h"
#include "app_values.h"
#include "host_api.h"
#include "orbit.h"
#include "strings.h"

enum {
    VERSION = 2, SECONDS_PER_MINUTE = 60,
    ALTITUDE_DECIMALS = 3, MAX_ALTITUDE_METERS = 2000000,
    TRACK_SAMPLE_MINUTES = 5, AXIS_DASH = 4, AXIS_PERIOD = 9, TELEMETRY_GAP = 64,
    TRACK_DASH_LENGTH = 4, TRACK_DASH_PERIOD = 8,
    MARKER_RADIUS = 9, TELEMETRY_TOP = 212, VALUE_TOP = 228,
    BADGE_EDGE = 8, BADGE_TOP = 12, BADGE_PADDING_X = 5, BADGE_PADDING_Y = 3, BADGE_GAP = 3,
    BADGE_COUNT = 2, DIRECTION_COUNT = 4, MAX_LONGITUDE_HUNDREDTHS = 18000,
    HUNDREDTHS_PER_TENTH = 10, TENTHS_PER_DEGREE = 10
};
typedef struct {
    int32_t version, has_data, status, latitude, longitude, altitude, velocity;
    int32_t solar_latitude, solar_longitude;
    int32_t second, minute; /* Older snapshots stored 0 or 1 in the seconds slot. */
    orbit_track_t track;
} state_t;
static state_t state;
static bool map_night;
static int32_t saved_minute, saved_status;
_Static_assert(sizeof(state_t) <= HOST_SCENE_STATE_MAX, "ISS snapshot fits scene storage");
_Static_assert(sizeof(state_t) <= HOST_KV_VALUE_MAX, "ISS snapshot fits one store value");
static bool restore_state(state_t *saved)
{
    if (saved->track.count < 0 || saved->track.count > TRACK_POINTS ||
        saved->track.next < 0 || saved->track.next >= TRACK_POINTS) { return false; }
    if (saved->version == 1) {
        if (saved->altitude < 0 || saved->altitude >= MAX_ALTITUDE_METERS / 1000) { return false; }
        saved->altitude *= 1000;
        saved->version = VERSION;
    }
    return saved->version == VERSION && saved->altitude >= 0 && saved->altitude < MAX_ALTITUDE_METERS;
}

void app_on_load(void)
{
    if ((!app_state_load("state", &state, sizeof state, VERSION) &&
         !app_state_load("state", &state, sizeof state, 1)) || !restore_state(&state)) {
        state = (state_t){ .version = VERSION };
    }
    saved_minute = state.minute;
    saved_status = state.status;
}

void app_on_focus(void)
{
    state_t live;
    if (scene_state_get(&live, sizeof live) == sizeof live && restore_state(&live) &&
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
        app_value_number(2, ALTITUDE_DECIMALS, &fresh.altitude) &&
        fresh.altitude >= 0 && fresh.altitude < MAX_ALTITUDE_METERS &&
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

static void draw_world(void)
{
    static uint8_t pixels[MAP_WIDTH * MAP_HEIGHT / 8];
    if (file_read("land.bin", 8, 0, pixels, sizeof pixels) != sizeof pixels) { return; }
    if (map_night) {
        for (int32_t byte = 0; byte < (int32_t)sizeof pixels; byte++) { pixels[byte] ^= 0xff; }
    }
    gfx_set_color(HOST_INK);
    gfx_draw_bitmap(0, 0, MAP_WIDTH, MAP_HEIGHT, pixels);
}

static void draw_track(void)
{
    int32_t prior_x = -1, prior_y = 0, prior_minute = 0, dash_phase = 0;
    int64_t now = time_now();
    int32_t minute = now > 0 ? (int32_t)(now / SECONDS_PER_MINUTE) : state.minute;
    gfx_set_color(map_night ? HOST_PAPER : HOST_INK);
    for (int32_t index = 0; index < state.track.count; index++) {
        const orbit_point_t *point = &state.track.points[(state.track.next + TRACK_POINTS - state.track.count + index) % TRACK_POINTS];
        if (point->minute <= minute - TRACK_POINTS) { prior_x = -1; continue; }
        int32_t x = orbit_x(point->longitude), y = orbit_y(point->latitude);
        int32_t dx = x - prior_x, dy = y - prior_y;
        int32_t distance_x = dx < 0 ? -dx : dx, distance_y = dy < 0 ? -dy : dy;
        int32_t steps = distance_x > distance_y ? distance_x : distance_y;
        /* Do not join across the date line or across an unobserved gap. */
        if (prior_x >= 0 && distance_x < MAP_WIDTH / 2 && point->minute - prior_minute <= TRACK_SAMPLE_MINUTES * 2 && steps) {
            for (int32_t step = 0; step < steps;) {
                bool ink = dash_phase < TRACK_DASH_LENGTH;
                int32_t run = (ink ? TRACK_DASH_LENGTH : TRACK_DASH_PERIOD) - dash_phase;
                if (run > steps - step) { run = steps - step; }
                if (ink) {
                    int32_t end = step + run - 1;
                    gfx_draw_line(prior_x + dx * step / steps, prior_y + dy * step / steps,
                                  prior_x + dx * end / steps, prior_y + dy * end / steps);
                }
                step += run;
                dash_phase = (dash_phase + run) % TRACK_DASH_PERIOD;
            }
        } else if (steps) { dash_phase = 0; }
        prior_x = x; prior_y = y; prior_minute = point->minute;
    }
}
static void draw_station(void)
{
    int32_t x = orbit_x(state.longitude), y = orbit_y(state.latitude);
    for (int32_t dash = 0; dash < MAP_WIDTH; dash += AXIS_PERIOD) {
        if (dash + AXIS_DASH < x - MARKER_RADIUS || dash > x + MARKER_RADIUS) {
            gfx_set_color(map_night ? HOST_PAPER : HOST_INK);
            gfx_draw_rect(dash, y, AXIS_DASH, 1, HOST_RECT_FILLED);
        }
    }
    for (int32_t dash = 0; dash < MAP_HEIGHT - AXIS_DASH; dash += AXIS_PERIOD) {
        if (dash + AXIS_DASH < y - MARKER_RADIUS || dash > y + MARKER_RADIUS) {
            gfx_set_color(map_night ? HOST_PAPER : HOST_INK);
            gfx_draw_rect(x, dash, 1, AXIS_DASH, HOST_RECT_FILLED);
        }
    }

    static const uint8_t bitmap[18] = {
        0x1c, 0, 0x22, 0, 0x41, 0, 0x80, 0x80, 0x88, 0x80,
        0x80, 0x80, 0x41, 0, 0x22, 0, 0x1c, 0
    };
    gfx_set_color(map_night ? HOST_PAPER : HOST_INK);
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
    int32_t absolute = value < 0 ? -value : value;
    int32_t tenths = (absolute + HUNDREDTHS_PER_TENTH / 2) / HUNDREDTHS_PER_TENTH;
    app_text_add_padded(&text, tenths / TENTHS_PER_DEGREE, 2);
    app_text_add(&text, ".");
    app_text_add_number(&text, tenths % TENTHS_PER_DEGREE);
    app_text_add(&text, APP_TEXT_DEGREE " ");
    app_text_add(&text, app_string(value < 0 ? negative : positive));
}

static void draw_coordinate_badges(void)
{
    const int32_t font = HOST_FONT_SMALL | HOST_FONT_TABULAR;
    char latitude[24], longitude[24], maximum[24];
    coordinate(latitude, state.latitude, STR_NORTH, STR_SOUTH);
    coordinate(longitude, state.longitude, STR_EAST, STR_WEST);
    int32_t width = 0;
    const int32_t directions[DIRECTION_COUNT] = { STR_NORTH, STR_SOUTH, STR_EAST, STR_WEST };
    for (int32_t index = 0; index < DIRECTION_COUNT; index++) {
        coordinate(maximum, MAX_LONGITUDE_HUNDREDTHS, directions[index], directions[index]);
        int32_t measured = app_text_width(font, maximum);
        if (measured > width) { width = measured; }
    }
    width += 2 * BADGE_PADDING_X;
    int32_t left = state.longitude < 0 ? BADGE_EDGE : MAP_WIDTH - BADGE_EDGE - width;
    int32_t capitals = gfx_font_cap_height(font);
    int32_t height = capitals + 2 * BADGE_PADDING_Y;
    const char *lines[BADGE_COUNT] = { latitude, longitude };
    for (int32_t row = 0; row < BADGE_COUNT; row++) {
        int32_t top = BADGE_TOP + row * (height + BADGE_GAP);
        gfx_set_color(map_night ? HOST_PAPER : HOST_INK);
        gfx_draw_rect(left, top, width, height, HOST_RECT_FILLED);
        gfx_set_color(map_night ? HOST_INK : HOST_PAPER);
        app_draw_centered(left + width / 2,
                          top + BADGE_PADDING_Y + capitals - gfx_font_ascent(font),
                          font, lines[row]);
    }
    gfx_set_color(HOST_INK);
}

typedef struct {
    char value[16];
    const char *unit, *label;
} measure_t;

static void measure_init(measure_t *measure, int32_t number, int32_t unit, int32_t label)
{
    app_text_t text;
    app_text_start(&text, measure->value, sizeof measure->value);
    app_text_add_number(&text, number);
    measure->unit = app_string(unit);
    measure->label = app_string(label);
}

static int32_t measure_width(const measure_t *measure)
{
    int32_t value_width = ui_value_width(HOST_FONT_TEXT | HOST_FONT_TABULAR, measure->value,
                                       app_text_length(measure->value), measure->unit,
                                       app_text_length(measure->unit));
    int32_t label_width = app_text_width(HOST_FONT_SMALL, measure->label);
    return value_width > label_width ? value_width : label_width;
}

static void draw_measure(int32_t center, const measure_t *measure)
{
    int32_t group_width = ui_value_width(HOST_FONT_TEXT | HOST_FONT_TABULAR, measure->value,
                                        app_text_length(measure->value), measure->unit,
                                        app_text_length(measure->unit));
    int32_t left = center - group_width / 2;
    int32_t label_inset = gfx_font_ascent(HOST_FONT_SMALL) - gfx_font_cap_height(HOST_FONT_SMALL);
    int32_t line_spacing = VALUE_TOP - TELEMETRY_TOP;
    int32_t block_height = line_spacing + gfx_font_ascent(HOST_FONT_TEXT) - label_inset;
    int32_t label_top = MAP_HEIGHT + (gfx_height() - MAP_HEIGHT - block_height) / 2 - label_inset;
    int32_t number_width = app_text_width(HOST_FONT_TEXT | HOST_FONT_TABULAR, measure->value);
    app_draw_centered(left + number_width / 2, label_top, HOST_FONT_SMALL, measure->label);
    ui_draw_value(left, label_top + line_spacing, HOST_FONT_TEXT | HOST_FONT_TABULAR, measure->value,
                  app_text_length(measure->value), measure->unit, app_text_length(measure->unit));
}

void app_on_draw(void)
{
    if (!state.has_data) { ui_draw_reason(state.status); return; }
    map_night = orbit_night(state.latitude, state.longitude, state.solar_latitude, state.solar_longitude);
    draw_world();
    draw_track();
    draw_station();
    draw_coordinate_badges();
    measure_t altitude, speed;
    measure_init(&altitude, state.altitude, STR_METERS, STR_ALTITUDE);
    measure_init(&speed, state.velocity, STR_KMH, STR_VELOCITY);
    int32_t altitude_width = measure_width(&altitude);
    int32_t speed_width = measure_width(&speed);
    int32_t group_left = (gfx_width() - altitude_width - TELEMETRY_GAP - speed_width) / 2;
    draw_measure(group_left + altitude_width / 2, &altitude);
    draw_measure(group_left + altitude_width + TELEMETRY_GAP + speed_width / 2, &speed);
}
