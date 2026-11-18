// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "json_pick.h"

/* A year can intersect 54 calendar weeks, including its partial edges. */
enum { CALENDAR_DAYS = 54 * 7, CALENDAR_BYTES = (CALENDAR_DAYS * 3 + 7) / 8 };
typedef struct {
    int32_t first_day, count, total;
    uint8_t levels[CALENDAR_BYTES];
} contribution_calendar_t;
/* Constant memory: JSON validation plus a bounded lexer and path stack.
 * No response body, contribution counts or date strings survive parsing. */
typedef struct {
    json_pick_t validation;
    contribution_calendar_t calendar;
    struct { int32_t role; bool key_next, array; char key[32]; } frames[16];
    int32_t depth, length, mode, unicode_left, unicode_value;
    int32_t day, level, day_fields, total_fields;
    bool escaped, broken, overflow;
    char token[64], login[40];
} calendar_picker_t;
void calendar_picker_start(calendar_picker_t *picker);
void calendar_picker_feed(calendar_picker_t *picker, const uint8_t *bytes, int32_t length);
bool calendar_picker_finish(calendar_picker_t *picker);
int32_t calendar_level(const contribution_calendar_t *calendar, int32_t index);
int32_t calendar_streak(const contribution_calendar_t *calendar, int32_t today);
