/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * Stocks: the price of a few tickers, one per screen, from the Yahoo
 * Finance chart API - New York, Tokyo, Frankfurt, indices, crypto - and
 * from the Moscow Exchange's own (ISS), which Yahoo no longer has. Neither
 * asks for a key or an account.
 *
 * Data: one list of tickers ("tickers"), the exchange said by a prefix as
 * TradingView writes it - "AAPL" and "7203.T" are Yahoo's, "MOEX:SBER" the
 * Moscow Exchange's. Each poll of the
 * manifest goes over the list ("each") and takes the items of its own
 * prefix ("prefix"), the one of Yahoo the rest; the host picks the values
 * from every answer and calls app_on_picked() once per ticker. Each answer
 * is written to the file "quotes" as one fixed-size record at the ticker's
 * position in the list, so the application can be unloaded at any time and
 * draw again from the file. The screens are the list, in its order.
 *
 * The Moscow chart is the trading day's candles, an address that names the
 * day: the quote brings the exchange's date, kept under "day", and the
 * candles' poll puts it into its address as {{day}}.
 *
 * Buttons: UP and DOWN switch tickers; OK starts and stops the rotation
 * (a timer, on the screen only, at the "rotate" interval).
 */
#include "app_days.h"
#include "app_fixed.h"
#include "app_state.h"
#include "app_text.h"
#include "app_values.h"
#include "host_api.h"
#include "strings.h" /* made of the `strings` folder by tools/perinkpheral */

#define MARGIN HOST_UI_MARGIN

/* The manifest's polls. */
enum { POLL_YAHOO, POLL_MOEX, POLL_MOEX_DAY };

/* Positions of the paths in the "pick" of POLL_YAHOO. */
enum {
    PICK_LONG_NAME,
    PICK_SHORT_NAME,
    PICK_SYMBOL,
    PICK_PRICE,
    PICK_PREVIOUS,
    PICK_PRICE_HINT,
    PICK_TIME,
    PICK_CURRENCY,
    PICK_CLOSE,
};

/* Of POLL_MOEX: a row of the security and one of its market data. */
enum {
    MOEX_SECID,
    MOEX_NAME,
    MOEX_PREVIOUS,
    MOEX_DECIMALS,
    MOEX_CURRENCY,
    MOEX_LAST,
    MOEX_UPDATED, /* the time of the last trade, Moscow's */
    MOEX_NOW,     /* the exchange's date and time */
};

/* Of POLL_MOEX_DAY: the closes of the day's candles. */
enum { MOEX_CLOSE };


#define TICKERS "tickers"
static const char QUOTES_FILE[] = "quotes";
#define QUOTES_FILE_LENGTH ((int32_t)sizeof QUOTES_FILE - 1)

#define MOSCOW_OFFSET_S (3 * 3600) /* Moscow keeps UTC+3 the year round */

#define TICKERS_MAX     16
#define DECIMALS_MAX    4
#define ROTATE_TIMER    0
#define ROTATE_DEFAULT  60
#define QUOTE_VERSION   3
#define STATE_KEY       "state"
#define STATE_VERSION   1
/* What an exchange that has no such ticker comes to: it answers with an
 * empty table, and the screen says "Not found" as for an HTTP 404. */
#define STATUS_NOT_FOUND 404
/* The Moscow day the candles are of. */
#define MOEX_DAY_KEY "day"
#define MOEX_DATE_LENGTH 10 /* "2026-11-15" */

/* One ticker as last received. `item` is the list entry it was asked for,
 * so a record left over from a reordered list is not shown for another
 * ticker. */
typedef struct {
    int32_t version;
    int32_t status;   /* last HTTP status or HOST_FAILED_* code; 0: none yet */
    int32_t has_data;
    int32_t decimals;
    int64_t price;    /* fixed point with `decimals` */
    int64_t previous;
    int64_t time;     /* regularMarketTime, Unix seconds */
    int32_t count;
    char    item[64];
    char    symbol[24];
    char    name[64];
    char    currency[8];
    int32_t series[HOST_CHART_VALUES_MAX];
} quote_t;

typedef struct {
    int32_t version;
    int32_t index;
    int32_t rotating;
} state_t;

static state_t state;
static state_t saved;
static quote_t quote;   /* the record of state.index */
static int32_t loaded = -1;

static int32_t ticker_count(void)
{
    int32_t count = app_setting_count(TICKERS);
    return count > TICKERS_MAX ? TICKERS_MAX : count;
}

static bool is_success(int32_t http_status)
{
    return http_status >= 200 && http_status < 300;
}

static void read_quote(int32_t index, quote_t *out)
{
    int32_t length = file_read(QUOTES_FILE, QUOTES_FILE_LENGTH, index * (int32_t)sizeof *out, out,
                               sizeof *out);
    if (length != (int32_t)sizeof *out || out->version != QUOTE_VERSION) {
        *out = (quote_t){ .version = QUOTE_VERSION };
    }
}

/* Records are written in place, so the file is first extended with empty
 * ones up to the record being written. */
static bool write_quote(int32_t index, const quote_t *record)
{
    int32_t size   = file_size(QUOTES_FILE, QUOTES_FILE_LENGTH);
    int32_t offset = index * (int32_t)sizeof *record;
    if (size < 0) {
        size = 0;
    }
    size -= size % (int32_t)sizeof *record;
    while (size < offset) {
        static const quote_t empty;
        if (file_write(QUOTES_FILE, QUOTES_FILE_LENGTH, size, &empty, sizeof empty) != HOST_OK) { return false; }
        size += (int32_t)sizeof empty;
    }
    return file_write(QUOTES_FILE, QUOTES_FILE_LENGTH, offset, record, sizeof *record) == HOST_OK;
}

/* The record of the ticker on the screen, empty if it belongs to another. */
static void load_current(void)
{
    char item[64];
    read_quote(state.index, &quote);
    if (app_setting_item(TICKERS, state.index, item, sizeof item) == 0 || !app_text_equal(item, quote.item)) {
        quote = (quote_t){ .version = QUOTE_VERSION };
    }
    loaded = state.index;
}

static void update_rotation(void)
{
    timer_set(ROTATE_TIMER, state.rotating && ticker_count() > 1 ? app_setting_seconds("rotate", ROTATE_DEFAULT) * 1000 : 0);
}

static void update_header(void)
{
    char       buffer[32];
    app_text_t text;
    int32_t    count = ticker_count();
    app_text_start(&text, buffer, sizeof buffer);
    if (count > 1) {
        app_text_add_number(&text, state.index + 1);
        app_text_add(&text, "/");
        app_text_add_number(&text, count);
    }
    if (state.rotating) {
        app_text_add(&text, count > 1 ? " " : "");
        app_text_add(&text, app_string(STR_AUTO));
    }
    if (app_text_length(buffer) == 0) {
        app_text_add(&text, " ");
    }
    header_set_text(HOST_HEADER_LEFT, buffer, app_text_length(buffer));
}

static void select_ticker(int32_t step)
{
    int32_t count = ticker_count();
    if (count == 0) {
        return;
    }
    state.index = (state.index + step + count) % count;
    load_current();
    update_header();
}

/* ---- Handlers ---- */

void app_on_load(void)
{
    if (!app_state_load(STATE_KEY, &state, sizeof state, STATE_VERSION)) {
        state = (state_t){ .version = STATE_VERSION };
    }
    saved = state;
    if (state.index >= ticker_count()) {
        state.index = 0;
    }
}

void app_on_terminate(void)
{
    if (state.index != saved.index || state.rotating != saved.rotating) {
        app_state_save(STATE_KEY, &state, sizeof state);
    }
}

void app_on_focus(void)
{
    load_current();
    update_header();
    update_rotation();
}

void app_on_settings(void)
{
    if (state.index >= ticker_count()) {
        state.index = 0;
    }
    load_current();
    update_header();
    update_rotation();
    screen_redraw(HOST_REDRAW_NORMAL);
}

void app_on_button(int32_t event)
{
    switch (event) {
    case BTN_UP_PRESSED:
        select_ticker(-1);
        break;
    case BTN_DOWN_PRESSED:
        select_ticker(1);
        break;
    case BTN_OK_PRESSED:
        /* The user's choice is kept at once, so a power loss does not undo it. */
        state.rotating = !state.rotating;
        app_state_save(STATE_KEY, &state, sizeof state);
        saved = state;
        update_header();
        break;
    default:
        return;
    }
    update_rotation();
    screen_redraw(HOST_REDRAW_NORMAL);
}

void app_on_timer(int32_t id)
{
    if (id != ROTATE_TIMER) {
        return;
    }
    select_ticker(1);
    update_rotation();
    screen_redraw(HOST_REDRAW_NORMAL);
}

/* The record of an item of the list, as it is kept, or a new one when the
 * one kept is another ticker's - the list was changed. False when there is
 * no such item. `had_data`: whether the screen had something for it. */
static bool record_for(int32_t item, quote_t *record, int32_t *had_data)
{
    char asked[64];
    if (app_setting_item(TICKERS, item, asked, sizeof asked) == 0) {
        return false;
    }
    read_quote(item, record);
    *had_data = record->has_data;
    if (!app_text_equal(asked, record->item)) {
        *record = (quote_t){ .version = QUOTE_VERSION };
        app_text_copy(record->item, sizeof record->item, asked);
    }
    return true;
}

static bool same_quote(const quote_t *first, const quote_t *second)
{
    const uint8_t *left = (const uint8_t *)first, *right = (const uint8_t *)second;
    for (int32_t byte = 0; byte < (int32_t)sizeof *first; byte++) {
        if (left[byte] != right[byte]) { return false; }
    }
    return true;
}

/* Stored and visible quotes may differ after a failed write. Compare each
 * separately so a return to the stored price still updates the screen. */
static void keep_quote(int32_t item, const quote_t *record, const quote_t *before, int32_t had_data)
{
    if (!same_quote(record, before)) { write_quote(item, record); }
    if (item == loaded && !same_quote(record, &quote)) {
        quote = *record;
        screen_redraw(had_data ? HOST_REDRAW_NORMAL : HOST_REDRAW_SOON);
    }
}

static void yahoo_values(int32_t item, int32_t http_status)
{
    quote_t record;
    int32_t had_data;
    if (!record_for(item, &record, &had_data)) {
        return;
    }
    quote_t before = record;
    int32_t hint = 2;
    int64_t price, previous;
    if (is_success(http_status)) {
        if (!app_value_number(PICK_PRICE_HINT, 0, &hint) || hint < 0 || hint > DECIMALS_MAX) {
            hint = 2;
        }
        /* The price is what is shown; the close before it is kept when there is one. */
        if (app_value_fixed(PICK_PRICE, hint, &price)) {
            record.has_data = 1;
            record.decimals = hint;
            record.price    = price;
            record.previous = app_value_fixed(PICK_PREVIOUS, hint, &previous) ? previous : price;
            if (!app_value_fixed(PICK_TIME, 0, &record.time)) {
                record.time = 0;
            }
            if (!app_value_text(PICK_LONG_NAME, record.name, sizeof record.name)
                && !app_value_text(PICK_SHORT_NAME, record.name, sizeof record.name)) {
                record.name[0] = 0;
            }
            if (!app_value_text(PICK_SYMBOL, record.symbol, sizeof record.symbol)) {
                app_text_copy(record.symbol, sizeof record.symbol, record.item);
            }
            if (!app_value_text(PICK_CURRENCY, record.currency, sizeof record.currency)) {
                record.currency[0] = 0;
            }
            record.count = app_value_series(PICK_CLOSE, hint, record.series, HOST_CHART_VALUES_MAX);
        }
    }
    record.status = http_status;
    keep_quote(item, &record, &before, had_data);
}

/* "2026-11-15" and "16:52:00", Moscow's, as Unix time. */
static int32_t digits_at(const char *text, int32_t count)
{
    int32_t number = 0;
    for (int32_t i = 0; i < count; i++) {
        number = number * 10 + (text[i] - '0');
    }
    return number;
}

static int64_t moscow_time(const char *date, const char *clock)
{
    int64_t days = app_days_from_date(digits_at(date, 4), digits_at(date + 5, 2), digits_at(date + 8, 2));
    int64_t clock_s = digits_at(clock, 2) * 3600 + digits_at(clock + 3, 2) * 60 + digits_at(clock + 6, 2);
    return days * 86400 + clock_s - MOSCOW_OFFSET_S;
}

static void moex_quote(int32_t item, int32_t http_status)
{
    quote_t record;
    int32_t had_data;
    if (!record_for(item, &record, &had_data)) {
        return;
    }
    quote_t before = record;
    if (is_success(http_status)) {
        int32_t decimals = 2;
        int64_t price, previous;
        char    now[24], updated[12];
        if (!app_value_number(MOEX_DECIMALS, 0, &decimals) || decimals < 0 || decimals > DECIMALS_MAX) {
            decimals = 2;
        }
        bool has_previous = app_value_fixed(MOEX_PREVIOUS, decimals, &previous);
        /* Before the first trade of the day the price is yesterday's close. */
        bool has_price = app_value_fixed(MOEX_LAST, decimals, &price);
        if (!has_price && has_previous) {
            price     = previous;
            has_price = true;
        }
        if (!app_value_text(MOEX_SECID, record.symbol, sizeof record.symbol)) {
            /* The exchange answers a ticker it does not have with empty tables. */
            record.has_data = 0;
            http_status     = STATUS_NOT_FOUND;
        } else if (has_price) {
            record.has_data = 1;
            record.decimals = decimals;
            record.price    = price;
            record.previous = has_previous ? previous : price;
            if (!app_value_text(MOEX_NAME, record.name, sizeof record.name)) {
                record.name[0] = 0;
            }
            if (!app_value_text(MOEX_CURRENCY, record.currency, sizeof record.currency)
                || app_text_equal(record.currency, "SUR")) {
                app_text_copy(record.currency, sizeof record.currency, "RUB"); /* the exchange's old code */
            }
            record.time = 0;
            /* "2026-11-15 17:07:01" and "16:52:00"; anything else is not trusted to be a moment. */
            if (app_value_text(MOEX_NOW, now, sizeof now) && app_text_length(now) == 19) {
                bool has_updated = app_value_text(MOEX_UPDATED, updated, sizeof updated)
                                   && app_text_length(updated) == 8;
                record.time      = moscow_time(now, has_updated ? updated : now + 11);
                /* The chart is of the exchange's day: a new day, or a ticker without one, asks for it. */
                char day[MOEX_DATE_LENGTH + 2];
                bool is_new_day = app_setting(MOEX_DAY_KEY, day, sizeof day) != MOEX_DATE_LENGTH ||
                                  !app_text_starts(now, day);
                if (is_new_day) {
                    app_state_save(MOEX_DAY_KEY, now, MOEX_DATE_LENGTH);
                }
                if (is_new_day || record.count == 0) {
                    poll_run(POLL_MOEX_DAY);
                }
            }
        }
    }
    record.status = http_status;
    keep_quote(item, &record, &before, had_data);
}

static void moex_day(int32_t item, int32_t http_status)
{
    quote_t record;
    char    asked[64];
    if (!is_success(http_status) || app_setting_item(TICKERS, item, asked, sizeof asked) == 0) {
        return;
    }
    read_quote(item, &record);
    /* The closes are numbers of the quote's decimals: without a quote they wait for the next. */
    if (!record.has_data || !app_text_equal(asked, record.item)) {
        return;
    }
    quote_t before = record;
    record.count = app_value_series(MOEX_CLOSE, record.decimals, record.series, HOST_CHART_VALUES_MAX);
    keep_quote(item, &record, &before, 1);
}

void app_on_picked(int32_t request, int32_t http_status)
{
    int32_t item = answer_item();
    if (item < 0 || item >= TICKERS_MAX) {
        return;
    }
    switch (request) {
    case POLL_YAHOO: yahoo_values(item, http_status); break;
    case POLL_MOEX: moex_quote(item, http_status); break;
    case POLL_MOEX_DAY: moex_day(item, http_status); break;
    default: break;
    }
}

/* ---- Drawing ---- */

static void draw_notice(const char *first, const char *second)
{
    host_notice_t notice = { .icon = HOST_ICON_WARNING, .is_working = 0 };
    app_text_copy(notice.first, sizeof notice.first, first);
    app_text_copy(notice.second, sizeof notice.second, second);
    ui_draw_notice(&notice);
}

static void draw_title(int32_t top, int32_t right)
{
    char       suffix_buffer[32];
    char       fitted[64];
    app_text_t suffix;
    int32_t    name_length = app_text_length(quote.name);

    app_text_start(&suffix, suffix_buffer, sizeof suffix_buffer);
    if (name_length > 0) {
        app_text_add(&suffix, " (");
        app_text_add(&suffix, quote.symbol);
        app_text_add(&suffix, ")");
    } else {
        app_text_add(&suffix, quote.symbol);
    }

    int32_t suffix_length = app_text_length(suffix_buffer);
    int32_t suffix_width  = gfx_text_width(HOST_FONT_TEXT, suffix_buffer, suffix_length);
    int32_t x             = MARGIN;
    if (name_length > 0) {
        int32_t length = gfx_fit_text(HOST_FONT_TEXT, quote.name, name_length,
                                      right - x - suffix_width, fitted, sizeof fitted);
        x += gfx_draw_text(x, top, HOST_FONT_TEXT, fitted, length);
    }
    int32_t length = gfx_fit_text(HOST_FONT_TEXT, suffix_buffer, suffix_length, right - x, fitted,
                                  sizeof fitted);
    gfx_draw_text(x, top, HOST_FONT_TEXT, fitted, length);
}

/* "16:00" today, "Fri 14 Nov 16:00" another day. */
static int32_t when_text(int64_t moment, char *text, int32_t capacity)
{
    host_local_time_t then, now;
    int32_t           length = 0;
    if (time_local_at(moment, &then) != HOST_OK || time_local(&now) != HOST_OK
        || then.year != now.year || then.month != now.month || then.day != now.day) {
        length = time_text(moment, HOST_TIME_DATE, text, capacity - 1);
        text[length++] = ' ';
    }
    return length + time_text(moment, HOST_TIME_CLOCK, text + length, capacity - length);
}

void app_on_draw(void)
{
    int32_t width  = gfx_width();
    int32_t height = gfx_height();

    if (ticker_count() == 0) {
        ui_draw_reason(HOST_FAILED_NOT_READY);
        return;
    }
    if (loaded != state.index) {
        load_current();
    }
    if (!quote.has_data) {
        if (quote.status == STATUS_NOT_FOUND) {
            draw_notice(app_string(STR_NOT_FOUND), quote.item);
        } else if (!ui_draw_reason(quote.status)) {
            draw_notice(app_string(STR_NO_DATA), quote.item);
        }
        return;
    }

    /* Row 1: name (TICKER), the whole width. */
    draw_title(MARGIN, width - MARGIN);

    /* Row 2: the price and its currency, on the left. */
    char       price_buffer[32];
    app_text_t text;
    app_text_start(&text, price_buffer, sizeof price_buffer);
    app_text_add_fixed(&text, quote.price, quote.decimals, 0);
    int32_t row = MARGIN + gfx_font_height(HOST_FONT_TEXT) + 4;
    ui_draw_value(MARGIN, row, HOST_FONT_MEDIUM, price_buffer, app_text_length(price_buffer), quote.currency,
             app_text_length(quote.currency));
    row += gfx_font_height(HOST_FONT_MEDIUM);

    /* The chart of the day, and when the last price was set. */
    int32_t caption_height = gfx_font_height(HOST_FONT_SMALL);
    int32_t chart_top      = row + 8;
    if (quote.count > 1) {
        host_chart_t chart = {
            .x        = MARGIN,
            .y        = chart_top,
            .width    = width - 2 * MARGIN,
            .height   = height - chart_top - caption_height - 8,
            .kind     = HOST_CHART_AREA,
            .flags    = HOST_CHART_RANGE_RIGHT | HOST_CHART_MARK_LAST,
            .decimals = quote.decimals,
            .count    = quote.count,
        };
        ui_draw_chart(&chart, quote.series);
    }
    /* When the last price was set, in the middle under the chart. */
    if (quote.time > 0) {
        char    when[32];
        int32_t length = when_text(quote.time, when, sizeof when);
        int32_t when_width = gfx_text_width(HOST_FONT_SMALL, when, length);
        gfx_draw_text((width - when_width) / 2, height - caption_height - 4, HOST_FONT_SMALL, when, length);
    }
}
