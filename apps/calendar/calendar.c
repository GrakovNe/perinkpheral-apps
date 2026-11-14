/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * What is coming, from any calendar that gives a link to itself - Google,
 * iCloud, Outlook, Nextcloud: the user pastes the calendar's secret
 * address on the device's page, the host fetches it now and again, and
 * the application reads it as it comes into the next events (ics.c). No
 * account is bound to anything and nothing passes a server of ours.
 *
 * It reminds, too: some minutes before an event the host says what begins,
 * over whatever has the screen (notify). The reminder is an alarm of the
 * host's at a moment worked out from the events in the store, so it comes
 * although the application is not loaded - and after a restart, which
 * forgets alarms, it is set again.
 */
#include <stdbool.h>
#include <stddef.h>

#include "app_state.h"
#include "app_text.h"
#include "app_ui.h"
#include "host_api.h"
#include "ics.h"
#include "strings.h" /* made of the `strings` folder by tools/mkapp */

#define FEED_POLL          0
#define REMIND_ALARM       0
#define TURN_TIMER         0
#define SECONDS_PER_MINUTE 60
#define SECONDS_PER_DAY    86400
#define MS_PER_SECOND      1000
#define DAYS_AHEAD         14
#define HTTP_OK            200
/* A reminder that is this late - the device was off - is not given. */
#define LATE_SECONDS       (5 * SECONDS_PER_MINUTE)
/* What a feed answered when it is not a calendar: a page asking to sign in. */
#define STATUS_NOT_A_CALENDAR 1
#define STATUS_NO_TIME        2

/* ---- what is kept ---- */

#define EVENTS_FILE    "events"
#define EVENTS_VERSION 3
typedef struct {
    int32_t     version;
    int32_t     count;
    ics_event_t events[ICS_EVENTS_MAX];
} stored_events_t;

#define STATE_KEY     "state"
#define STATE_VERSION 2
typedef struct {
    int32_t version;
    int32_t status;      /* of the last fetch: 0 none yet, an HTTP one, a HOST_FAILED_*, a STATUS_* */
    int64_t reminded_to; /* reminders due up to this moment were given */
} state_t;

static stored_events_t stored;
static state_t         state;

static void save_state(void)
{
    kv_set(STATE_KEY, sizeof STATE_KEY - 1, &state, sizeof state);
}

static bool is_same(const void *one, const void *other, int32_t size)
{
    const uint8_t *a = one, *b = other;
    for (int32_t i = 0; i < size; i++) {
        if (a[i] != b[i]) {
            return false;
        }
    }
    return true;
}

/* The number a choice begins with, in seconds: "10 min", "1 h". */
static int32_t seconds_of(const char *choice, int32_t otherwise)
{
    int32_t number = 0, at = 0;
    while (choice[at] >= '0' && choice[at] <= '9') {
        number = number * 10 + (choice[at++] - '0');
    }
    if (at == 0) {
        return otherwise;
    }
    return number * (choice[at + 1] == 'h' ? 3600 : SECONDS_PER_MINUTE);
}

/* How long before an event the reminder comes; -1: never. */
static int32_t lead_seconds(void)
{
    char choice[16];
    if (app_setting("remind", choice, sizeof choice) == 0) {
        return 10 * SECONDS_PER_MINUTE; /* nothing chosen: the manifest's default */
    }
    return choice[0] == 'O' ? -1 : seconds_of(choice, 0);
}

/* ---- the reminder ---- */

/* The alarm for the reminder that comes next, or none. */
static void schedule_reminder(void)
{
    int64_t now = time_now();
    int32_t lead = lead_seconds();
    alarm_set(REMIND_ALARM, 0);
    if (now == 0 || lead < 0) {
        return;
    }
    for (int32_t i = 0; i < stored.count; i++) {
        const ics_event_t *event = &stored.events[i];
        int64_t due = event->start - lead;
        if (event->is_all_day || due <= state.reminded_to || event->start + LATE_SECONDS < now) {
            continue;
        }
        if (due <= now) {
            alarm_set(REMIND_ALARM, 1);
        } else {
            alarm_set_at(REMIND_ALARM, due);
        }
        return; /* the events are in order: this is the nearest */
    }
}

/* The reminder is the host's to show: when it begins, and what it is. */
static void remind_of(const ics_event_t *event)
{
    host_notice_t notice = { .icon = HOST_ICON_BELL };
    notice.first[time_text(event->start, HOST_TIME_CLOCK, notice.first, sizeof notice.first - 1)] = '\0';
    const char *title = event->title[0] != '\0' ? event->title : app_string(STR_EVENT);
    int32_t length = 0;
    while (title[length] != '\0' && length < (int32_t)sizeof notice.detail - 1) {
        notice.detail[length] = title[length];
        length++;
    }
    /* Cut between characters, not inside one of several bytes. */
    while (length > 0 && ((uint8_t)title[length] & 0xC0) == 0x80) {
        length--;
    }
    notice.detail[length] = '\0';
    notify(&notice);
}

void app_on_alarm(int32_t id)
{
    (void)id;
    int64_t now = time_now();
    int32_t lead = lead_seconds();
    for (int32_t i = 0; i < stored.count && lead >= 0 && now != 0; i++) {
        const ics_event_t *event = &stored.events[i];
        int64_t due = event->start - lead;
        if (!event->is_all_day && due > state.reminded_to && due <= now &&
            event->start + LATE_SECONDS >= now) {
            remind_of(event);
        }
    }
    if (now != 0) {
        state.reminded_to = now;
    }
    save_state();
    schedule_reminder();
}

/* ---- the feed ---- */

static ics_t       reader;
static ics_event_t incoming[ICS_EVENTS_MAX];
static bool        is_reading;

static void store_events(int32_t count)
{
    static stored_events_t fresh;
    fresh = (stored_events_t){ .version = EVENTS_VERSION, .count = count };
    for (int32_t i = 0; i < count; i++) {
        fresh.events[i] = incoming[i];
    }
    /* The flash is written only when the calendar is another. */
    if (!is_same(&fresh, &stored, sizeof fresh)) {
        stored = fresh;
        file_write(EVENTS_FILE, sizeof EVENTS_FILE - 1, 0, &stored, sizeof stored);
    }
}

static void finish_fetch(int32_t status)
{
    is_reading = false;
    if (state.status != status) {
        state.status = status;
        save_state();
    }
    schedule_reminder();
    request_redraw(HOST_REDRAW_NORMAL);
}

static bool is_calendar_start(const uint8_t *data, int32_t length)
{
    static const char OPENING[] = "BEGIN:VCALENDAR";
    int32_t at = 0;
    /* Some servers put a mark of the encoding first. */
    if (length >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) {
        at = 3;
    }
    for (int32_t i = 0; OPENING[i] != '\0'; i++) {
        if (at + i >= length || data[at + i] != (uint8_t)OPENING[i]) {
            return false;
        }
    }
    return true;
}

void app_on_data(int32_t request, const uint8_t *data, int32_t length, int32_t flags,
                 int32_t http_status)
{
    if (request != FEED_POLL) {
        return;
    }
    if (flags & HOST_DATA_FAILED) {
        finish_fetch(http_status); /* what was there stays */
        return;
    }
    if (flags & HOST_DATA_FIRST) {
        int64_t now = time_now();
        is_reading = false;
        if (http_status != HTTP_OK) {
            finish_fetch(http_status);
        } else if (now == 0) {
            finish_fetch(STATUS_NO_TIME); /* "the next two weeks" needs a today */
        } else if (!is_calendar_start(data, length)) {
            finish_fetch(STATUS_NOT_A_CALENDAR);
        } else {
            ics_begin(&reader, incoming, now, now + (int64_t)DAYS_AHEAD * SECONDS_PER_DAY,
                      time_zone_offset());
            is_reading = true;
        }
    }
    if (!is_reading) {
        return;
    }
    ics_feed(&reader, (const char *)data, length);
    if (flags & HOST_DATA_LAST) {
        store_events(ics_end(&reader));
        finish_fetch(HTTP_OK);
    }
}

/* ---- when things happen ---- */

/* The list changes by itself when an event begins or ends. */
static void schedule_next_change(void)
{
    int64_t now = time_now(), next = 0;
    for (int32_t i = 0; i < stored.count && now != 0; i++) {
        const ics_event_t *event = &stored.events[i];
        int64_t turn = event->start > now ? event->start : event->end > now ? event->end : 0;
        if (turn != 0 && (next == 0 || turn < next)) {
            next = turn;
        }
    }
    /* A timer is at most some hours away: whatever is further is looked
     * at again when it comes. */
    int64_t in_seconds = next == 0 || next - now > 6 * 3600 ? 6 * 3600 : next - now + 1;
    timer_set(TURN_TIMER, (int32_t)in_seconds * MS_PER_SECOND);
}

void app_init(void)
{
    if (kv_get(STATE_KEY, sizeof STATE_KEY - 1, &state, sizeof state) != sizeof state ||
        state.version != STATE_VERSION) {
        state = (state_t){ .version = STATE_VERSION };
    }
    if (file_read(EVENTS_FILE, sizeof EVENTS_FILE - 1, 0, &stored, sizeof stored) !=
            sizeof stored ||
        stored.version != EVENTS_VERSION || stored.count < 0 || stored.count > ICS_EVENTS_MAX) {
        stored = (stored_events_t){ .version = EVENTS_VERSION };
    }
}

void app_on_focus(void)
{
    schedule_next_change();
}

void app_on_timer(int32_t id)
{
    (void)id;
    request_redraw(HOST_REDRAW_NORMAL);
    schedule_next_change();
}

void app_on_intent(int32_t intent)
{
    /* A restart forgot the alarm, and a clock set anew moves "in ten
     * minutes" to another moment. */
    (void)intent;
    schedule_reminder();
}

void app_on_settings(void)
{
    schedule_reminder();
    request_redraw(HOST_REDRAW_NORMAL);
}

/* ---- the buttons ---- */

/* What is over is not shown; the events are in order of their start. */
static int32_t    shown[ICS_EVENTS_MAX];
static app_list_t list;
static bool       is_reading_one; /* the page of one event, opened with OK */
static uint32_t   read_uid;       /* which: the list under it may change */
static int64_t    read_start;

static int32_t count_visible_events(int64_t now)
{
    int32_t count = 0;
    for (int32_t i = 0; i < stored.count; i++) {
        if (stored.events[i].end > now) {
            shown[count++] = i;
        }
    }
    return count;
}

void app_on_button(int32_t event)
{
    if (event == BTN_OK_LONG_PRESSED) {
        poll_now(FEED_POLL);
        return;
    }
    if (is_reading_one) {
        return;
    }
    switch (app_list_button(&list, event)) {
    case APP_UI_DONE:
        is_reading_one = list.count > 0 && list.selected < list.count;
        if (is_reading_one) {
            read_uid = stored.events[shown[list.selected]].uid;
            read_start = stored.events[shown[list.selected]].start;
        }
        request_redraw(HOST_REDRAW_NORMAL);
        break;
    case APP_UI_CHANGED:
        request_redraw(HOST_REDRAW_NO_BLINK);
        break;
    default:
        break;
    }
}

void app_on_back(void)
{
    if (is_reading_one) {
        is_reading_one = false;
        request_redraw(HOST_REDRAW_NORMAL);
    } else {
        request_exit();
    }
}

/* ---- the picture ---- */

/* When an event is, as short as the host says it: "15:00" today, "Sat
 * 15:00" this week, "21 Nov" beyond it; a whole day is its day alone, and
 * one that has begun is "Now". */
static int32_t say_when(const ics_event_t *event, int64_t now, char *line, int32_t size)
{
    if (event->start <= now) {
        const char *word = app_string(event->is_all_day ? STR_TODAY : STR_NOW);
        int32_t     length = app_text_length(word);
        for (int32_t i = 0; i < length; i++) {
            line[i] = word[i];
        }
        return length;
    }
    return time_text(event->start, event->is_all_day ? HOST_TIME_NEAR_DAY : HOST_TIME_NEAR, line,
                     size);
}

/* Why there is nothing to show: the host's word for what the network
 * came to, and this application's own for the rest. */
static void draw_reason(void)
{
    host_notice_t notice = { .icon = HOST_ICON_WARNING };
    app_text_t    first, detail;
    app_text_start(&first, notice.first, sizeof notice.first);
    app_text_start(&detail, notice.detail, sizeof notice.detail);
    if (state.status == STATUS_NOT_A_CALENDAR) {
        app_text_add(&first, app_string(STR_NOT_A_CALENDAR));
        app_text_add(&detail, app_string(STR_CHECK_THE_ADDRESS));
    } else if (state.status == STATUS_NO_TIME) {
        app_text_add(&first, app_string(STR_NO_TIME_YET));
        notice.icon = HOST_ICON_CLOCK;
    } else if (ui_reason(state.status) == 0) {
        app_text_add(&first, app_string(STR_NO_EVENTS));
        notice.icon = HOST_ICON_CHECK;
    } else {
        return;
    }
    ui_notice(&notice);
}

#define MOMENT_MAX  47 /* "Завтра 15:04" is two bytes a letter */
#define TITLE_LINES 4

/* One event, whole: when it is, and its name over as many lines as it takes. */
static void draw_event(const ics_event_t *event)
{
    char       when[2 * MOMENT_MAX + 8], moment[MOMENT_MAX + 1];
    app_text_t line;
    int32_t    width = gfx_width() - 2 * HOST_UI_MARGIN;

    moment[time_text(event->start, HOST_TIME_DATE, moment, MOMENT_MAX)] = '\0';
    int32_t top = ui_title(moment, app_text_length(moment));

    app_text_start(&line, when, sizeof when);
    if (event->is_all_day) {
        app_text_add(&line, app_string(STR_ALL_DAY));
    } else {
        moment[time_text(event->start, HOST_TIME_CLOCK, moment, MOMENT_MAX)] = '\0';
        app_text_add(&line, moment);
        app_text_add(&line, " - ");
        moment[time_text(event->end, HOST_TIME_CLOCK, moment, MOMENT_MAX)] = '\0';
        app_text_add(&line, moment);
    }
    ui_text(HOST_UI_MARGIN, top, HOST_UI_TITLE, when, app_text_length(when), width);
    top += ui_text_height(HOST_UI_TITLE) + HOST_UI_MARGIN / 2;

    const char *name = event->title[0] != '\0' ? event->title : app_string(STR_EVENT);
    ui_text_lines(HOST_UI_MARGIN, top, HOST_UI_TEXT, name, app_text_length(name), width, TITLE_LINES);
    ui_actions(NULL, 0, app_string(STR_BACK), app_text_length(app_string(STR_BACK)));
}

void app_on_draw(void)
{
    int64_t now = time_now();
    int32_t count = count_visible_events(now);
    if (count == 0 || now == 0) {
        is_reading_one = false;
        draw_reason();
        return;
    }
    list.count = count;
    if (list.selected >= count) {
        list.selected = count - 1;
    }
    if (is_reading_one) {
        /* The event being read, wherever it is now; one that is over, or
         * gone from the calendar, gives the screen back to the list. */
        for (int32_t row = 0; row < count; row++) {
            const ics_event_t *event = &stored.events[shown[row]];
            if (event->uid == read_uid && event->start == read_start) {
                list.selected = row;
                draw_event(event);
                return;
            }
        }
        is_reading_one = false;
    }
    int32_t first = app_list_first(&list), visible = ui_list_visible(&list);
    for (int32_t row = first; row < first + visible && row < count; row++) {
        const ics_event_t *event = &stored.events[shown[row]];
        char when[MOMENT_MAX + 1];
        int32_t when_length = say_when(event, now, when, MOMENT_MAX);
        /* The host's row keeps the moment whole and cuts the name. */
        const char *name = event->title[0] != '\0' ? event->title : app_string(STR_EVENT);
        ui_list_row(&list, row, name, app_text_length(name), when, when_length, HOST_ICON_NONE);
    }
}
