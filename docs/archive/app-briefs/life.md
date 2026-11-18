# Initial brief: `life` - Conway's Game of Life

Archived design brief for an application that has shipped. This records the original
proposal, not current behavior or outstanding work. See the [archive index](README.md)
and [contribution guide](../../../CONTRIBUTING.md).

Reference implementations: `apps/badapple` (a row of pictures with
`HOST_REDRAW_NO_BLINK`, one bitmap drawn per frame) and `apps/kit` for the buttons.

## What it is

The cellular automaton of John Conway on the whole screen: a field of cells, each alive or dead,
every generation a cell with 3 live neighbours is born and one with 2 or 3 survives; the rest die.
The proposed application is an interactive demo and panel-refresh benchmark, targeting about 1.5 frames per second.

```
 ┌──────────────────────────────────────────────┐
 │   ██        █ █          ██                  │
 │  ██          ██   ...    (80 x 60 cells,     │
 │   █          █            5 px each)         │
 │                                              │
 │                            Gen 128   Running │
 └──────────────────────────────────────────────┘
```
No network, no settings needed beyond the speed.

## Manifest

- `name` `life`, `title` "Life" / "Жизнь", `description` "Conway's Game of Life." /
  "Игра «Жизнь» Конвея." (the Russian quotes are fine; the build checks the glyphs).
- `fullscreen` `true` - the whole 400x300.
- `settings`:
  - `speed`, `choice`, "Speed" / "Скорость", options `["Slow", "Fast"]`, default `"Slow"`:
    Slow is a generation every 2 s, Fast as fast as the panel takes them (a timer of 600 ms).
  - `cell`, `choice`, "Cell" / "Клетка", options `["4 px", "5 px", "8 px"]`, default `"5 px"`.
  - `seed`, `number`, "Seed", `"hidden": true` - not on the page; a test puts it in the store to
    make the field the same every run. Empty: random.
- An icon: a glider (the five-cell pattern) on 11x11 is the natural one.

## The field

- With a cell of 5 px the field is 80 x 60 cells; of 4 px 100 x 75; of 8 px 50 x 37. The edges
  wrap (a torus): the cell left of column 0 is the last column.
- Keep a generation as bits: 100 x 75 = 7500 bits = 938 bytes; two of them (now and next). Static
  arrays sized for the largest field.
- A step: for every cell count its 8 neighbours from the current bits into the next; swap. Keep it
  integer and simple; 7500 cells is a few hundred thousand operations - well inside the 400 ms a
  handler should take (measure with `time_uptime_ms` in a first build, then remove the log_write).
- **Drawing**: never a `gfx_draw_rect` per live cell (thousands of host calls). Build one 400x300 1-bit
  bitmap in the application's memory - 400/8 x 300 = 15000 bytes, a static array - with every
  live cell as a cell-sized square of set bits, and call `gfx_draw_bitmap(0, 0, 400, 300, bits)` once
  in `app_on_draw()` (read its bit order in `sdk/host/gfx.h`: rows top to bottom, the most
  significant bit the leftmost pixel). Then the status line over it.
- A status line at the bottom right, small font, on a white box so that cells do not cross it:
  "Gen 128" and "Running"/"Paused" (Russian: "Пок. 128", "Идёт"/"Пауза" - or simply the number
  and a pause icon; short).

## Behaviour

- **Start**: a random field (about a third alive). Random: a small xorshift generator seeded with
  the `seed` setting when it is set, else with `time_uptime_ms()` and `time_boot_id()` - no C
  library.
- **OK**: run / pause. Running: `timer_set(STEP_TIMER, period)`; in `app_on_timer` one generation,
  `screen_redraw(HOST_REDRAW_NO_BLINK)`, and the timer set again. Paused: the timer cancelled and
  **one** `screen_redraw(HOST_REDRAW_CLEAN)` - a full refresh that clears what the quick
  pictures left on the panel.
- **DOWN**: one generation (when paused). **Long DOWN**: ten.
- **UP**: a new random field. **Long UP**: a field with a few classic patterns placed - a glider
  gun, some gliders, a pulsar - if the module fits the code limit (keep each as a short list of
  coordinates).
- **Stop by itself** - to limit continuous refreshing, pause after 500
  generations, or when the field is empty, or when it repeats itself (keep a cheap hash of the
  last few generations: the same hash twice in the last 3 is a still life or a blinker). Paused by
  itself, it says why in the status line ("Still" / "Покой", "Empty" / "Пусто", "500").
- **Leaving the screen** pauses (timers stop anyway). The field is not kept: it is 938 bytes,
  more than the session's 128, and writing it to the store would be a write of flash every
  generation. Back on the screen, a new field begins (from the seed when it is set), paused.

## Test

`tests/life` with `"store": {"seed": "1"}`: `wait 1`, `shot start` (the same field every run),
`press 3` (one generation), `shot step`, `press 2`, `wait 3`, `press 2`, `shot paused`. And
`tests/life-ru` with the status line in Russian.
