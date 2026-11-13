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

#include "app_text.h"
#include "app_ui.h"
#include "host_api.h"
#include "pictures.h" /* made of the `pictures` folder by tools/mkapp */

#define COUNT_OF(array) ((int32_t)(sizeof(array) / sizeof(array)[0]))
#define REDRAW          HOST_REDRAW_NO_BLINK

typedef enum {
    PAGE_PRIMITIVES,
    PAGE_LIST,
    PAGE_OPTIONS,
    PAGE_DETAILS,
    PAGE_CELL,
    PAGE_CELLS,
    PAGE_PICKER,
    PAGE_DATE,
    PAGE_TIME,
    PAGE_MESSAGE,
    PAGE_QUESTION,
    PAGE_WORKING,
    PAGE_COUNT,
    PAGE_CONTENTS = PAGE_COUNT, /* the list of the pages above */
} page_t;

static const char *const PAGE_NAMES[PAGE_COUNT] = {
    "Text and parts", "List",    "Options",  "Details", "Cell",    "Cells",
    "Picker",         "Date and time", "Time", "Message", "Question", "Working",
};

static page_t     page = PAGE_CONTENTS;
static app_list_t contents = { .count = PAGE_COUNT };

static const char *const UNITS[] = { "Celsius", "Fahrenheit", "Kelvin" };
static app_list_t        units = { .count = COUNT_OF(UNITS) };
static int32_t           unit_set;

static const char *const PACES[] = { "1 min", "5 min", "10 min", "15 min", "30 min", "1 h" };
static app_picker_t      pace = { .count = COUNT_OF(PACES), .chosen = 3 };

static app_list_t settings = { .count = 3 };

/* Facts nobody chooses from, more of them than fit: UP and DOWN move the
 * list itself. It stands under a title and over the row of buttons. */
static const char *const FACT_LABELS[] = { "Network", "Address", "Signal", "Firmware",
                                           "Slot",    "Memory",  "Uptime" };
static const char *const FACT_VALUES[] = { "Home",  "192.168.1.70", "-62 dBm", "0.1.0",
                                           "ota_0", "122 KB",       "3 h 12 min" };
static app_list_t facts = { .count = COUNT_OF(FACT_LABELS), .selected = -1 };
static app_date_t when;
static int32_t    bar_value = 3;

static int32_t length_of(const char *text)
{
    return app_text_length(text);
}

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
    request_redraw(REDRAW);
}

/* What one of the library's screens said about a press. */
static void follow(app_ui_result_t result)
{
    if (result == APP_UI_CHANGED) {
        request_redraw(REDRAW);
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
            request_exit();
        } else if (result == APP_UI_CHANGED) {
            request_redraw(REDRAW);
        }
        break;
    }
    case PAGE_LIST:
        follow(app_list_button(&settings, event));
        break;
    case PAGE_DETAILS:
        if (app_list_button(&facts, event) == APP_UI_CHANGED) {
            request_redraw(REDRAW);
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
            request_redraw(REDRAW);
        } else if (event == BTN_DOWN_PRESSED && bar_value > 0) {
            bar_value--;
            request_redraw(REDRAW);
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
        request_exit();
    } else if (page == PAGE_DATE || page == PAGE_TIME) {
        follow(app_date_button(&when, BTN_BACK_PRESSED));
    } else {
        open(PAGE_CONTENTS);
    }
}

static void draw_rows(app_list_t *list, const char *const *labels, const char *const *values,
                      int32_t checked)
{
    int32_t first = app_list_first(list);
    for (int32_t i = first; i < first + ui_list_visible(list) && i < list->count; i++) {
        const char *value = values != NULL ? values[i] : "";
        ui_list_row(list, i, labels[i], length_of(labels[i]), value, length_of(value),
                    i == checked ? HOST_ICON_CHECK : HOST_ICON_NONE);
    }
}

static void copy(char *to, int32_t size, const char *from);

/* Cells: a name, a picture and a value in a box of any size, laid out and
 * sized by the host. Those that stand together are of one size - the size
 * the longest value can have. */
static void draw_cells(int32_t count)
{
    static const char *const NAMES[] = { "Living room", "Humidity", "Power", "Door" };
    static const char *const VALUES[] = { "21\xC2\xB0", "40%", "1.2kW", "Closed" };
    static const uint8_t *const PICTURES[] = { PICTURE_HOME, PICTURE_DROP, PICTURE_BOLT, NULL };
    int32_t columns = count > 1 ? 2 : 1, rows = count > 2 ? 2 : 1;
    int32_t width = gfx_width() / columns, height = gfx_height() / rows;
    host_cell_t cells[4];
    int32_t     size = 0;
    for (int32_t i = 0; i < count; i++) {
        cells[i] = (host_cell_t){ .x = i % columns * width, .y = i / columns * height,
                                  .width = width, .height = height };
        copy(cells[i].name, sizeof cells[i].name, NAMES[i]);
        copy(cells[i].value, sizeof cells[i].value, VALUES[i]);
        int32_t fits = ui_cell_fit(&cells[i], PICTURES[i]);
        size = fits > size ? fits : size;
    }
    for (int32_t i = 0; i < count; i++) {
        cells[i].size = size;
        ui_cell(&cells[i], PICTURES[i]);
    }
    if (columns > 1) {
        gfx_rect(width, HOST_UI_MARGIN, 1, gfx_height() - 2 * HOST_UI_MARGIN, HOST_RECT_FILLED);
        gfx_rect(HOST_UI_MARGIN, height, gfx_width() - 2 * HOST_UI_MARGIN, 1, HOST_RECT_FILLED);
    }
}

static void draw_primitives(void)
{
    static const char TEXT[] = "Text, as it is read";
    static const char CAPTION[] = "UP AND DOWN MOVE THE BAR";
    int32_t top = ui_title("Title", 5);
    ui_text(16, top, HOST_UI_TEXT, TEXT, sizeof TEXT - 1, 0);
    ui_text(16, top + 30, HOST_UI_CAPTION, CAPTION, sizeof CAPTION - 1, 0);
    int32_t row = top + 54;
    ui_icon(16, row + 22, HOST_ICON_BELL, HOST_UI_ICON_SMALL);
    ui_icon(52, row, HOST_ICON_BELL, HOST_UI_ICON_MEDIUM);
    char number[8];
    app_text_t text;
    app_text_start(&text, number, sizeof number);
    app_text_add_number(&text, bar_value);
    ui_text(120, row - 12, HOST_UI_VALUE, number, length_of(number), 0);
    ui_button(262, row + 14, "UP", 2, "More", 4, false);
    ui_bar(16, row + 58, gfx_width() - 32, bar_value, 10);
    ui_actions("", 0, "Close", 5);
}

static void copy(char *to, int32_t size, const char *from)
{
    app_text_t text;
    app_text_start(&text, to, size);
    app_text_add(&text, from);
}

void app_on_draw(void)
{
    static const char *const SETTING_LABELS[] = { "Units", "Refresh", "Show the city" };
    static const char *const SETTING_VALUES[] = { "Celsius", "15 min", "On" };
    host_notice_t notice = { .icon = HOST_ICON_NONE };

    switch (page) {
    case PAGE_CONTENTS:
        draw_rows(&contents, PAGE_NAMES, NULL, -1);
        break;
    case PAGE_PRIMITIVES:
        draw_primitives();
        break;
    case PAGE_LIST:
        draw_rows(&settings, SETTING_LABELS, SETTING_VALUES, -1);
        break;
    case PAGE_OPTIONS:
        draw_rows(&units, UNITS, NULL, unit_set);
        break;
    case PAGE_DETAILS:
        facts.height = gfx_height() - HOST_UI_ACTIONS_HEIGHT;
        draw_rows(&facts, FACT_LABELS, FACT_VALUES, -1);
        ui_actions("", 0, "Back", 4);
        break;
    case PAGE_CELL:
        draw_cells(1);
        break;
    case PAGE_CELLS:
        draw_cells(4);
        break;
    case PAGE_PICKER:
        app_picker_draw(&pace, PACES);
        break;
    case PAGE_DATE:
    case PAGE_TIME:
        app_date_draw(&when);
        break;
    case PAGE_MESSAGE:
        copy(notice.first, sizeof notice.first, "Not set up");
        copy(notice.detail, sizeof notice.detail, "Open the page");
        notice.icon = HOST_ICON_GEAR;
        ui_notice(&notice);
        break;
    case PAGE_QUESTION:
        copy(notice.first, sizeof notice.first, "Remove");
        copy(notice.second, sizeof notice.second, "everything?");
        copy(notice.ok, sizeof notice.ok, "Remove");
        copy(notice.back, sizeof notice.back, "Cancel");
        notice.icon = HOST_ICON_TRASH;
        ui_notice(&notice);
        break;
    case PAGE_WORKING:
        copy(notice.first, sizeof notice.first, "Loading");
        copy(notice.second, sizeof notice.second, "in progress");
        notice.is_working = 1;
        ui_notice(&notice);
        break;
    default:
        break;
    }
}
