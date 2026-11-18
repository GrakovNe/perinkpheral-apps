# Application tests

Run from the repository root with Java 21 or later, wasi-sdk, a C compiler and Make.
`PERINKPHERAL` selects the SDK checkout for shell tests (default `../perinkpheral`).

```sh
tests/check-logic.sh
tests/life/check-session.sh
tests/stopwatch/check-resume.sh
sh tests/pomodoro/check-stop.sh
apps/badapple/test/check.sh
apps/rates/test/check.sh
../perinkpheral/tools/perinkpheral sim-test tests
```

`check-logic.sh` covers air-quality ranking, the streamed GitHub calendar parser and
ISS geometry/history with address and undefined-behavior sanitizers. The Life check
compares generations and rendered cells against an independent Conway implementation,
and checks scene restoration. Pomodoro covers stop and reboot; Bad Apple checks every
rendered frame against the original pixels; Rates covers streamed history and cache failures. The Stopwatch check runs the actual application handlers
through cold boot, delayed time synchronization and background restoration.
Calendar's standalone reader test is in
[apps/calendar/test](../apps/calendar/test/); CI builds it with SDK text/date helpers.

Simulator scenarios run real WASM applications with local mock responses and compare
frames pixel by pixel. A `mocks.json` file names response files relative to itself;
shared inputs live in [fixtures](fixtures/README.md). Keep those paths valid when
moving a scenario or response. `tests/github/calendar.json` is the larger GraphQL
fixture used by its streaming parser and simulator cases.

Run `sim-test tests --update` only for an intended rendering change and inspect each
changed PNG. Then repeat without `--update`; never regenerate baselines merely to
silence a mismatch. English/Russian and Departure/Jersey variants cover the recent
applications. The normal simulator runner needs permission to listen on localhost.

CI runs `sim-test tests` with offline fixtures. Passing simulator checks do not establish panel timing, real
button behavior, network endurance or battery life. Those belong to the
[firmware's hardware qualification](https://github.com/GrakovNe/perinkpheral/blob/main/docs/testing/stability.md).
