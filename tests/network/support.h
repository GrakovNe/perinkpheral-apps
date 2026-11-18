// SPDX-License-Identifier: MIT
#pragma once
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define HOST_IMPORT(name, ...) __VA_ARGS__
#include "host_api.h"

typedef struct { char key[16]; uint8_t data[1024]; int32_t size; } record_t;
static record_t records[16];
static int writes, redraws, sends, file_writes;
static int32_t send_result = HOST_OK, write_result = HOST_OK;
static char request_body[HOST_HTTP_BODY_MAX + 1];
static int64_t now = 1768478400;
static uint8_t event_file[8192];
static int32_t event_file_size;
static int32_t session_value, session_length;

static record_t *record(const char *key, int32_t length)
{
    for (int index = 0; index < 16; index++) {
        if (!records[index].key[0]) {
            assert(length < (int32_t)sizeof records[index].key);
            memcpy(records[index].key, key, length);
            records[index].key[length] = 0;
            return &records[index];
        }
        if ((int32_t)strlen(records[index].key) == length && !memcmp(records[index].key, key, length)) return &records[index];
    }
    assert(!"too many test keys");
    return NULL;
}
static void setting(const char *key, const char *value)
{
    record_t *slot = record(key, (int32_t)strlen(key));
    slot->size = (int32_t)strlen(value);
    memcpy(slot->data, value, slot->size);
}
int32_t kv_get(const char *key, int32_t length, void *value, int32_t capacity)
{
    record_t *slot = record(key, length);
    if (!slot->size) return HOST_ERR_NOT_FOUND;
    if (slot->size > capacity) return HOST_ERR_NO_SPACE;
    memcpy(value, slot->data, slot->size);
    return slot->size;
}
int32_t kv_set(const char *key, int32_t key_length, const void *value, int32_t length)
{
    writes++;
    if (write_result != HOST_OK) return write_result;
    record_t *slot = record(key, key_length);
    assert(length >= 0 && length <= (int32_t)sizeof slot->data);
    memcpy(slot->data, value, length);
    slot->size = length;
    return HOST_OK;
}
int32_t file_read(const char *name, int32_t length, int32_t offset, void *out, int32_t capacity)
{
    (void)name; (void)length;
    assert(offset == 0 && capacity >= event_file_size);
    memcpy(out, event_file, event_file_size);
    return event_file_size;
}
int32_t file_write(const char *name, int32_t name_length, int32_t offset, const void *bytes, int32_t length)
{
    (void)name; (void)name_length;
    assert(offset == 0 && length <= (int32_t)sizeof event_file);
    file_writes++;
    if (write_result != HOST_OK) return write_result;
    memcpy(event_file, bytes, length);
    event_file_size = length;
    return HOST_OK;
}
int32_t http_send(int32_t tag, int32_t method, const char *url, int32_t url_length,
                 const char *headers, int32_t headers_length, const void *body, int32_t body_length)
{
    (void)tag; (void)method; (void)url; (void)url_length;
    (void)headers; (void)headers_length;
    sends++;
    assert(body_length <= HOST_HTTP_BODY_MAX);
    memcpy(request_body, body, body_length);
    request_body[body_length] = 0;
    return send_result;
}
void screen_redraw(int32_t flags) { (void)flags; redraws++; }
int64_t time_now(void) { return now; }
int32_t time_zone_offset(void) { return 0; }
int32_t alarm_set(int32_t id, int32_t seconds) { (void)id; (void)seconds; return HOST_OK; }
int32_t alarm_set_at(int32_t id, int64_t when) { (void)id; (void)when; return HOST_OK; }
int32_t alarm_set_every(int32_t id, int32_t seconds, int32_t first)
{ (void)id; (void)seconds; (void)first; return HOST_OK; }
int32_t timer_set(int32_t id, int32_t ms) { (void)id; (void)ms; return HOST_OK; }
int32_t session_get(void *value, int32_t capacity)
{
    if (!session_length) return HOST_ERR_NOT_FOUND;
    assert(session_length == sizeof session_value && capacity >= session_length);
    memcpy(value, &session_value, session_length);
    return session_length;
}
int32_t header_set_text(int32_t side, const char *text, int32_t length)
{ (void)side; (void)text; (void)length; return HOST_OK; }
void log_write(int32_t level, const char *text, int32_t length)
{ (void)level; (void)text; (void)length; }
