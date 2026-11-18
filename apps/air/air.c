// SPDX-License-Identifier: MIT
// Copyright (c) 2026 GrakovNe
/*
 * Air quality from Open-Meteo and CAMS: an index and its picture, two
 * particle details and, under the rule, UV and the three worst irritants.
 * The host picks current readings only. One versioned snapshot keeps the
 * last good answer and its place; missing pollen stays distinct from zero.
 * Changing the index scale needs no request. OK asks for fresh data.
 */
#include <stddef.h>

#include "air_quality.h"
#include "app_draw.h"
#include "app_fixed.h"
#include "app_state.h"
#include "app_text.h"
#include "app_values.h"
#include "host_api.h"
#include "strings.h"

enum { EU, US, PM25, PM10, UV, IRRITANT_FIRST };
enum {
    STATE_VERSION = 3, PREVIOUS_STATE_VERSION = 2, MARGIN = 16, HERO_LEFT = MARGIN + 6,
    HERO_PICTURE = 88, HERO_TEXT_GAP = 16, HERO_TEXT_RIGHT = 246,
    DETAIL_LEFT = 266,
    DETAIL_TOP = 17, DETAIL_LINE = 46, DETAIL_VALUE_GAP = 13,
    RULE_TOP = MARGIN + HERO_PICTURE + MARGIN,
    COLUMN_COUNT = 4, COLUMN_NAME_TOP = 130, COLUMN_PICTURE_TOP = 148,
    COLUMN_PICTURE = 44, COLUMN_VALUE_TOP = 195, COLUMN_UNIT_TOP = 222, COLUMN_BAND_TOP = 242,
    COLUMN_INSET = 6
};

typedef struct {
    int32_t version, has_data;
    int32_t aqi[2], pm25, pm10, uv, irritants[AIR_IRRITANTS];
    char lat[40], lon[40];
    int64_t updated_at;
} state_t;

static state_t state;
static int32_t status, saved_status;
static bool urgent;

static const struct { int32_t name, icon; } IRRITANTS[AIR_IRRITANTS] = {
    [AIR_OZONE] = { STR_OZONE, HOST_ICON_GAS },
    [AIR_NO2] = { STR_NO2, HOST_ICON_GAS },
    [AIR_SO2] = { STR_SO2, HOST_ICON_GAS },
    [AIR_CO] = { STR_CO, HOST_ICON_GAS },
    [AIR_DUST] = { STR_DUST, HOST_ICON_DUST },
    [AIR_ALDER] = { STR_ALDER, HOST_ICON_POLLEN },
    [AIR_BIRCH] = { STR_BIRCH, HOST_ICON_POLLEN },
    [AIR_GRASS] = { STR_GRASS, HOST_ICON_POLLEN },
    [AIR_MUGWORT] = { STR_MUGWORT, HOST_ICON_POLLEN },
    [AIR_RAGWEED] = { STR_RAGWEED, HOST_ICON_POLLEN },
    [AIR_OLIVE] = { STR_OLIVE, HOST_ICON_POLLEN }
};

static void save_status(void)
{
    if (status != saved_status && kv_set("status", 6, &status, sizeof status) == HOST_OK) { saved_status = status; }
}

static bool place_set(void)
{
    char lat[40], lon[40];
    return app_setting("lat", lat, sizeof lat) > 0 && app_setting("lon", lon, sizeof lon) > 0;
}

static bool same_place(const state_t *value)
{
    char lat[40], lon[40];
    app_setting("lat", lat, sizeof lat);
    app_setting("lon", lon, sizeof lon);
    return app_text_equal(value->lat, lat) && app_text_equal(value->lon, lon);
}

static void show_header(void)
{
    char city[HOST_HEADER_TEXT_MAX + 1];
    int32_t length = app_setting_place("city", city, sizeof city);
    header_set_text(HOST_HEADER_RIGHT, city, app_setting_is_on("header_city") ? length : 0);
}

static int32_t index_band(int32_t index, bool us)
{
    static const int32_t limits[2][5] = { { 20, 40, 60, 80, 100 }, { 50, 100, 150, 200, 300 } };
    int32_t band = 0;
    while (band < 5 && index > limits[us][band]) { band++; }
    return band;
}

static const char *index_word(int32_t index, bool us)
{
    static const int32_t words[2][6] = {
        { STR_GOOD, STR_FAIR, STR_MODERATE, STR_POOR, STR_VERY_POOR, STR_EXTREME },
        { STR_GOOD, STR_MODERATE, STR_SENSITIVE, STR_UNHEALTHY, STR_VERY_UNHEALTHY, STR_HAZARDOUS }
    };
    return app_string(words[us][index_band(index, us)]);
}

static const char *band_word(int32_t band)
{
    static const int32_t words[AIR_BANDS] = { STR_LOW, STR_MODERATE, STR_HIGH, STR_VERY_HIGH };
    return app_string(words[band]);
}

void app_on_load(void)
{
    int32_t length = kv_get("state", 5, &state, sizeof state);
    if (length == offsetof(state_t, updated_at) && state.version == PREVIOUS_STATE_VERSION) {
        state.version = STATE_VERSION;
        state.updated_at = 0;
    } else if (length != sizeof state || state.version != STATE_VERSION) {
        state = (state_t){ .version = STATE_VERSION };
    }
    if (kv_get("status", 6, &status, sizeof status) != sizeof status) { status = 0; }
    saved_status = status;
}

void app_on_focus(void) { show_header(); }

void app_on_settings(void)
{
    if (state.has_data && !same_place(&state)) {
        state = (state_t){ .version = STATE_VERSION };
        app_state_forget("state");
        status = 0;
        save_status();
    }
    show_header();
    screen_redraw(HOST_REDRAW_NORMAL);
}

void app_on_button(int32_t event)
{
    if (event == BTN_OK_PRESSED) { urgent = true; poll_run(0); }
}

void app_on_picked(int32_t request, int32_t http_status)
{
    if (request != 0) { return; }
    status = http_status;
    if (status >= 200 && status < 300 && place_set()) {
        state_t fresh = { .version = STATE_VERSION, .has_data = 1 };
        bool whole = app_value_number(EU, 0, &fresh.aqi[0]) && fresh.aqi[0] >= 0
            && app_value_number(US, 0, &fresh.aqi[1]) && fresh.aqi[1] >= 0
            && app_value_number(PM25, 1, &fresh.pm25) && fresh.pm25 >= 0
            && app_value_number(PM10, 1, &fresh.pm10) && fresh.pm10 >= 0
            && app_value_number(UV, 1, &fresh.uv) && fresh.uv >= 0;
        for (int32_t irritant = 0; irritant < AIR_IRRITANTS; irritant++) {
            int32_t value;
            if (app_value_number(IRRITANT_FIRST + irritant, 1, &value) && value >= 0) {
                fresh.irritants[irritant] = value;
            } else {
                fresh.irritants[irritant] = AIR_MISSING;
                if (irritant < AIR_ALDER) { whole = false; }
            }
        }
        app_setting("lat", fresh.lat, sizeof fresh.lat);
        app_setting("lon", fresh.lon, sizeof fresh.lon);
        if (whole) {
            fresh.updated_at = time_now();
            bool changed = false;
            const uint8_t *saved = (const uint8_t *)&state, *received = (const uint8_t *)&fresh;
            for (int32_t index = 0; index < (int32_t)sizeof state; index++) {
                changed |= saved[index] != received[index];
            }
            state = fresh;
            if (changed) { app_state_save("state", &state, sizeof state); }
        } else { status = HOST_FAILED_NO_ANSWER; }
    }
    save_status();
    screen_redraw(urgent ? HOST_REDRAW_SOON : HOST_REDRAW_NORMAL);
    urgent = false;
}

static void number_text(char *buffer, int32_t capacity, int32_t value, int32_t decimals)
{
    app_text_t text;
    app_text_start(&text, buffer, capacity);
    app_text_add_fixed(&text, value, decimals, 0);
}

#define HERO_WORD_GAP 10 /* between the index's capitals and its word */
#define HERO_LABEL_GAP 8

static void draw_hero(bool us)
{
    int32_t band = index_band(state.aqi[us], us);
    int32_t icon = band < (us ? 1 : 2) ? HOST_ICON_LEAF : band < 3 ? HOST_ICON_WIND : HOST_ICON_MASK;
    gfx_draw_icon(HERO_LEFT, MARGIN, icon, HERO_PICTURE / HOST_ICON_CELLS);
    char value[16];
    number_text(value, sizeof value, state.aqi[us], 0);
    int32_t left = HERO_LEFT + HERO_PICTURE + HERO_TEXT_GAP;
    int32_t width = HERO_TEXT_RIGHT - left;
    int32_t font = HOST_FONT_DISPLAY | HOST_FONT_TABULAR;
    if (app_text_width(font, value) > width) { font = HOST_FONT_LARGE | HOST_FONT_TABULAR; }
    if (app_text_width(font, value) > width) { font = HOST_FONT_MEDIUM | HOST_FONT_TABULAR; }
    /* Centre the label, number and quality together in the upper section. */
    int32_t gap = HERO_WORD_GAP;
    int32_t cap = gfx_font_cap_height(font), word_cap = gfx_font_cap_height(HOST_FONT_SMALL);
    int32_t block_height = word_cap + HERO_LABEL_GAP + cap + gap + word_cap;
    int32_t label_top = (RULE_TOP - block_height) / 2;
    app_draw_centered(left + width / 2, label_top - (gfx_font_ascent(HOST_FONT_SMALL) - word_cap),
                      HOST_FONT_SMALL, us ? "US AQI" : "EU AQI");
    int32_t top = label_top + word_cap + HERO_LABEL_GAP;
    app_draw_centered(left + width / 2, top - (gfx_font_ascent(font) - cap), font, value);
    const char *word = index_word(state.aqi[us], us);
    int32_t word_top = top + cap + gap;
    app_draw_centered(left + width / 2, word_top - (gfx_font_ascent(HOST_FONT_SMALL) - word_cap),
                      HOST_FONT_SMALL, word);
}

static int32_t detail_font(void)
{
    int32_t font = HOST_FONT_TEXT | HOST_FONT_TABULAR;
    int32_t available = gfx_width() - MARGIN - DETAIL_LEFT;
    const char *unit = app_string(STR_CONCENTRATION);
    const int32_t numbers[] = { state.pm25, state.pm10 };
    for (int32_t row = 0; row < 2; row++) {
        char value[24];
        number_text(value, sizeof value, numbers[row], 1);
        if (ui_value_width(font, value, app_text_length(value), unit, app_text_length(unit)) > available) {
            return HOST_FONT_SMALL | HOST_FONT_TABULAR;
        }
    }
    return font;
}

static void draw_detail(int32_t row, int32_t label, int32_t number, int32_t font)
{
    int32_t top = DETAIL_TOP + row * DETAIL_LINE;
    int32_t left = DETAIL_LEFT;
    app_draw_text(left, top, HOST_FONT_SMALL, app_string(label));
    char value[24];
    number_text(value, sizeof value, number, 1);
    const char *unit = app_string(STR_CONCENTRATION);
    ui_draw_value(left, top + DETAIL_VALUE_GAP, font, value, app_text_length(value), unit, app_text_length(unit));
}

static void draw_molecule(int32_t left, int32_t top)
{
    /* The 44-pixel symbol shares the tile icons' four-pixel grid. */
    gfx_draw_rect(left + 20, top + 12, 4, 16, HOST_RECT_FILLED);
    gfx_draw_rect(left + 4, top + 24, 36, 4, HOST_RECT_FILLED);
    gfx_draw_rect(left + 4, top + 28, 4, 4, HOST_RECT_FILLED);
    gfx_draw_rect(left + 36, top + 28, 4, 4, HOST_RECT_FILLED);
    const int32_t atoms[][2] = { {16, 0}, {0, 32}, {32, 32} };
    for (int32_t atom = 0; atom < 3; atom++) {
        gfx_draw_rect(left + atoms[atom][0], top + atoms[atom][1], 12, 12, HOST_RECT_FILLED);
        gfx_set_color(HOST_PAPER);
        gfx_draw_rect(left + atoms[atom][0] + 4, top + atoms[atom][1] + 4, 4, 4,
                      HOST_RECT_FILLED);
        gfx_set_color(HOST_INK);
    }
}

static void draw_tile(int32_t column, int32_t name, int32_t icon, int32_t value, int32_t band, int32_t unit)
{
    int32_t width = gfx_width() / COLUMN_COUNT;
    int32_t center = column * width + width / 2;
    int32_t available = width - 2 * COLUMN_INSET;
    const char *label = app_string(name);
    char fitted[64];
    int32_t length = gfx_fit_text(HOST_FONT_SMALL, label, app_text_length(label),
                                  available, fitted, sizeof fitted - 1);
    fitted[length > 0 ? length : 0] = '\0';
    app_draw_centered(center, COLUMN_NAME_TOP, HOST_FONT_SMALL, fitted);
    if (icon == HOST_ICON_GAS) {
        draw_molecule(center - COLUMN_PICTURE / 2, COLUMN_PICTURE_TOP);
    } else {
        gfx_draw_icon(center - COLUMN_PICTURE / 2, COLUMN_PICTURE_TOP, icon, COLUMN_PICTURE / HOST_ICON_CELLS);
    }
    char number[24];
    /* Grains and micrograms are counted whole; only the UV index has a decimal worth reading. */
    if (column == 0) {
        number_text(number, sizeof number, value, 1);
    } else {
        number_text(number, sizeof number, (value + (value < 0 ? -5 : 5)) / 10, 0);
    }
    int32_t font = HOST_FONT_TEXT | HOST_FONT_TABULAR;
    if (app_text_width(font, number) > available) { font = HOST_FONT_SMALL | HOST_FONT_TABULAR; }
    app_draw_centered(center, COLUMN_VALUE_TOP, font, number);
    app_draw_centered(center, COLUMN_UNIT_TOP, HOST_FONT_SMALL, app_string(unit));
    app_draw_centered(center, COLUMN_BAND_TOP, HOST_FONT_SMALL, band_word(band));
}

void app_on_draw(void)
{
    if (!place_set()) { ui_draw_reason(HOST_FAILED_NOT_READY); return; }
    if (!state.has_data || !same_place(&state)) {
        if (!ui_draw_reason(status)) {
            host_notice_t notice = { .icon = HOST_ICON_WARNING };
            app_text_copy(notice.first, sizeof notice.first, app_string(STR_NO_DATA));
            ui_draw_notice(&notice);
        }
        return;
    }
    draw_hero(app_setting_is("scale", "US"));
    int32_t font = detail_font();
    draw_detail(0, STR_PM25, state.pm25, font);
    draw_detail(1, STR_PM10, state.pm10, font);
    gfx_draw_rect(MARGIN, RULE_TOP, gfx_width() - 2 * MARGIN, 1, HOST_RECT_FILLED);
    /* WHO UV bands: Low <3, Moderate <6, High <8; Very high includes
     * Extreme (11+). https://www.who.int/news-room/questions-and-answers/item/radiation-the-ultraviolet-(uv)-index */
    int32_t uv_band = state.uv < 30 ? 0 : state.uv < 60 ? 1 : state.uv < 80 ? 2 : 3;
    draw_tile(0, STR_UV, HOST_ICON_SUN, state.uv, uv_band, STR_INDEX);
    int32_t selected[AIR_TILES];
    int32_t count = air_worst(state.irritants, selected);
    for (int32_t tile = 0; tile < count; tile++) {
        int32_t irritant = selected[tile];
        draw_tile(tile + 1, IRRITANTS[irritant].name, IRRITANTS[irritant].icon,
                  state.irritants[irritant], air_band(irritant, state.irritants[irritant]),
                  irritant >= AIR_ALDER ? STR_GRAINS : STR_CONCENTRATION);
    }
}
