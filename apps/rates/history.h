/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HISTORY_POINTS 32
#define HISTORY_DAYS 30
#define HISTORY_ID_BYTES 16

typedef struct {
    char id[HISTORY_ID_BYTES];
    int32_t count, as_of;
    int32_t days[HISTORY_POINTS];
    int32_t values[HISTORY_POINTS]; /* RUB per unit, four decimals. */
} history_t;

typedef struct {
    history_t result;
    char tag[256], text[64];
    size_t tag_length, text_length, total;
    int32_t day, nominal;
    int32_t value;
    uint8_t depth, element, fields;
    bool in_tag, broken, started, finished, declaration;
    char quote;
} history_parser_t;

bool history_valid_id(const char *id);
void history_start(history_parser_t *parser);
bool history_feed(history_parser_t *parser, const uint8_t *data, size_t length);
bool history_finish(history_parser_t *parser);
int32_t history_day(const char *text, bool iso);

/* Positive fixed decimals as returned by the CBR feeds; excess places truncate. */
bool history_amount(const char *text, int32_t decimals, int32_t *value);
