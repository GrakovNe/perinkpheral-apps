/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * The calendar's reader, tried on a computer:
 *     cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -I.. -o test_ics test_ics.c ../ics.c && ./test_ics
 */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "ics.h"

static int checks, failures;
#define CHECK(condition)                                                      \
    do {                                                                      \
        checks++;                                                             \
        if (!(condition)) {                                                   \
            failures++;                                                       \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);       \
        }                                                                     \
    } while (0)

static int64_t utc(int year, int month, int day, int hour, int minute)
{
    struct tm parts = { .tm_year = year - 1900, .tm_mon = month - 1, .tm_mday = day,
                        .tm_hour = hour, .tm_min = minute };
    return (int64_t)timegm(&parts);
}

static ics_t       ics;
static ics_event_t events[ICS_EVENTS_MAX];

#define MSK (3 * 3600)

/* The file in pieces of `piece` bytes, as the network would give it. */
static int32_t read_file(const char *text, int64_t from, int days, int32_t offset, int piece)
{
    ics_begin(&ics, events, from, from + (int64_t)days * 86400, offset);
    int32_t length = (int32_t)strlen(text);
    for (int32_t at = 0; at < length; at += piece) {
        ics_feed(&ics, text + at, length - at < piece ? length - at : piece);
    }
    return ics_end(&ics);
}

#define ZONES                                                                              \
    "BEGIN:VTIMEZONE\r\nTZID:Europe/Berlin\r\n"                                            \
    "BEGIN:DAYLIGHT\r\nTZOFFSETFROM:+0100\r\nTZOFFSETTO:+0200\r\nDTSTART:19700329T020000\r\n" \
    "RRULE:FREQ=YEARLY;BYMONTH=3;BYDAY=-1SU\r\nEND:DAYLIGHT\r\n"                           \
    "BEGIN:STANDARD\r\nTZOFFSETFROM:+0200\r\nTZOFFSETTO:+0100\r\nDTSTART:19701025T030000\r\n" \
    "RRULE:FREQ=YEARLY;BYMONTH=10;BYDAY=-1SU\r\nEND:STANDARD\r\nEND:VTIMEZONE\r\n"         \
    "BEGIN:VTIMEZONE\r\nTZID:Europe/Moscow\r\n"                                            \
    "BEGIN:STANDARD\r\nTZOFFSETFROM:+0300\r\nTZOFFSETTO:+0300\r\nDTSTART:19700101T000000\r\n" \
    "END:STANDARD\r\nEND:VTIMEZONE\r\n"

static void test_single_events(void)
{
    static const char FILE_TEXT[] =
        "BEGIN:VCALENDAR\r\nVERSION:2.0\r\n" ZONES
        "BEGIN:VEVENT\r\nUID:a\r\nDTSTART:20261113T120000Z\r\nDTEND:20261113T130000Z\r\n"
        "SUMMARY:Lunch\\, with a comma\r\nEND:VEVENT\r\n"
        "BEGIN:VEVENT\r\nUID:b\r\nDTSTART;TZID=Europe/Moscow:20261114T100000\r\n"
        "DTEND;TZID=Europe/Moscow:20261114T113000\r\nSUMMARY:A very long title that is fol\r\n"
        " ded over two lines\r\nBEGIN:VALARM\r\nACTION:DISPLAY\r\nDESCRIPTION:Not the title\r\n"
        "TRIGGER:-PT10M\r\nEND:VALARM\r\nEND:VEVENT\r\n"
        "BEGIN:VEVENT\r\nUID:c\r\nDTSTART;VALUE=DATE:20261115\r\nDTEND;VALUE=DATE:20261116\r\n"
        "SUMMARY:\xD0\x94\xD0\xB5\xD0\xBD\xD1\x8C \xD1\x80\xD0\xBE\xD0\xB6\xD0\xB4\xD0\xB5\xD0\xBD\xD0\xB8\xD1\x8F\r\n"
        "END:VEVENT\r\n"
        "BEGIN:VEVENT\r\nUID:d\r\nDTSTART:20251113T120000Z\r\nSUMMARY:Last year\r\nEND:VEVENT\r\n"
        "BEGIN:VEVENT\r\nUID:e\r\nDTSTART;TZID=Europe/Berlin:20261116T090000\r\n"
        "DURATION:PT45M\r\nSUMMARY:Berlin in winter\r\nEND:VEVENT\r\n"
        "BEGIN:VEVENT\r\nUID:f\r\nDTSTART:20261117T080000\r\nSUMMARY:No zone at all\r\n"
        "STATUS:CONFIRMED\r\nEND:VEVENT\r\n"
        "BEGIN:VEVENT\r\nUID:g\r\nDTSTART:20261117T090000Z\r\nSUMMARY:Called off\r\n"
        "STATUS:CANCELLED\r\nEND:VEVENT\r\n"
        "END:VCALENDAR\r\n";
    int64_t now = utc(2026, 11, 13, 9, 0);
    for (int piece = 1; piece <= 97; piece += 32) {
        int32_t count = read_file(FILE_TEXT, now, 14, MSK, piece);
        CHECK(count == 5);
        CHECK(events[0].start == utc(2026, 11, 13, 12, 0) && events[0].end == utc(2026, 11, 13, 13, 0));
        CHECK(strcmp(events[0].title, "Lunch, with a comma") == 0);
        CHECK(events[1].start == utc(2026, 11, 14, 7, 0) && events[1].end == utc(2026, 11, 14, 8, 30));
        CHECK(strcmp(events[1].title, "A very long title that is folded over two lines") == 0);
        CHECK(events[2].is_all_day && events[2].start == utc(2026, 11, 14, 21, 0)); /* midnight in Moscow */
        CHECK(events[2].end - events[2].start == 86400);
        CHECK(strcmp(events[2].title, "\xD0\x94\xD0\xB5\xD0\xBD\xD1\x8C \xD1\x80\xD0\xBE\xD0\xB6\xD0\xB4\xD0\xB5\xD0\xBD\xD0\xB8\xD1\x8F") == 0);
        CHECK(events[3].start == utc(2026, 11, 16, 8, 0) && events[3].end == utc(2026, 11, 16, 8, 45));
        CHECK(events[4].start == utc(2026, 11, 17, 5, 0)); /* 08:00 on the device */
    }
}

static void test_summer_time(void)
{
    static const char FILE_TEXT[] =
        "BEGIN:VCALENDAR\r\n" ZONES
        "BEGIN:VEVENT\r\nUID:a\r\nDTSTART;TZID=Europe/Berlin:20260601T090000\r\nSUMMARY:S\r\n"
        "RRULE:FREQ=MONTHLY\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";
    /* The 1st of every month at nine in Berlin: seven in UTC in summer,
     * eight in winter, the last Sunday of October between them. */
    int32_t count = read_file(FILE_TEXT, utc(2026, 9, 15, 0, 0), 120, 0, 50);
    CHECK(count == 4);
    CHECK(events[0].start == utc(2026, 10, 1, 7, 0));
    CHECK(events[1].start == utc(2026, 11, 1, 8, 0));
    CHECK(events[2].start == utc(2026, 12, 1, 8, 0));
    CHECK(events[3].start == utc(2027, 1, 1, 8, 0));
}

static void test_repeating(void)
{
    static const char FILE_TEXT[] =
        "BEGIN:VCALENDAR\r\n" ZONES
        /* every working day since 2019, with one day taken out and one moved */
        "BEGIN:VEVENT\r\nUID:standup\r\nDTSTART;TZID=Europe/Moscow:20190107T103000\r\n"
        "DTEND;TZID=Europe/Moscow:20190107T104500\r\nSUMMARY:Standup\r\n"
        "RRULE:FREQ=WEEKLY;BYDAY=MO,TU,WE,TH,FR\r\n"
        "EXDATE;TZID=Europe/Moscow:20261117T103000,20200101T103000\r\nEND:VEVENT\r\n"
        "BEGIN:VEVENT\r\nUID:standup\r\nRECURRENCE-ID;TZID=Europe/Moscow:20261118T103000\r\n"
        "DTSTART;TZID=Europe/Moscow:20261118T150000\r\nDTEND;TZID=Europe/Moscow:20261118T151500\r\n"
        "SUMMARY:Standup, moved\r\nEND:VEVENT\r\n"
        /* every second week, three times in all */
        "BEGIN:VEVENT\r\nUID:review\r\nDTSTART:20261110T140000Z\r\nSUMMARY:Review\r\n"
        "RRULE:FREQ=WEEKLY;INTERVAL=2;COUNT=3\r\nEND:VEVENT\r\n"
        /* every day until the 15th */
        "BEGIN:VEVENT\r\nUID:pills\r\nDTSTART:20261101T060000Z\r\nSUMMARY:Pills\r\n"
        "RRULE:FREQ=DAILY;UNTIL=20261115T235959Z\r\nEND:VEVENT\r\n"
        /* the second Tuesday of a month */
        "BEGIN:VEVENT\r\nUID:board\r\nDTSTART:20260113T160000Z\r\nSUMMARY:Board\r\n"
        "RRULE:FREQ=MONTHLY;BYDAY=2TU\r\nEND:VEVENT\r\n"
        /* a birthday */
        "BEGIN:VEVENT\r\nUID:birthday\r\nDTSTART;VALUE=DATE:19900120\r\nSUMMARY:Birthday\r\n"
        "RRULE:FREQ=YEARLY\r\nEND:VEVENT\r\n"
        "END:VCALENDAR\r\n";
    int64_t now = utc(2026, 11, 13, 8, 0); /* a Friday, 11:00 in Moscow */
    int32_t count = read_file(FILE_TEXT, now, 7, MSK, 64);
    /* Fri 13: standup 10:30 is over. Pills 14, 15. Mon 16 standup; Tue 17 taken out;
     * Wed 18 moved to 15:00; Thu 19, Fri 20 standup (20th 07:30 UTC is inside the week). */
    CHECK(count == 6);
    CHECK(strcmp(events[0].title, "Pills") == 0 && events[0].start == utc(2026, 11, 14, 6, 0));
    CHECK(strcmp(events[1].title, "Pills") == 0 && events[1].start == utc(2026, 11, 15, 6, 0));
    CHECK(strcmp(events[2].title, "Standup") == 0 && events[2].start == utc(2026, 11, 16, 7, 30));
    CHECK(strcmp(events[3].title, "Standup, moved") == 0 && events[3].start == utc(2026, 11, 18, 12, 0));
    CHECK(strcmp(events[4].title, "Standup") == 0 && events[4].start == utc(2026, 11, 19, 7, 30));
    CHECK(strcmp(events[5].title, "Standup") == 0 && events[5].start == utc(2026, 11, 20, 7, 30));

    /* Sixty days: the review's second and third time and no fourth, the board, no birthday yet. */
    count = read_file(FILE_TEXT, utc(2026, 11, 23, 0, 0), 60, MSK, 64);
    int reviews = 0, boards = 0, birthdays = 0;
    for (int32_t i = 0; i < count; i++) {
        reviews += strcmp(events[i].title, "Review") == 0;
        boards += strcmp(events[i].title, "Board") == 0;
        birthdays += strcmp(events[i].title, "Birthday") == 0;
    }
    CHECK(count == ICS_EVENTS_MAX); /* the standups fill it: the earliest are kept */
    CHECK(reviews == 2);
    CHECK(boards == 1 && birthdays == 0);

    count = read_file(FILE_TEXT, utc(2027, 1, 19, 0, 0), 2, MSK, 64);
    birthdays = 0;
    for (int32_t i = 0; i < count; i++) {
        if (strcmp(events[i].title, "Birthday") == 0) {
            birthdays++;
            CHECK(events[i].is_all_day && events[i].start == utc(2027, 1, 19, 21, 0));
        }
    }
    CHECK(birthdays == 1);
}

static void test_rejects_non_calendar(void)
{
    CHECK(read_file("", 0, 14, 0, 8) == 0);
    CHECK(read_file("<html><body>Sign in</body></html>", 0, 14, 0, 8) == 0);
    CHECK(read_file("BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\nSUMMARY:No start\r\nEND:VEVENT\r\n", 0, 14, 0, 8) == 0);
    CHECK(read_file("BEGIN:VEVENT\nDTSTART:2026\nEND:VEVENT\nBEGIN:VEVENT\nDTSTART:20261113T25\n"
                    "RRULE:FREQ=DAILY;INTERVAL=0;BYDAY=,,XX,9\nEND:VEVENT", utc(2026, 11, 13, 0, 0), 14, 0, 3) == 0);
    /* a line far longer than is kept, and an event that never ends */
    static char huge[5000];
    int at = snprintf(huge, sizeof huge, "BEGIN:VEVENT\nDTSTART:20261114T100000Z\nDESCRIPTION:");
    memset(huge + at, 'x', 4000);
    snprintf(huge + at + 4000, sizeof huge - at - 4000, "\nSUMMARY:After a long line\nEND:VEVENT\n");
    CHECK(read_file(huge, utc(2026, 11, 13, 0, 0), 14, 0, 100) == 1);
    CHECK(strcmp(events[0].title, "After a long line") == 0);
}

int main(void)
{
    test_single_events();
    test_summer_time();
    test_repeating();
    test_rejects_non_calendar();
    printf("%d checks, %d failed\n", checks, failures);
    return failures != 0;
}
