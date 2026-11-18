/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * The weather now and for three days, from Open-Meteo: no key, no
 * account, the device asks the service itself. The manifest names the
 * request and the numbers wanted from the answer; Perinkpheral OS makes the
 * request at the pace the user chose, whether this application is on the
 * screen or not, and hands the numbers to app_on_picked(). They go into
 * the store, and the picture is drawn from the store: off the screen the
 * application is loaded for the call and unloaded after it.
 *
 * The settings are in the store too, under the keys the manifest gives
 * them: the place (`lat`, `lon`), the units and how often to ask.
 */
#include <stdbool.h>
#include <stddef.h>

#include "app_draw.h"
#include "app_state.h"
#include "app_text.h"
#include "app_values.h"
#include "host_api.h"
#include "strings.h" /* made of the `strings` folder by tools/perinkpheral */
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
#define SECONDS_PER_DAY 86400

/* What was learned last, as it lies in the store. */
#define FORECAST_KEY     "forecast"
#define FORECAST_VERSION 5
#define PREVIOUS_FORECAST_VERSION 4
typedef struct {
    int32_t version;
    int32_t day;         /* the local day of days[0], in days since 1970; 0 unknown */
    int32_t temperature; /* whole degrees */
    int32_t code;        /* WMO weather code */
    int32_t wind;        /* km/h */
    int32_t rain_chance; /* of today, percent */
    struct {
        int32_t code, high, low;
    } days[DAYS];
    char lat[40], lon[40];
} forecast_t;

static forecast_t forecast;

/* Today, in the device's time, as days since 1970; 0 while the time is unknown. */
static int32_t local_day(void)
{
    int64_t now = time_now();
    return now == 0 ? 0 : (int32_t)((now + time_zone_offset()) / SECONDS_PER_DAY);
}
static bool       has_forecast;
static bool       forecast_dirty;

static void forecast_place(forecast_t *value)
{
    app_setting("lat", value->lat, sizeof value->lat);
    app_setting("lon", value->lon, sizeof value->lon);
}

static bool same_place(void)
{
    char lat[40], lon[40];
    app_setting("lat", lat, sizeof lat);
    app_setting("lon", lon, sizeof lon);
    return app_text_equal(lat, forecast.lat) && app_text_equal(lon, forecast.lon);
}

#define CITY_KEY        "city"
#define HEADER_CITY_KEY "header_city"

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
    int32_t length = kv_get(FORECAST_KEY, sizeof FORECAST_KEY - 1, &forecast, sizeof forecast);
    bool migrated = length == offsetof(forecast_t, lat) && forecast.version == PREVIOUS_FORECAST_VERSION;
    forecast_dirty = migrated;
    if (migrated) {
        /* The previous version cleared its cache on every settings change. */
        forecast.version = FORECAST_VERSION;
        forecast_place(&forecast);
    }
    has_forecast = (length == sizeof forecast || migrated) &&
                   forecast.version == FORECAST_VERSION && same_place();
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

/* Perinkpheral OS's header: on the left the city the weather is for, while its
 * setting is on; the right is Perinkpheral OS's own - the date, or the time. */
static void show_header(void)
{
    char city[HOST_HEADER_TEXT_MAX + 1];
    int32_t length = app_setting_place(CITY_KEY, city, sizeof city);
    if (app_setting_is_on(HEADER_CITY_KEY) && length > 0) {
        header_set_text(HOST_HEADER_LEFT, city, length);
    } else {
        header_set_text(HOST_HEADER_LEFT, " ", 1); /* a space: the side stays empty */
    }
}

void app_on_load(void)
{
    load_forecast();
    show_header();
}

void app_on_focus(void)
{
    show_header();
}

void app_on_picked(int32_t request, int32_t http_status)
{
    (void)request;
    if (http_status != last_status) {
        last_status = http_status;
        kv_set(STATUS_KEY, sizeof STATUS_KEY - 1, &last_status, sizeof last_status);
        screen_redraw(HOST_REDRAW_NORMAL);
    }
    forecast_t fresh = { .version = FORECAST_VERSION, .day = local_day() };
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
    forecast_place(&fresh);
    bool changed = !has_forecast;
    for (int32_t byte = 0; byte < (int32_t)sizeof fresh; byte++) {
        changed |= ((const uint8_t *)&fresh)[byte] != ((const uint8_t *)&forecast)[byte];
    }
    forecast = fresh;
    has_forecast = true;
    if (changed || forecast_dirty) {
        forecast_dirty = !app_state_save(FORECAST_KEY, &forecast, sizeof forecast);
    }
    show_header(); /* the day may have turned since the last answer */
    screen_redraw(HOST_REDRAW_NORMAL);
}

/* The place may have changed, and then the forecast in hand is for
 * somewhere else: it is dropped, and Perinkpheral OS fetches a new one at once. */
void app_on_settings(void)
{
    if (has_forecast && !same_place()) {
        has_forecast = false;
        forecast_dirty = false;
        app_state_forget(FORECAST_KEY);
    }
    show_header();
    screen_redraw(HOST_REDRAW_NORMAL);
}

void app_on_button(int32_t event)
{
    switch (event) {
    case BTN_OK_PRESSED:
        poll_run(WEATHER_POLL); /* "refresh": the answer redraws the screen */
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
/* The details: two lines in the middle of the large icon's height, their pictures in one column and the values
 * starting at one edge beside it. A unit is in the small font. */
#define DETAIL_TOP        30
#define DETAIL_LINE       36
#define DETAIL_ICON_GAP   10
/* The rule stands as far under the large icon as the icon stands under
 * the header's rule: the same margin on both sides. */
#define RULE_TOP          (MARGIN + ICON_SIDE(LARGE_ICON_CELL) + MARGIN)
/* A day: its name as a caption, its icon, and "high / low" in one line. */
#define DAY_NAME_TOP      133
#define DAY_ICON_TOP      158
#define DAY_RANGE_TOP     214
#define DAY_SLASH_GAP     3

/* How wide a line of the details is: the picture, and the value with its
 * unit as Perinkpheral OS sets them. */
static int32_t detail_width(const char *value, const char *unit)
{
    return ICON_SIDE(DETAIL_ICON_CELL) + DETAIL_ICON_GAP +
           ui_value_width(HOST_FONT_TEXT, value, app_text_length(value), unit,
                          app_text_length(unit));
}

static void draw_detail(int32_t left, int32_t line, detail_t detail, const char *value,
                        const char *unit)
{
    int32_t top = DETAIL_TOP + line * DETAIL_LINE;
    int32_t baseline = top + gfx_font_ascent(HOST_FONT_TEXT);
    int32_t icon_size = ICON_SIDE(DETAIL_ICON_CELL);
    /* The picture stands on the text's baseline. */
    gfx_draw_icon(left, baseline - icon_size, DETAIL_ICONS[detail], DETAIL_ICON_CELL);
    ui_draw_value(left + icon_size + DETAIL_ICON_GAP, top, HOST_FONT_TEXT, value,
             app_text_length(value), unit, app_text_length(unit));
}

static void draw_now(void)
{
    const condition_t *now = condition_of(forecast.code);
    /* The ink of the icon, not its cell, starts at the left edge and ends
     * where the digits are counted from. */
    host_icon_ink_t ink = { 0, ICON_SIDE(LARGE_ICON_CELL) - 1, 0, ICON_SIDE(LARGE_ICON_CELL) - 1 };
    gfx_icon_ink(WEATHER_ICONS[now->icon], LARGE_ICON_CELL, &ink);
    int32_t icon_left = NOW_LEFT - ink.first_x;
    gfx_draw_icon(icon_left, MARGIN, WEATHER_ICONS[now->icon], LARGE_ICON_CELL);

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
    int32_t cap_top = MARGIN + (ICON_SIDE(LARGE_ICON_CELL) - cap_height) / 2;
    int32_t text_left = icon_left + ink.last_x + 1 + NOW_TEXT_GAP;
    app_text_add(&text, APP_TEXT_DEGREE);
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
    /* A forecast of yesterday, kept through a night without an answer: its days move left, and
     * what it does not know is left blank rather than called today's. */
    int32_t today = local_day();
    int32_t shift = forecast.day != 0 && today > forecast.day ? today - forecast.day : 0;
    int32_t column_width = gfx_width() / DAYS;
    gfx_draw_rect(MARGIN, RULE_TOP, gfx_width() - 2 * MARGIN, 1, HOST_RECT_FILLED);
    for (int32_t day = 0; day < DAYS; day++) {
        int32_t center = day * column_width + column_width / 2;
        /* "Sat", in the device's language; today is "Today". */
        char name[32];
        if (day == 0) {
            app_text_copy(name, sizeof name, app_string(STR_TODAY));
        } else if (now != 0) {
            app_time_text(now + (int64_t)day * SECONDS_PER_DAY, HOST_TIME_WEEKDAY_SHORT, name,
                          sizeof name);
        } else {
            name[0] = '\0';
        }
        app_draw_centered(center, DAY_NAME_TOP, HOST_FONT_SMALL, name);
        if (day + shift >= DAYS) {
            continue;
        }
        int32_t icon_size = ICON_SIDE(SMALL_ICON_CELL);
        gfx_draw_icon(center - icon_size / 2, DAY_ICON_TOP,
                 WEATHER_ICONS[condition_of(forecast.days[day + shift].code)->icon], SMALL_ICON_CELL);

        /* "high / low", the slash closer to its numbers than a space would
         * put it: three such ranges of a cold week have to stay apart. */
        char high[8];
        char low[8];
        app_text_t text;
        app_text_start(&text, high, sizeof high);
        app_text_add_number(&text, shown_degrees(forecast.days[day + shift].high));
        app_text_start(&text, low, sizeof low);
        app_text_add_number(&text, shown_degrees(forecast.days[day + shift].low));
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
    if (ui_draw_reason(last_status) == 0) {
        host_notice_t no_data = { .icon = HOST_ICON_WARNING };
        app_text_copy(no_data.first, sizeof no_data.first, app_string(STR_NO_DATA));
        ui_draw_notice(&no_data);
    }
}
