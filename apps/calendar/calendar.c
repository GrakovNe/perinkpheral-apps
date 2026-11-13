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
 * It reminds, too: some minutes before an event it takes the screen from
 * whatever has it and says what begins. The reminder is an alarm of the
 * host's at a moment worked out from the events in the store, so it comes
 * although the application is not loaded - and after a restart, which
 * forgets alarms, it is set again.
 */
#include <stdbool.h>
#include <stddef.h>

#include "app_text.h"
#include "app_ui.h"
#include "host_api.h"
#include "ics.h"

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
#define EVENTS_VERSION 1
typedef struct {
    int32_t     version;
    int32_t     count;
    ics_event_t events[ICS_EVENTS_MAX];
} stored_events_t;

#define STATE_KEY     "state"
#define STATE_VERSION 1
typedef struct {
    int32_t version;
    int32_t status;      /* of the last fetch: 0 none yet, an HTTP one, a HOST_FAILED_*, a STATUS_* */
    int64_t reminded_to; /* reminders due up to this moment were given */
    /* The reminder on the screen, until somebody answers it. */
    int32_t is_reminding;
    int32_t took_the_screen;
    int64_t reminded_start;
    char    reminded_title[ICS_TITLE_MAX + 1];
} state_t;

static stored_events_t stored;
static state_t         state;
static bool            is_on_screen;

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

static int32_t setting(const char *key, int32_t key_length, char *value, int32_t size)
{
    int32_t length = kv_get(key, key_length, value, size - 1);
    value[length > 0 ? length : 0] = '\0';
    return length > 0 ? length : 0;
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
    if (setting("remind", 6, choice, sizeof choice) == 0) {
        return 10 * SECONDS_PER_MINUTE; /* nothing chosen: the manifest's default */
    }
    return choice[0] == 'O' ? -1 : seconds_of(choice, 0);
}

static void wake_at_the_next_turn(void);

/* ---- the reminder ---- */

/* The alarm for the reminder that comes next, or none. */
static void set_the_alarm(void)
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

void app_on_alarm(int32_t id)
{
    (void)id;
    int64_t now = time_now();
    int32_t lead = lead_seconds();
    for (int32_t i = 0; i < stored.count && lead >= 0 && now != 0; i++) {
        const ics_event_t *event = &stored.events[i];
        int64_t due = event->start - lead;
        if (event->is_all_day || due <= state.reminded_to || due > now ||
            event->start + LATE_SECONDS < now) {
            continue;
        }
        state.is_reminding = 1;
        state.took_the_screen = !is_on_screen;
        state.reminded_start = event->start;
        for (int32_t at = 0; at <= ICS_TITLE_MAX; at++) {
            state.reminded_title[at] = event->title[at];
        }
        request_focus();
        request_redraw(HOST_REDRAW_SOON);
        if (is_on_screen) {
            wake_at_the_next_turn();
        }
        break;
    }
    if (now != 0) {
        state.reminded_to = now;
    }
    save_state();
    set_the_alarm();
}

/* ---- the feed ---- */

static ics_t       reader;
static ics_event_t incoming[ICS_EVENTS_MAX];
static bool        is_reading;

static void take_the_events(int32_t count)
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

static void end_the_fetch(int32_t status)
{
    is_reading = false;
    if (state.status != status) {
        state.status = status;
        save_state();
    }
    set_the_alarm();
    request_redraw(HOST_REDRAW_NORMAL);
}

static bool begins_a_calendar(const uint8_t *data, int32_t length)
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
        end_the_fetch(http_status); /* what was there stays */
        return;
    }
    if (flags & HOST_DATA_FIRST) {
        int64_t now = time_now();
        is_reading = false;
        if (http_status != HTTP_OK) {
            end_the_fetch(http_status);
        } else if (now == 0) {
            end_the_fetch(STATUS_NO_TIME); /* "the next two weeks" needs a today */
        } else if (!begins_a_calendar(data, length)) {
            end_the_fetch(STATUS_NOT_A_CALENDAR);
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
        take_the_events(ics_end(&reader));
        end_the_fetch(HTTP_OK);
    }
}

static void keep_the_pace(void)
{
    char choice[16];
    setting("every", 5, choice, sizeof choice);
    poll_set_interval(FEED_POLL, seconds_of(choice, 15 * SECONDS_PER_MINUTE));
}

/* ---- when things happen ---- */

/* The list changes by itself when an event begins or ends. */
static void wake_at_the_next_turn(void)
{
    int64_t now = time_now(), next = 0;
    if (state.is_reminding && now != 0) {
        /* "In 5 min" is another line a minute later. */
        int64_t left = state.reminded_start - now;
        timer_set(TURN_TIMER, (int32_t)(left > 0 ? (left - 1) % SECONDS_PER_MINUTE + 1
                                                 : SECONDS_PER_MINUTE) * MS_PER_SECOND);
        return;
    }
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
    is_on_screen = true;
    wake_at_the_next_turn();
}

void app_on_blur(void)
{
    is_on_screen = false;
}

void app_on_timer(int32_t id)
{
    (void)id;
    request_redraw(HOST_REDRAW_NORMAL);
    wake_at_the_next_turn();
}

void app_on_intent(int32_t intent)
{
    /* A restart forgot the alarm, and a clock set anew moves "in ten
     * minutes" to another moment. */
    if (intent == HOST_INTENT_STARTED) {
        keep_the_pace();
    }
    set_the_alarm();
}

void app_on_settings(void)
{
    keep_the_pace();
    set_the_alarm();
    request_redraw(HOST_REDRAW_NORMAL);
}

/* ---- the buttons ---- */

static app_list_t list = { .selected = -1 };

static void answer_the_reminder(void)
{
    bool leaves = state.took_the_screen != 0;
    state.is_reminding = 0;
    save_state();
    if (leaves) {
        request_exit(); /* back to what the user was looking at */
    } else {
        request_redraw(HOST_REDRAW_NORMAL);
    }
}

void app_on_button(int32_t event)
{
    if (state.is_reminding) {
        if (event == BTN_OK_PRESSED) {
            answer_the_reminder();
        }
        return;
    }
    if (event == BTN_OK_PRESSED) {
        poll_now(FEED_POLL);
    } else if (app_list_button(&list, event) == APP_UI_CHANGED) {
        request_redraw(HOST_REDRAW_NO_BLINK);
    }
}

void app_on_back(void)
{
    if (state.is_reminding) {
        answer_the_reminder();
    } else {
        request_exit();
    }
}

/* ---- the picture ---- */

static const char *const WEEKDAYS[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
static const char *const MONTHS[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                      "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

static void add_clock(app_text_t *text, const host_local_time_t *at)
{
    app_text_add_padded(text, at->hour, 2);
    app_text_add(text, ":");
    app_text_add_padded(text, at->minute, 2);
}

/* How many days from today the moment is, on the user's calendar. */
static int32_t days_from_today(int64_t now, int64_t moment)
{
    host_local_time_t today;
    time_local_at(now, &today);
    /* Midnight today, as a moment; the user's days are counted from it. */
    today.hour = today.minute = today.second = 0;
    int64_t midnight = time_from_local(&today);
    int64_t since = moment - midnight;
    return (int32_t)(since >= 0 ? since / SECONDS_PER_DAY : -1);
}

/* When an event is, as short as it can be said: "15:00" today, "Sat 15:00"
 * this week, "21 Nov" beyond it; a whole day is its day alone. */
static void say_when(const ics_event_t *event, int64_t now, char *line, int32_t size)
{
    app_text_t        text;
    host_local_time_t at;
    app_text_start(&text, line, size);
    time_local_at(event->start, &at);
    int32_t days = days_from_today(now, event->start);
    if (event->start <= now) {
        app_text_add(&text, event->is_all_day ? "Today" : "Now");
    } else if (days == 0) {
        if (event->is_all_day) {
            app_text_add(&text, "Today");
        } else {
            add_clock(&text, &at);
        }
    } else if (days < 7) {
        app_text_add(&text, days == 1 ? "Tmrw" : WEEKDAYS[at.weekday]);
        if (!event->is_all_day) {
            app_text_add(&text, " ");
            add_clock(&text, &at);
        }
    } else {
        app_text_add_number(&text, at.day);
        app_text_add(&text, " ");
        app_text_add(&text, MONTHS[at.month - 1]);
    }
}

/* The reminder: when it begins, how soon that is, and what it is - the
 * title in the line of ordinary text, which holds the most of it. */
static void draw_reminder(void)
{
    host_notice_t     notice = { .icon = HOST_ICON_BELL, .ok = "Dismiss" };
    host_local_time_t at;
    app_text_t        text;
    time_local_at(state.reminded_start, &at);
    app_text_start(&text, notice.first, sizeof notice.first);
    add_clock(&text, &at);
    app_text_start(&text, notice.second, sizeof notice.second);
    int64_t minutes =
        (state.reminded_start - time_now() + SECONDS_PER_MINUTE - 1) / SECONDS_PER_MINUTE;
    if (minutes > 0) {
        app_text_add(&text, "In ");
        app_text_add_number(&text, (int32_t)minutes);
        app_text_add(&text, " min");
    } else {
        app_text_add(&text, "Now");
    }
    app_text_start(&text, notice.detail, sizeof notice.detail);
    app_text_add(&text, state.reminded_title[0] != '\0' ? state.reminded_title : "Event");
    ui_notice(&notice);
}

/* Why there is nothing to show: what the last fetch came to. */
static void draw_reason(void)
{
    static const struct {
        int32_t     status;
        int32_t     icon;
        const char *first, *detail;
    } REASONS[] = {
        { 0, HOST_ICON_HOURGLASS, "Loading", "" },
        { HOST_FAILED_NOT_READY, HOST_ICON_GEAR, "Not set up", "Open the page" },
        { HOST_FAILED_NO_NETWORK, HOST_ICON_NO_SIGNAL, "No network", "" },
        { HTTP_OK, HOST_ICON_CHECK, "No events", "" },
        { STATUS_NOT_A_CALENDAR, HOST_ICON_WARNING, "Not a calendar", "Check the address" },
        { STATUS_NO_TIME, HOST_ICON_CLOCK, "No time yet", "" },
        { HOST_FAILED_TOO_LARGE, HOST_ICON_WARNING, "Too large", "" },
    };
    host_notice_t notice = { .icon = HOST_ICON_WARNING };
    app_text_t    first, detail;
    app_text_start(&first, notice.first, sizeof notice.first);
    app_text_start(&detail, notice.detail, sizeof notice.detail);
    for (size_t i = 0; i < sizeof REASONS / sizeof REASONS[0]; i++) {
        if (REASONS[i].status == state.status) {
            notice.icon = REASONS[i].icon;
            app_text_add(&first, REASONS[i].first);
            app_text_add(&detail, REASONS[i].detail);
            ui_notice(&notice);
            return;
        }
    }
    app_text_add(&first, "No answer");
    app_text_add(&detail, "Code ");
    app_text_add_number(&detail, state.status);
    ui_notice(&notice);
}

/* From the row's left edge to its value, and what the scroll bar takes. */
#define ROW_GAP      16
#define ROW_BAR_ROOM 12

void app_on_draw(void)
{
    int64_t now = time_now();
    if (state.is_reminding && state.reminded_start + LATE_SECONDS > now) {
        draw_reminder();
        return;
    }
    /* What is over is not shown; the events are in order of their start. */
    static int32_t shown[ICS_EVENTS_MAX];
    int32_t        count = 0;
    for (int32_t i = 0; i < stored.count; i++) {
        if (stored.events[i].end > now) {
            shown[count++] = i;
        }
    }
    if (count == 0 || now == 0) {
        draw_reason();
        return;
    }
    list.count = count;
    int32_t first = app_list_first(&list), visible = ui_list_visible(&list);
    for (int32_t row = first; row < first + visible && row < count; row++) {
        const ics_event_t *event = &stored.events[shown[row]];
        char when[16], title[ICS_TITLE_MAX + 1];
        say_when(event, now, when, sizeof when);
        int32_t when_length = app_text_length(when);
        /* The title has what the moment leaves of the row. */
        int32_t room = gfx_width() - 2 * HOST_UI_MARGIN - ROW_GAP - (count > visible ? ROW_BAR_ROOM : 0) -
                       gfx_text_width(HOST_FONT_TEXT, when, when_length);
        const char *name = event->title[0] != '\0' ? event->title : "Event";
        int32_t title_length = gfx_text_fit(HOST_FONT_TEXT, name, app_text_length(name), room, title,
                                            sizeof title);
        ui_list_row(&list, row, title, title_length, when, when_length, HOST_ICON_NONE);
    }
}
