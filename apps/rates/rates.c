// SPDX-License-Identifier: MIT
// Copyright (c) 2026 GrakovNe
/*
 * Exchange rates: the Bank of Russia's daily prices, kept as integers of
 * ten-thousandths of a ruble. Three streaming pickers share each piece:
 * one picker holds at most sixteen paths, and eight currencies need more.
 * Only a complete, valid response replaces the stored snapshot. Settings
 * changed during a delivery invalidate it. Navigation lives in the session.
 * UP and DOWN scroll; OK asks for fresh data.
 */
#include "app_draw.h"
#include "app_fixed.h"
#include "app_state.h"
#include "app_text.h"
#include "app_ui.h"
#include "app_values.h"
#include "host_api.h"
#include "json_pick.h"
#include "strings.h"

#define CURRENCIES_MAX 8
#define PICKERS 3
#define PER_PICKER 3
#define FIELDS 4
#define PATHS (PER_PICKER * FIELDS + 1)

typedef struct {
    int64_t value, previous;
    int32_t nominal;
    char code[4], name[64];
} rate_t;
typedef struct {
    int32_t version, has_data;
    char date[32];
    rate_t rates[CURRENCIES_MAX];
} state_t;
_Static_assert(sizeof(state_t) <= HOST_KV_VALUE_MAX, "snapshot fits one store value");
static state_t state, incoming;
static app_list_t list = { .selected = -1, .height = 226 };
static json_pick_t pickers[PICKERS];
static char paths[PICKERS][PATHS][40];
static const char *path_refs[PICKERS][PATHS];
static int32_t count, status, saved_status;
static bool reading, urgent;

static int32_t currency_count(void)
{
    int32_t n = app_setting_count("codes");
    return n > CURRENCIES_MAX ? CURRENCIES_MAX : n;
}

static bool valid_code(const char *code)
{
    return app_text_length(code) == 3 && code[0] >= 'A' && code[0] <= 'Z'
        && code[1] >= 'A' && code[1] <= 'Z' && code[2] >= 'A' && code[2] <= 'Z';
}

static void begin_reading(void)
{
    incoming = (state_t){ .version = 1, .has_data = 1 };
    count = currency_count();
    for (int32_t group = 0; group < PICKERS; group++) {
        static const char *const fields[] = { "Value", "Previous", "Nominal", "Name" };
        for (int32_t p = 0; p < PATHS; p++) {
            paths[group][p][0] = 0;
            path_refs[group][p] = paths[group][p];
        }
        app_text_copy(paths[group][0], sizeof paths[group][0], "Date");
        for (int32_t slot = 0; slot < PER_PICKER; slot++) {
            int32_t i = group * PER_PICKER + slot;
            if (i >= count) { break; }
            char code[64];
            app_setting_item("codes", i, code, sizeof code);
            app_text_copy(incoming.rates[i].code, sizeof incoming.rates[i].code, code);
            for (int32_t f = 0; f < FIELDS; f++) {
                app_text_t text;
                app_text_start(&text, paths[group][1 + slot * FIELDS + f], 40);
                app_text_add(&text, "Valute.");
                app_text_add(&text, valid_code(code) ? code : "INVALID");
                app_text_add(&text, ".");
                app_text_add(&text, fields[f]);
            }
        }
        int32_t slots = count - group * PER_PICKER;
        if (slots < 0) { slots = 0; }
        if (slots > PER_PICKER) { slots = PER_PICKER; }
        json_pick_init(&pickers[group], path_refs[group], 1 + slots * FIELDS);
    }
    reading = true;
}

static bool take_response(void)
{
    for (int32_t group = 0; group < PICKERS; group++) {
        if (!json_pick_finish(&pickers[group])) { return false; }
    }
    const char *date = json_pick_value(&pickers[0], 0);
    if (!date || app_text_length(date) < 10 || date[4] != '-' || date[7] != '-') { return false; }
    app_text_copy(incoming.date, sizeof incoming.date, date);
    for (int32_t i = 0; i < count; i++) {
        rate_t *rate = &incoming.rates[i];
        json_pick_t *pick = &pickers[i / PER_PICKER];
        int32_t p = 1 + (i % PER_PICKER) * FIELDS;
        const char *value = json_pick_value(pick, p);
        const char *previous = json_pick_value(pick, p + 1);
        const char *nominal = json_pick_value(pick, p + 2);
        const char *name = json_pick_value(pick, p + 3);
        if (!value || !previous || !nominal || !app_fixed_parse(value, 4, &rate->value)
            || !app_fixed_parse(previous, 4, &rate->previous)
            || !app_parse_number(nominal, 0, &rate->nominal)
            || rate->nominal < 1 || rate->value < 0 || rate->previous < 0) {
            rate->nominal = 0;
        }
        if (name) { app_text_copy(rate->name, sizeof rate->name, name); }
    }
    const uint8_t *a = (const uint8_t *)&state, *b = (const uint8_t *)&incoming;
    bool changed = false;
    for (int32_t i = 0; i < (int32_t)sizeof state; i++) { changed |= a[i] != b[i]; }
    state = incoming;
    if (changed) { app_state_save("state", &state, sizeof state); }
    return true;
}

void app_on_load(void)
{
    if (!app_state_load("state", &state, sizeof state, 1)) { state = (state_t){ .version = 1 }; }
    if (kv_get("status", 6, &status, sizeof status) != sizeof status) { status = 0; }
    saved_status = status;
}
void app_on_focus(void)
{
    app_session_load(&list, sizeof list);
    list.count = currency_count();
}
void app_on_blur(void) { app_session_save(&list, sizeof list); }
void app_on_settings(void)
{
    reading = false;
    list.count = currency_count();
    screen_redraw(HOST_REDRAW_NORMAL);
}
void app_on_button(int32_t event)
{
    if (event == BTN_OK_PRESSED) { urgent = true; poll_run(0); }
    else if (app_list_button(&list, event) == APP_UI_CHANGED) { screen_redraw(HOST_REDRAW_NORMAL); }
}
void app_on_data(int32_t request, const uint8_t *data, int32_t length, int32_t flags, int32_t http_status)
{
    if (request != 0) { return; }
    if (flags & HOST_DATA_FAILED) {
        status = http_status;
        reading = false;
    } else {
        if (flags & HOST_DATA_FIRST) {
            status = http_status;
            begin_reading();
        }
        if (reading && status >= 200 && status < 300) {
            for (int32_t group = 0; group < PICKERS; group++) {
                json_pick_feed(&pickers[group], (const char *)data, length);
            }
            if ((flags & HOST_DATA_LAST) && !take_response()) { status = HOST_FAILED_NO_ANSWER; }
        }
    }
    if (flags & (HOST_DATA_LAST | HOST_DATA_FAILED)) {
        reading = false;
        if (status != saved_status && kv_set("status", 6, &status, sizeof status) == HOST_OK) { saved_status = status; }
        screen_redraw(urgent ? HOST_REDRAW_SOON : HOST_REDRAW_NORMAL);
        urgent = false;
    }
}

void app_on_draw(void)
{
    list.count = currency_count();
    if (!list.count) { ui_draw_reason(HOST_FAILED_NOT_READY); return; }
    if (!state.has_data && ui_draw_reason(status)) { return; }
    int32_t first = app_list_first(&list), visible = ui_list_visible(&list);
    for (int32_t i = first; i < list.count && i < first + visible; i++) {
        char code[64], label[80], value[48], name[80];
        app_setting_item("codes", i, code, sizeof code);
        const rate_t *rate = 0;
        for (int32_t j = 0; j < CURRENCIES_MAX; j++) {
            if (valid_code(code) && app_text_equal(state.rates[j].code, code)) { rate = &state.rates[j]; break; }
        }
        app_text_t text;
        app_text_start(&text, label, sizeof label);
        app_text_add(&text, code);
        if (rate && rate->nominal > 1) {
            app_text_add(&text, " ("); app_text_add_number(&text, rate->nominal); app_text_add(&text, ")");
        }
        app_text_start(&text, value, sizeof value);
        if (rate && rate->nominal) {
            app_text_add_fixed(&text, rate->value, 4, 0);
            app_text_add(&text, " / ");
            app_text_add_fixed(&text, app_fixed_rescale(rate->value - rate->previous, 4, 2), 2, APP_FIXED_SIGN);
        } else { app_text_add(&text, app_string(STR_NO_DATA)); }
        int32_t y = 8 + (i - first) * 42;
        /* The kit owns the scrollbar; measured columns keep all digits visible. */
        ui_draw_list_row(&list, i, "", 0, "", 0, HOST_ICON_NONE);
        int32_t right = gfx_width() - HOST_UI_MARGIN - (list.count > visible ? 8 : 0);
        int32_t font = HOST_FONT_TEXT | HOST_FONT_TABULAR;
        int32_t value_width = app_text_width(font, value);
        char fitted[80];
        int32_t label_font = app_text_width(HOST_FONT_TEXT, label) <= right - value_width - 28 ? HOST_FONT_TEXT : HOST_FONT_SMALL;
        int32_t n = gfx_fit_text(label_font, label, app_text_length(label), right - value_width - 28, fitted, sizeof fitted);
        gfx_draw_text(HOST_UI_MARGIN, y, label_font, fitted, n);
        app_draw_right(right, y, font, value);
        host_text_argument_t arg = { .text = rate ? rate->name : "", .length = rate ? app_text_length(rate->name) : 0 };
        const char *pattern = app_string(STR_CURRENCY_NAME);
        n = text_fill(pattern, app_text_length(pattern), &arg, 1, name, sizeof name);
        n = gfx_fit_text(HOST_FONT_SMALL, name, n, right - HOST_UI_MARGIN, fitted, sizeof fitted);
        gfx_draw_text(HOST_UI_MARGIN, y + 25, HOST_FONT_SMALL, fitted, n);
    }
    if (state.has_data) {
        app_draw_text(HOST_UI_MARGIN, gfx_height() - gfx_font_height(HOST_FONT_SMALL) - 4,
                      HOST_FONT_SMALL, app_string(STR_COLUMNS));
        int32_t year = 0, month = 0, day = 0;
        for (int32_t i = 0; i < 10; i++) {
            if (i == 4 || i == 7) { continue; }
            int32_t digit = state.date[i] - '0';
            if (i < 4) { year = year * 10 + digit; }
            else if (i < 7) { month = month * 10 + digit; }
            else { day = day * 10 + digit; }
        }
        host_local_time_t parts = { .year = year, .month = month, .day = day, .hour = 12 };
        int64_t moment;
        char date[48];
        if (time_from_local(&parts, &moment) == HOST_OK) {
            app_time_text(moment, HOST_TIME_LONG_DATE, date, sizeof date);
            app_draw_right(gfx_width() - HOST_UI_MARGIN, gfx_height() - gfx_font_height(HOST_FONT_SMALL) - 4, HOST_FONT_SMALL, date);
        }
    }
}
