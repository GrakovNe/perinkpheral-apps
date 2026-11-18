# Initial brief: `pomodoro` - alternating work sessions and breaks

Archived design brief for an application that has shipped. This records the original
proposal, not current behavior or outstanding work. See the [archive index](README.md)
and [contribution guide](../../../CONTRIBUTING.md).

Use `apps/timer` as the reference for completion alarms, minute updates, notices,
header status and a countdown based on uptime and the boot identifier.

## What it is

Focus sessions and breaks in turns: 25 minutes of work, a 5 minute break, and after every fourth
work a 15 minute break. It runs while the user is in other applications: the minutes left stand in
the header, and the end of every phase is a notice over whatever has the screen.

```
   WORK                         3 of 4
   
             18 min
   
   ████████████░░░░░░░░    (how much of the phase is gone)
   
                 OK Pause
```
No network. No sounds (the device has none).

## Manifest

- `name` `pomodoro`, `title` "Pomodoro" / "Помодоро", `description` "Work and breaks in turns." /
  "Работа и перерывы по очереди.".
- No polls, no hosts. `intents` none (a restart ends a session, as timer's countdown ends).
- `settings`:
  - `work`, `choice`, "Work" / "Работа", options `["15 min", "25 min", "45 min", "50 min"]`,
    default `"25 min"`.
  - `break`, `choice`, "Break" / "Перерыв", options `["5 min", "10 min"]`, default `"5 min"`.
  - `long`, `choice`, "Long break" / "Длинный перерыв", options `["15 min", "20 min", "30 min"]`,
    default `"15 min"`.
  - `rounds`, `choice`, "Long break after" / "Длинный перерыв после", options `["2", "3", "4"]`,
    default `"4"` (works).
  Read the configured duration with `app_setting_seconds("work", 25 * 60)`.
- An icon: a tomato-like round fruit is fine, or an hourglass with a mark - not Perinkpheral OS's own
  hourglass icon copied.

## State

One struct in the store, a version first (copy timer's `timer_state_t` and widen it):
- `phase`: work, break, long break;
- `round`: which work of the cycle (1..rounds);
- `ends_at`: uptime in seconds when the phase ends, 0 when not running; `boot`: `time_boot_id()`
  of the run `ends_at` belongs to - a restart ends the session (timer's `app_on_load` shows how);
- `paused_left`: seconds left when paused, 0 when not paused;
- `phase_length`: seconds, as it was when the phase began (a setting changed meanwhile applies
  from the next phase).
Write it when it changes (start, pause, phase end, skip, reset) - not every minute.

## Behaviour

- **Not started**: the screen says what comes first ("Work", 25 min) and `OK Start`.
- **OK** starts or pauses. Running: `alarm_set(END_ALARM, seconds_left)` for the phase's end,
  `alarm_set_every(MINUTE_ALARM, 60, seconds to the next whole minute)` for the header, and
  `header_set_status(HOST_HEADER_RIGHT, icon, "18 min")`. Paused: both alarms cancelled
  (`alarm_set(id, 0)`), the header says "Paused" / "Пауза", the left time kept.
- **DOWN** skips to the next phase (a work skipped counts as done: the round moves on).
- **Long OK** resets to the first work of the cycle, not running; clear the application's header status
  (`header_set_status` with an empty text).
- **UP** does nothing (or the same as DOWN backwards - not needed).
- **The end of a phase** (`app_on_alarm(END_ALARM)`, on or off the screen): the next phase begins
  at once (running on), the state saved, and a notice: `notification_post` with "Break" / "Перерыв" and
  "5 min", or "Work" / "Работа" and "Round 3 of 4" - `host_notice_t` with an icon (bell). Then
  the alarms set again for the new phase.
- **MINUTE_ALARM**: the header's minutes updated; on the screen a redraw (`HOST_REDRAW_NORMAL`).
- **On the screen**, a timer (`timer_set`) redraws the minutes when they change, as timer does;
  timers stop when the application leaves, alarms go on.
- `app_on_updated()`: a phase running is set going again where it is - its alarms and the header
  (timer has it).
- Count whole minutes up: "18 min" while 17:01-18:00 are left; "<1 min" in the last minute.

## Screen

- Not full screen.
- The phase's name large at the top left (`ui_draw_title` or the medium font), "3 of 4" / "3 из 4" at
  the right, small.
- The minutes in the huge font, centred (`ui_draw_value` or `app_draw_centered` with `HOST_FONT_HUGE`
  and the unit after it in the text font).
- A bar of how much of the phase is gone: `ui_draw_bar(x, y, w, gone, length)`.
- Along the bottom what OK does - "Start" or "Pause" - with `app_draw_actions(word, NULL)`.
  DOWN to skip and long OK to reset are secondary actions and are not labelled on
  this screen.

## Test

`tests/pomodoro`: start (`press 2`), `shot started`, `forward 1500` (25 min), `wait 1`, `shot
break` (the notice on the screen), `press 2` (dismiss), `shot break-running`. And in Russian.
