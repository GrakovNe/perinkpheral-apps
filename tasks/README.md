# Tasks: applications to write

Each file here describes one application to write for perINKpheral, in enough detail to be done
without asking. Read this file first: what holds for all of them.

## The device, in one paragraph

An e-paper screen of 400x300 pixels, 1 bit, on an ESP32-C6, with four buttons: 1 UP, 2 OK,
3 DOWN, 4 BACK. A partial refresh takes about half a second, so at most about 1.5 pictures a
second; it sits on a shelf and is read at a glance. Applications are WebAssembly modules written
in C, run one at a time in 64 KB of memory, unloaded whenever another needs the room, and loaded
again from what they stored. The device fetches the data itself and hands it to the application.

## Read before writing

In the repository `perinkpheral` (next to this one):
- `sdk/host_api.h` and `sdk/host/*.h` - the only contract: every call, every limit, every rule,
  in the comments. `base.h`, `app.h`, `gfx.h`, `ui.h`, `net.h`, `store.h`, `time.h`.
- `sdk/lib/*.h` - what applications link instead of a C library: `app_text.h` (text building),
  `app_draw.h`, `app_ui.h` (what the buttons do on the kit's screens), `app_state.h` (the store,
  settings, the session), `app_values.h` and `app_fixed.h` (numbers with decimals), `json_pick.h`,
  `app_days.h` (dates).
- `docs/guide/perinkpheral-app-guide.pdf` - the guide; its source is `docs/guide/source/guide.html`.
- The application named as the model in each task, in `apps/` here: read it whole and copy its
  shape - the file's opening comment, how it keeps state, how it draws.

## Rules the build enforces, and the ones it cannot

- **No floating point**, anywhere: the build refuses a module with a float operation. Numbers
  with decimals are integers of tenths or hundredths - `app_fixed_parse` and `app_values.h`.
- **No C library**: no `printf`, `malloc`, `strlen`. Use `sdk/lib` (`app_text_*`, `app_alloc`).
- **Draw only in `app_on_draw()`**. Handlers change the state and call `screen_redraw()`.
- **The application is unloaded at any moment** - when it leaves the screen, and for another
  application's data. What must survive goes into the store (`app_state_save`, at most 2 KB for
  everything, a write of flash each time: write when it changed, not every second). Where the user
  is - a page, a selection - goes into the session (`app_session_save` in `app_on_blur`,
  `app_session_load` in `app_on_focus`).
- **A handler has 3 seconds**; one that takes over 400 ms is said in the log.
- **Alarms and timers**: `timer_set` works only on the screen and stops when the application
  leaves it; `alarm_set*` works on or off the screen. A new version does not inherit alarms: set
  them again in `app_on_updated()`, and after a restart in the `started` intent if needed.
- Every string a person reads is in `strings/en.json` and `strings/ru.json` (the same keys), and
  the manifest's title, description and settings are translated under the key `manifest` of
  `ru.json`. The build makes `strings.h` (`STR_...`, `app_string(id)`). It refuses a string the
  code never uses, a key missing in one language, a letter the Russian glyphs lack.
- Interface text is short and standard: "Loading", "No data", "Every 5 min" - no sentences.
  The description in the manifest is one line of what the application is, at most 95 characters:
  "Exchange rates.", not "Shows you the latest exchange rates every morning".
- Everything in English in the code and comments; plain hyphens, never long dashes.
- Every source file starts with `// SPDX-License-Identifier: MIT` (see any application).
- The manifest: `name` (the folder's name), `version` "0.1.0", `title`, `description`, `abi` 3,
  `icon` - 11 rows of 11 characters, `#` ink and `.` paper, drawn in the manner of the others.

## Building and checking

```
export WASI_SDK_PATH=<where wasi-sdk is>
../perinkpheral/tools/perinkpheral build apps/<name> --out /tmp/<name>   # build and check
../perinkpheral/tools/perinkpheral serve apps/<name>                     # in a browser, with buttons
../perinkpheral/tools/perinkpheral sim-test tests                         # every scenario
./build.sh                                                               # the catalog
```
A failed build says what is wrong in one line: read it and fix exactly that.

Every application gets a test: `tests/<name>/scenario.json`, its `mocks.json` (the network's
answers, so that the test never goes online) and the pictures it expects. The format is in
`perinkpheral/tools/cli/src/main/kotlin/org/perinkpheral/cli/SimTest.kt`; `tests/weather` is an
example. Run `sim-test tests --update` once to write the pictures, look at every one of them, and
keep only those that are right.

## Done means

- `build` passes with no warning; the summary it prints shows the module and memory in their limits.
- `sim-test tests` passes, with pictures you looked at.
- `./build.sh` adds it to the catalog.
- Both languages look right in the simulator (`"language": "ru"` in a scenario).
- Leave the changes uncommitted for review on `abi-freeze`. Never push.
