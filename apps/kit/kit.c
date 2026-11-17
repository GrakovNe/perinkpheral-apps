/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * Every part of the host's look an application may ask for, each on a
 * page of its own: what a developer copies from. The pages are a list;
 * OK opens one, BACK returns.
 */
#include <stdbool.h>
#include <stddef.h>

#include "app_draw.h"
#include "app_text.h"
#include "app_ui.h"
#include "host_api.h"
#include "strings.h" /* made of the `strings` folder by tools/perinkpheral */

#define COUNT_OF(array) ((int32_t)(sizeof(array) / sizeof(array)[0]))
#define REDRAW          HOST_REDRAW_NO_BLINK

/* A text of the strings in the device's language, and its length. */
#define TEXT(id) app_string(id), app_text_length(app_string(id))

typedef enum {
    PAGE_PRIMITIVES,
    PAGE_LIST,
    PAGE_OPTIONS,
    PAGE_DETAILS,
    PAGE_CELL,
    PAGE_CELLS,
    PAGE_CHARTS,
    PAGE_PICKER,
    PAGE_DATE,
    PAGE_TIME,
    PAGE_MESSAGE,
    PAGE_QUESTION,
    PAGE_WORKING,
    PAGE_COUNT,
    PAGE_CONTENTS = PAGE_COUNT, /* the list of the pages above */
} page_t;

static const int32_t PAGE_NAMES[PAGE_COUNT] = {
    STR_PAGE_PRIMITIVES, STR_PAGE_LIST,   STR_PAGE_OPTIONS, STR_PAGE_DETAILS, STR_PAGE_CELL,
    STR_PAGE_CELLS,      STR_PAGE_CHARTS, STR_PAGE_PICKER,  STR_PAGE_DATE,    STR_PAGE_TIME,
    STR_PAGE_MESSAGE,    STR_PAGE_QUESTION, STR_PAGE_WORKING,
};

static page_t     page = PAGE_CONTENTS;
static app_list_t contents = { .count = PAGE_COUNT };

static const int32_t UNITS[] = { STR_CELSIUS, STR_FAHRENHEIT, STR_KELVIN };
static app_list_t        units = { .count = COUNT_OF(UNITS) };
static int32_t           unit_set;

static const int32_t PACES[] = { STR_PACE_1M,  STR_PACE_5M,  STR_PACE_10M,
                                 STR_PACE_15M, STR_PACE_30M, STR_PACE_1H };
static const char   *pace_names[COUNT_OF(PACES)]; /* the paces in the device's language */
static app_picker_t      pace = { .count = COUNT_OF(PACES), .chosen = 3 };

static app_list_t settings = { .count = 3 };

/* Facts nobody chooses from, more of them than fit: UP and DOWN move the
 * list itself. It stands under a title and over the row of buttons. */
static const int32_t FACT_LABELS[] = { STR_NETWORK, STR_ADDRESS, STR_SIGNAL, STR_FIRMWARE,
                                      STR_SLOT,    STR_MEMORY,  STR_UPTIME };
/* a value that is the same in every language is given as it is: -1 */
static const int32_t     FACT_VALUES[] = { STR_HOME, -1, STR_DBM, -1, -1, STR_KB, STR_UPTIME_VALUE };
static const char *const FACT_TEXTS[] = { "", "192.168.1.70", "", "0.1.0", "ota_0", "", "" };
static app_list_t facts = { .count = COUNT_OF(FACT_LABELS), .selected = -1 };
static app_date_t when;
static int32_t    bar_value = 3;

static void open(page_t next)
{
    page = next;
    if (page == PAGE_DATE) {
        app_date_start(&when, HOST_DATE_HAS_TIME | HOST_DATE_HAS_DATE, 2026, 2099);
    } else if (page == PAGE_TIME) {
        app_date_start(&when, HOST_DATE_HAS_TIME, 2026, 2099);
    } else if (page == PAGE_OPTIONS) {
        units.selected = unit_set;
    }
    screen_redraw(REDRAW);
}

/* What one of the library's screens said about a press. */
static void follow(app_ui_result_t result)
{
    if (result == APP_UI_CHANGED) {
        screen_redraw(REDRAW);
    } else if (result == APP_UI_DONE || result == APP_UI_CANCELLED) {
        open(PAGE_CONTENTS);
    }
}

void app_on_button(int32_t event)
{
    switch (page) {
    case PAGE_CONTENTS: {
        app_ui_result_t result = app_list_button(&contents, event);
        if (result == APP_UI_DONE) {
            open((page_t)contents.selected);
        } else if (result == APP_UI_CANCELLED) {
            screen_leave();
        } else if (result == APP_UI_CHANGED) {
            screen_redraw(REDRAW);
        }
        break;
    }
    case PAGE_LIST:
        follow(app_list_button(&settings, event));
        break;
    case PAGE_DETAILS:
        if (app_list_button(&facts, event) == APP_UI_CHANGED) {
            screen_redraw(REDRAW);
        }
        break;
    case PAGE_OPTIONS: {
        app_ui_result_t result = app_list_button(&units, event);
        if (result == APP_UI_DONE) {
            unit_set = units.selected; /* OK sets the one under the bar */
            result = APP_UI_CHANGED;
        }
        follow(result);
        break;
    }
    case PAGE_PICKER:
        follow(app_picker_button(&pace, event));
        break;
    case PAGE_DATE:
    case PAGE_TIME:
        follow(app_date_button(&when, event));
        break;
    case PAGE_PRIMITIVES:
        if (event == BTN_UP_PRESSED && bar_value < 10) {
            bar_value++;
            screen_redraw(REDRAW);
        } else if (event == BTN_DOWN_PRESSED && bar_value > 0) {
            bar_value--;
            screen_redraw(REDRAW);
        } else if (event == BTN_OK_PRESSED) {
            open(PAGE_CONTENTS);
        }
        break;
    default:
        if (event == BTN_OK_PRESSED) {
            open(PAGE_CONTENTS);
        }
        break;
    }
}

/* It has pages of its own, so BACK is its to answer: a part back on the
 * date, the contents from any other page, and out from the contents. */
void app_on_back(void)
{
    if (page == PAGE_CONTENTS) {
        screen_leave();
    } else if (page == PAGE_DATE || page == PAGE_TIME) {
        follow(app_date_button(&when, BTN_BACK_PRESSED));
    } else {
        open(PAGE_CONTENTS);
    }
}

/* Rows of strings; `values` and `texts` may be NULL, and a value of -1
 * is the one of `texts` at its place. */
static void draw_rows(app_list_t *list, const int32_t *labels, const int32_t *values,
                      const char *const *texts, int32_t checked)
{
    int32_t first = app_list_first(list);
    for (int32_t i = first; i < first + ui_list_visible(list) && i < list->count; i++) {
        const char *label = app_string(labels[i]);
        const char *value = values == NULL ? "" : values[i] >= 0 ? app_string(values[i]) : texts[i];
        ui_draw_list_row(list, i, label, app_text_length(label), value, app_text_length(value),
                    i == checked ? HOST_ICON_CHECK : HOST_ICON_NONE);
    }
}

/* Cells: a name, a picture and a value in a box of any size, laid out and
 * sized by the host. Those that stand together are of one size - the size
 * the longest value can have. */
static void draw_cells(int32_t count)
{
    static const int32_t     NAMES[] = { STR_LIVING_ROOM, STR_HUMIDITY, STR_POWER, STR_DOOR };
    static const char *const VALUES[] = { "21\xC2\xB0", "40%", NULL, NULL };
    const char *values[] = { VALUES[0], VALUES[1], app_string(STR_POWER_VALUE),
                             app_string(STR_CLOSED) };
    static const int32_t ICONS[] = { HOST_ICON_HOME, HOST_ICON_DROP, HOST_ICON_BOLT, HOST_ICON_NONE };
    int32_t columns = count > 1 ? 2 : 1, rows = count > 2 ? 2 : 1;
    int32_t width = gfx_width() / columns, height = gfx_height() / rows;
    host_cell_t cells[4];
    for (int32_t i = 0; i < count; i++) {
        cells[i] = (host_cell_t){ .x = i % columns * width, .y = i / columns * height,
                                  .width = width, .height = height };
        app_text_copy(cells[i].name, sizeof cells[i].name, app_string(NAMES[i]));
        app_text_copy(cells[i].value, sizeof cells[i].value, values[i]);
    }
    app_draw_cells(cells, 0, ICONS, count); /* one size for all of them */
    if (columns > 1) {
        gfx_draw_rect(width, HOST_UI_MARGIN, 1, gfx_height() - 2 * HOST_UI_MARGIN, HOST_RECT_FILLED);
        gfx_draw_rect(HOST_UI_MARGIN, height, gfx_width() - 2 * HOST_UI_MARGIN, 1, HOST_RECT_FILLED);
    }
}

/* The chart in its four kinds. Numbers in, a picture out: the host scales
 * the series to the box. */
static void draw_charts(void)
{
    static const int32_t SERIES[] = { 712, 715, 715, 721, 730, 728, 735, 741, 752, 760,
                                      758, 771, 780, 779, 790, 802, 815, 811, 824, 840 };
    static const int32_t DAILY[] = { 3, 0, 6, 9, 2, 7, 6, 11, 8, 4, 13, 9, 1, 11 };
    static const int32_t NAMES[] = { STR_LINE, STR_AREA, STR_BARS, STR_STEPS };
    int32_t top = ui_draw_title(TEXT(STR_PAGE_CHARTS));
    int32_t column = (gfx_width() - 3 * HOST_UI_MARGIN) / 2;
    int32_t row = (gfx_height() - top - 2 * HOST_UI_MARGIN) / 2;
    /* the chart under its caption, whatever the typeface's height */
    int32_t caption = ui_text_height(HOST_UI_CAPTION) + 6;
    for (int32_t i = 0; i < 4; i++) {
        int32_t left = HOST_UI_MARGIN + i % 2 * (column + HOST_UI_MARGIN);
        int32_t at = top + i / 2 * (row + HOST_UI_MARGIN);
        ui_draw_text(left, at, HOST_UI_CAPTION, TEXT(NAMES[i]), 0);
        host_chart_t chart = {
            .x = left, .y = at + caption, .width = column, .height = row - caption,
            .kind = i, /* HOST_CHART_LINE, AREA, BARS, STEPS */
            .flags = HOST_CHART_AXIS,
            .count = i == 2 ? (int32_t)(sizeof DAILY / sizeof DAILY[0])
                            : (int32_t)(sizeof SERIES / sizeof SERIES[0]),
        };
        if (i == 0) {
            chart.flags |= HOST_CHART_RANGE | HOST_CHART_MARK_LAST;
        } else if (i == 2) {
            chart.flags |= HOST_CHART_FROM_ZERO;
        }
        ui_draw_chart(&chart, i == 2 ? DAILY : SERIES);
    }
}

static void draw_primitives(void)
{
    int32_t top = ui_draw_title(TEXT(STR_TITLE));
    ui_draw_text(16, top, HOST_UI_TEXT, TEXT(STR_TEXT), 0);
    ui_draw_text(16, top + 30, HOST_UI_CAPTION, TEXT(STR_CAPTION), 0);
    int32_t row = top + 54;
    ui_draw_icon(16, row + 22, HOST_ICON_BELL, HOST_UI_ICON_SMALL);
    ui_draw_icon(52, row, HOST_ICON_BELL, HOST_UI_ICON_MEDIUM);
    char number[8];
    app_text_t text;
    app_text_start(&text, number, sizeof number);
    app_text_add_number(&text, bar_value);
    ui_draw_text(120, row - 12, HOST_UI_VALUE, number, app_text_length(number), 0);
    ui_draw_button(262, row + 14, "UP", 2, TEXT(STR_MORE), false);
    ui_draw_bar(16, row + 58, gfx_width() - 32, bar_value, 10);
    app_draw_actions(NULL, app_string(STR_CLOSE));
}

void app_on_draw(void)
{
    static const int32_t SETTING_LABELS[] = { STR_UNITS, STR_REFRESH, STR_SHOW_CITY };
    static const int32_t SETTING_VALUES[] = { STR_CELSIUS, STR_PACE_15M, STR_ON };
    host_notice_t notice = { .icon = HOST_ICON_NONE };

    switch (page) {
    case PAGE_CONTENTS:
        draw_rows(&contents, PAGE_NAMES, NULL, NULL, -1);
        break;
    case PAGE_PRIMITIVES:
        draw_primitives();
        break;
    case PAGE_LIST:
        draw_rows(&settings, SETTING_LABELS, SETTING_VALUES, NULL, -1);
        break;
    case PAGE_OPTIONS:
        draw_rows(&units, UNITS, NULL, NULL, unit_set);
        break;
    case PAGE_DETAILS:
        facts.height = gfx_height() - HOST_UI_ACTIONS_HEIGHT;
        draw_rows(&facts, FACT_LABELS, FACT_VALUES, FACT_TEXTS, -1);
        app_draw_actions(NULL, app_string(STR_BACK));
        break;
    case PAGE_CELL:
        draw_cells(1);
        break;
    case PAGE_CELLS:
        draw_cells(4);
        break;
    case PAGE_CHARTS:
        draw_charts();
        break;
    case PAGE_PICKER:
        for (int32_t i = 0; i < COUNT_OF(PACES); i++) {
            pace_names[i] = app_string(PACES[i]);
        }
        app_picker_draw(&pace, pace_names);
        break;
    case PAGE_DATE:
    case PAGE_TIME:
        app_date_draw(&when);
        break;
    case PAGE_MESSAGE:
        app_text_copy(notice.first, sizeof notice.first, app_string(STR_NOT_SET_UP));
        app_text_copy(notice.detail, sizeof notice.detail, app_string(STR_OPEN_PAGE));
        notice.icon = HOST_ICON_GEAR;
        ui_draw_notice(&notice);
        break;
    case PAGE_QUESTION:
        app_text_copy(notice.first, sizeof notice.first, app_string(STR_REMOVE));
        app_text_copy(notice.second, sizeof notice.second, app_string(STR_EVERYTHING));
        app_text_copy(notice.ok, sizeof notice.ok, app_string(STR_REMOVE));
        app_text_copy(notice.back, sizeof notice.back, app_string(STR_CANCEL));
        notice.icon = HOST_ICON_TRASH;
        ui_draw_notice(&notice);
        break;
    case PAGE_WORKING:
        app_text_copy(notice.first, sizeof notice.first, app_string(STR_LOADING));
        app_text_copy(notice.second, sizeof notice.second, app_string(STR_IN_PROGRESS));
        notice.is_working = 1;
        ui_draw_notice(&notice);
        break;
    default:
        break;
    }
}
