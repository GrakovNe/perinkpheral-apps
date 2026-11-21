// SPDX-License-Identifier: MIT
/*
 * Statuspage summaries of the services the user lists, affected first.
 * An item of the list is a status page's host; Perinkpheral OS asks them one at
 * a time (the manifest's "each") and an answer updates its own row.
 * UP and DOWN scroll the rows; OK asks every service again.
 *
 * The store keeps one record of all rows, written when a row changes, and
 * the time the last round ended, written once a round: an answer that says
 * what the last one said writes nothing.
 */
#include "app_draw.h"
#include "app_state.h"
#include "app_text.h"
#include "app_values.h"
#include "host_api.h"
#include "manifest_limits.h"
#include "strings.h"

#define SERVICES "services"
#define ANSWERS  "answers"
#define CHECKED  "checked"

enum {
    /* One-character items with a line break between fill the list's value. */
    SERVICES_MAX = (MANIFEST_SETTING_VALUE_MAX + 1) / 2,
    NAME_SIZE = 48,
    RECORD_VERSION = 3,
    RECORD_HEADER = 8, /* version, count, name room, two spare bytes */
    SLOT_FIXED = 7,    /* source, updated_at, state */
    CHECKED_VERSION = 1,
    OPERATIONAL = 0,
    UNKNOWN = 5,
    HAS_DATA = 1,
    FAILED = 2, /* the last answer was not a good one: the row keeps the one before */
    ROWS_TOP = 10,
    ROW_HEIGHT = 62,
};

/* The store as Perinkpheral OS charges it: the list at its longest, the pace, the
 * record at its largest and the round's time. */
#define STORED(bytes) (64 + ((bytes) + 31) / 32 * 32)
_Static_assert(RECORD_HEADER + SERVICES_MAX * SLOT_FIXED <= HOST_KV_VALUE_MAX,
               "the longest list fits one value");
_Static_assert(STORED(MANIFEST_SETTING_VALUE_MAX) + STORED(MANIFEST_SETTING_OPTION_MAX) +
                       STORED(HOST_KV_VALUE_MAX) + STORED(8) <= HOST_KV_TOTAL_MAX,
               "settings and answers fit the store");

typedef struct {
    uint16_t source; /* of the listed host: a record follows its item when the list changes */
    uint8_t  indicator;
    uint8_t  flags;
    uint32_t updated_at;
    char     name[NAME_SIZE];
} service_t;

typedef struct { int32_t version; uint32_t at; } checked_t;
typedef struct { int32_t first_row; } session_t;
APP_SESSION_FITS(session_t);

static service_t services[SERVICES_MAX];
static int32_t   count;
static char      listed[MANIFEST_SETTING_VALUE_MAX + 1]; /* the items, each terminated */
static uint8_t   item_at[SERVICES_MAX];
static uint32_t  list_hash;
static uint32_t  checked_at;
static bool      answers_unsaved; /* the store refused the last write of them */
static uint8_t   record[HOST_KV_VALUE_MAX];
static uint8_t   order[SERVICES_MAX];
static int32_t   first_row, shown_rows = 4;

static const char *const LEGACY_PAGES[] = { "page1", "page2", "page3", "page4" };
static const char *const INDICATORS[] = { "none", "minor", "major", "critical", "maintenance" };
static const int32_t WORDS[] = { STR_OPERATIONAL, STR_MINOR, STR_MAJOR, STR_CRITICAL, STR_MAINTENANCE, STR_UNKNOWN };
static const int32_t ICONS[] = { HOST_ICON_CHECK, HOST_ICON_WARNING, HOST_ICON_CROSS, HOST_ICON_CROSS, HOST_ICON_GEAR, HOST_ICON_WARNING };

static uint32_t text_hash(const char *text, int32_t length)
{
    uint32_t hash = 2166136261u;
    for (int32_t at = 0; at < length; at++) { hash = (hash ^ (uint8_t)text[at]) * 16777619u; }
    return hash;
}

static uint16_t source_of(const char *host)
{
    uint32_t hash = text_hash(host, app_text_length(host));
    return (uint16_t)(hash ^ hash >> 16);
}

/* The bytes of a name a record of `services_count` rows has room for. */
static int32_t name_room(int32_t services_count)
{
    if (services_count <= 0) { return 0; }
    int32_t room = (HOST_KV_VALUE_MAX - RECORD_HEADER) / services_count - SLOT_FIXED;
    return room < 0 ? 0 : room < NAME_SIZE ? room : NAME_SIZE - 1;
}

/* Cut between characters, and said so, as Perinkpheral OS cuts a text. */
static void keep_name(char *name, const char *from, int32_t room)
{
    int32_t length = app_text_length(from);
    if (length > room) {
        length = room >= 6 ? room - 3 : room;
        while (length > 0 && ((uint8_t)from[length] & 0xC0) == 0x80) { length--; }
    }
    for (int32_t at = 0; at < length; at++) { name[at] = from[at]; }
    if (app_text_length(from) > room && room >= 6) {
        for (int32_t dot = 0; dot < 3; dot++) { name[length++] = '.'; }
    }
    name[length] = '\0';
}

static uint8_t *slot_of(int32_t index, int32_t room)
{
    return record + RECORD_HEADER + index * (SLOT_FIXED + room);
}

/* The rows as one store value: its length. */
static int32_t pack(void)
{
    int32_t room = name_room(count);
    record[0] = RECORD_VERSION;
    record[1] = record[2] = record[3] = 0;
    record[4] = (uint8_t)count;
    record[5] = (uint8_t)room;
    record[6] = record[7] = 0;
    for (int32_t index = 0; index < count; index++) {
        const service_t *service = &services[index];
        uint8_t *slot = slot_of(index, room);
        slot[0] = (uint8_t)service->source;
        slot[1] = (uint8_t)(service->source >> 8);
        for (int32_t byte = 0; byte < 4; byte++) { slot[2 + byte] = (uint8_t)(service->updated_at >> 8 * byte); }
        slot[6] = (uint8_t)(service->indicator | service->flags << 4);
        char name[NAME_SIZE];
        keep_name(name, service->name, room);
        int32_t length = app_text_length(name);
        for (int32_t at = 0; at < room; at++) { slot[SLOT_FIXED + at] = at < length ? (uint8_t)name[at] : 0; }
    }
    return RECORD_HEADER + count * (SLOT_FIXED + room);
}

/* Reads the list and gives every item the row the record has for its
 * host, wherever the row was: an edited list keeps what is still asked.
 * False when the record is not one of this version. */
static bool adopt(int32_t length)
{
    int32_t kept = 0, kept_room = 0;
    bool    is_valid = length >= RECORD_HEADER && record[0] == RECORD_VERSION && !record[1] && !record[2] && !record[3];
    if (is_valid) {
        kept = record[4];
        kept_room = record[5];
        is_valid = kept <= SERVICES_MAX && kept_room < NAME_SIZE &&
                   length == RECORD_HEADER + kept * (SLOT_FIXED + kept_room);
    }
    if (!is_valid) { kept = 0; }

    int32_t list_length = app_setting(SERVICES, listed, sizeof listed);
    list_hash = text_hash(listed, list_length);
    count = 0;
    for (int32_t at = 0; at < list_length && count < SERVICES_MAX; at++) {
        if (at == 0 || listed[at - 1] == '\0') { item_at[count++] = (uint8_t)at; }
        if (listed[at] == '\n') { listed[at] = '\0'; }
    }
    int32_t room = name_room(count);
    for (int32_t index = 0; index < count; index++) {
        service_t service = { .source = source_of(listed + item_at[index]), .indicator = UNKNOWN };
        int32_t   found = -1;
        for (int32_t slot = 0; slot < kept && found != index; slot++) {
            const uint8_t *bytes = slot_of(slot, kept_room);
            if ((bytes[0] | bytes[1] << 8) == service.source && (found < 0 || slot == index)) { found = slot; }
        }
        if (found >= 0) {
            const uint8_t *bytes = slot_of(found, kept_room);
            for (int32_t byte = 0; byte < 4; byte++) { service.updated_at |= (uint32_t)bytes[2 + byte] << 8 * byte; }
            service.indicator = bytes[6] & 0x0F;
            service.flags = bytes[6] >> 4;
            if (service.indicator > UNKNOWN) { service.indicator = UNKNOWN; }
            char name[NAME_SIZE];
            for (int32_t at = 0; at < kept_room; at++) { name[at] = (char)bytes[SLOT_FIXED + at]; }
            name[kept_room] = '\0';
            keep_name(service.name, name, room);
        }
        services[index] = service;
    }
    return is_valid;
}

/* The list as the settings have it now; the rows follow a change. */
static void follow_list(void)
{
    char    now[sizeof listed];
    int32_t length = app_setting(SERVICES, now, sizeof now);
    if (text_hash(now, length) != list_hash) { adopt(pack()); }
}

static uint32_t clock_now(void)
{
    int64_t now = time_now();
    return now > 0 ? (uint32_t)now : 0;
}

/* When the row's answer was last a good one. A row that keeps answering
 * the same is not written at every answer: the round's end vouches for it. */
static uint32_t confirmed_at(const service_t *service)
{
    if (!(service->flags & HAS_DATA)) { return 0; }
    bool is_own = (service->flags & FAILED) || service->updated_at > checked_at;
    return is_own ? service->updated_at : checked_at;
}

static bool is_fresh(const service_t *service, uint32_t now, uint32_t interval)
{
    uint32_t at = confirmed_at(service);
    return (service->flags & HAS_DATA) && !(service->flags & FAILED) && at > 0 && now >= at &&
           now - at <= 2 * interval;
}

/* What the row says: the indicator of an answer that is still good. */
static int32_t shown_indicator(const service_t *service, uint32_t now, uint32_t interval)
{
    return is_fresh(service, now, interval) ? service->indicator : UNKNOWN;
}

/* The rows in the order they are drawn: affected first, each group in the
 * list's order. */
static void arrange(uint32_t now, uint32_t interval)
{
    int32_t row = 0;
    for (int32_t group = 0; group < 2; group++) {
        for (int32_t index = 0; index < count; index++) {
            bool is_operational = shown_indicator(&services[index], now, interval) == OPERATIONAL;
            if (is_operational == (group == 1)) { order[row++] = (uint8_t)index; }
        }
    }
}

/* The top row after a step of `by` rows, kept inside the list. */
static int32_t scrolled(int32_t first, int32_t by, int32_t rows, int32_t visible)
{
    int32_t last_first = rows > visible ? rows - visible : 0;
    int32_t to = first + by;
    return to < 0 ? 0 : to > last_first ? last_first : to;
}

static void show_header(void)
{
    uint32_t now = clock_now(), interval = (uint32_t)app_setting_seconds("pace", 300);
    int32_t  down = 0;
    bool     is_stale = false, has_unknown = false;
    for (int32_t index = 0; index < count; index++) {
        const service_t *service = &services[index];
        bool is_good = is_fresh(service, now, interval);
        is_stale |= !is_good;
        has_unknown |= is_good && service->indicator == UNKNOWN;
        down += is_good && service->indicator != OPERATIONAL && service->indicator != UNKNOWN;
    }
    char    text[64];
    int32_t length;
    /* A confirmed outage is the news, whichever other row is late: with a
     * list of the user's, one host that does not answer would hide it. */
    if (down) {
        host_text_argument_t argument = { .number = down };
        const char *pattern = app_string(STR_AFFECTED);
        length = text_fill(pattern, app_text_length(pattern), &argument, 1, text, sizeof text);
    } else {
        app_text_copy(text, sizeof text,
                      app_string(!count ? STR_UNKNOWN : is_stale ? STR_STALE : has_unknown ? STR_UNKNOWN : STR_ALL_GOOD));
        length = app_text_length(text);
    }
    header_set_text(HOST_HEADER_LEFT, text, length);
}

void app_on_load(void)
{
    checked_t checked;
    checked_at = app_state_load(CHECKED, &checked, sizeof checked, CHECKED_VERSION) ? checked.at : 0;
    int32_t length = kv_get(ANSWERS, app_text_length(ANSWERS), record, sizeof record);
    if (adopt(length)) { return; }
    /* Before 0.2.0 the pages were four link settings. What a user stored
     * there is nobody's now and would be charged to the store for good. */
    for (int32_t page = 0; page < 4; page++) {
        uint8_t nothing = 0;
        int32_t key_length = app_text_length(LEGACY_PAGES[page]);
        if (kv_get(LEGACY_PAGES[page], key_length, &nothing, 0) != HOST_ERR_NOT_FOUND) {
            kv_set(LEGACY_PAGES[page], key_length, &nothing, 0);
        }
    }
}

void app_on_focus(void)
{
    session_t session;
    first_row = app_session_load(&session, sizeof session) ? scrolled(session.first_row, 0, count, shown_rows) : 0;
    show_header();
}

void app_on_blur(void)
{
    session_t session = { .first_row = first_row };
    app_session_save(&session, sizeof session);
}

void app_on_settings(void)
{
    follow_list();
    first_row = scrolled(first_row, 0, count, shown_rows);
    show_header();
    screen_redraw(HOST_REDRAW_NORMAL);
}

void app_on_button(int32_t event)
{
    int32_t by = event == BTN_UP_PRESSED ? -1 : event == BTN_DOWN_PRESSED ? 1
                 : event == BTN_UP_LONG_PRESSED ? -shown_rows : event == BTN_DOWN_LONG_PRESSED ? shown_rows : 0;
    if (event == BTN_OK_PRESSED) { poll_run(0); }
    int32_t to = scrolled(first_row, by, count, shown_rows);
    if (to != first_row) {
        first_row = to;
        screen_redraw(HOST_REDRAW_NORMAL);
    }
}

void app_on_picked(int32_t request, int32_t http_status)
{
    (void)request;
    follow_list();
    int32_t item = answer_item();
    if (item < 0 || item >= count) { return; } /* of an item the list no longer has */
    service_t *service = &services[item];
    service_t  before = *service;
    uint32_t   now = clock_now();
    char indicator[32], name[HOST_VALUE_MAX + 1];
    if (http_status >= 200 && http_status < 300 &&
        app_value_text(0, name, sizeof name) && name[0] &&
        app_value_text(1, indicator, sizeof indicator)) {
        keep_name(service->name, name, name_room(count));
        service->indicator = UNKNOWN;
        for (int32_t state = 0; state < UNKNOWN; state++) {
            if (app_text_equal(indicator, INDICATORS[state])) { service->indicator = (uint8_t)state; }
        }
        service->flags = HAS_DATA;
        service->updated_at = now;
    } else if ((service->flags & HAS_DATA) && !(service->flags & FAILED)) {
        service->updated_at = confirmed_at(service);
        service->flags |= FAILED;
    }
    bool is_changed = service->indicator != before.indicator || service->flags != before.flags ||
                      !app_text_equal(service->name, before.name);
    /* A write the store refused is made again with the next answer, the same or not. */
    if (is_changed || answers_unsaved) {
        answers_unsaved = kv_set(ANSWERS, app_text_length(ANSWERS), record, pack()) != HOST_OK;
    }
    if (item == count - 1 && now > 0) {
        checked_at = now;
        checked_t checked = { .version = CHECKED_VERSION, .at = now };
        app_state_save(CHECKED, &checked, sizeof checked);
    }
    show_header();
    screen_redraw(HOST_REDRAW_NORMAL);
}

/* Perinkpheral OS draws its scroll bar with a row of its list, and a row outside
 * the visible ones is the bar alone. Its rows are lower than these, so the
 * list it is told of is this one in its own rows: the same share shown,
 * the same way down. */
static void draw_scroll_bar(void)
{
    host_list_t bar = { .selected = -1 };
    int32_t     host_rows = ui_list_visible(&bar);
    if (count <= shown_rows || host_rows <= 0) { return; }
    bar.count = (count * host_rows + shown_rows - 1) / shown_rows;
    bar.first = first_row * (bar.count - host_rows) / (count - shown_rows);
    ui_draw_list_row(&bar, -1, "", 0, "", 0, HOST_ICON_NONE);
}

void app_on_draw(void)
{
    if (!count) { ui_draw_reason(HOST_FAILED_NOT_READY); return; }
    uint32_t now = clock_now(), interval = (uint32_t)app_setting_seconds("pace", 300);
    arrange(now, interval);
    shown_rows = (gfx_height() - ROWS_TOP) / ROW_HEIGHT;
    if (shown_rows < 1) { shown_rows = 1; }
    first_row = scrolled(first_row, 0, count, shown_rows);
    for (int32_t row = first_row; row < count && row < first_row + shown_rows; row++) {
        const service_t *service = &services[order[row]];
        int32_t indicator = shown_indicator(service, now, interval);
        bool    is_late = (service->flags & HAS_DATA) && !is_fresh(service, now, interval);
        int32_t top = ROWS_TOP + (row - first_row) * ROW_HEIGHT;
        gfx_draw_icon(16, top + 12, ICONS[indicator], 2);
        /* Until a page says its name, and where the record has no room for one: the host. */
        const char *name = service->name[0] ? service->name : listed + item_at[order[row]];
        char fitted[64];
        int32_t length = gfx_fit_text(HOST_FONT_TEXT, name, app_text_length(name), 330, fitted, sizeof fitted);
        gfx_draw_text(54, top, HOST_FONT_TEXT, fitted, length > 0 ? length : 0);
        app_draw_text(54, top + 30, HOST_FONT_SMALL,
                      app_string(is_late ? STR_NO_UPDATE : WORDS[indicator]));
        if (confirmed_at(service) > 0) {
            char updated[48];
            app_time_text(confirmed_at(service), HOST_TIME_NEAR, updated, sizeof updated);
            app_draw_right(384, top + 30, HOST_FONT_SMALL, updated);
        }
        if (row + 1 < count && row + 1 < first_row + shown_rows) { gfx_draw_line(54, top + 55, 384, top + 55); }
    }
    draw_scroll_bar();
}
