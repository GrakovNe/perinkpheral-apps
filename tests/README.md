# Application tests

Run from the repository root with Java 21 or later, wasi-sdk (`WASI_SDK_PATH`) and a C
compiler. The tools are the firmware repository's; `../perinkpheral-os` below is a checkout
of it.

```sh
../perinkpheral-os/tools/perinkpheral app-check apps
../perinkpheral-os/tools/perinkpheral sim-test tests
```

There are no test scripts here and no list of applications anywhere: both commands take a
folder and find what is in it.

## app-check: every application against the contract

`app-check apps` compiles each application's C for this computer, with the address and
undefined-behaviour sanitizers, against the SDK's stand-in host, and runs the SDK's
capability checks on it. The checks know no application. A new application is checked by
doing nothing.

| Capability | Runs on an application that | What it holds the application to |
|---|---|---|
| `sessions` | any | Unloaded and loaded again as a scene that waited in the stack, it shows the same picture and BACK leaves it in as many presses. Opened anew from the menu, it is on its first page. It survives a restart, and a start with an empty store. |
| `store` | calls `kv_set` or `file_write` | Every write is within the limits (1 KB a value, 2 KB an application), with the settings at their longest too. An unchanged answer, a request that fails again and a visit that changed nothing write nothing new: every change is a write of the flash, and a value the store already holds is none, as on the device. A refused write does not stop it or loop, and what it did not save is saved when the same answer comes again. |
| `timers` | calls `timer_set` or `alarm_set*` | A timer 5 s late or delivered twice leaves the same timers waiting as one on time. Timers that waited before an unload wait again after `app_on_focus()`. The clock set a day on or back loses no timer. Alarms come on the screen and off it, and `app_on_updated()` sets them again. |
| `network` | has `polls` or calls `http_send` | An answer in pieces of 1, 7 and 1000 bytes shows what the whole answer shows. One that stops without `HOST_DATA_LAST`, with the background instance that took it, changes nothing. `HOST_DATA_FAILED` after pieces is a request that failed and no more. After every failure status and HTTP 503, 404 and 304 the next whole answer is shown as if nothing had happened. An answer that arrives off the screen is there when the application is back. |
| `drawing` | exports `app_on_draw` | Every state the checks reach draws a frame, with no call the host refuses, and no handler draws outside `app_on_draw()`. |

"The same" is told without knowing the application: by the picture - every drawing call and
its arguments of the frame last drawn, as the device draws one when the application takes the
screen or asks for it, and the header around it - and by the presses of BACK that leave. The clock stands still unless a check
moves it, so an application that shows the time shows the same time before and after.

A line of the report is an application and a capability: `ok`, `FAIL` with what was expected
and seen, or `-` where the capability does not apply. `--app <name>` and
`--only <capability>` narrow a run.

### Why a check fails

- **sessions, another picture after the unload**: where the user is - a page, a row, a value
  half set - is not in the session. Save it in `app_on_blur()` (`app_session_save`), read it
  in `app_on_focus()`.
- **store, written N times**: a handler saves something that changes every time - a count, a
  time - although the answer did not. Compare with what was stored; `app_on_terminate()` is
  no exception.
- **store, not saved when the same answer comes again**: the write was refused, the value in
  memory now equals the answer, and "unchanged" is taken for "saved". Remember that the save
  failed.
- **timers, set again in app_on_focus()**: a timer is set where the user starts something and
  nowhere else. Timers stop when the application leaves the screen.
- **network, an answer that stops**: something of the answer is saved before `HOST_DATA_LAST`.
- **network, drawn anew another picture**: a handler changed what is shown and did not call
  `screen_redraw()`; the panel keeps the old frame until something else asks for one.
- **network, http_send() answered HOST_ERR_ARGUMENT**: the request goes to a host `hosts` does not
  list, or its headers are not four of "Name: value"; the device refuses it when it is queued.
- **network, off the screen another picture**: an answer is read with something kept in memory
  from the instance that asked for it. Off the screen every delivery is an instance of its own.
- **stopped in a handler** with a sanitizer's line: a read or write outside an array, an
  overflow, a shift too far. The line names the source.
- **did not return in 3 s**: a loop without an end; a handler has three seconds on the device.

### What an application keeps beside itself

`apps/<name>/test/check.json` says when and with what the application is checked. Its keys
are a simulator scenario's:

```json
{
  "at": 1794832230,
  "tz": "Europe/Moscow",
  "store": { "city": "Saint Petersburg", "lat": "59.94", "lon": "30.31" },
  "mocks": "../../../tests/weather/mocks.json"
}
```

`mocks` is the answers to its requests: an object of address to answer, or the name of a
file that holds one, the same file a scenario uses. Without an answer a request is told
`HOST_FAILED_NO_ANSWER`, the report says so, and the network checks see only failures. An
application whose first picture depends on chance or on how fast it draws says in `store`
how it is made repeatable, as Life does with its seed.

### An application's own logic

What only one application knows - Conway's rule, an orbit, a calendar's grammar, the order
of its rows - is tested by `apps/<name>/test/*.c`. Each file is a program: `app-check`
compiles it with the same flags against the same stand-in host and runs it in that folder;
it passes when it exits 0 and is reported as `logic <file>`. A test includes the
application's source (`#include "../life.c"`) to reach its state, calls its handlers, and
reads what the host saw from `standin()` (`sdk/check/standin.h` of the firmware repository)
instead of writing a host of its own. Write one when the application has logic of its own;
what the table above covers needs none.

## sim-test: the pictures

Simulator scenarios run real WASM applications with local mock responses and compare
frames pixel by pixel. A `mocks.json` file names response files relative to itself;
shared inputs live in [fixtures](fixtures/README.md). Keep those paths valid when
moving a scenario or response: `apps/<name>/test/check.json` names the same files.

Run `sim-test tests --update` only for an intended rendering change and inspect each
changed PNG. Then repeat without `--update`; never regenerate baselines merely to
silence a mismatch. English/Russian and Departure/Jersey variants cover the recent
applications. The normal simulator runner needs permission to listen on localhost.

The two commands divide the work. `app-check` runs the application's C natively: it finds
memory faults and holds the contract's promises, and its host draws nothing. `sim-test`
runs the module in the firmware's own code: what the panel shows is its to say.

CI runs both with offline fixtures. Passing checks do not establish panel timing, real
button behavior, network endurance or battery life. Those belong to the
[firmware's hardware qualification](https://github.com/Perinkpheral/perinkpheral-os/blob/main/docs/testing/stability.md).
