/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * A screen that is described in Home Assistant and drawn here.
 *
 * In Home Assistant it is one template sensor with an attribute `screen`.
 * It does not arrange anything: the screen is drawn here, by hand, the
 * way the weather is, and the attribute says what stands in its places -
 * a line a place, the parts of it divided by '|', the values put in by
 * Home Assistant's own templates:
 *
 *     hero|{{ state_attr('weather.home','temperature') | round }}°|{{ states('weather.home') }}
 *     detail|{{ state_attr('weather.home','humidity') }}|%|drop
 *     detail|{{ state_attr('weather.home','pressure') | round }}|hPa|gauge
 *     column|Room|thermometer|{{ states('sensor.room') | round(1) }}°
 *     column|Fridge|thermometer|{{ states('sensor.fridge') }}°
 *     column|CO2|gauge|{{ states('sensor.co2') }}
 *
 * `hero` is the one thing the screen is for: a value of a few characters
 * and a large picture. `detail` - two of them - is a smaller value with
 * its unit and a small picture, beside the hero. `column` - up to four -
 * is a name, a picture and a value, in a row under the rule.
 *
 * The application asks for that attribute, already rendered, every so
 * often - one request, plain text back - and keeps the last answer in
 * its store. It knows nothing of what the places say: what is shown and
 * how it is worked out is all Home Assistant's.
 */
#include <stdbool.h>
#include <stddef.h>

#include "app_text.h"
#include "host_api.h"
#include "icons.h"

#define SECONDS_PER_MINUTE 60

/* What is asked and what comes of it, by its tag. */
enum { REQUEST_SCREEN = HOST_REQUEST_FIRST };
#define REFRESH_ALARM 0

/* The screen as Home Assistant rendered it, kept as it came. */
#define SCREEN_KEY "screen"
#define SCREEN_MAX 1000 /* a value of the store */
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
#define HTTP_OK           200
#define HTTP_UNAUTHORIZED 401
static int32_t last_status;

/* The list layout: the host's list of facts, moved with UP and DOWN. */
static host_list_t list = { .selected = -1 };

static int32_t length_of(const char *text)
{
    return app_text_length(text);
}

/* ---- asking ---- */

static int32_t refresh_seconds(void)
{
    static const struct {
        const char *name;
        int32_t     seconds;
    } PACES[] = { { "1 min", 60 }, { "5 min", 300 }, { "15 min", 900 }, { "1 h", 3600 } };
    char chosen[16];
    int32_t length = kv_get("every", 5, chosen, sizeof chosen - 1);
    chosen[length > 0 ? length : 0] = '\0';
    for (size_t i = 0; i < sizeof PACES / sizeof PACES[0]; i++) {
        const char *name = PACES[i].name;
        int32_t at = 0;
        while (name[at] != '\0' && name[at] == chosen[at]) {
            at++;
        }
        if (name[at] == chosen[at]) {
            return PACES[i].seconds;
        }
    }
    return 5 * SECONDS_PER_MINUTE; /* nothing chosen: the manifest's default */
}

/* One request: "render this attribute of that sensor". The server and the
 * token are the host's to put in - the application never sees the token. */
/* One request is enough for an instance that has just asked. */
static bool has_asked;

static void ask(void)
{
    has_asked = true;
    char entity[64];
    int32_t length = kv_get("entity", 6, entity, sizeof entity - 1);
    entity[length > 0 ? length : 0] = '\0';

    if (entity[0] == '\0') {
        /* No sensor named: the screen is the integration's, which knows the
         * device by the secret it gave it and answers with the screen. */
        static const char URL[] = "{{server}}/api/perinkpheral/screen";
        static const char HEADERS[] = "Authorization: Bearer {{token}}";
        http_request(REQUEST_SCREEN, HOST_HTTP_GET, URL, sizeof URL - 1, HEADERS,
                     sizeof HEADERS - 1, "", 0);
        return;
    }
    /* A sensor of the user's own making: its attribute, rendered as a template. */
    static const char URL[] = "{{server}}/api/template";
    static const char HEADERS[] = "Authorization: Bearer {{token}}\nContent-Type: application/json";
    char body[160];
    app_text_t text;
    app_text_start(&text, body, sizeof body);
    app_text_add(&text, "{\"template\":\"{{ state_attr('");
    app_text_add(&text, entity);
    app_text_add(&text, "','screen') }}\"}");
    http_request(REQUEST_SCREEN, HOST_HTTP_POST, URL, sizeof URL - 1, HEADERS, sizeof HEADERS - 1,
                 body, length_of(body));
}

/* The screen is asked for now and again from now on. */
static void keep_fresh(void)
{
    alarm_set_every(REFRESH_ALARM, refresh_seconds(), 0);
    ask();
}

/* ---- the answer ---- */

static void take_the_answer(void)
{
    last_status = coming_status;
    kv_set(STATUS_KEY, sizeof STATUS_KEY - 1, &last_status, sizeof last_status);
    /* A sensor that is not there renders as "None": not a screen. */
    bool is_a_screen = coming_status == HTTP_OK && coming_length > 0 && coming[0] != 'N';
    if (!is_a_screen) {
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
        take_the_answer();
        request_redraw(HOST_REDRAW_NORMAL);
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
        take_the_answer();
        request_redraw(HOST_REDRAW_NORMAL);
    }
}

/* ---- when to ask ---- */

/* ---- the time in the header, for whoever wants it ----
 *
 * A setting, off unless turned on: the hour and the minute on the right of
 * the host's header, where the date stands. It follows the clock only
 * while the application is on the screen, which is when its header is. */
#define HEADER_TIME_KEY    "header_time"
#define MINUTE_TIMER       0
#define SECONDS_PER_MINUTE 60
#define MS_PER_SECOND      1000
#define CLOCK_RETRY_MS     5000 /* while the time is not known yet */

static void show_time_in_header(void)
{
    char value[4];
    bool is_wanted = kv_get(HEADER_TIME_KEY, sizeof HEADER_TIME_KEY - 1, value, sizeof value) == 1 &&
                     value[0] == '1';
    host_local_time_t now;
    if (!is_wanted) {
        header_text(HOST_HEADER_RIGHT, "", 0); /* the host's date again */
        timer_set(MINUTE_TIMER, 0);
        return;
    }
    if (time_local(&now) != HOST_OK) {
        timer_set(MINUTE_TIMER, CLOCK_RETRY_MS);
        return;
    }
    char       line[8];
    app_text_t text;
    app_text_start(&text, line, sizeof line);
    app_text_add_padded(&text, now.hour, 2);
    app_text_add(&text, ":");
    app_text_add_padded(&text, now.minute, 2);
    header_text(HOST_HEADER_RIGHT, line, app_text_length(line));
    timer_set(MINUTE_TIMER, (SECONDS_PER_MINUTE - now.second) * MS_PER_SECOND);
}

void app_on_timer(int32_t id)
{
    (void)id;
    show_time_in_header();
    request_redraw(HOST_REDRAW_NORMAL);
}

void app_init(void)
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

/* Opened, it shows what is true now and not what was true at the last
 * alarm. */
void app_on_focus(void)
{
    show_time_in_header();
    if (!has_asked) {
        ask();
    }
}

void app_on_settings(void)
{
    show_time_in_header();
    request_redraw(HOST_REDRAW_NORMAL);
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
    } else if (event == BTN_UP_PRESSED && list.first > 0) {
        list.first--;
        request_redraw(HOST_REDRAW_NO_BLINK);
    } else if (event == BTN_DOWN_PRESSED) {
        list.first++; /* held to the list's end when it is drawn */
        request_redraw(HOST_REDRAW_NO_BLINK);
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

static bool is(const char *text, const char *word)
{
    while (*word != '\0' && *text == *word) {
        text++;
        word++;
    }
    return *text == *word;
}

/* ---- text ----
 *
 * A value may have a degree sign in it, as Home Assistant gives it: the
 * host draws that one, and everything else beyond ASCII as "?". */

#define DEGREE_FIRST  ((char)0xC2)
#define DEGREE_SECOND ((char)0xB0)

static int32_t degree_at(const char *text)
{
    for (int32_t i = 0; text[i] != '\0'; i++) {
        if (text[i] == DEGREE_FIRST && text[i + 1] == DEGREE_SECOND) {
            return i;
        }
    }
    return -1;
}

static int32_t text_width(int32_t font, const char *text)
{
    return gfx_text_width(font, text, app_text_length(text));
}

/* `top` is the top of the line, as gfx_text takes it. Returns the width. */
static int32_t draw_text(int32_t left, int32_t top, int32_t font, const char *text)
{
    return gfx_text(left, top, font, text, app_text_length(text));
}

static void draw_centered(int32_t center, int32_t top, int32_t font, const char *text)
{
    draw_text(center - text_width(font, text) / 2, top, font, text);
}

/* ---- pictures, by the names a screen may use ---- */

/* Where a picture is drawn from: each kind is a sheet of its own grid. */
typedef enum {
    PICTURE_WEATHER, /* an icon_t: the weather's own, 44 and 22 cells */
    PICTURE_DETAIL,  /* a detail_t, 11 cells */
    PICTURE_THING,   /* a thing_t, 11 cells */
    PICTURE_FINE,    /* a fine_t, 22 cells */
} picture_kind_t;

typedef struct {
    const char    *name;
    picture_kind_t kind;
    int32_t        index;
} picture_t;

/* The states of a weather entity are names here, so that
 * "{{ states('weather.home') }}" is a picture as it stands. */
static const picture_t PICTURES[] = {
    { "sunny", PICTURE_WEATHER, ICON_CLEAR },
    { "partlycloudy", PICTURE_WEATHER, ICON_PARTLY_CLOUDY },
    { "cloudy", PICTURE_WEATHER, ICON_CLOUDY },
    { "fog", PICTURE_WEATHER, ICON_CLOUDY },
    { "windy", PICTURE_WEATHER, ICON_CLOUDY },
    { "windy-variant", PICTURE_WEATHER, ICON_CLOUDY },
    { "rainy", PICTURE_WEATHER, ICON_RAIN },
    { "pouring", PICTURE_WEATHER, ICON_RAIN },
    { "snowy", PICTURE_WEATHER, ICON_SNOW },
    { "snowy-rainy", PICTURE_WEATHER, ICON_SNOW },
    { "hail", PICTURE_WEATHER, ICON_SNOW },
    { "lightning", PICTURE_WEATHER, ICON_THUNDER },
    { "lightning-rainy", PICTURE_WEATHER, ICON_THUNDER },
    { "clear-night", PICTURE_THING, THING_MOON },
    { "umbrella", PICTURE_DETAIL, DETAIL_RAIN },
    { "wind", PICTURE_DETAIL, DETAIL_WIND },
    { "drop", PICTURE_FINE, FINE_DROP },
    { "co2", PICTURE_FINE, FINE_CO2 },
    { "bolt", PICTURE_FINE, FINE_BOLT },
    { "gauge", PICTURE_THING, THING_GAUGE },
    { "thermometer", PICTURE_THING, THING_THERMOMETER },
    { "battery", PICTURE_THING, THING_BATTERY },
    { "bulb", PICTURE_THING, THING_BULB },
    { "door", PICTURE_THING, THING_DOOR },
    { "home", PICTURE_THING, THING_HOME },
    { "moon", PICTURE_THING, THING_MOON },
    { "plug", PICTURE_THING, THING_PLUG },
    { "chip", PICTURE_THING, THING_CHIP },
    { "network", PICTURE_THING, THING_NETWORK },
    { "bell", PICTURE_THING, THING_BELL },
    { "clock", PICTURE_THING, THING_CLOCK },
    { "check", PICTURE_THING, THING_CHECK },
    { "warning", PICTURE_THING, THING_WARNING },
    { "info", PICTURE_THING, THING_INFO },
    { "play", PICTURE_THING, THING_PLAY },
};

static const picture_t *picture_named(const char *name)
{
    for (size_t i = 0; i < sizeof PICTURES / sizeof PICTURES[0]; i++) {
        if (is(name, PICTURES[i].name)) {
            return &PICTURES[i];
        }
    }
    return NULL;
}

/* The three sizes a picture stands in: beside the hero, over a column,
 * beside a detail. */
#define HERO_PICTURE   (LARGE_ICON_CELLS * ICON_CELL_PIXELS)  /* 88 */
#define COLUMN_PICTURE (SMALL_ICON_CELLS * ICON_CELL_PIXELS)  /* 44 */
#define DETAIL_PICTURE (DETAIL_ICON_CELLS * ICON_CELL_PIXELS) /* 22 */

/* The drawing of a picture for a square of `side` pixels: the weather and
 * the thermometer have one made for the largest. */
static const uint8_t *picture_bits(const picture_t *picture, int32_t side)
{
    switch (picture->kind) {
    case PICTURE_WEATHER:
        return side >= HERO_PICTURE ? LARGE_ICONS[picture->index] : SMALL_ICONS[picture->index];
    case PICTURE_DETAIL:
        return DETAIL_ICONS[picture->index];
    case PICTURE_FINE:
        return FINE_ICONS[picture->index];
    case PICTURE_THING:
    default:
        return picture->index == THING_THERMOMETER && side >= HERO_PICTURE
                   ? LARGE_THERMOMETER
                   : THING_ICONS[picture->index];
    }
}

static void draw_picture(const picture_t *picture, int32_t left, int32_t top, int32_t side)
{
    const uint8_t *bits = picture_bits(picture, side);
    gfx_picture(left, top, bits, side / bits[0]);
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
#define DETAIL_UNIT_GAP 5
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
    draw_text(text_left, cap_top - (gfx_font_ascent(font) - cap_height), font, value);
}

typedef struct {
    char value[24];
    char unit[12];
    const picture_t *picture;
} detail_line_t;

static int32_t detail_width(const detail_line_t *detail)
{
    int32_t width = text_width(HOST_FONT_TEXT, detail->value);
    if (detail->picture != NULL) {
        width += DETAIL_PICTURE + DETAIL_GAP;
    }
    if (detail->unit[0] != '\0') {
        width += DETAIL_UNIT_GAP +
                 gfx_text_width(HOST_FONT_SMALL, detail->unit, app_text_length(detail->unit));
    }
    return width;
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
    int32_t value_left = left + (has_pictures ? DETAIL_PICTURE + DETAIL_GAP : 0);
    int32_t value_right = value_left + draw_text(value_left, top, HOST_FONT_TEXT, detail->value);
    gfx_text(value_right + DETAIL_UNIT_GAP, baseline - gfx_font_ascent(HOST_FONT_SMALL),
             HOST_FONT_SMALL, detail->unit, app_text_length(detail->unit));
}

static void copy(char *to, int32_t size, const char *from)
{
    app_text_t text;
    app_text_start(&text, to, size);
    app_text_add(&text, from);
}

static void draw_column(int32_t number, int32_t count, const line_t *line)
{
    int32_t width = gfx_width() / count;
    int32_t center = number * width + width / 2;
    const picture_t *picture = picture_named(line->parts[1]);
    draw_centered(center, COLUMN_NAME_TOP, HOST_FONT_SMALL, line->parts[0]);
    if (picture != NULL) {
        draw_picture(picture, center - COLUMN_PICTURE / 2, COLUMN_PICTURE_TOP, COLUMN_PICTURE);
    }
    draw_centered(center, COLUMN_VALUE_TOP, HOST_FONT_TEXT, line->parts[2]);
}

/* ---- the other layouts: items in cells, one large, or in a list ----
 *
 * "layout|tiles", "layout|single" or "layout|list" as the first line, and
 * then lines of one shape whatever the layout: "item|name|picture|value".
 * The description still says what is shown and never where: a layout is
 * chosen whole, and the items fall into its places in their order. */

#define ITEMS_MAX 12

typedef struct {
    char name[24];
    char value[24];
    const picture_t *picture;
} item_t;

static int32_t read_items(item_t items[ITEMS_MAX])
{
    line_t  line;
    int32_t at = 0, count = 0;
    while (count < ITEMS_MAX && next_line(&at, &line)) {
        if (is(line.place, "item")) {
            copy(items[count].name, sizeof items[count].name, line.parts[0]);
            items[count].picture = picture_named(line.parts[1]);
            copy(items[count].value, sizeof items[count].value, line.parts[2]);
            count++;
        }
    }
    return count;
}

/* An item as a cell of the host's: its name, its picture and its value,
 * laid out and sized by the kit. */
static const uint8_t *cell_of(host_cell_t *cell, int32_t left, int32_t top, int32_t width,
                              int32_t height, const item_t *item)
{
    *cell = (host_cell_t){ .x = left, .y = top, .width = width, .height = height };
    copy(cell->name, sizeof cell->name, item->name);
    copy(cell->value, sizeof cell->value, item->value);
    bool is_whole_screen = width >= gfx_width() && height >= gfx_height();
    return item->picture == NULL
               ? NULL
               : picture_bits(item->picture, is_whole_screen ? HOST_UI_CELL_PICTURE_LARGE
                                                             : HOST_UI_CELL_PICTURE_SMALL);
}

/* One item is the whole screen, two are halves side by side, three and
 * four are quarters, with a rule between neighbours. */
static void draw_cells_of(const item_t *items, int32_t count)
{
    int32_t columns = count > 1 ? 2 : 1, rows = count > 2 ? 2 : 1;
    int32_t width = gfx_width() / columns, height = gfx_height() / rows;
    int32_t shown = count < columns * rows ? count : columns * rows;
    /* One size for all of them: the size the longest value can have. */
    host_cell_t cell;
    int32_t     size = 0;
    for (int32_t i = 0; i < shown; i++) {
        const uint8_t *picture =
            cell_of(&cell, i % columns * width, i / columns * height, width, height, &items[i]);
        int32_t fits = ui_cell_fit(&cell, picture);
        size = fits > size ? fits : size;
    }
    for (int32_t i = 0; i < shown; i++) {
        const uint8_t *picture =
            cell_of(&cell, i % columns * width, i / columns * height, width, height, &items[i]);
        cell.size = size;
        ui_cell(&cell, picture);
    }
    if (columns > 1) {
        gfx_rect(width, MARGIN, 1, gfx_height() - 2 * MARGIN, HOST_RECT_FILLED);
    }
    if (rows > 1) {
        gfx_rect(MARGIN, height, gfx_width() - 2 * MARGIN, 1, HOST_RECT_FILLED);
    }
}

/* A list: the host's own, of facts nobody chooses from - a name at the
 * left edge, its value at the right. UP and DOWN move it when it is longer
 * than the screen. */
static void draw_list_of(const item_t *items, int32_t count)
{
    list.count = count;
    int32_t shown = ui_list_visible(&list);
    if (list.first > count - shown) {
        list.first = count > shown ? count - shown : 0;
    }
    for (int32_t i = list.first; i < list.first + shown && i < count; i++) {
        ui_list_row(&list, i, items[i].name, app_text_length(items[i].name), items[i].value,
                    app_text_length(items[i].value), HOST_ICON_NONE);
    }
}

typedef enum { LAYOUT_HERO, LAYOUT_TILES, LAYOUT_SINGLE, LAYOUT_LIST } layout_t;

static layout_t layout_asked(void)
{
    line_t  line;
    int32_t at = 0;
    while (next_line(&at, &line)) {
        if (is(line.place, "layout")) {
            return is(line.parts[0], "tiles")    ? LAYOUT_TILES
                   : is(line.parts[0], "single") ? LAYOUT_SINGLE
                   : is(line.parts[0], "list")   ? LAYOUT_LIST
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
        if (is(line.place, "detail") && detail_count < DETAILS_MAX) {
            detail_line_t *detail = &details[detail_count++];
            copy(detail->value, sizeof detail->value, line.parts[0]);
            copy(detail->unit, sizeof detail->unit, line.parts[1]);
            detail->picture = picture_named(line.parts[2]);
            has_detail_pictures = has_detail_pictures || detail->picture != NULL;
        } else if (is(line.place, "column") && column_count < COLUMNS_MAX) {
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

    gfx_rect(MARGIN, RULE_TOP, gfx_width() - 2 * MARGIN, 1, HOST_RECT_FILLED);
    int32_t column = 0;
    at = 0;
    while (next_line(&at, &line)) {
        if (is(line.place, "hero")) {
            draw_hero(&line);
        } else if (is(line.place, "column") && column < column_count) {
            draw_column(column++, column_count, &line);
        }
    }
}

/* Nothing to show yet, and why. */
static void draw_reason(void)
{
    host_notice_t notice = { .icon = HOST_ICON_GEAR };
    app_text_t first, detail;
    app_text_start(&first, notice.first, sizeof notice.first);
    app_text_start(&detail, notice.detail, sizeof notice.detail);
    if (last_status == 0) {
        app_text_add(&first, "Loading");
        notice.icon = HOST_ICON_HOURGLASS;
    } else if (last_status == HOST_FAILED_NOT_READY) {
        app_text_add(&first, "Not set up");
        app_text_add(&detail, "Open the page");
    } else if (last_status == HOST_FAILED_NO_NETWORK) {
        app_text_add(&first, "No network");
        notice.icon = HOST_ICON_NO_SIGNAL;
    } else if (last_status == HTTP_UNAUTHORIZED) {
        app_text_add(&first, "Access denied");
        app_text_add(&detail, "Check the token");
        notice.icon = HOST_ICON_WARNING;
    } else if (last_status == HTTP_OK) {
        app_text_add(&first, "No screen");
        app_text_add(&detail, "Set it up in HA");
        notice.icon = HOST_ICON_WARNING;
    } else {
        app_text_add(&first, "No answer");
        app_text_add(&detail, "Code ");
        app_text_add_number(&detail, last_status);
        notice.icon = HOST_ICON_WARNING;
    }
    ui_notice(&notice);
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
