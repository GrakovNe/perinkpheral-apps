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
 * The place is two settings, `lat` and `lon`, in the store too.
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
};

#define WEATHER_POLL  0
#define DAYS          3
#define HTTP_OK       200
#define DAYS_PER_WEEK 7

/* What was learned last, as it lies in the store. */
#define FORECAST_KEY     "forecast"
#define FORECAST_VERSION 1
typedef struct {
    int32_t version;
    int32_t temperature; /* whole degrees */
    int32_t code;        /* WMO weather code */
    int32_t wind;        /* km/h */
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

void app_init(void)
{
    load_forecast();
}

void app_on_values(int32_t request, int32_t http_status)
{
    (void)request;
    forecast_t fresh = { .version = FORECAST_VERSION };
    bool is_whole = http_status == HTTP_OK &&
                    app_value_number(VALUE_TEMPERATURE, 0, &fresh.temperature) &&
                    app_value_number(VALUE_CODE, 0, &fresh.code) &&
                    app_value_number(VALUE_WIND, 0, &fresh.wind);
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

/* The place changed: the forecast in hand is for somewhere else. The
 * host fetches a new one at once. */
void app_on_settings(void)
{
    has_forecast = false;
    kv_set(FORECAST_KEY, sizeof FORECAST_KEY - 1, &forecast, 0);
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
#define LARGE_ICON_CELL   8
#define SMALL_ICON_CELL   4
#define NOW_TEXT_LEFT     (MARGIN + ICON_CELLS * LARGE_ICON_CELL + 24)
#define TEMPERATURE_TOP   0
#define DEGREE_SIZE       14 /* the degree sign is a small square with a hole */
#define DEGREE_STROKE     4
#define DEGREE_GAP        6
#define WORDS_TOP         72
#define WIND_TOP          98
#define RULE_TOP          126
#define DAY_NAME_TOP      134
#define DAY_ICON_TOP      162
#define DAY_RANGE_TOP     212
#define LINE_HEIGHT       30

static void draw_icon(icon_t icon, int32_t left, int32_t top, int32_t cell)
{
    for (int32_t row = 0; row < ICON_CELLS; row++) {
        const char *line = ICONS[icon][row];
        /* A run of ink in a row is one rectangle. */
        for (int32_t column = 0; column < ICON_CELLS;) {
            if (line[column] != '#') {
                column++;
                continue;
            }
            int32_t run_start = column;
            while (column < ICON_CELLS && line[column] == '#') {
                column++;
            }
            gfx_rect(left + run_start * cell, top + row * cell, (column - run_start) * cell, cell,
                     HOST_RECT_FILLED);
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

static void draw_now(void)
{
    const condition_t *now = condition_of(forecast.code);
    draw_icon(now->icon, MARGIN, MARGIN, LARGE_ICON_CELL);

    char buffer[24];
    app_text_t text;
    app_text_start(&text, buffer, sizeof buffer);
    app_text_add_number(&text, forecast.temperature);
    int32_t digits_width = draw_text(NOW_TEXT_LEFT, TEMPERATURE_TOP, HOST_FONT_DISPLAY, buffer);
    /* The font has no degree sign: it is ASCII. */
    int32_t degree_left = NOW_TEXT_LEFT + digits_width + DEGREE_GAP;
    int32_t degree_top = TEMPERATURE_TOP + gfx_font_ascent(HOST_FONT_DISPLAY) -
                         gfx_font_cap_height(HOST_FONT_DISPLAY);
    gfx_rect(degree_left, degree_top, DEGREE_SIZE, DEGREE_SIZE, HOST_RECT_FILLED);
    gfx_color(HOST_PAPER);
    gfx_rect(degree_left + DEGREE_STROKE, degree_top + DEGREE_STROKE,
             DEGREE_SIZE - 2 * DEGREE_STROKE, DEGREE_SIZE - 2 * DEGREE_STROKE, HOST_RECT_FILLED);
    gfx_color(HOST_INK);

    draw_text(NOW_TEXT_LEFT, WORDS_TOP, HOST_FONT_TEXT, now->words);
    app_text_start(&text, buffer, sizeof buffer);
    app_text_add(&text, "Wind ");
    app_text_add_number(&text, forecast.wind);
    app_text_add(&text, " km/h");
    draw_text(NOW_TEXT_LEFT, WIND_TOP, HOST_FONT_TEXT, buffer);
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
        draw_icon(condition_of(forecast.days[day].code)->icon,
                  center - ICON_CELLS * SMALL_ICON_CELL / 2, DAY_ICON_TOP, SMALL_ICON_CELL);

        char buffer[16];
        app_text_t text;
        app_text_start(&text, buffer, sizeof buffer);
        app_text_add_number(&text, forecast.days[day].high);
        app_text_add(&text, " / ");
        app_text_add_number(&text, forecast.days[day].low);
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
    draw_text(MARGIN, MARGIN, HOST_FONT_TEXT, "No forecast yet.");
    draw_text(MARGIN, MARGIN + 2 * LINE_HEIGHT, HOST_FONT_TEXT,
              has_place ? "Waiting for an answer" : "Set the place on the");
    draw_text(MARGIN, MARGIN + 3 * LINE_HEIGHT, HOST_FONT_TEXT,
              has_place ? "from the network." : "device's web page.");
}
