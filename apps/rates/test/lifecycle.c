/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HOST_IMPORT(name, ...) __VA_ARGS__
#include "../rates.c"

uint8_t app_strings[APP_STRINGS_BYTES];
static const char *configured_codes = "USD\nJPY";
static uint8_t history_file[4096];
static int32_t history_size;
static bool fail_writes;
static int writes, requests, redraws, polls;
static char requested_urls[16][192];
static struct { char key[16]; uint8_t bytes[1024]; int length; } values[16];

int32_t kv_get(const char *key, int32_t key_length, void *value, int32_t capacity)
{
    if (key_length == 5 && !memcmp(key, "codes", 5)) {
        int32_t length = (int32_t)strlen(configured_codes);
        if (capacity < length) return HOST_ERR_NO_SPACE;
        memcpy(value, configured_codes, (size_t)length);
        return length;
    }
    for (int index = 0; index < 16; ++index)
        if ((int)strlen(values[index].key) == key_length && !memcmp(values[index].key, key, (size_t)key_length)) {
            if (capacity < values[index].length) return HOST_ERR_NO_SPACE;
            memcpy(value, values[index].bytes, (size_t)values[index].length);
            return values[index].length;
        }
    return HOST_ERR_NOT_FOUND;
}
int32_t kv_set(const char *key, int32_t key_length, const void *value, int32_t length)
{
    for (int index = 0; index < 16; ++index)
        if (!values[index].key[0] || ((int)strlen(values[index].key) == key_length && !memcmp(values[index].key, key, (size_t)key_length))) {
            assert(length <= 1024 && key_length < 16);
            memcpy(values[index].key, key, (size_t)key_length); values[index].key[key_length] = 0;
            memcpy(values[index].bytes, value, (size_t)length); values[index].length = length;
            return HOST_OK;
        }
    return HOST_ERR_NO_SPACE;
}
int32_t file_size(const char *name, int32_t length) { (void)name; (void)length; return history_size; }
int32_t file_read(const char *name, int32_t length, int32_t offset, void *bytes, int32_t capacity)
{
    (void)name; (void)length;
    if (offset < 0 || capacity < 0 || offset + capacity > history_size) return HOST_ERR_NOT_FOUND;
    memcpy(bytes, history_file + offset, (size_t)capacity);
    return capacity;
}
int32_t file_write(const char *name, int32_t name_length, int32_t offset, const void *bytes, int32_t length)
{
    (void)name; (void)name_length;
    if (fail_writes) return HOST_ERR_FAILED;
    assert(offset >= 0 && offset <= history_size && length >= 0 && offset + length <= (int32_t)sizeof history_file);
    memcpy(history_file + offset, bytes, (size_t)length);
    if (offset + length > history_size) history_size = offset + length;
    ++writes;
    return HOST_OK;
}
int32_t http_send(int32_t tag, int32_t method, const char *url, int32_t url_length,
                  const char *headers, int32_t headers_length, const void *body, int32_t body_length)
{
    (void)headers; (void)headers_length; (void)body; (void)body_length;
    assert(tag >= HOST_REQUEST_FIRST && method == HOST_HTTP_GET && requests < 16);
    assert(url_length < 192);
    memcpy(requested_urls[requests], url, (size_t)url_length);
    requested_urls[requests++][url_length] = 0;
    return HOST_OK;
}
int32_t poll_run(int32_t id) { assert(id == 0); ++polls; return HOST_OK; }
int32_t timer_set(int32_t id, int32_t milliseconds) { (void)id; (void)milliseconds; return HOST_OK; }
int32_t header_set_text(int32_t side, const char *text, int32_t length) { (void)side; (void)text; (void)length; return HOST_OK; }
void screen_redraw(int32_t flags) { (void)flags; ++redraws; }

static void deliver(int request, const char *body, int status)
{
    size_t length = strlen(body);
    for (size_t offset = 0; offset < length; offset += 17) {
        size_t count = length - offset < 17 ? length - offset : 17;
        int flags = (offset == 0 ? HOST_DATA_FIRST : 0) | (offset + count == length ? HOST_DATA_LAST : 0);
        app_on_data(request, (const uint8_t *)body + offset, (int32_t)count, flags, status);
    }
}
static char *read_text(const char *path)
{
    FILE *input = fopen(path, "rb"); assert(input);
    char *text = calloc(1, 16384); assert(text);
    size_t size = fread(text, 1, 16383, input); assert(size && !ferror(input)); fclose(input);
    return text;
}
int main(int argc, char **argv)
{
    assert(argc == 4);
    char *daily = read_text(argv[1]), *usd = read_text(argv[2]), *jpy = read_text(argv[3]);
    app_on_load();
    app_on_focus();
    assert(polls == 1);
    deliver(0, daily, 200);
    assert(state.has_data && currency_count() == 2 && requests == 2);
    assert(strstr(requested_urls[0], "VAL_NM_RQ=R01235"));
    assert(strstr(requested_urls[1], "VAL_NM_RQ=R01820"));
    assert(rate_for(1)->value / rate_for(1)->nominal == 5182);
    deliver(HOST_REQUEST_FIRST, usd, 200);
    assert(chart.history.count > 1 && !strcmp(chart.code, "USD"));
    int previous_writes = writes;
    deliver(HOST_REQUEST_FIRST, usd, 200);
    assert(writes == previous_writes);
    state_t saved_state = state;
    deliver(0, "{\"Date\":\"bad\"}", 200);
    assert(!memcmp(&saved_state, &state, sizeof state));
    configured_codes = "JPY\nUSD";
    app_on_settings();
    assert(chart.history.count == 0);
    /* An already queued USD answer follows its ID, never its old slot. */
    deliver(HOST_REQUEST_FIRST, usd, 200);
    assert(chart.history.count == 0);
    cached_history_t record;
    read_history(1, &record);
    assert(!strcmp(record.code, "USD") && record.history.count > 1);
    fail_writes = true;
    deliver(HOST_REQUEST_FIRST + 1, jpy, 200);
    assert(chart.history.count == 0);
    fail_writes = false;
    deliver(HOST_REQUEST_FIRST + 1, jpy, 200);
    assert(!strcmp(chart.code, "JPY") && chart.history.count > 1);
    assert(chart.history.values[chart.history.count - 1] < 10000);
    previous_writes = writes;
    request_histories();
    assert(writes == previous_writes);
    cached_history_t before = chart;
    deliver(HOST_REQUEST_FIRST, "<ValCurs ID=\"R01820\">broken", 200);
    assert(!memcmp(&before, &chart, sizeof chart));
    app_on_button(BTN_DOWN_PRESSED);
    assert(navigation.index == 1 && !strcmp(chart.code, "USD"));
    app_on_button(BTN_OK_PRESSED);
    assert(navigation.rotating);
    app_on_timer(ROTATE_TIMER);
    assert(navigation.index == 0 && !strcmp(chart.code, "JPY"));
    free(daily); free(usd); free(jpy);
    puts("Rates lifecycle: cached quotes, ID-safe reorder, write failures, history reuse, navigation and rotation passed");
    return 0;
}
