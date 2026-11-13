# Calendar

What is coming, from any calendar that gives a link to itself: Google, iCloud,
Outlook, Nextcloud, Fastmail. The device fetches the calendar itself - no
account is bound to anything and nothing passes a server of ours - and reminds
of an event some minutes before it, whatever is on the screen then.

## Setting it up

Every calendar service has an address under which a calendar can be read by
whoever knows it. Copy it and paste it into **Calendar address** on the device's
page (Apps, the gear on Calendar):

- **Google Calendar**: Settings, the calendar, "Secret address in iCal format".
- **iCloud**: share the calendar as a public calendar and copy its link.
- **Outlook**: Settings, Calendar, Shared calendars, Publish a calendar, the ICS link.
- **Nextcloud and others**: the calendar's "subscription" or "export" link.

An address that begins with `webcal://` is taken as it is. The address is a
secret: whoever has it reads the calendar.

**Refresh** is how often the calendar is fetched; **Reminder** is how long
before an event the device says so, or Off.

## On the device

The next two weeks, a line an event: what, and when - "15:00" today, "Tmrw
9:30", "Sat 12:00", "21 Nov" beyond a week, "Now" while it lasts. UP and DOWN
move the list, OK fetches the calendar now. A reminder takes the screen with
the event's time and name; OK or BACK dismisses it and returns what was there.

## What it reads

Events with a start and an end, whole days, times in UTC, in a zone the
calendar describes (summer time included) and in no zone at all; events that
repeat by the day, the week, the month and the year - with an interval, a
count, an end, days of the week, "the second Tuesday", days taken out - and
single occurrences that were moved or cancelled. A calendar of a megabyte, some
years of meetings, is read in about ten seconds.

Not read: rules by position in a set (BYSETPOS), by week number, by hour; such
an event repeats by its plain frequency. One calendar, the 24 nearest events.
The fonts of the device are ASCII: Russian is shown in Latin letters, other
scripts as "?".

`test/test_ics.c` tries the reader on a computer.
