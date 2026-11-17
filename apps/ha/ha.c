/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * A screen that is described in Home Assistant and drawn here. The
 * perINKpheral integration of Home Assistant (perinkpheral-hacs) sets it up:
 * it gives the application its server and a secret of its own (hidden
 * settings, written by the integration and not on the device's page) and
 * answers with the screen - a line a place, the parts of it divided by '|':
 *     hero|21°|Cloudy
 *     detail|54|%|drop
 *     column|Room|thermometer|22.5°
 * `hero` is the one thing the screen is for: a value of a few characters
 * and a large picture. `detail` - two of them - is a smaller value with
 * its unit and a small picture, beside the hero. `column` - up to four -
 * is a name, a picture and a value, in a row under the rule. A layout of
 * cells or a list is chosen in the integration too.
 * The application asks for the screen every so often - one request, plain
 * text back - and keeps the last answer in its store. It knows nothing of
 * what the places say: what is shown and how it is worked out is all Home
 * Assistant's.
 */
#include <stdbool.h>
#include <stddef.h>

#include "app_draw.h"
#include "app_state.h"
#include "app_text.h"
#include "app_ui.h"
#include "host_api.h"
#include "strings.h" /* made of the `strings` folder by tools/perinkpheral */

/* What is asked and what comes of it, by its tag. */
enum { REQUEST_SCREEN = HOST_REQUEST_FIRST };
#define REFRESH_ALARM 0

/* The screen as Home Assistant rendered it, kept as it came. */
#define SCREEN_KEY "screen"
/* Seven lines of the dashboard are some 350 bytes; a list has room for
 * about fifteen. What comes beyond it is cut. */
#define SCREEN_MAX 512
static char    screen[SCREEN_MAX + 1];
static int32_t screen_length;

/* The answer being taken, piece by piece: it becomes the screen only when
 * it has come whole. */
static char    coming[SCREEN_MAX + 1];
static int32_t coming_length;
static int32_t coming_status;

/* Why there is nothing to show, when there is nothing: the status of the
 * last answer, an HTTP one or a HOST_FAILED_*. 0: nothing was asked yet. */
#define STATUS_KEY "status"
#define HTTP_OK    200
static int32_t last_status;

/* The list layout: the host's list of facts, moved with UP and DOWN. */
static host_list_t list = { .selected = -1 };

/* ---- asking ---- */

/* How often: the "every" setting, "5 min" as the manifest has it when
 * nothing is chosen. */
#define REFRESH_DEFAULT_S (5 * 60)

/* One request is enough for an instance that has just asked. */
static bool has_asked;

/* One request: the screen as Home Assistant renders it. The server and the
 * token are the host's to put in - the application never sees the token. */
static void ask(void)
{
    has_asked = true;
    /* The screen is the integration's, which knows the device by the secret it gave it and
     * answers with the screen. */
    static const char URL[] = "{{server}}/api/perinkpheral/screen";
    static const char HEADERS[] = "Authorization: Bearer {{token}}";
    http_send(REQUEST_SCREEN, HOST_HTTP_GET, URL, sizeof URL - 1, HEADERS, sizeof HEADERS - 1,
                 "", 0);
}

/* The screen is asked for now and again from now on. */
/* An alarm the device has no room for: said in the log, or what depends on it is taken for a hang. */
static void say_if_unset(int32_t result, const char *what)
{
    if (result != HOST_OK) {
        log_write(HOST_LOG_WARN, what, app_text_length(what));
    }
}

static void keep_fresh(void)
{
    say_if_unset(alarm_set_every(REFRESH_ALARM, app_setting_seconds("every", REFRESH_DEFAULT_S), 0),
                 "no room for the refresh's alarm");
    ask();
}

/* ---- the answer ---- */

static void accept_answer(void)
{
    last_status = coming_status;
    kv_set(STATUS_KEY, sizeof STATUS_KEY - 1, &last_status, sizeof last_status);
    /* A sensor that is not there renders as "None": not a screen. */
    bool is_screen_payload = coming_status == HTTP_OK && coming_length > 0 && coming[0] != 'N';
    if (!is_screen_payload) {
        return; /* what was shown before stays */
    }
    for (int32_t i = 0; i < coming_length; i++) {
        screen[i] = coming[i];
    }
    screen_length = coming_length;
    screen[screen_length] = '\0';
    kv_set(SCREEN_KEY, sizeof SCREEN_KEY - 1, screen, screen_length);
}

void app_on_data(int32_t request, const uint8_t *data, int32_t length, int32_t flags,
                 int32_t http_status)
{
    if (request != REQUEST_SCREEN) {
        return;
    }
    if (flags & HOST_DATA_FAILED) {
        coming_status = http_status;
        coming_length = 0;
        accept_answer();
        screen_redraw(HOST_REDRAW_NORMAL);
        return;
    }
    if (flags & HOST_DATA_FIRST) {
        coming_status = http_status;
        coming_length = 0;
    }
    for (int32_t i = 0; i < length && coming_length < SCREEN_MAX; i++) {
        coming[coming_length++] = (char)data[i];
    }
    if (flags & HOST_DATA_LAST) {
        accept_answer();
        screen_redraw(HOST_REDRAW_NORMAL);
    }
}

/* ---- when to ask ---- */

void app_on_load(void)
{
    int32_t length = kv_get(SCREEN_KEY, sizeof SCREEN_KEY - 1, screen, SCREEN_MAX);
    screen_length = length > 0 ? length : 0;
    screen[screen_length] = '\0';
    if (kv_get(STATUS_KEY, sizeof STATUS_KEY - 1, &last_status, sizeof last_status) !=
        sizeof last_status) {
        /* The first run ever: nobody has set the alarm yet. */
        last_status = 0;
        keep_fresh();
    }
}

void app_on_intent(int32_t intent)
{
    if (intent == HOST_INTENT_STARTED) {
        keep_fresh(); /* a restart forgot the alarm */
    }
}

/* So did the version before this one, for this one. */
void app_on_updated(void)
{
    keep_fresh();
}

/* Opened, it shows what is true now and not what was true at the last
 * alarm. */
/* How far down the list the user was, kept while HA waits under another scene or is unloaded
 * for another application's answer. */
typedef struct {
    int32_t first, count;
} place_t;
APP_SESSION_FITS(place_t);

void app_on_blur(void)
{
    place_t place = { .first = list.first, .count = list.count };
    app_session_save(&place, sizeof place);
}

void app_on_focus(void)
{
    place_t place;
    if (app_session_load(&place, sizeof place)) {
        /* held to the list's end when it is drawn; the count is the last one drawn, for a press
         * that comes before the next drawing */
        list.first = place.first;
        list.count = place.count;
    }
    if (!has_asked) {
        ask();
    }
}

void app_on_settings(void)
{
    keep_fresh();
}

void app_on_alarm(int32_t id)
{
    (void)id;
    ask();
}

void app_on_button(int32_t event)
{
    if (event == BTN_OK_PRESSED) {
        ask(); /* "refresh" */
    } else if (event != BTN_BACK_PRESSED && app_list_button(&list, event) == APP_UI_CHANGED) {
        /* UP and DOWN a row, held a page; held to the list's end when it is drawn */
        screen_redraw(HOST_REDRAW_NO_BLINK);
    }
}

/* ---- the lines of the screen ---- */

#define PARTS_MAX 4
#define LINE_MAX  95

typedef struct {
    char        text[LINE_MAX + 1];
    const char *place; /* "hero", "detail", "column" */
    const char *parts[PARTS_MAX];
} line_t;

/* The next line of the screen, taken apart at the '|'. False at the end. */
static bool next_line(int32_t *at, line_t *line)
{
    while (*at < screen_length && (screen[*at] == '\n' || screen[*at] == '\r')) {
        (*at)++;
    }
    if (*at >= screen_length) {
        return false;
    }
    int32_t length = 0, parts = 0;
    line->place = line->text;
    for (; *at < screen_length && screen[*at] != '\n'; (*at)++) {
        char character = screen[*at];
        if (length >= LINE_MAX || character == '\r') {
            continue;
        }
        if (character == '|' && parts < PARTS_MAX) {
            line->text[length++] = '\0';
            line->parts[parts++] = line->text + length;
        } else {
            line->text[length++] = character;
        }
    }
    line->text[length] = '\0';
    for (int32_t i = parts; i < PARTS_MAX; i++) {
        line->parts[i] = "";
    }
    return true;
}

/* ---- text ----
 *
 * A value may have a degree sign in it, as Home Assistant gives it: the
 * host draws that one, and everything else beyond ASCII as "?". */

/* Where the degree sign is in a value, or -1. */
static int32_t degree_at(const char *text)
{
    for (int32_t i = 0; text[i] != '\0'; i++) {
        if (app_text_starts(text + i, APP_TEXT_DEGREE)) {
            return i;
        }
    }
    return -1;
}

/* ---- pictures, by the names a screen may use ---- */

/* A picture is one of the host's icons, which draws each at any size from
 * its finest drawing: the weather in fine lines, the things of a home. */
typedef struct {
    const char *name;
    int32_t     icon;
} picture_t;

/* The states of a weather entity are names here, so that
 * "{{ states('weather.home') }}" is a picture as it stands. */
static const picture_t PICTURES[] = {
    { "sunny", HOST_ICON_SUN },
    { "partlycloudy", HOST_ICON_PARTLY_CLOUDY },
    { "cloudy", HOST_ICON_CLOUD },
    { "fog", HOST_ICON_CLOUD },
    { "windy", HOST_ICON_CLOUD },
    { "windy-variant", HOST_ICON_CLOUD },
    { "rainy", HOST_ICON_RAIN },
    { "pouring", HOST_ICON_RAIN },
    { "snowy", HOST_ICON_SNOW },
    { "snowy-rainy", HOST_ICON_SNOW },
    { "hail", HOST_ICON_SNOW },
    { "lightning", HOST_ICON_THUNDER },
    { "lightning-rainy", HOST_ICON_THUNDER },
    { "clear-night", HOST_ICON_MOON },
    { "umbrella", HOST_ICON_UMBRELLA },
    { "wind", HOST_ICON_WIND },
    { "drop", HOST_ICON_DROP },
    { "co2", HOST_ICON_CO2 },
    { "bolt", HOST_ICON_BOLT },
    { "gauge", HOST_ICON_GAUGE },
    { "thermometer", HOST_ICON_THERMOMETER },
    { "battery", HOST_ICON_BATTERY },
    { "bulb", HOST_ICON_BULB },
    { "door", HOST_ICON_DOOR },
    { "home", HOST_ICON_HOME },
    { "moon", HOST_ICON_MOON },
    { "plug", HOST_ICON_PLUG },
    { "chip", HOST_ICON_CHIP },
    { "network", HOST_ICON_NETWORK },
    { "bell", HOST_ICON_BELL },
    { "clock", HOST_ICON_CLOCK },
    { "check", HOST_ICON_CHECK },
    { "warning", HOST_ICON_WARNING },
    { "info", HOST_ICON_INFO },
    { "play", HOST_ICON_PLAY },
    { "sunrise", HOST_ICON_SUNRISE },
    { "sunset", HOST_ICON_SUNSET },
};

static const picture_t *picture_named(const char *name)
{
    for (size_t i = 0; i < sizeof PICTURES / sizeof PICTURES[0]; i++) {
        if (app_text_equal(name, PICTURES[i].name)) {
            return &PICTURES[i];
        }
    }
    return NULL;
}

/* The three sizes a picture stands in: beside the hero, over a column,
 * beside a detail. */
#define HERO_PICTURE   88
#define COLUMN_PICTURE 44
#define DETAIL_PICTURE 22

static void draw_picture(const picture_t *picture, int32_t left, int32_t top, int32_t side)
{
    gfx_draw_icon(left, top, picture->icon, side / HOST_ICON_CELLS);
}

/* ---- the screen: the weather's own composition, its places filled ---- */

#define MARGIN       16
/* The hero stands a little inside the rule under it, on both sides. */
#define HERO_LEFT    (MARGIN + 6)
/* From the picture to the value. */
#define HERO_TEXT_GAP 22
/* Two characters are as large as there is; a longer value steps down one
 * size, or it would run into the details. */
#define HERO_CHARACTERS_MAX 2
/* The details: two lines in the middle of the hero's height, their
 * pictures in one column, a unit in the small font after the value. */
#define DETAIL_TOP      30
#define DETAIL_LINE     36
#define DETAIL_GAP      10
#define DETAILS_MAX     2
/* The rule stands as far under the hero as the hero stands under the
 * header's rule. */
#define RULE_TOP        (MARGIN + HERO_PICTURE + MARGIN)
/* A column: its name as a caption, its picture, its value. */
#define COLUMN_NAME_TOP    133
#define COLUMN_PICTURE_TOP 158
#define COLUMN_VALUE_TOP   214
#define COLUMNS_MAX        4

static void draw_hero(const line_t *line)
{
    const char *value = line->parts[0];
    const picture_t *picture = picture_named(line->parts[1]);
    int32_t text_left = HERO_LEFT;
    if (picture != NULL) {
        draw_picture(picture, HERO_LEFT, MARGIN, HERO_PICTURE);
        text_left += HERO_PICTURE + HERO_TEXT_GAP;
    }
    int32_t degree = degree_at(value);
    int32_t characters = degree >= 0 ? degree : app_text_length(value);
    int32_t font = (characters <= HERO_CHARACTERS_MAX ? HOST_FONT_DISPLAY : HOST_FONT_LARGE) |
                   HOST_FONT_TABULAR;
    /* The value stands in the middle of the picture's height. */
    int32_t cap_height = gfx_font_cap_height(font);
    int32_t cap_top = MARGIN + (HERO_PICTURE - cap_height) / 2;
    app_draw_text(text_left, cap_top - (gfx_font_ascent(font) - cap_height), font, value);
}

typedef struct {
    char value[24];
    char unit[12];
    const picture_t *picture;
} detail_line_t;

static int32_t detail_width(const detail_line_t *detail)
{
    return (detail->picture != NULL ? DETAIL_PICTURE + DETAIL_GAP : 0) +
           ui_value_width(HOST_FONT_TEXT, detail->value, app_text_length(detail->value),
                          detail->unit, app_text_length(detail->unit));
}

static void draw_detail(int32_t left, int32_t number, int32_t count, const detail_line_t *detail,
                        bool has_pictures)
{
    /* One alone stands where the middle of two would be. */
    int32_t top = DETAIL_TOP + number * DETAIL_LINE + (count == 1 ? DETAIL_LINE / 2 : 0);
    int32_t baseline = top + gfx_font_ascent(HOST_FONT_TEXT);
    if (detail->picture != NULL) {
        /* The picture stands on the text's baseline. */
        draw_picture(detail->picture, left, baseline - DETAIL_PICTURE, DETAIL_PICTURE);
    }
    /* The value and, after it, its unit: the host's to set. */
    ui_draw_value(left + (has_pictures ? DETAIL_PICTURE + DETAIL_GAP : 0), top, HOST_FONT_TEXT,
             detail->value, app_text_length(detail->value), detail->unit,
             app_text_length(detail->unit));
}

static void draw_column(int32_t number, int32_t count, const line_t *line)
{
    int32_t width = gfx_width() / count;
    int32_t center = number * width + width / 2;
    const picture_t *picture = picture_named(line->parts[1]);
    app_draw_centered(center, COLUMN_NAME_TOP, HOST_FONT_SMALL, line->parts[0]);
    if (picture != NULL) {
        draw_picture(picture, center - COLUMN_PICTURE / 2, COLUMN_PICTURE_TOP, COLUMN_PICTURE);
    }
    app_draw_centered(center, COLUMN_VALUE_TOP, HOST_FONT_TEXT, line->parts[2]);
}

/* ---- the other layouts: items in cells, one large, or in a list ----
 *
 * "layout|tiles", "layout|single" or "layout|list" as the first line, and
 * then lines of one shape whatever the layout: "item|name|picture|value".
 * The description still says what is shown and never where: a layout is
 * chosen whole, and the items fall into its places in their order. */

#define ITEMS_MAX 12

typedef struct {
    char name[48]; /* as wide as the host's cell takes: half as many letters of Cyrillic */
    char value[48];
    const picture_t *picture;
} item_t;

static int32_t read_items(item_t items[ITEMS_MAX])
{
    line_t  line;
    int32_t at = 0, count = 0;
    while (count < ITEMS_MAX && next_line(&at, &line)) {
        if (app_text_equal(line.place, "item")) {
            app_text_copy(items[count].name, sizeof items[count].name, line.parts[0]);
            items[count].picture = picture_named(line.parts[1]);
            app_text_copy(items[count].value, sizeof items[count].value, line.parts[2]);
            count++;
        }
    }
    return count;
}

/* An item as a cell of the host's - its name, its picture and its value,
 * laid out and sized by the kit - and the icon in it, or HOST_ICON_NONE. */
static int32_t cell_of(host_cell_t *cell, int32_t left, int32_t top, int32_t width, int32_t height,
                       const item_t *item)
{
    *cell = (host_cell_t){ .x = left, .y = top, .width = width, .height = height };
    app_text_copy(cell->name, sizeof cell->name, item->name);
    app_text_copy(cell->value, sizeof cell->value, item->value);
    return item->picture == NULL ? HOST_ICON_NONE : item->picture->icon;
}

/* One item is the whole screen, two are halves side by side, three and
 * four are quarters, with a rule between neighbours. */
static void draw_cells_of(const item_t *items, int32_t count)
{
    int32_t columns = count > 1 ? 2 : 1, rows = count > 2 ? 2 : 1;
    int32_t width = gfx_width() / columns, height = gfx_height() / rows;
    int32_t shown = count < columns * rows ? count : columns * rows;
    host_cell_t cells[4];
    int32_t     icons[4];
    for (int32_t i = 0; i < shown; i++) {
        icons[i] = cell_of(&cells[i], i % columns * width, i / columns * height, width, height, &items[i]);
    }
    app_draw_cells(cells, 0, icons, shown); /* one size for all of them */
    if (columns > 1) {
        gfx_draw_rect(width, MARGIN, 1, gfx_height() - 2 * MARGIN, HOST_RECT_FILLED);
    }
    if (rows > 1) {
        gfx_draw_rect(MARGIN, height, gfx_width() - 2 * MARGIN, 1, HOST_RECT_FILLED);
    }
}

/* A list: the host's own, of facts nobody chooses from - a name at the
 * left edge, its value at the right. UP and DOWN move it when it is longer
 * than the screen. */
static void draw_list_of(const item_t *items, int32_t count)
{
    list.count = count;
    int32_t first = app_list_first(&list), shown = ui_list_visible(&list);
    for (int32_t i = first; i < first + shown && i < count; i++) {
        ui_draw_list_row(&list, i, items[i].name, app_text_length(items[i].name), items[i].value,
                    app_text_length(items[i].value), HOST_ICON_NONE);
    }
}

typedef enum { LAYOUT_HERO, LAYOUT_TILES, LAYOUT_SINGLE, LAYOUT_LIST } layout_t;

static layout_t layout_asked(void)
{
    line_t  line;
    int32_t at = 0;
    while (next_line(&at, &line)) {
        if (app_text_equal(line.place, "layout")) {
            return app_text_equal(line.parts[0], "tiles")    ? LAYOUT_TILES
                   : app_text_equal(line.parts[0], "single") ? LAYOUT_SINGLE
                   : app_text_equal(line.parts[0], "list")   ? LAYOUT_LIST
                                                 : LAYOUT_HERO;
        }
    }
    return LAYOUT_HERO;
}

static void draw_hero_layout(void)
{
    line_t line;
    detail_line_t details[DETAILS_MAX];
    int32_t at = 0, detail_count = 0, column_count = 0;
    bool    has_detail_pictures = false;
    /* What there is of each place, before any of it is drawn: the details
     * are one block, and the columns share the width. */
    while (next_line(&at, &line)) {
        if (app_text_equal(line.place, "detail") && detail_count < DETAILS_MAX) {
            detail_line_t *detail = &details[detail_count++];
            app_text_copy(detail->value, sizeof detail->value, line.parts[0]);
            app_text_copy(detail->unit, sizeof detail->unit, line.parts[1]);
            detail->picture = picture_named(line.parts[2]);
            has_detail_pictures = has_detail_pictures || detail->picture != NULL;
        } else if (app_text_equal(line.place, "column") && column_count < COLUMNS_MAX) {
            column_count++;
        }
    }
    /* The details are as far from the right end of the rule as the hero
     * is from its left end; their pictures are one column. */
    int32_t widest = 0;
    for (int32_t i = 0; i < detail_count; i++) {
        int32_t width = detail_width(&details[i]) +
                        (has_detail_pictures && details[i].picture == NULL
                             ? DETAIL_PICTURE + DETAIL_GAP
                             : 0);
        widest = width > widest ? width : widest;
    }
    for (int32_t i = 0; i < detail_count; i++) {
        draw_detail(gfx_width() - HERO_LEFT - widest, i, detail_count, &details[i],
                    has_detail_pictures);
    }

    gfx_draw_rect(MARGIN, RULE_TOP, gfx_width() - 2 * MARGIN, 1, HOST_RECT_FILLED);
    int32_t column = 0;
    at = 0;
    while (next_line(&at, &line)) {
        if (app_text_equal(line.place, "hero")) {
            draw_hero(&line);
        } else if (app_text_equal(line.place, "column") && column < column_count) {
            draw_column(column++, column_count, &line);
        }
    }
}

/* Why there is no screen: as every application says it, and for an
 * answer that came with no screen in it, this one's own word. */
static void draw_reason(void)
{
    if (ui_draw_reason(last_status) == 0) {
        host_notice_t no_screen = { .icon = HOST_ICON_WARNING };
        app_text_copy(no_screen.first, sizeof no_screen.first, app_string(STR_NO_SCREEN));
        app_text_copy(no_screen.detail, sizeof no_screen.detail, app_string(STR_SET_UP_IN_HA));
        ui_draw_notice(&no_screen);
    }
}

void app_on_draw(void)
{
    if (screen_length > 0) {
        static item_t items[ITEMS_MAX]; /* too large for the stack */
        layout_t      layout = layout_asked();
        int32_t       count = layout == LAYOUT_HERO ? 0 : read_items(items);
        if (layout == LAYOUT_HERO) {
            draw_hero_layout();
        } else if (layout == LAYOUT_LIST) {
            draw_list_of(items, count);
        } else {
            draw_cells_of(items, layout == LAYOUT_SINGLE && count > 1 ? 1 : count);
        }
    } else {
        draw_reason();
    }
}
