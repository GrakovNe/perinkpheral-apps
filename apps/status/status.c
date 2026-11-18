// SPDX-License-Identifier: MIT
/*
 * Four Statuspage summaries, with affected services first. Each row keeps
 * its source and last good answer in the store; a cleared link hides it.
 * Perinkpheral OS owns polling and the refresh pace. OK asks all enabled pages.
 */
#include "app_draw.h"
#include "app_state.h"
#include "app_text.h"
#include "app_values.h"
#include "host_api.h"
#include "strings.h"

enum { PAGES = 4, VERSION = 2, PREVIOUS_VERSION = 1, UNKNOWN = 5 };
typedef struct {
    int32_t version, has_data, indicator, status;
    uint32_t source_hash;
    char name[64];
    int64_t updated_at;
} page_t;
static page_t pages[PAGES];
typedef struct { int32_t version; page_t pages[PAGES]; } snapshot_t;

typedef struct {
    int32_t version, has_data, indicator, status;
    uint32_t source_hash;
    char name[64], description[64];
} previous_page_t;
typedef struct { int32_t version; previous_page_t pages[PAGES]; } previous_snapshot_t;
_Static_assert(sizeof(snapshot_t) <= HOST_KV_VALUE_MAX, "Status answers fit one value");
static const char *const SETTINGS[PAGES] = { "page1", "page2", "page3", "page4" };
static const char *const INDICATORS[] = { "none", "minor", "major", "critical", "maintenance" };
static const int32_t WORDS[] = { STR_OPERATIONAL, STR_MINOR, STR_MAJOR, STR_CRITICAL, STR_MAINTENANCE, STR_UNKNOWN };
static const int32_t ICONS[] = { HOST_ICON_CHECK, HOST_ICON_WARNING, HOST_ICON_CROSS, HOST_ICON_CROSS, HOST_ICON_GEAR, HOST_ICON_WARNING };

static uint32_t source_hash(const char *source)
{
    uint32_t hash = 2166136261u;
    while (*source) { hash = (hash ^ (uint8_t)*source++) * 16777619u; }
    return hash;
}

static bool enabled(int32_t index)
{
    char source[256];
    return app_setting(SETTINGS[index], source, sizeof source) > 0;
}

static bool current(int32_t index)
{
    char source[256];
    app_setting(SETTINGS[index], source, sizeof source);
    return pages[index].has_data && source_hash(source) == pages[index].source_hash;
}

static bool fresh(int32_t index)
{
    int32_t interval = app_setting_seconds("pace", 300);
    return current(index) && pages[index].status >= 200 && pages[index].status < 300 &&
           pages[index].updated_at > 0 && time_now() >= pages[index].updated_at &&
           time_now() - pages[index].updated_at <= 2 * interval;
}

static void show_header(void)
{
    int32_t down = 0, active = 0;
    bool stale = false;
    for (int32_t index = 0; index < PAGES; index++) {
        active += enabled(index);
        stale |= enabled(index) && !fresh(index);
        if (enabled(index) && current(index) && pages[index].indicator > 0 && pages[index].indicator < UNKNOWN) { down++; }
    }
    char text[64];
    int32_t length;
    if (down) {
        host_text_argument_t argument = { .number = down };
        const char *pattern = app_string(STR_AFFECTED);
        length = text_fill(pattern, app_text_length(pattern), &argument, 1, text, sizeof text);
    } else {
        app_text_copy(text, sizeof text, app_string(STR_ALL_GOOD));
        length = app_text_length(text);
        for (int32_t index = 0; index < PAGES; index++) {
            if (enabled(index) && (!current(index) || pages[index].indicator == UNKNOWN)) {
                app_text_copy(text, sizeof text, app_string(STR_UNKNOWN));
                length = app_text_length(text);
                break;
            }
        }
    }
    if (stale || !active) {
        app_text_copy(text, sizeof text, app_string(active ? STR_STALE : STR_UNKNOWN));
        length = app_text_length(text);
    }
    header_set_text(HOST_HEADER_LEFT, text, length);
}

void app_on_load(void)
{
    snapshot_t snapshot;
    if (app_state_load("answers", &snapshot, sizeof snapshot, VERSION)) {
        for (int32_t index = 0; index < PAGES; index++) { pages[index] = snapshot.pages[index]; }
        return;
    }

    previous_snapshot_t previous;
    bool loaded = app_state_load("answers", &previous, sizeof previous, PREVIOUS_VERSION);
    for (int32_t index = 0; index < PAGES; index++) {
        page_t fresh = { .version = VERSION, .indicator = UNKNOWN };
        if (loaded) {
            const previous_page_t *page = &previous.pages[index];
            fresh.has_data = page->has_data;
            fresh.status = page->status;
            fresh.source_hash = page->source_hash;
            app_text_copy(fresh.name, sizeof fresh.name, page->name);
        }
        pages[index] = fresh;
    }
}
void app_on_focus(void) { show_header(); }
void app_on_settings(void) { show_header(); screen_redraw(HOST_REDRAW_NORMAL); }
void app_on_button(int32_t event)
{
    if (event == BTN_OK_PRESSED) {
        for (int32_t index = 0; index < PAGES; index++) { if (enabled(index)) { poll_run(index); } }
    }
}
void app_on_picked(int32_t request, int32_t http_status)
{
    if (request < 0 || request >= PAGES || !enabled(request)) { return; }
    page_t fresh = pages[request];
    if (!current(request)) { fresh = (page_t){ .version = VERSION, .indicator = UNKNOWN }; }
    char source[256];
    app_setting(SETTINGS[request], source, sizeof source);
    fresh.source_hash = source_hash(source);
    fresh.status = http_status;
    char indicator[32], name[64], description[64];
    if (http_status >= 200 && http_status < 300 &&
        app_value_text(0, name, sizeof name) && name[0] &&
        app_value_text(1, indicator, sizeof indicator) &&
        app_value_text(2, description, sizeof description)) {
        app_text_copy(fresh.name, sizeof fresh.name, name);
        fresh.indicator = UNKNOWN;
        for (int32_t state = 0; state < UNKNOWN; state++) {
            if (app_text_equal(indicator, INDICATORS[state])) { fresh.indicator = state; }
        }
        fresh.has_data = 1;
        fresh.updated_at = time_now();
    } else if (http_status >= 200 && http_status < 300) {
        fresh.status = HOST_FAILED_NO_ANSWER;
    }
    bool changed = false;
    for (int32_t byte = 0; byte < (int32_t)sizeof fresh; byte++) {
        changed |= ((const uint8_t *)&fresh)[byte] != ((const uint8_t *)&pages[request])[byte];
    }
    pages[request] = fresh;
    if (changed) {
        /* One blob saves four NVS records' overhead. Even four maximum
         * length links, the pace and these answers fit the 2 KB store. */
        snapshot_t snapshot = { .version = VERSION };
        for (int32_t index = 0; index < PAGES; index++) { snapshot.pages[index] = pages[index]; }
        app_state_save("answers", &snapshot, sizeof snapshot);
    }
    show_header();
    screen_redraw(HOST_REDRAW_NORMAL);
}
void app_on_draw(void)
{
    int32_t row = 0, active = 0;
    for (int32_t index = 0; index < PAGES; index++) { active += enabled(index); }
    if (!active) { ui_draw_reason(HOST_FAILED_NOT_READY); return; }
    /* The setting order stays stable within the affected and healthy groups. */
    for (int32_t group = 0; group < 2; group++) {
        for (int32_t index = 0; index < PAGES; index++) {
            if (!enabled(index)) { continue; }
            bool is_fresh = fresh(index);
            int32_t indicator = is_fresh ? pages[index].indicator : UNKNOWN;
            if ((indicator == 0) != (group == 1)) { continue; }
            int32_t top = 10 + row++ * 62;
            gfx_draw_icon(16, top + 12, ICONS[indicator], 2);
            char label[64];
            host_text_argument_t argument = { .number = index + 1 };
            const char *pattern = app_string(STR_PAGE);
            int32_t label_length = text_fill(pattern, app_text_length(pattern), &argument, 1, label, sizeof label - 1);
            label[label_length > 0 ? label_length : 0] = '\0';
            const char *name = current(index) ? pages[index].name : label;
            char fitted[64];
            int32_t length = gfx_fit_text(HOST_FONT_TEXT, name, app_text_length(name), 330, fitted, sizeof fitted);
            gfx_draw_text(54, top, HOST_FONT_TEXT, fitted, length > 0 ? length : 0);
            app_draw_text(54, top + 30, HOST_FONT_SMALL,
                          app_string(!is_fresh && current(index) ? STR_NO_UPDATE : WORDS[indicator]));
            if (current(index) && pages[index].updated_at > 0) {
                char updated[48];
                app_time_text(pages[index].updated_at, HOST_TIME_NEAR, updated, sizeof updated);
                app_draw_right(384, top + 30, HOST_FONT_SMALL, updated);
            }
            if (row < active) { gfx_draw_line(54, top + 55, 384, top + 55); }
        }
    }
}
