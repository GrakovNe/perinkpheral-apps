// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
#include <string.h>
/* The stand-in host counts no file reads: the application's go through a counting one here. */
#define file_read counted_file_read
#include "../stocks.c"
#undef file_read
#include "standin.h"

int32_t file_read(const char *name, int32_t name_length, int32_t offset, void *bytes, int32_t capacity);

static const char *values[] = { "Apple", "Apple", "AAPL", "123.45", "122.00", "2", "1794955920", "USD" };
static int reads;

int32_t counted_file_read(const char *name, int32_t name_length, int32_t offset, void *bytes, int32_t capacity)
{
    reads++;
    return file_read(name, name_length, offset, bytes, capacity);
}

static int writes(void) { return standin()->file_writes; }
static int redraws(void) { return standin()->redraws; }

static int32_t quotes_size(void)
{
    int32_t size = 0;
    return standin_file("quotes", &size) != NULL ? size : 0;
}

static void fail_writes(bool fails)
{
    standin()->refuse_writes = fails ? HOST_ERR_NO_SPACE : HOST_OK;
}

/* An answer of item 0 through the SDK's picker and app_on_picked(); no write is refused by the limits. */
static void answer(int32_t poll, int32_t status, const char *body)
{
    standin_picked(poll, 0, status, body, body != NULL ? (int32_t)strlen(body) : 0);
    assert(standin()->refused_what[0] == '\0');
}

/* Yahoo's answer with `values` in the manifest's pick order, and two closes. */
static void yahoo_answer(int32_t status)
{
    char body[512];
    snprintf(body, sizeof body,
        "{\"chart\":{\"result\":[{\"meta\":{\"longName\":\"%s\",\"shortName\":\"%s\",\"symbol\":\"%s\","
        "\"regularMarketPrice\":%s,\"chartPreviousClose\":%s,\"priceHint\":%s,\"regularMarketTime\":%s,"
        "\"currency\":\"%s\"},\"indicators\":{\"quote\":[{\"close\":[122.00,123.45]}]}}]}}",
        values[0], values[1], values[2], values[3], values[4], values[5], values[6], values[7]);
    answer(POLL_YAHOO, status, status == 200 ? body : NULL);
}

static void moex_quote_answer(void)
{
    answer(POLL_MOEX, 200,
        "{\"securities\":{\"data\":[[\"SBER\",\"Sberbank\",200.00,2,\"SUR\"]]},"
        "\"marketdata\":{\"data\":[[205.00,\"15:30:00\",\"2026-11-18 15:31:00\"]]}}");
}

static void moex_day_answer(void)
{
    answer(POLL_MOEX_DAY, 200, "{\"candles\":{\"data\":[[122.00],[123.45]]}}");
}

int main(void)
{
    standin_reset();
    standin_set("tickers", "AAPL");
    standin_handlers.on_picked = app_on_picked;
    loaded = 0;
    yahoo_answer(200);
    assert(writes() == 1 && redraws() == 1 && reads == 1 && quote.price == 12345);
    for (int poll = 0; poll < 100; poll++) yahoo_answer(200);
    assert(writes() == 1 && redraws() == 1 && reads == 101);
    values[PICK_PRICE] = "124.50";
    yahoo_answer(200);
    assert(writes() == 2 && redraws() == 2 && quote.price == 12450);
    fail_writes(true);
    values[PICK_PRICE] = "125.00";
    yahoo_answer(200);
    assert(writes() == 2 && quote.price == 12500);
    int before_redraws = redraws();
    yahoo_answer(200);
    assert(writes() == 2 && quote.price == 12500 && redraws() == before_redraws);
    values[PICK_PRICE] = "124.50";
    yahoo_answer(200);
    assert(writes() == 2 && quote.price == 12450 && redraws() == before_redraws + 1);
    yahoo_answer(200);
    assert(writes() == 2 && redraws() == before_redraws + 1);
    values[PICK_PRICE] = "125.00";
    yahoo_answer(200);
    assert(writes() == 2 && quote.price == 12500 && redraws() == before_redraws + 2);
    fail_writes(false);
    yahoo_answer(200);
    assert(writes() == 3 && quote.price == 12500 && redraws() == before_redraws + 2);
    yahoo_answer(503);
    assert(writes() == 4 && quote.has_data && quote.price == 12500 && quote.status == 503);
    yahoo_answer(503);
    assert(writes() == 4);
    standin_set("tickers", "MOEX:SBER");
    moex_quote_answer();
    assert(quote.price == 20500 && !strcmp(quote.currency, "RUB") && quote.count == 0);
    int before_writes = writes();
    moex_quote_answer();
    assert(writes() == before_writes);
    moex_day_answer();
    assert(writes() == before_writes + 1 && quote.count == 2);
    moex_day_answer();
    assert(writes() == before_writes + 1);
    quote_t record = { .version = QUOTE_VERSION, .price = 300 };
    fail_writes(true);
    assert(!write_quote(3, &record) && quotes_size() == sizeof record);
    fail_writes(false);
    assert(write_quote(3, &record) && quotes_size() == 4 * sizeof record);
    puts("Stocks storage: 100 unchanged polls write nothing; changed quotes, full storage and retries passed");
}
