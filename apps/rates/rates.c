/* SPDX-License-Identifier: MIT */
#include "app_days.h"
#include "history.h"
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
    int32_t value;
    int32_t nominal;
    char code[4], name[64], id[HISTORY_ID_BYTES];
} rate_t;
typedef struct {
    int32_t version, has_data;
    char date[32];
    rate_t rates[CURRENCIES_MAX];
} state_t;
_Static_assert(sizeof(state_t) <= HOST_KV_VALUE_MAX, "snapshot fits one store value");
static state_t state, incoming;
typedef struct {
    int32_t version, index, rotating;
} navigation_t;
typedef struct {
    int32_t version;
    char code[4];
    history_t history;
} cached_history_t;
_Static_assert(sizeof(cached_history_t) * CURRENCIES_MAX <= 4096, "history fits one storage block");
static navigation_t navigation;
static history_parser_t history_parser;
static bool history_reading;
static cached_history_t chart;
static const char HISTORY_FILE[] = "history";
#define HISTORY_FILE_LENGTH 7
#define ROTATE_TIMER 0
#define HISTORY_VERSION 1
#define SNAPSHOT_VERSION 2
static void request_histories(void);
static void load_chart(void);

static json_pick_t pickers[PICKERS];
static char paths[PICKERS][PATHS][40];
static const char *path_refs[PICKERS][PATHS];
static int32_t count, status, saved_status;
static bool reading;

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
    incoming = (state_t){ .version = SNAPSHOT_VERSION, .has_data = 1 };
    count = currency_count();
    for (int32_t group = 0; group < PICKERS; group++) {
        static const char *const fields[] = { "Value", "Nominal", "Name", "ID" };
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
    if (!date || !history_day(date, true)) return false;
    app_text_copy(incoming.date, sizeof incoming.date, date);
    for (int32_t i = 0; i < count; i++) {
        rate_t *rate = &incoming.rates[i];
        json_pick_t *pick = &pickers[i / PER_PICKER];
        int32_t p = 1 + (i % PER_PICKER) * FIELDS;
        const char *value = json_pick_value(pick, p);
        const char *nominal = json_pick_value(pick, p + 1);
        const char *name = json_pick_value(pick, p + 2);
        const char *id = json_pick_value(pick, p + 3);
        int32_t nominal_value = 0;
        if (!value || !nominal || !history_amount(value, 4, &rate->value)

            || !history_amount(nominal, 0, &nominal_value)
            || nominal_value < 1 || rate->value < 0) {
            rate->nominal = 0;
        } else rate->nominal = (int32_t)nominal_value;
        if (name) { app_text_copy(rate->name, sizeof rate->name, name); }
        if (id) { app_text_copy(rate->id, sizeof rate->id, id); }
    }
    const uint8_t *a = (const uint8_t *)&state, *b = (const uint8_t *)&incoming;
    bool changed = false;
    for (int32_t i = 0; i < (int32_t)sizeof state; i++) { changed |= a[i] != b[i]; }
    state = incoming;
    if (changed) { app_state_save("state", &state, sizeof state); }
    return true;
}

static const rate_t *rate_for(int32_t index)
{
    char code[64];
    app_setting_item("codes", index, code, sizeof code);
    if (!valid_code(code)) return 0;
    for (int32_t slot = 0; slot < CURRENCIES_MAX; ++slot)
        if (app_text_equal(state.rates[slot].code, code)) return &state.rates[slot];
    return 0;
}

static void read_history(int32_t index, cached_history_t *record)
{
    if (file_read(HISTORY_FILE, HISTORY_FILE_LENGTH, index * (int32_t)sizeof *record,
                  record, sizeof *record) != sizeof *record || record->version != HISTORY_VERSION ||
        record->history.count < 1 || record->history.count > HISTORY_POINTS) {
        *record = (cached_history_t){0};
    }
    record->code[sizeof record->code - 1] = 0;
    record->history.id[sizeof record->history.id - 1] = 0;
}

static bool write_history(int32_t index, const cached_history_t *record)
{
    int32_t offset = index * (int32_t)sizeof *record;
    int32_t size = file_size(HISTORY_FILE, HISTORY_FILE_LENGTH);
    if (size < 0) size = 0;
    static const uint8_t zeros[sizeof(cached_history_t)] = {0};
    while (size < offset) {
        int32_t length = offset - size;
        if (length > (int32_t)sizeof zeros) length = sizeof zeros;
        if (file_write(HISTORY_FILE, HISTORY_FILE_LENGTH, size, zeros, length) != HOST_OK) return false;
        size += length;
    }
    return file_write(HISTORY_FILE, HISTORY_FILE_LENGTH, offset, record, sizeof *record) == HOST_OK;
}

static void load_chart(void)
{
    read_history(navigation.index, &chart);
    const rate_t *rate = rate_for(navigation.index);
    if (!rate || !app_text_equal(rate->code, chart.code) || !app_text_equal(rate->id, chart.history.id))
        chart = (cached_history_t){0};
}

static void append_date(app_text_t *text, int32_t day)
{
    int32_t year, month, date;
    app_date_from_days(day, &year, &month, &date);
    app_text_add_number(text, date);
    app_text_add(text, "/");
    app_text_add_number(text, month);
    app_text_add(text, "/");
    app_text_add_number(text, year);
}

static bool safe_id(const char *id)
{
    int32_t length = app_text_length(id);
    if (length < 2 || length >= HISTORY_ID_BYTES || id[0] != 'R') return false;
    for (int32_t index = 1; index < length; ++index)
        if ((id[index] < '0' || id[index] > '9') && (id[index] < 'A' || id[index] > 'Z')) return false;
    return true;
}

static void request_histories(void)
{
    int32_t day = history_day(state.date, true);
    if (!day) return;
    for (int32_t index = 0; index < currency_count(); ++index) {
        const rate_t *rate = rate_for(index);
        if (!rate || !rate->nominal || !safe_id(rate->id)) continue;
        cached_history_t previous;
        read_history(index, &previous);
        if (app_text_equal(previous.code, rate->code) && app_text_equal(previous.history.id, rate->id) &&
            previous.history.count && previous.history.as_of >= day) continue;
        char url[192];
        app_text_t text;
        app_text_start(&text, url, sizeof url);
        app_text_add(&text, "https://www.cbr.ru/scripts/XML_dynamic.asp?date_req1=");
        append_date(&text, day - HISTORY_DAYS + 1);
        app_text_add(&text, "&date_req2=");
        append_date(&text, day);
        app_text_add(&text, "&VAL_NM_RQ=");
        app_text_add(&text, rate->id);
        http_send(HOST_REQUEST_FIRST + index, HOST_HTTP_GET, url, app_text_length(url), "", 0, "", 0);
    }
}

static void update_header(void)
{
    char buffer[32];
    app_text_t text;
    app_text_start(&text, buffer, sizeof buffer);
    app_text_add_number(&text, navigation.index + 1);
    app_text_add(&text, "/");
    app_text_add_number(&text, currency_count());
    if (navigation.rotating) { app_text_add(&text, " "); app_text_add(&text, app_string(STR_AUTO)); }
    header_set_text(HOST_HEADER_LEFT, buffer, app_text_length(buffer));
}

static void update_rotation(void)
{
    char setting[16];
    app_setting("rotate", setting, sizeof setting);
    int32_t minutes = app_text_equal(setting, "10 min") ? 10 : app_text_equal(setting, "5 min") ? 5 :
                      app_text_equal(setting, "2 min") ? 2 : 1;
    timer_set(ROTATE_TIMER, navigation.rotating && currency_count() > 1 ? minutes * 60000 : 0);
}

static void select_currency(int32_t step)
{
    int32_t total = currency_count();
    if (!total) return;
    navigation.index = (navigation.index + step + total) % total;
    load_chart();
    update_header();
}

void app_on_load(void)
{
    if (!app_state_load("state", &state, sizeof state, SNAPSHOT_VERSION)) state = (state_t){ .version = SNAPSHOT_VERSION };
    if (kv_get("status", 6, &status, sizeof status) != sizeof status) status = 0;
    saved_status = status;
    if (!app_state_load("navigation", &navigation, sizeof navigation, 1)) navigation = (navigation_t){ .version = 1 };
    if (navigation.index < 0 || navigation.index >= currency_count()) navigation.index = 0;
}

void app_on_focus(void)
{
    if (!state.has_data) poll_run(0);
    load_chart();
    update_header();
    update_rotation();
}

void app_on_terminate(void)
{
    navigation_t stored;
    if (!app_state_load("navigation", &stored, sizeof stored, 1) || stored.index != navigation.index || stored.rotating != navigation.rotating)
        app_state_save("navigation", &navigation, sizeof navigation);
}

void app_on_settings(void)
{
    reading = history_reading = false;
    if (navigation.index >= currency_count()) navigation.index = 0;
    load_chart();
    update_header();
    update_rotation();
    request_histories();
    screen_redraw(HOST_REDRAW_NORMAL);
}

void app_on_button(int32_t event)
{
    if (event == BTN_UP_PRESSED) select_currency(-1);
    else if (event == BTN_DOWN_PRESSED) select_currency(1);
    else if (event == BTN_OK_PRESSED) {
        navigation.rotating = !navigation.rotating;
        app_state_save("navigation", &navigation, sizeof navigation);
        update_header();
    } else return;
    update_rotation();
    screen_redraw(HOST_REDRAW_NORMAL);
}

void app_on_timer(int32_t id)
{
    if (id != ROTATE_TIMER || !navigation.rotating) return;
    select_currency(1);
    update_rotation();
    screen_redraw(HOST_REDRAW_NORMAL);
}

static void history_answer(const uint8_t *data, int32_t length, int32_t flags, int32_t http_status)
{
    if (flags & HOST_DATA_FIRST) {
        history_start(&history_parser);
        history_reading = http_status >= 200 && http_status < 300;
    }
    if (flags & HOST_DATA_FAILED) history_reading = false;
    if (history_reading && length > 0)
        history_reading = history_feed(&history_parser, data, (size_t)length);
    if ((flags & HOST_DATA_LAST) && history_reading && history_finish(&history_parser)) {
        const history_t *fresh = &history_parser.result;
        int32_t current_day = history_day(state.date, true);
        for (int32_t index = 0; index < currency_count(); ++index) {
            const rate_t *rate = rate_for(index);
            if (!rate || !app_text_equal(rate->id, fresh->id) || fresh->as_of > current_day) continue;
            cached_history_t previous;
            read_history(index, &previous);
            bool same_currency = app_text_equal(previous.code, rate->code) && app_text_equal(previous.history.id, fresh->id);
            if (same_currency && previous.history.count &&
                previous.history.as_of > fresh->as_of) continue;
            cached_history_t record = { .version = HISTORY_VERSION, .history = *fresh };
            app_text_copy(record.code, sizeof record.code, rate->code);
            bool changed = false;
            for (size_t byte = 0; byte < sizeof record; ++byte)
                changed |= ((const uint8_t *)&previous)[byte] != ((const uint8_t *)&record)[byte];
            if (changed && !write_history(index, &record)) continue;
            if (index == navigation.index) { chart = record; screen_redraw(HOST_REDRAW_NORMAL); }
        }
    }
    if (flags & (HOST_DATA_LAST | HOST_DATA_FAILED)) history_reading = false;
}

void app_on_data(int32_t request, const uint8_t *data, int32_t length, int32_t flags, int32_t http_status)
{
    if (request >= HOST_REQUEST_FIRST && request < HOST_REQUEST_FIRST + CURRENCIES_MAX) {
        history_answer(data, length, flags, http_status);
        return;
    }
    if (request != 0) return;
    if (flags & HOST_DATA_FAILED) {
        status = http_status;
        reading = false;
    } else {
        if (flags & HOST_DATA_FIRST) { status = http_status; begin_reading(); }
        if (reading && status >= 200 && status < 300) {
            for (int32_t group = 0; group < PICKERS; ++group)
                json_pick_feed(&pickers[group], (const char *)data, length);
            if (flags & HOST_DATA_LAST) {
                if (!take_response()) status = HOST_FAILED_NO_ANSWER;
                else { load_chart(); request_histories(); }
            }
        }
    }
    if (flags & (HOST_DATA_LAST | HOST_DATA_FAILED)) {
        reading = false;
        if (status != saved_status && kv_set("status", 6, &status, sizeof status) == HOST_OK) saved_status = status;
        screen_redraw(HOST_REDRAW_NORMAL);
    }
}

static void draw_centered(int32_t center, int32_t y, int32_t font, const char *text)
{
    int32_t length = app_text_length(text);
    gfx_draw_text(center - gfx_text_width(font, text, length) / 2, y, font, text, length);
}

void app_on_draw(void)
{
    int32_t width = gfx_width(), height = gfx_height();
    if (!currency_count()) { ui_draw_reason(HOST_FAILED_NOT_READY); return; }
    const rate_t *rate = rate_for(navigation.index);
    if (!rate || rate->nominal < 1) {
        if (!ui_draw_reason(status)) draw_centered(width / 2, height / 2, HOST_FONT_TEXT, app_string(STR_NO_DATA));
        return;
    }
    char title[96], fitted[96], price[32];
    app_text_t text;
    bool named = app_string(STR_CURRENCY_NAME)[0] && rate->name[0];
    app_text_start(&text, fitted, sizeof fitted);
    if (named) { app_text_add(&text, rate->name); app_text_add(&text, " ("); }
    app_text_add(&text, rate->code);
    app_text_add(&text, named ? ")" : " / RUB");
    int32_t length = gfx_fit_text(HOST_FONT_TEXT, fitted, app_text_length(fitted), width - 2 * HOST_UI_MARGIN, title, sizeof title);
    gfx_draw_text(HOST_UI_MARGIN, HOST_UI_MARGIN, HOST_FONT_TEXT, title, length);
    app_text_start(&text, price, sizeof price);
    int32_t normalized = (int32_t)(rate->value / rate->nominal);
    app_text_add_number(&text, normalized / 10000);
    char fraction[6] = { '.', 0, 0, 0, 0, 0 };
    for (int32_t index = 4; index > 0; --index) {
        fraction[index] = (char)('0' + normalized % 10);
        normalized /= 10;
    }
    app_text_add(&text, fraction);
    int32_t row = HOST_UI_MARGIN + gfx_font_height(HOST_FONT_TEXT) + 4;
    ui_draw_value(HOST_UI_MARGIN, row, HOST_FONT_MEDIUM, price, app_text_length(price), "RUB", 3);
    int32_t chart_top = row + gfx_font_height(HOST_FONT_MEDIUM) + 8;
    int32_t caption_height = gfx_font_height(HOST_FONT_SMALL);
    if (chart.history.count > 1) {
        host_chart_t plot = {
            .x = HOST_UI_MARGIN, .y = chart_top, .width = width - 2 * HOST_UI_MARGIN,
            .height = height - chart_top - caption_height - 8,
            .kind = HOST_CHART_AREA, .flags = HOST_CHART_RANGE_RIGHT | HOST_CHART_MARK_LAST,
            .decimals = 4, .count = chart.history.count
        };
        ui_draw_chart(&plot, chart.history.values);
    } else draw_centered(width / 2, chart_top + 30, HOST_FONT_SMALL, app_string(STR_NO_HISTORY));
    char caption[64];
    app_text_start(&text, caption, sizeof caption);
    app_text_add(&text, app_string(STR_PERIOD));
    app_text_add(&text, " · ");
    append_date(&text, chart.history.count ? chart.history.days[chart.history.count - 1] : history_day(state.date, true));
    draw_centered(width / 2, height - caption_height - 4, HOST_FONT_SMALL, caption);
}
