// SPDX-License-Identifier: MIT
/*
 * GitHub's own contribution levels, streamed into three bits per day.
 * One versioned snapshot holds the calendar and total for its login.
 * Opening or OK refreshes it; an alarm also refreshes every six hours.
 */
#include "app_days.h"
#include "app_draw.h"
#include "app_state.h"
#include "app_text.h"
#include "calendar_data.h"
#include "host_api.h"
#include "strings.h"

enum { VERSION = 1, REFRESH_SECONDS = 6 * 3600, CELL = 6, STEP = 7, GRID_LEFT = 15, GRID_TOP = 157 };
typedef struct {
    int32_t version, has_data, status;
    uint32_t login_hash;
    contribution_calendar_t calendar;
} state_t;
static state_t state;
static state_t saved_state;
static calendar_picker_t picker;
static int32_t response_status;
static bool receiving;

static void save_state(void)
{
    for (int32_t byte = 0; byte < (int32_t)sizeof state; byte++) {
        if (((const uint8_t *)&state)[byte] != ((const uint8_t *)&saved_state)[byte]) {
            if (app_state_save("state", &state, sizeof state)) { saved_state = state; }
            return;
        }
    }
}

static uint32_t login_hash(const char *login)
{
    uint32_t hash = 2166136261u;
    for (; *login; login++) {
        char letter = *login >= 'A' && *login <= 'Z' ? *login + ('a' - 'A') : *login;
        hash = (hash ^ (uint8_t)letter) * 16777619u;
    }
    return hash;
}
static bool credentials(char *login, int32_t capacity)
{
    char token[256];
    if (!app_setting("login", login, capacity) || !app_setting("token", token, sizeof token)) { return false; }
    for (const char *letter = login; *letter; letter++) {
        if (!((*letter >= 'a' && *letter <= 'z') || (*letter >= 'A' && *letter <= 'Z') ||
              (*letter >= '0' && *letter <= '9') || *letter == '-')) { return false; }
    }
    return true;
}
static void schedule(void)
{
    if (alarm_set_every(0, REFRESH_SECONDS, 0) != HOST_OK) {
        static const char message[] = "GitHub refresh alarm unavailable";
        log_write(HOST_LOG_WARN, message, sizeof message - 1);
    }
}
static void refresh(void)
{
    char login[40];
    if (!credentials(login, sizeof login)) { screen_redraw(HOST_REDRAW_NORMAL); return; }
    char body[HOST_HTTP_BODY_MAX + 1];
    app_text_t text;
    app_text_start(&text, body, sizeof body);
    app_text_add(&text, "{\"query\":\"query($login:String!){user(login:$login){login contributionsCollection{contributionCalendar{totalContributions weeks{contributionDays{contributionLevel date}}}}}}\",\"variables\":{\"login\":\"");
    app_text_add(&text, login);
    app_text_add(&text, "\"}}");
    static const char url[] = "https://api.github.com/graphql";
    static const char headers[] = "Authorization: bearer {{token}}\nUser-Agent: perinkpheral\nContent-Type: application/json";
    int32_t result = http_send(HOST_REQUEST_FIRST, HOST_HTTP_POST, url, sizeof url - 1,
                              headers, sizeof headers - 1, body, app_text_length(body));
    if (result != HOST_OK) {
        state.status = result == HOST_ERR_NO_SPACE ? HOST_FAILED_NO_MEMORY : HOST_FAILED_NOT_READY;
        save_state();
        screen_redraw(HOST_REDRAW_NORMAL);
    }
}
void app_on_load(void)
{
    if (!app_state_load("state", &state, sizeof state, VERSION)) { state = (state_t){ .version = VERSION }; }
    saved_state = state;
}
void app_on_focus(void)
{
    int32_t entered;
    if (!app_session_load(&entered, sizeof entered)) { refresh(); }
}
void app_on_blur(void)
{
    int32_t entered = 1;
    app_session_save(&entered, sizeof entered);
}
void app_on_installed(void) { schedule(); }
void app_on_updated(void) { schedule(); }
void app_on_settings(void)
{
    char login[40];
    if (!credentials(login, sizeof login) || state.login_hash != login_hash(login)) {
        state = (state_t){ .version = VERSION };
        app_state_forget("state");
        saved_state = state;
    }
    schedule();
    refresh();
    screen_redraw(HOST_REDRAW_NORMAL);
}
void app_on_alarm(int32_t id) { if (!id) { refresh(); } }
void app_on_intent(int32_t intent)
{
    if (intent == HOST_INTENT_STARTED) { schedule(); }
    if (intent == HOST_INTENT_NETWORK_UP) { refresh(); }
    if (intent == HOST_INTENT_TIME_CHANGED) { screen_redraw(HOST_REDRAW_NORMAL); }
}
void app_on_button(int32_t event) { if (event == BTN_OK_PRESSED) { refresh(); } }
void app_on_data(int32_t request, const uint8_t *bytes, int32_t length, int32_t flags, int32_t http_status)
{
    if (request != HOST_REQUEST_FIRST) { return; }
    if (flags & HOST_DATA_FIRST) { calendar_picker_start(&picker); response_status = http_status; receiving = true; }
    if (flags & HOST_DATA_FAILED) {
        receiving = false;
        state.status = http_status;
    } else if (receiving) {
        calendar_picker_feed(&picker, bytes, length);
        if (!(flags & HOST_DATA_LAST)) { return; }
        receiving = false;
        char login[40];
        if (response_status >= 200 && response_status < 300 && calendar_picker_finish(&picker) &&
            credentials(login, sizeof login) && login_hash(login) == login_hash(picker.login)) {
            state.has_data = 1;
            state.login_hash = login_hash(login);
            state.calendar = picker.calendar;
            state.status = response_status;
        } else { state.status = response_status >= 200 && response_status < 300 ? HOST_FAILED_NO_ANSWER : response_status; }
    } else { return; }
    save_state();
    screen_redraw(HOST_REDRAW_NORMAL);
}
static void draw_number(int32_t center, int32_t top, int32_t value)
{
    char buffer[16];
    app_text_t text;
    app_text_start(&text, buffer, sizeof buffer);
    app_text_add_number(&text, value);
    int32_t font = HOST_FONT_LARGE | HOST_FONT_TABULAR;
    if (app_text_width(font, buffer) > 180) { font = HOST_FONT_MEDIUM | HOST_FONT_TABULAR; }
    app_draw_centered(center, top, font, buffer);
}
static void draw_month(int32_t column, int32_t month)
{
    gfx_set_color(HOST_INK);
    const int32_t months[] = { STR_JAN, STR_FEB, STR_MAR, STR_APR, STR_MAY, STR_JUN,
        STR_JUL, STR_AUG, STR_SEP, STR_OCT, STR_NOV, STR_DEC };
    const char *label = app_string(months[month - 1]);
    int32_t left = GRID_LEFT + column * STEP;
    int32_t right = left + app_text_width(HOST_FONT_SMALL, label);
    if (right > gfx_width() - GRID_LEFT) { return; }
    app_draw_text(left, GRID_TOP - 22, HOST_FONT_SMALL, label);
}
void app_on_draw(void)
{
    char login[40];
    if (!credentials(login, sizeof login)) { ui_draw_reason(HOST_FAILED_NOT_READY); return; }
    if (!state.has_data || state.login_hash != login_hash(login)) { ui_draw_reason(state.status); return; }
    host_local_time_t today;
    int32_t streak = time_local(&today) == HOST_OK ? calendar_streak(&state.calendar,
        app_days_from_date(today.year, today.month, today.day)) : 0;
    app_draw_centered(104, 10, HOST_FONT_SMALL, app_string(STR_CONTRIBUTIONS));
    app_draw_centered(298, 10, HOST_FONT_SMALL, app_string(STR_STREAK));
    draw_number(104, 33, state.calendar.total);
    draw_number(298, 33, streak);
    app_draw_centered(298, 92, HOST_FONT_SMALL, app_string(STR_DAYS));
    gfx_draw_line(200, 18, 200, 103);
    /* Anchor the final column to the last returned week's Sunday. In a
     * leap-year edge case spanning 54 weeks, only the earliest partial
     * week is cropped; the stored year and its total remain complete. */
    int32_t last = state.calendar.first_day + state.calendar.count - 1;
    int32_t first = last - (last + 4) % 7 - 52 * 7;
    int32_t prior_month = 0;
    for (int32_t column = 0; column < 53; column++) {
        int32_t year, month, day;
        app_date_from_days(first + column * 7 + 3, &year, &month, &day);
        if (month != prior_month) {
            /* Two-month ticks leave room for three-letter names in both host fonts. */
            if (month % 2 == 0 && (column > 0 || day <= 7)) { draw_month(column, month); }
            prior_month = month;
        }
        for (int32_t row = 0; row < 7; row++) {
            int32_t index = first + column * 7 + row - state.calendar.first_day;
            if (index < 0 || index >= state.calendar.count) { continue; }
            int32_t level = calendar_level(&state.calendar, index);
            if (!level) { continue; }
            gfx_set_color(level * 4);
            gfx_draw_rect(GRID_LEFT + column * STEP, GRID_TOP + row * STEP, CELL, CELL, HOST_RECT_FILLED);
        }
    }
    gfx_set_color(HOST_INK);
    app_draw_text(15, 225, HOST_FONT_SMALL, login);
    app_draw_text(205, 225, HOST_FONT_SMALL, app_string(STR_LESS));
    for (int32_t level = 0; level <= 4; level++) {
        gfx_set_color(level * 4);
        gfx_draw_rect(265 + level * STEP, 232, CELL, CELL, HOST_RECT_FILLED);
    }
    gfx_set_color(HOST_INK);
    app_draw_right(386, 225, HOST_FONT_SMALL, app_string(STR_MORE));
}
