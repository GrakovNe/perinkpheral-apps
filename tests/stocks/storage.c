// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define HOST_IMPORT(name, ...) __VA_ARGS__
#include "../../apps/stocks/stocks.c"

uint8_t app_strings[APP_STRINGS_BYTES];
static const char *tickers = "AAPL";
static const char *values[] = { "Apple", "Apple", "AAPL", "123.45", "122.00", "2", "1794955920", "USD" };
static uint8_t quotes[16 * sizeof(quote_t)];
static int32_t quotes_size, reply_item;
static int writes, reads, redraws;
static bool fail_writes;
int32_t kv_get(const char *key, int32_t key_length, void *buffer, int32_t capacity)
{
    if (key_length != 7 || memcmp(key, "tickers", 7)) return HOST_ERR_NOT_FOUND;
    int32_t length = (int32_t)strlen(tickers); assert(length <= capacity);
    memcpy(buffer, tickers, (size_t)length); return length;
}
int32_t file_read(const char *name, int32_t length, int32_t offset, void *buffer, int32_t capacity)
{
    (void)name; (void)length; reads++;
    if (offset < 0 || offset + capacity > quotes_size) return HOST_ERR_NOT_FOUND;
    memcpy(buffer, quotes + offset, (size_t)capacity); return capacity;
}
int32_t file_size(const char *name, int32_t length) { (void)name; (void)length; return quotes_size; }
int32_t file_write(const char *name, int32_t name_length, int32_t offset, const void *buffer, int32_t length)
{
    (void)name; (void)name_length;
    if (fail_writes) return HOST_ERR_NO_SPACE;
    assert(offset >= 0 && offset <= quotes_size && offset + length <= (int32_t)sizeof quotes);
    memcpy(quotes + offset, buffer, (size_t)length);
    if (offset + length > quotes_size) quotes_size = offset + length;
    writes++; return HOST_OK;
}
int32_t answer_item(void) { return reply_item; }
int32_t answer_value(int32_t index, char *buffer, int32_t capacity)
{
    assert(index >= 0 && index < 8);
    int32_t length = (int32_t)strlen(values[index]); assert(length <= capacity);
    memcpy(buffer, values[index], (size_t)length); return length;
}
int32_t answer_count(int32_t index) { (void)index; return 2; }
int32_t answer_element(int32_t index, int32_t element, char *buffer, int32_t capacity)
{
    (void)index; assert(element < 2 && capacity >= 6);
    const char *value = element ? "123.45" : "122.00";
    memcpy(buffer, value, 6); return 6;
}
void screen_redraw(int32_t flags) { (void)flags; redraws++; }
int32_t poll_run(int32_t id) { (void)id; return HOST_OK; }
int32_t kv_set(const char *key, int32_t key_length, const void *value, int32_t length)
{ (void)key; (void)key_length; (void)value; (void)length; return HOST_OK; }

int main(void)
{
    loaded = 0;
    yahoo_values(0, 200);
    assert(writes == 1 && redraws == 1 && reads == 1 && quote.price == 12345);
    for (int poll = 0; poll < 100; poll++) yahoo_values(0, 200);
    assert(writes == 1 && redraws == 1 && reads == 101);
    values[PICK_PRICE] = "124.50";
    yahoo_values(0, 200);
    assert(writes == 2 && redraws == 2 && quote.price == 12450);
    fail_writes = true;
    values[PICK_PRICE] = "125.00";
    yahoo_values(0, 200);
    assert(writes == 2 && quote.price == 12500);
    int before_redraws = redraws;
    yahoo_values(0, 200);
    assert(writes == 2 && quote.price == 12500 && redraws == before_redraws);
    values[PICK_PRICE] = "124.50";
    yahoo_values(0, 200);
    assert(writes == 2 && quote.price == 12450 && redraws == before_redraws + 1);
    yahoo_values(0, 200);
    assert(writes == 2 && redraws == before_redraws + 1);
    values[PICK_PRICE] = "125.00";
    yahoo_values(0, 200);
    assert(writes == 2 && quote.price == 12500 && redraws == before_redraws + 2);
    fail_writes = false;
    yahoo_values(0, 200);
    assert(writes == 3 && quote.price == 12500 && redraws == before_redraws + 2);
    yahoo_values(0, 503);
    assert(writes == 4 && quote.has_data && quote.price == 12500 && quote.status == 503);
    yahoo_values(0, 503);
    assert(writes == 4);
    tickers = "MOEX:SBER";
    const char *moex[] = { "SBER", "Sberbank", "200.00", "2", "SUR", "205.00", "15:30:00", "2026-11-18 15:31:00" };
    for (int value = 0; value < 8; value++) values[value] = moex[value];
    moex_quote(0, 200);
    assert(quote.price == 20500 && !strcmp(quote.currency, "RUB") && quote.count == 0);
    int before_writes = writes;
    moex_quote(0, 200);
    assert(writes == before_writes);
    moex_day(0, 200);
    assert(writes == before_writes + 1 && quote.count == 2);
    moex_day(0, 200);
    assert(writes == before_writes + 1);
    quote_t record = { .version = QUOTE_VERSION, .price = 300 };
    fail_writes = true;
    assert(!write_quote(3, &record) && quotes_size == sizeof record);
    fail_writes = false;
    assert(write_quote(3, &record) && quotes_size == 4 * sizeof record);
    puts("Stocks storage: 100 unchanged polls write nothing; changed quotes, full storage and retries passed");
}
