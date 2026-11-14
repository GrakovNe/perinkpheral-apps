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

#include "app_draw.h"
#include "app_state.h"
#include "app_text.h"
#include "app_values.h"
#include "host_api.h"
#include "strings.h" /* made of the `strings` folder by tools/mkapp */
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

static const char CITY_KEY[] = "city";
static const char HEADER_CITY_KEY[] = "header_city";


/* ---- what a WMO code means ---- */

typedef struct {
    int32_t     up_to; /* the last code this line is for */
    icon_t      icon;
} condition_t;

static const condition_t CONDITIONS[] = {
    { 0, ICON_CLEAR },
    { 1, ICON_CLEAR },
    { 2, ICON_PARTLY_CLOUDY },
    /* Fog is shown as an overcast sky: no picture of fog reads as fog at
     * this size. */
    { 48, ICON_CLOUDY },
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

/* What the last answer came to, for when there is nothing to show. */
#define STATUS_KEY "status"
static int32_t last_status;

static void load_forecast(void)
{
    if (kv_get(STATUS_KEY, sizeof STATUS_KEY - 1, &last_status, sizeof last_status) !=
        sizeof last_status) {
        last_status = 0;
    }
    has_forecast = app_state_load(FORECAST_KEY, &forecast, sizeof forecast, FORECAST_VERSION);
}

/* ---- the settings ---- */

static bool shows_fahrenheit(void)
{
    return app_setting_is("units", "fahrenheit");
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


/* The host's header: on the left the city the weather is for, while its
 * setting is on; the right is the host's own - the date, or the time. */
static void show_header(void)
{
    static const char EMPTY[] = " ";

    /* The setting is "Saint Petersburg, Saint Petersburg, Russia": the city
     * is what stands before the first comma. */
    char city[128];
    int32_t stored = kv_get(CITY_KEY, sizeof CITY_KEY - 1, city, sizeof city);
    int32_t city_length = 0;
    while (city_length < stored && city[city_length] != ',') {
        city_length++;
    }
    if (city_length > HOST_HEADER_TEXT_MAX) {
        city_length = HOST_HEADER_TEXT_MAX;
    }
    if (app_setting_is_on(HEADER_CITY_KEY) && city_length > 0) {
        header_text(HOST_HEADER_LEFT, city, city_length);
    } else {
        header_text(HOST_HEADER_LEFT, EMPTY, sizeof EMPTY - 1);
    }

}

void app_init(void)
{
    load_forecast();
    show_header();
}

void app_on_focus(void)
{
    show_header();
}

void app_on_values(int32_t request, int32_t http_status)
{
    (void)request;
    if (http_status != last_status) {
        last_status = http_status;
        kv_set(STATUS_KEY, sizeof STATUS_KEY - 1, &last_status, sizeof last_status);
        request_redraw(HOST_REDRAW_NORMAL);
    }
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
    app_state_save(FORECAST_KEY, &forecast, sizeof forecast);
    show_header(); /* the day may have turned since the last answer */
    request_redraw(HOST_REDRAW_NORMAL);
}

/* The place may have changed, and then the forecast in hand is for
 * somewhere else: it is dropped, and the host fetches a new one at once. */
void app_on_settings(void)
{
    has_forecast = false;
    app_state_forget(FORECAST_KEY);
    show_header();
    request_redraw(HOST_REDRAW_NORMAL);
}

void app_on_button(int32_t event)
{
    switch (event) {
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
/* The host draws the sign; the fonts are ASCII and have none. */
#define DEGREE_SIGN       "\xC2\xB0"
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

static void draw_icon(const uint8_t *picture, int32_t left, int32_t top)
{
    gfx_picture(left, top, picture, ICON_CELL_PIXELS);
}

/* The columns of an icon that have ink: icons are not all as wide as
 * their cell. A picture is its width, its height and a bit a cell. */
static void icon_ink_columns(const uint8_t *picture, int32_t *first, int32_t *end)
{
    int32_t width = picture[0], height = picture[1], stride = (width + 7) / 8;
    *first = width;
    *end = 0;
    for (int32_t row = 0; row < height; row++) {
        for (int32_t column = 0; column < width; column++) {
            if ((picture[2 + row * stride + column / 8] & (0x80 >> (column % 8))) == 0) {
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

/* How wide a line of the details is: the picture, and the value with its
 * unit as the host sets them. */
static int32_t detail_width(const char *value, const char *unit)
{
    return DETAIL_ICON_CELLS * ICON_CELL_PIXELS + DETAIL_ICON_GAP +
           ui_value_width(HOST_FONT_TEXT, value, app_text_length(value), unit,
                          app_text_length(unit));
}

static void draw_detail(int32_t left, int32_t line, detail_t detail, const char *value,
                        const char *unit)
{
    int32_t top = DETAIL_TOP + line * DETAIL_LINE;
    int32_t baseline = top + gfx_font_ascent(HOST_FONT_TEXT);
    int32_t icon_size = DETAIL_ICON_CELLS * ICON_CELL_PIXELS;
    /* The picture stands on the text's baseline. */
    draw_icon(DETAIL_ICONS[detail], left, baseline - icon_size);
    ui_value(left + icon_size + DETAIL_ICON_GAP, top, HOST_FONT_TEXT, value,
             app_text_length(value), unit, app_text_length(unit));
}

static void draw_now(void)
{
    const condition_t *now = condition_of(forecast.code);
    /* The ink of the icon, not its cell, starts at the left edge and ends
     * where the digits are counted from. */
    int32_t ink_first;
    int32_t ink_end;
    icon_ink_columns(LARGE_ICONS[now->icon], &ink_first, &ink_end);
    int32_t icon_left = NOW_LEFT - ink_first * ICON_CELL_PIXELS;
    draw_icon(LARGE_ICONS[now->icon], icon_left, MARGIN);

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
    app_text_add(&text, DEGREE_SIGN);
    app_draw_text(text_left, cap_top - (gfx_font_ascent(font) - cap_height), font, buffer);

    char rain[8];
    app_text_start(&text, rain, sizeof rain);
    app_text_add_number(&text, forecast.rain_chance);
    app_text_add(&text, "%");
    char wind[8];
    app_text_start(&text, wind, sizeof wind);
    app_text_add_number(&text, shown_speed(forecast.wind));
    const char *wind_unit = app_string(shows_fahrenheit() ? STR_MILES_PER_HOUR : STR_KM_PER_HOUR);

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
    int64_t now = time_now();
    int32_t column_width = gfx_width() / DAYS;
    gfx_rect(MARGIN, RULE_TOP, gfx_width() - 2 * MARGIN, 1, HOST_RECT_FILLED);
    for (int32_t day = 0; day < DAYS; day++) {
        int32_t center = day * column_width + column_width / 2;
        /* "Sat", from the host's "Sat 14 Nov": its first word, in the
         * device's language. */
        char name[32];
        if (day == 0) {
            app_text_t text;
            app_text_start(&text, name, sizeof name);
            app_text_add(&text, app_string(STR_TODAY));
        } else {
            int32_t length = now != 0 ? time_text(now + (int64_t)day * 86400, HOST_TIME_DATE, name,
                                                  sizeof name - 1)
                                      : 0;
            name[length] = '\0';
            for (int32_t i = 0; i < length; i++) {
                if (name[i] == ' ') {
                    name[i] = '\0';
                }
            }
        }
        app_draw_centered(center, DAY_NAME_TOP, HOST_FONT_SMALL, name);
        int32_t icon_size = SMALL_ICON_CELLS * ICON_CELL_PIXELS;
        draw_icon(SMALL_ICONS[condition_of(forecast.days[day].code)->icon],
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
        int32_t high_width = app_text_width(HOST_FONT_TEXT, high);
        int32_t slash_width = gfx_text_width(HOST_FONT_TEXT, "/", 1);
        int32_t low_width = app_text_width(HOST_FONT_TEXT, low);
        int32_t left = center - (high_width + slash_width + low_width + 2 * DAY_SLASH_GAP) / 2;
        app_draw_text(left, DAY_RANGE_TOP, HOST_FONT_TEXT, high);
        left += high_width + DAY_SLASH_GAP;
        app_draw_text(left, DAY_RANGE_TOP, HOST_FONT_TEXT, "/");
        app_draw_text(left + slash_width + DAY_SLASH_GAP, DAY_RANGE_TOP, HOST_FONT_TEXT, low);
    }
}

void app_on_draw(void)
{
    if (has_forecast) {
        draw_now();
        draw_days();
        return;
    }
    /* Why not, as every application says it; an answer that came without
     * a forecast in it is this one's own to name. */
    if (ui_reason(last_status) == 0) {
        host_notice_t no_data = { .icon = HOST_ICON_WARNING };
        app_text_t    text;
        app_text_start(&text, no_data.first, sizeof no_data.first);
        app_text_add(&text, app_string(STR_NO_DATA));
        ui_notice(&no_data);
    }
}
