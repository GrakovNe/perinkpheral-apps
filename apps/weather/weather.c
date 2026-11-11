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
    VALUE_RAIN_CHANCE = VALUE_DAY_LOW + 3,
};

#define WEATHER_POLL  0
#define DAYS          3
#define HTTP_OK       200
#define DAYS_PER_WEEK 7

/* What was learned last, as it lies in the store. */
#define FORECAST_KEY     "forecast"
#define FORECAST_VERSION 3
typedef struct {
    int32_t version;
    int32_t temperature; /* whole degrees */
    int32_t code;        /* WMO weather code */
    int32_t wind;        /* km/h */
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
} condition_t;

static const condition_t CONDITIONS[] = {
    { 0, ICON_CLEAR },
    { 1, ICON_CLEAR },
    { 2, ICON_PARTLY_CLOUDY },
    { 3, ICON_CLOUDY },
    { 48, ICON_FOG },
    { 57, ICON_RAIN },
    { 67, ICON_RAIN },
    { 77, ICON_SNOW },
    { 82, ICON_RAIN },
    { 86, ICON_SNOW },
    { 99, ICON_THUNDER },
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
/* What is now stands a little inside the rule under it, on both sides. */
#define NOW_LEFT          (MARGIN + 6)
/* From the ink of the large icon to the digits. */
#define NOW_TEXT_GAP      22
#define HERO_CHARACTERS_MAX 2
#define DEGREE_SIZE       14 /* the degree sign is a small square with a hole */
#define DEGREE_STROKE     4
#define DEGREE_GAP        6
/* The details: two lines in the middle of the large icon's height, their pictures in one column and the values
 * starting at one edge beside it. A unit is in the small font. */
#define DETAIL_TOP        30
#define DETAIL_LINE       36
#define DETAIL_ICON_GAP   10
#define DETAIL_UNIT_GAP   5
/* The rule stands as far under the large icon as the icon stands under
 * the header's rule: the same margin on both sides. */
#define RULE_TOP          (MARGIN + LARGE_ICON_CELLS * ICON_CELL_PIXELS + MARGIN)
/* A day: its name as a caption, its icon, and "high / low" in one line. */
#define DAY_NAME_TOP      133
#define DAY_ICON_TOP      158
#define DAY_RANGE_TOP     214
#define DAY_SLASH_GAP     3
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

/* The columns of an icon that have ink: icons are not all as wide as
 * their cell. */
static void icon_ink_columns(const char *const *rows, int32_t cells, int32_t *first, int32_t *end)
{
    *first = cells;
    *end = 0;
    for (int32_t row = 0; row < cells; row++) {
        for (int32_t column = 0; column < cells; column++) {
            if (rows[row][column] != '#') {
                continue;
            }
            if (column < *first) {
                *first = column;
            }
            if (column + 1 > *end) {
                *end = column + 1;
            }
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

/* How wide a line of the details is: the picture, the value, and a unit
 * that is a word in the small font. */
static int32_t detail_width(const char *value, const char *unit)
{
    int32_t width = DETAIL_ICON_CELLS * ICON_CELL_PIXELS + DETAIL_ICON_GAP +
                    gfx_text_width(HOST_FONT_TEXT, value, app_text_length(value));
    if (unit[0] != '\0') {
        width += DETAIL_UNIT_GAP + gfx_text_width(HOST_FONT_SMALL, unit, app_text_length(unit));
    }
    return width;
}

static void draw_detail(int32_t left, int32_t line, detail_t detail, const char *value,
                        const char *unit)
{
    int32_t top = DETAIL_TOP + line * DETAIL_LINE;
    int32_t baseline = top + gfx_font_ascent(HOST_FONT_TEXT);
    int32_t icon_size = DETAIL_ICON_CELLS * ICON_CELL_PIXELS;
    /* The picture stands on the text's baseline. */
    draw_icon(DETAIL_ICONS[detail], DETAIL_ICON_CELLS, left, baseline - icon_size);

    int32_t value_left = left + icon_size + DETAIL_ICON_GAP;
    int32_t value_right = value_left + draw_text(value_left, top, HOST_FONT_TEXT, value);
    draw_text(value_right + DETAIL_UNIT_GAP, baseline - gfx_font_ascent(HOST_FONT_SMALL),
              HOST_FONT_SMALL, unit);
}

static void draw_now(void)
{
    const condition_t *now = condition_of(forecast.code);
    /* The ink of the icon, not its cell, starts at the left edge and ends
     * where the digits are counted from. */
    int32_t ink_first;
    int32_t ink_end;
    icon_ink_columns(LARGE_ICONS[now->icon], LARGE_ICON_CELLS, &ink_first, &ink_end);
    int32_t icon_left = NOW_LEFT - ink_first * ICON_CELL_PIXELS;
    draw_icon(LARGE_ICONS[now->icon], LARGE_ICON_CELLS, icon_left, MARGIN);

    char buffer[24];
    app_text_t text;
    app_text_start(&text, buffer, sizeof buffer);
    app_text_add_number(&text, shown_degrees(forecast.temperature));
    /* Two characters are as large as there is; "-12" at that size would run
     * into the details, and steps down one. */
    int32_t font = app_text_length(buffer) <= HERO_CHARACTERS_MAX ? HOST_FONT_DISPLAY
                                                                 : HOST_FONT_LARGE;
    /* The digits stand in the middle of the large icon's height. */
    int32_t cap_height = gfx_font_cap_height(font);
    int32_t cap_top = MARGIN + (LARGE_ICON_CELLS * ICON_CELL_PIXELS - cap_height) / 2;
    int32_t text_left = icon_left + ink_end * ICON_CELL_PIXELS + NOW_TEXT_GAP;
    int32_t digits_width =
        draw_text(text_left, cap_top - (gfx_font_ascent(font) - cap_height), font, buffer);
    draw_degree(text_left + digits_width + DEGREE_GAP, cap_top, DEGREE_SIZE, DEGREE_STROKE);

    char rain[8];
    app_text_start(&text, rain, sizeof rain);
    app_text_add_number(&text, forecast.rain_chance);
    app_text_add(&text, "%");
    char wind[8];
    app_text_start(&text, wind, sizeof wind);
    app_text_add_number(&text, shown_speed(forecast.wind));
    const char *wind_unit = shows_fahrenheit() ? "mph" : "km/h";

    /* The two lines are one block, as far from the right end of the rule as
     * the large icon is from its left end; the pictures are one column. */
    int32_t rain_width = detail_width(rain, "");
    int32_t wind_width = detail_width(wind, wind_unit);
    int32_t left = gfx_width() - NOW_LEFT - (rain_width > wind_width ? rain_width : wind_width);
    draw_detail(left, 0, DETAIL_RAIN, rain, "");
    draw_detail(left, 1, DETAIL_WIND, wind, wind_unit);
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
        draw_centered(center, DAY_NAME_TOP, HOST_FONT_SMALL, name);
        int32_t icon_size = SMALL_ICON_CELLS * ICON_CELL_PIXELS;
        draw_icon(SMALL_ICONS[condition_of(forecast.days[day].code)->icon], SMALL_ICON_CELLS,
                  center - icon_size / 2, DAY_ICON_TOP);

        /* "high / low", the slash closer to its numbers than a space would
         * put it: three such ranges of a cold week have to stay apart. */
        char high[8];
        char low[8];
        app_text_t text;
        app_text_start(&text, high, sizeof high);
        app_text_add_number(&text, shown_degrees(forecast.days[day].high));
        app_text_start(&text, low, sizeof low);
        app_text_add_number(&text, shown_degrees(forecast.days[day].low));
        int32_t high_width = gfx_text_width(HOST_FONT_TEXT, high, app_text_length(high));
        int32_t slash_width = gfx_text_width(HOST_FONT_TEXT, "/", 1);
        int32_t low_width = gfx_text_width(HOST_FONT_TEXT, low, app_text_length(low));
        int32_t left = center - (high_width + slash_width + low_width + 2 * DAY_SLASH_GAP) / 2;
        draw_text(left, DAY_RANGE_TOP, HOST_FONT_TEXT, high);
        left += high_width + DAY_SLASH_GAP;
        draw_text(left, DAY_RANGE_TOP, HOST_FONT_TEXT, "/");
        draw_text(left + slash_width + DAY_SLASH_GAP, DAY_RANGE_TOP, HOST_FONT_TEXT, low);
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
