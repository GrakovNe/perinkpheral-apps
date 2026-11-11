/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * The weather now and for three days, from Open-Meteo: no key, no
 * account, the device asks the service itself. The manifest names the
 * request and the twelve numbers wanted from the answer; the host makes
 * the request every fifteen minutes whether this application is on the
 * screen or not, and hands the numbers to app_on_values(). They go into
 * the store, and the picture is drawn from the store: the application is
 * unloaded between any two calls.
 *
 * The settings are in the store too, under the keys the manifest gives
 * them: the place (`lat`, `lon`), the units and how often to ask.
 */
#include <stdbool.h>

#include "app_text.h"
#include "app_values.h"
#include "host_api.h"
#include "icons.h"

/* The order of "pick" in the manifest. */
enum {
    VALUE_TEMPERATURE,
    VALUE_CODE,
    VALUE_WIND,
    VALUE_DAY_CODE,                             /* three of each from here on */
    VALUE_DAY_HIGH = VALUE_DAY_CODE + 3,
    VALUE_DAY_LOW = VALUE_DAY_HIGH + 3,
    VALUE_FEELS_LIKE = VALUE_DAY_LOW + 3,
    VALUE_RAIN_CHANCE,
};

#define WEATHER_POLL  0
#define DAYS          3
#define HTTP_OK       200
#define DAYS_PER_WEEK 7

/* What was learned last, as it lies in the store. */
#define FORECAST_KEY     "forecast"
#define FORECAST_VERSION 2
typedef struct {
    int32_t version;
    int32_t temperature; /* whole degrees */
    int32_t code;        /* WMO weather code */
    int32_t wind;        /* km/h */
    int32_t feels_like;  /* whole degrees */
    int32_t rain_chance; /* of today, percent */
    struct {
        int32_t code, high, low;
    } days[DAYS];
} forecast_t;

static forecast_t forecast;
static bool       has_forecast;

static const char PLACE_KEY[] = "lat";

/* ---- what a WMO code means ---- */

typedef struct {
    int32_t     up_to; /* the last code this line is for */
    icon_t      icon;
    const char *words;
} condition_t;

static const condition_t CONDITIONS[] = {
    { 0, ICON_CLEAR, "Clear" },
    { 1, ICON_CLEAR, "Mostly clear" },
    { 2, ICON_PARTLY_CLOUDY, "Partly cloudy" },
    { 3, ICON_CLOUDY, "Overcast" },
    { 48, ICON_FOG, "Fog" },
    { 57, ICON_RAIN, "Drizzle" },
    { 67, ICON_RAIN, "Rain" },
    { 77, ICON_SNOW, "Snow" },
    { 82, ICON_RAIN, "Showers" },
    { 86, ICON_SNOW, "Snow showers" },
    { 99, ICON_THUNDER, "Thunderstorm" },
};
#define CONDITION_COUNT ((int32_t)(sizeof CONDITIONS / sizeof CONDITIONS[0]))

static const condition_t *condition_of(int32_t code)
{
    for (int32_t i = 0; i < CONDITION_COUNT; i++) {
        if (code <= CONDITIONS[i].up_to) {
            return &CONDITIONS[i];
        }
    }
    return &CONDITIONS[CONDITION_COUNT - 1];
}

/* ---- the store ---- */

static void load_forecast(void)
{
    has_forecast = kv_get(FORECAST_KEY, sizeof FORECAST_KEY - 1, &forecast, sizeof forecast) ==
                       sizeof forecast &&
                   forecast.version == FORECAST_VERSION;
}

/* ---- the settings ---- */

static bool setting_is(const char *key, int32_t key_length, const char *wanted)
{
    char    value[16];
    int32_t length = kv_get(key, key_length, value, sizeof value - 1);
    if (length < 0) {
        return false;
    }
    value[length] = '\0';
    for (int32_t i = 0; i <= length; i++) {
        if (value[i] != wanted[i]) {
            return false;
        }
    }
    return true;
}
#define SETTING_IS(key, wanted) setting_is(key, sizeof key - 1, wanted)

static bool shows_fahrenheit(void)
{
    return SETTING_IS("units", "fahrenheit");
}

/* The service is asked in Celsius and km/h whatever the user reads. */
static int32_t shown_degrees(int32_t celsius)
{
    return shows_fahrenheit() ? (celsius * 9 + (celsius >= 0 ? 2 : -2)) / 5 + 32 : celsius;
}

static int32_t shown_speed(int32_t km_per_hour)
{
    return shows_fahrenheit() ? (km_per_hour * 621 + 500) / 1000 : km_per_hour;
}

/* The manifest asks every fifteen minutes; the user may want it rarer. */
static void apply_pace(void)
{
    int32_t minutes = SETTING_IS("every", "30 min")    ? 30
                      : SETTING_IS("every", "1 hour")  ? 60
                      : SETTING_IS("every", "3 hours") ? 180
                                                       : 15;
    poll_set_interval(WEATHER_POLL, minutes * 60);
}

void app_init(void)
{
    load_forecast();
    apply_pace();
}

void app_on_values(int32_t request, int32_t http_status)
{
    (void)request;
    forecast_t fresh = { .version = FORECAST_VERSION };
    bool is_whole = http_status == HTTP_OK &&
                    app_value_number(VALUE_TEMPERATURE, 0, &fresh.temperature) &&
                    app_value_number(VALUE_CODE, 0, &fresh.code) &&
                    app_value_number(VALUE_WIND, 0, &fresh.wind) &&
                    app_value_number(VALUE_FEELS_LIKE, 0, &fresh.feels_like) &&
                    app_value_number(VALUE_RAIN_CHANCE, 0, &fresh.rain_chance);
    for (int32_t day = 0; day < DAYS && is_whole; day++) {
        is_whole = app_value_number(VALUE_DAY_CODE + day, 0, &fresh.days[day].code) &&
                   app_value_number(VALUE_DAY_HIGH + day, 0, &fresh.days[day].high) &&
                   app_value_number(VALUE_DAY_LOW + day, 0, &fresh.days[day].low);
    }
    if (!is_whole) {
        return; /* what was known before is still the best there is */
    }
    forecast = fresh;
    has_forecast = true;
    kv_set(FORECAST_KEY, sizeof FORECAST_KEY - 1, &forecast, sizeof forecast);
    request_redraw(HOST_REDRAW_NORMAL);
}

/* The place may have changed, and then the forecast in hand is for
 * somewhere else: it is dropped, and the host fetches a new one at once. */
void app_on_settings(void)
{
    has_forecast = false;
    kv_set(FORECAST_KEY, sizeof FORECAST_KEY - 1, &forecast, 0);
    apply_pace();
    request_redraw(HOST_REDRAW_NORMAL);
}

void app_on_button(int32_t event)
{
    switch (event) {
    case BTN_BACK_PRESSED:
        request_exit();
        break;
    case BTN_OK_PRESSED:
        poll_now(WEATHER_POLL); /* "refresh": the answer redraws the screen */
        break;
    }
}

/* ---- the picture ---- */

#define MARGIN            16
#define NOW_TEXT_LEFT     (MARGIN + LARGE_ICON_CELLS * ICON_CELL_PIXELS + 24)
#define TEMPERATURE_TOP   2
#define DEGREE_SIZE       14 /* the degree sign is a small square with a hole */
#define DEGREE_STROKE     4
#define DEGREE_GAP        6
#define SMALL_DEGREE_SIZE 8
#define SMALL_DEGREE_STROKE 2
#define SMALL_DEGREE_GAP  3
#define WORDS_TOP         80
/* The small print: three lines against the right edge, a picture before each. */
#define DETAIL_TOP        14
#define DETAIL_LINE       32
#define DETAIL_ICON_GAP   8
/* The rule stands as far under the large icon as the icon stands under
 * the header's rule: the same margin on both sides. */
#define RULE_TOP          (MARGIN + LARGE_ICON_CELLS * ICON_CELL_PIXELS + MARGIN)
#define DAY_NAME_TOP      132
#define DAY_ICON_TOP      162
#define DAY_RANGE_TOP     214
#define LINE_HEIGHT       30

static void draw_icon(const char *const *rows, int32_t cells, int32_t left, int32_t top)
{
    for (int32_t row = 0; row < cells; row++) {
        const char *line = rows[row];
        /* A run of ink in a row is one rectangle. */
        for (int32_t column = 0; column < cells;) {
            if (line[column] != '#') {
                column++;
                continue;
            }
            int32_t run_start = column;
            while (column < cells && line[column] == '#') {
                column++;
            }
            gfx_rect(left + run_start * ICON_CELL_PIXELS, top + row * ICON_CELL_PIXELS,
                     (column - run_start) * ICON_CELL_PIXELS, ICON_CELL_PIXELS, HOST_RECT_FILLED);
        }
    }
}

static int32_t draw_text(int32_t left, int32_t top, int32_t font, const char *text)
{
    return gfx_text(left, top, font, text, app_text_length(text));
}

static void draw_centered(int32_t center, int32_t top, int32_t font, const char *text)
{
    int32_t length = app_text_length(text);
    gfx_text(center - gfx_text_width(font, text, length) / 2, top, font, text, length);
}

/* A degree sign the font does not have - it is ASCII: a square with a hole. */
static void draw_degree(int32_t left, int32_t top, int32_t size, int32_t stroke)
{
    gfx_rect(left, top, size, size, HOST_RECT_FILLED);
    gfx_color(HOST_PAPER);
    gfx_rect(left + stroke, top + stroke, size - 2 * stroke, size - 2 * stroke, HOST_RECT_FILLED);
    gfx_color(HOST_INK);
}

/* Where a line of the small print with this value would begin: at its picture. */
static int32_t detail_left(const char *value)
{
    return gfx_width() - MARGIN - gfx_text_width(HOST_FONT_TEXT, value, app_text_length(value)) -
           DETAIL_ICON_GAP - DETAIL_ICON_CELLS * ICON_CELL_PIXELS;
}

/* One line of the small print against the right edge: a picture, the value,
 * and a degree sign after it when the value is a temperature. */
static void draw_detail(int32_t line, detail_t detail, const char *value, bool is_degrees)
{
    int32_t top = DETAIL_TOP + line * DETAIL_LINE;
    int32_t length = app_text_length(value);
    int32_t degree_width = is_degrees ? SMALL_DEGREE_GAP + SMALL_DEGREE_SIZE : 0;
    int32_t text_left = gfx_width() - MARGIN - degree_width -
                        gfx_text_width(HOST_FONT_TEXT, value, length);
    gfx_text(text_left, top, HOST_FONT_TEXT, value, length);
    if (is_degrees) {
        int32_t cap_top = top + gfx_font_ascent(HOST_FONT_TEXT) -
                          gfx_font_cap_height(HOST_FONT_TEXT);
        draw_degree(gfx_width() - MARGIN - SMALL_DEGREE_SIZE, cap_top, SMALL_DEGREE_SIZE,
                    SMALL_DEGREE_STROKE);
    }
    /* The picture stands on the text's baseline. */
    int32_t icon_size = DETAIL_ICON_CELLS * ICON_CELL_PIXELS;
    draw_icon(DETAIL_ICONS[detail], DETAIL_ICON_CELLS, text_left - DETAIL_ICON_GAP - icon_size,
              top + gfx_font_ascent(HOST_FONT_TEXT) - icon_size);
}

static void draw_now(void)
{
    const condition_t *now = condition_of(forecast.code);
    draw_icon(LARGE_ICONS[now->icon], LARGE_ICON_CELLS, MARGIN, MARGIN);

    char buffer[24];
    app_text_t text;
    app_text_start(&text, buffer, sizeof buffer);
    app_text_add_number(&text, shown_degrees(forecast.temperature));
    int32_t digits_width = draw_text(NOW_TEXT_LEFT, TEMPERATURE_TOP, HOST_FONT_DISPLAY, buffer);
    draw_degree(NOW_TEXT_LEFT + digits_width + DEGREE_GAP,
                TEMPERATURE_TOP + gfx_font_ascent(HOST_FONT_DISPLAY) -
                    gfx_font_cap_height(HOST_FONT_DISPLAY),
                DEGREE_SIZE, DEGREE_STROKE);
    int32_t words_right = NOW_TEXT_LEFT +
                          draw_text(NOW_TEXT_LEFT, WORDS_TOP, HOST_FONT_TEXT, now->words);

    app_text_start(&text, buffer, sizeof buffer);
    app_text_add_number(&text, shown_degrees(forecast.feels_like));
    draw_detail(0, DETAIL_FEELS_LIKE, buffer, true);

    app_text_start(&text, buffer, sizeof buffer);
    app_text_add_number(&text, forecast.rain_chance);
    app_text_add(&text, "%");
    draw_detail(1, DETAIL_RAIN, buffer, false);

    /* The longest of the three goes last: it is below the large digits. */
    app_text_start(&text, buffer, sizeof buffer);
    app_text_add_number(&text, shown_speed(forecast.wind));
    int32_t number_length = app_text_length(buffer);
    app_text_add(&text, shows_fahrenheit() ? " mph" : " km/h");
    /* It shares its line with the words on the left: when those are long
     * - "Partly cloudy" - the unit gives way and the number stands alone. */
    if (detail_left(buffer) < words_right + MARGIN) {
        buffer[number_length] = '\0';
    }
    draw_detail(2, DETAIL_WIND, buffer, false);
}

static void draw_days(void)
{
    static const char *const WEEKDAYS[DAYS_PER_WEEK] = { "Sun", "Mon", "Tue", "Wed",
                                                         "Thu", "Fri", "Sat" };
    host_local_time_t today;
    bool knows_the_day = time_local(&today) == HOST_OK;
    int32_t column_width = gfx_width() / DAYS;
    gfx_rect(MARGIN, RULE_TOP, gfx_width() - 2 * MARGIN, 1, HOST_RECT_FILLED);
    for (int32_t day = 0; day < DAYS; day++) {
        int32_t center = day * column_width + column_width / 2;
        const char *name = day == 0         ? "Today"
                           : knows_the_day ? WEEKDAYS[(today.weekday + day) % DAYS_PER_WEEK]
                                           : "";
        draw_centered(center, DAY_NAME_TOP, HOST_FONT_TEXT, name);
        draw_icon(SMALL_ICONS[condition_of(forecast.days[day].code)->icon], SMALL_ICON_CELLS,
                  center - SMALL_ICON_CELLS * ICON_CELL_PIXELS / 2, DAY_ICON_TOP);

        char buffer[16];
        app_text_t text;
        app_text_start(&text, buffer, sizeof buffer);
        app_text_add_number(&text, shown_degrees(forecast.days[day].high));
        app_text_add(&text, " / ");
        app_text_add_number(&text, shown_degrees(forecast.days[day].low));
        draw_centered(center, DAY_RANGE_TOP, HOST_FONT_TEXT, buffer);
    }
}

void app_on_draw(void)
{
    if (has_forecast) {
        draw_now();
        draw_days();
        return;
    }
    char place[2];
    bool has_place = kv_get(PLACE_KEY, sizeof PLACE_KEY - 1, place, sizeof place) !=
                     HOST_ERR_NOT_FOUND;
    draw_text(MARGIN, MARGIN, HOST_FONT_TEXT, "No data.");
    draw_text(MARGIN, MARGIN + 2 * LINE_HEIGHT, HOST_FONT_TEXT,
              has_place ? "Waiting for network." : "Set location in settings.");
}
