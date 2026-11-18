# Calendar

Shows upcoming events from an iCalendar (ICS) subscription. The device downloads
the feed directly from its provider. Reminders can appear over another application.

## Setting it up

If your calendar provider offers an ICS subscription or publishing URL, paste it
into **Calendar address** on the device's page (Installed apps, Calendar settings).
Examples:

- **Google Calendar**: Settings, the calendar, "Secret address in iCal format".
- **iCloud**: share the calendar as a public calendar and copy its link.
- **Outlook**: Settings, Calendar, Shared calendars, Publish a calendar, the ICS link.
- **Nextcloud and others**: the calendar's "subscription" or "export" link.

The device page converts `webcal://` and `webcals://` links to `https://`.
Treat a private subscription URL as a secret: anyone with it can read the feed.

**Refresh** sets the download interval. **Reminder** sets how long before an event
to show a notice; choose Off to disable reminders.

## On the device

The list shows events for the next two weeks, with a name and time on each line.
Times appear as "15:00" today, "Tmrw 9:30", "Sat 12:00", "21 Nov" beyond a week,
or "Now" during an event. UP and DOWN scroll; OK refreshes the feed. A reminder
shows the event's time and name. OK or BACK dismisses it and restores the previous screen.

## What it reads

The parser supports timed and all-day events, UTC, floating times and time zones
defined in the feed, including daylight-saving rules. Recurrence supports daily,
weekly, monthly and yearly intervals, counts, end dates, weekdays, ordinal weekdays
such as the second Tuesday, exclusions, and moved or cancelled occurrences.
Responses are parsed incrementally.

Not read: rules by position in a set (BYSETPOS), by week number, by hour; such
an event repeats by its plain frequency. One calendar, the 24 nearest events.
Text uses the host's selected font and installed glyph packs. Missing glyphs fall
back to the host's spelling rules; install the appropriate pack for Cyrillic or Greek.

`test/test_ics.c` tests the parser on the host computer.
