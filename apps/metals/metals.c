/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * Bank of Russia accounting prices of four precious metals, in rubles a
 * gram. The latest two published days give the prices and their changes;
 * the month gives its chart. One metal per screen; no records on a bank
 * holiday.
 *
 * The request names today's local date and the date 31 days before it.
 * XML arrives in pieces: only one tag and one Buy number are buffered.
 * A separate month collects the records; only LAST, a closed Metall and
 * complete days can replace the saved month. A failed or interrupted
 * answer leaves the last good data alone. Prices are integer kopecks.
 *
 * UP and DOWN change the metal in the session; OK asks again. The metal
 * setting supplies the initial metal. A repeating alarm asks every six
 * hours even while the application is off the screen.
 */
#include "app_days.h"
#include "app_draw.h"
#include "app_fixed.h"
#include "app_state.h"
#include "app_text.h"
#include "host_api.h"
#include "strings.h"

#define METALS        4
#define DAYS_MAX      31
#define STATE_VERSION 1
#define STATE_KEY     "month"
#define REQUEST       HOST_REQUEST_FIRST
#define ALARM         0
#define EVERY_SECONDS (6 * 3600)
#define DAY_SECONDS   86400
#define TEXT_MAX      48
#define MARGIN        HOST_UI_MARGIN

typedef struct {
    int32_t date; /* days since 1970 */
    int32_t prices[METALS]; /* two decimals; zero means missing while receiving */
} day_t;

typedef struct {
    int32_t version;
    int32_t count;
    day_t days[DAYS_MAX]; /* oldest first */
} month_t;

typedef struct {
    int32_t metal;
    int32_t configured;
} session_t;
APP_SESSION_FITS(session_t);
_Static_assert(sizeof(month_t) <= HOST_KV_VALUE_MAX, "the month fits one store value");

static month_t month;
static bool    month_unsaved; /* the store refused the last write of it */
static month_t incoming;
static session_t session;
static int32_t status;
static bool pending;

/* A scanner of this feed's ASCII tags, independent of the XML encoding.
 * Sell and the XML declaration carry nothing the application needs. */
typedef struct {
    char tag[128];
    char number[32];
    int32_t tag_length, number_length;
    int32_t date, metal, price;
    int32_t http_status;
    bool active, in_tag, in_buy, in_record, has_price;
    bool opened, closed, broken;
} scanner_t;
static scanner_t scan;

static const char *const CHOICES[METALS] = { "Gold", "Silver", "Platinum", "Palladium" };
static const int32_t NAMES[METALS] = { STR_GOLD, STR_SILVER, STR_PLATINUM, STR_PALLADIUM };

static bool space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/* Attribute names are matched at boundaries, not inside another name. */
static bool attribute(const char *tag, const char *name, char *out, int32_t capacity)
{
    int32_t name_length = app_text_length(name);
    for (int32_t at = 0; tag[at]; at++) {
        if (!space(tag[at]) || !app_text_starts(tag + at + 1, name)) {
            continue;
        }
        int32_t value = at + 1 + name_length;
        while (space(tag[value])) { value++; }
        if (tag[value++] != '=') { continue; }
        while (space(tag[value])) { value++; }
        char quote = tag[value++];
        if (quote != '"' && quote != '\'') { return false; }
        int32_t length = 0;
        while (tag[value] && tag[value] != quote) {
            if (length >= capacity - 1) { return false; }
            out[length++] = tag[value++];
        }
        out[length] = 0;
        return tag[value] == quote;
    }
    return false;
}

static int32_t digits(const char *text, int32_t count)
{
    int32_t result = 0;
    for (int32_t i = 0; i < count; i++) {
        if (text[i] < '0' || text[i] > '9') { return -1; }
        result = result * 10 + text[i] - '0';
    }
    return result;
}

static bool record_date(const char *text, int32_t *date)
{
    if (app_text_length(text) != 10 || text[2] != '.' || text[5] != '.') { return false; }
    int32_t day = digits(text, 2), mon = digits(text + 3, 2), year = digits(text + 6, 4);
    if (year < 1970 || year > 2100 || mon < 1 || mon > 12 || day < 1 || day > 31) { return false; }
    *date = app_days_from_date(year, mon, day);
    int32_t check_year, check_mon, check_day;
    app_date_from_days(*date, &check_year, &check_mon, &check_day);
    return year == check_year && mon == check_mon && day == check_day;
}

/* Keep the newest 31 dates, sorted even if a response is newest first.
 * A repeated record replaces its price without adding another day. */
static void add_price(void)
{
    int32_t at = 0;
    while (at < incoming.count && incoming.days[at].date < scan.date) { at++; }
    if (at == incoming.count || incoming.days[at].date != scan.date) {
        if (incoming.count == DAYS_MAX) {
            if (at == 0) { return; }
            for (int32_t i = 1; i < incoming.count; i++) { incoming.days[i - 1] = incoming.days[i]; }
            incoming.count--;
            at--;
        }
        for (int32_t i = incoming.count; i > at; i--) { incoming.days[i] = incoming.days[i - 1]; }
        incoming.days[at] = (day_t){ .date = scan.date };
        incoming.count++;
    }
    incoming.days[at].prices[scan.metal] = scan.price;
}

static void tag_finished(void)
{
    scan.tag[scan.tag_length] = 0;
    const char *tag = scan.tag;
    if (app_text_starts(tag, "Metall ") || app_text_equal(tag, "Metall")) {
        if (scan.opened || scan.closed) { scan.broken = true; }
        scan.opened = true;
    } else if (app_text_equal(tag, "/Metall")) {
        if (!scan.opened || scan.closed || scan.in_record) { scan.broken = true; }
        scan.closed = true;
    } else if (app_text_starts(tag, "Record ")) {
        char date[16], code[8];
        if (!scan.opened || scan.closed || scan.in_record ||
            !attribute(tag, "Date", date, sizeof date) || !record_date(date, &scan.date) ||
            !attribute(tag, "Code", code, sizeof code) || app_text_length(code) != 1 ||
            code[0] < '1' || code[0] > '4') {
            scan.broken = true;
            return;
        }
        scan.metal = code[0] - '1';
        scan.in_record = true;
        scan.has_price = false;
    } else if (app_text_equal(tag, "Buy")) {
        if (!scan.in_record || scan.in_buy || scan.has_price) { scan.broken = true; }
        scan.in_buy = true;
        scan.number_length = 0;
    } else if (app_text_equal(tag, "/Buy")) {
        int64_t price;
        scan.number[scan.number_length] = 0;
        if (!scan.in_buy || !app_fixed_parse(scan.number, 2, &price) || price <= 0 || price > INT32_MAX) {
            scan.broken = true;
            return;
        }
        scan.price = (int32_t)price;
        scan.has_price = true;
        scan.in_buy = false;
    } else if (app_text_equal(tag, "/Record")) {
        if (!scan.in_record || scan.in_buy || !scan.has_price) { scan.broken = true; return; }
        add_price();
        scan.in_record = false;
    } else if (scan.in_buy) {
        scan.broken = true;
    }
}

static void scan_bytes(const uint8_t *bytes, int32_t length)
{
    for (int32_t i = 0; i < length && !scan.broken; i++) {
        char c = (char)bytes[i];
        if (c == '<') {
            if (scan.in_tag) { scan.broken = true; }
            scan.in_tag = true;
            scan.tag_length = 0;
        } else if (scan.in_tag) {
            if (c == '>') {
                tag_finished();
                scan.in_tag = false;
            } else if (scan.tag_length < (int32_t)sizeof scan.tag - 1) {
                scan.tag[scan.tag_length++] = c;
            } else {
                scan.broken = true;
            }
        } else if (scan.in_buy) {
            if (scan.number_length < (int32_t)sizeof scan.number - 1) {
                scan.number[scan.number_length++] = c == ',' ? '.' : c;
            } else {
                scan.broken = true;
            }
        } else if (scan.closed && !space(c)) {
            scan.broken = true;
        }
    }
}

static bool complete(void)
{
    if (scan.broken || scan.in_tag || scan.in_buy || scan.in_record || !scan.closed || incoming.count == 0) {
        return false;
    }
    for (int32_t i = 0; i < incoming.count; i++) {
        for (int32_t metal = 0; metal < METALS; metal++) {
            if (incoming.days[i].prices[metal] <= 0) { return false; }
        }
    }
    return true;
}

static bool same_month(void)
{
    if (month.count != incoming.count) { return false; }
    for (int32_t i = 0; i < month.count; i++) {
        if (month.days[i].date != incoming.days[i].date) { return false; }
        for (int32_t metal = 0; metal < METALS; metal++) {
            if (month.days[i].prices[metal] != incoming.days[i].prices[metal]) { return false; }
        }
    }
    return true;
}

/* ---- Requests and handlers ---- */

static int32_t configured_metal(void)
{
    for (int32_t i = 0; i < METALS; i++) {
        if (app_setting_is("metal", CHOICES[i])) { return i; }
    }
    return 0;
}

static void update_header(void)
{
    char buffer[HOST_HEADER_TEXT_MAX + 1];
    app_text_t text;
    app_text_start(&text, buffer, sizeof buffer);
    app_text_add_number(&text, session.metal + 1);
    app_text_add(&text, "/4");
    header_set_text(HOST_HEADER_LEFT, buffer, app_text_length(buffer));
}

static void schedule(void)
{
    if (alarm_set_every(ALARM, EVERY_SECONDS, 0) != HOST_OK) {
        static const char message[] = "Could not schedule metals updates";
        log_write(HOST_LOG_WARN, message, sizeof message - 1);
    }
}

static void add_date(app_text_t *text, int32_t date)
{
    int32_t year, mon, day;
    app_date_from_days(date, &year, &mon, &day);
    app_text_add_padded(text, day, 2);
    app_text_add(text, "/");
    app_text_add_padded(text, mon, 2);
    app_text_add(text, "/");
    app_text_add_padded(text, year, 4);
}

static void ask(void)
{
    if (pending) { return; }
    host_local_time_t now;
    if (time_local(&now) != HOST_OK) {
        status = HOST_FAILED_NO_NETWORK; /* the clock comes from the network */
    } else {
        int32_t today = app_days_from_date(now.year, now.month, now.day);
        char url[160];
        app_text_t text;
        app_text_start(&text, url, sizeof url);
        app_text_add(&text, "https://www.cbr.ru/scripts/xml_metall.asp?date_req1=");
        add_date(&text, today - 31);
        app_text_add(&text, "&date_req2=");
        add_date(&text, today);
        int32_t result = http_send(REQUEST, HOST_HTTP_GET, url, app_text_length(url), 0, 0, 0, 0);
        pending = result == HOST_OK;
        status = pending ? 0 : result == HOST_ERR_NO_SPACE ? HOST_FAILED_NO_MEMORY : HOST_FAILED_NOT_READY;
    }
    if (month.count == 0) { screen_redraw(HOST_REDRAW_SOON); }
}

void app_on_load(void)
{
    if (!app_state_load(STATE_KEY, &month, sizeof month, STATE_VERSION) ||
        month.count < 0 || month.count > DAYS_MAX) {
        month = (month_t){ .version = STATE_VERSION };
    }
}

void app_on_installed(void) { schedule(); }
void app_on_updated(void) { schedule(); }

void app_on_intent(int32_t intent)
{
    if (intent == HOST_INTENT_STARTED) { schedule(); }
}

void app_on_alarm(int32_t id)
{
    if (id == ALARM) { ask(); }
}

void app_on_focus(void)
{
    int32_t configured = configured_metal();
    bool restored = app_session_load(&session, sizeof session);
    if (!restored || session.configured != configured ||
        session.metal < 0 || session.metal >= METALS) {
        session = (session_t){ .metal = configured, .configured = configured };
    }
    update_header();
    if (!restored) { ask(); }
}

void app_on_blur(void) { app_session_save(&session, sizeof session); }

void app_on_settings(void)
{
    int32_t configured = configured_metal();
    session = (session_t){ .metal = configured, .configured = configured };
    update_header();
    screen_redraw(HOST_REDRAW_NORMAL);
}

void app_on_button(int32_t event)
{
    switch (event) {
    case BTN_UP_PRESSED: session.metal = (session.metal + METALS - 1) % METALS; break;
    case BTN_DOWN_PRESSED: session.metal = (session.metal + 1) % METALS; break;
    case BTN_OK_PRESSED: ask(); break;
    default: return;
    }
    update_header();
    screen_redraw(HOST_REDRAW_NORMAL);
}

void app_on_data(int32_t request, const uint8_t *data, int32_t length, int32_t flags, int32_t http_status)
{
    if (request != REQUEST) { return; }
    if (flags & HOST_DATA_FAILED) {
        scan.active = false;
        incoming = (month_t){ .version = STATE_VERSION };
        pending = false;
        status = http_status;
        if (month.count == 0) { screen_redraw(HOST_REDRAW_SOON); }
        return;
    }
    if (flags & HOST_DATA_FIRST) {
        incoming = (month_t){ .version = STATE_VERSION };
        scan = (scanner_t){ .active = true, .http_status = http_status };
        pending = true;
    }
    if (!scan.active) { return; }
    if (scan.http_status >= 200 && scan.http_status < 300) { scan_bytes(data, length); }
    if (!(flags & HOST_DATA_LAST)) { return; }
    scan.active = false;
    pending = false;
    status = scan.http_status;
    bool had_data = month.count > 0;
    if (status >= 200 && status < 300) {
        if (!complete()) {
            status = HOST_FAILED_NO_ANSWER;
        } else {
            bool is_new = !same_month();
            /* A write the store refused is made again with the next answer, the same or not. */
            if (is_new || month_unsaved) { month_unsaved = !app_state_save(STATE_KEY, &incoming, sizeof incoming); }
            if (is_new) {
                month = incoming;
                screen_redraw(had_data ? HOST_REDRAW_NORMAL : HOST_REDRAW_SOON);
            }
        }
    }
    if (!had_data) { screen_redraw(HOST_REDRAW_SOON); }
}

/* ---- Drawing ---- */

/* The same formatting is used by the price and chart captions. */
static void price_text(char *out, int32_t capacity, int32_t price)
{
    char raw[32];
    app_text_t text;
    app_text_start(&text, raw, sizeof raw);
    app_text_add_fixed(&text, price, 2, 0);
    int32_t whole = app_text_length(raw) - 3;
    app_text_start(&text, out, capacity);
    for (int32_t i = 0; raw[i]; i++) {
        if (i > 0 && i < whole && (whole - i) % 3 == 0) { app_text_add(&text, " "); }
        char one[] = { raw[i] == '.' ? app_string(STR_DECIMAL_POINT)[0] : raw[i], 0 };
        app_text_add(&text, one);
    }
}

static void change_text(char *out, int32_t metal)
{
    if (month.count < 2) { app_text_copy(out, TEXT_MAX, "--"); return; }
    int64_t previous = month.days[month.count - 2].prices[metal];
    int64_t change = month.days[month.count - 1].prices[metal] - previous;
    int64_t percent = app_fixed_ratio(change * 100, previous, 1);
    app_text_t text;
    app_text_start(&text, out, TEXT_MAX);
    if (percent == 0) { app_text_add(&text, change < 0 ? "-" : "+"); }
    app_text_add_fixed(&text, percent, 1, APP_FIXED_SIGN);
    app_text_add(&text, " %");
    for (int32_t i = 0; out[i]; i++) {
        if (out[i] == '.') { out[i] = app_string(STR_DECIMAL_POINT)[0]; }
    }
}

void app_on_draw(void)
{
    if (month.count == 0) { ui_draw_reason(status); return; }
    int32_t width = gfx_width(), height = gfx_height();
    int32_t small = gfx_font_height(HOST_FONT_SMALL);
    int32_t metal = session.metal;

    /* The same name and price rows as stocks, then the signed daily change. */
    app_draw_text(MARGIN, MARGIN, HOST_FONT_TEXT, app_string(NAMES[metal]));
    char price[TEXT_MAX], change[TEXT_MAX];
    price_text(price, sizeof price, month.days[month.count - 1].prices[metal]);
    int32_t row = MARGIN + gfx_font_height(HOST_FONT_TEXT) + 4;
    const char *unit = app_string(STR_UNIT);
    ui_draw_value(MARGIN, row, HOST_FONT_MEDIUM, price, app_text_length(price), unit, app_text_length(unit));
    row += gfx_font_height(HOST_FONT_MEDIUM);
    change_text(change, metal);
    app_draw_text(MARGIN, row, HOST_FONT_SMALL, change);
    row += small;

    /* Local noon keeps the stored calendar date when formatted by Perinkpheral OS. */
    char date[48];
    int64_t moment = (int64_t)month.days[month.count - 1].date * DAY_SECONDS + 12 * 3600 - time_zone_offset();
    app_time_text(moment, HOST_TIME_LONG_DATE, date, sizeof date);
    app_draw_text(MARGIN, height - small - 4, HOST_FONT_SMALL, app_string(STR_SOURCE));
    app_draw_right(width - MARGIN, height - small - 4, HOST_FONT_SMALL, date);

    if (month.count < 2) { return; }
    int32_t series[DAYS_MAX], low = INT32_MAX, high = 0;
    for (int32_t i = 0; i < month.count; i++) {
        series[i] = month.days[i].prices[metal];
        low = series[i] < low ? series[i] : low;
        high = series[i] > high ? series[i] : high;
    }
    char least[TEXT_MAX], greatest[TEXT_MAX];
    price_text(least, sizeof least, low);
    price_text(greatest, sizeof greatest, high);
    int32_t captions = app_text_width(HOST_FONT_SMALL, least);
    int32_t high_width = app_text_width(HOST_FONT_SMALL, greatest);
    captions = (high_width > captions ? high_width : captions) + 6;
    int32_t top = row + 8, bottom = height - small - 8;
    /* Stocks' right-hand range captions, localized and grouped like the price. */
    int32_t ascent = gfx_font_ascent(HOST_FONT_SMALL);
    app_draw_right(width - MARGIN, top + gfx_font_cap_height(HOST_FONT_SMALL) - ascent,
                   HOST_FONT_SMALL, greatest);
    app_draw_right(width - MARGIN, bottom - 1 - ascent, HOST_FONT_SMALL, least);
    host_chart_t chart = { .x = MARGIN, .y = top,
                          .width = width - 2 * MARGIN - captions, .height = bottom - top,
                          .kind = HOST_CHART_AREA, .flags = HOST_CHART_MARK_LAST,
                          .decimals = 2, .count = month.count };
    ui_draw_chart(&chart, series);
}
