# Developing applications

The [firmware repository](https://github.com/perinkpheral/perinkpheral) owns the SDK and
shared tools. Read its [Perinkpheral OS application contract](https://github.com/perinkpheral/perinkpheral/blob/main/sdk/host_api.h)
and [application guide](https://github.com/perinkpheral/perinkpheral/releases/latest/download/perinkpheral-app-guide.pdf)
before changing an application. ABI 3 requires firmware 0.1.30 or later.

## Layout

- `apps/<name>/`: C sources, manifest, translations, pictures and package files.
- `apps/<name>/test/`: what its checks need to know of it (`check.json`) and tests of its
  own logic (`*.c`).
- `tests/<scenario>/`: simulator steps, network mocks and reviewed reference PNGs.
- [tests/fixtures](tests/fixtures/README.md): shared offline response samples.
- `glyphs/`: generated font packs and licenses used as catalog inputs.
- `catalog/`: generated, committed packages consumed directly by devices.
- [Archived design briefs](docs/archive/app-briefs/README.md): original proposals for
  applications already shipped; not a queue of unfinished work.

Application-specific notes belong beside the application. Runtime package data belongs
in its `files/`; design source assets that must not ship may live in `assets/`.
The SDK processes `pictures/` separately into code. Do not move reference images or
frozen inputs into a build directory just because they were generated once.

## Code and state

Use C11, four spaces, meaningful snake_case names and an SPDX MIT header. The modules
have no C library or floating point: use `sdk/lib`, integer fixed-point values and
`app_alloc()`. Keep text in `strings/en.json` and `strings/ru.json`; translate manifest
text too. Follow an existing application with the same interaction model.

Draw only in `app_on_draw()`. Handlers update state and request `screen_redraw()`.
An instance is unloaded when it leaves the screen or makes room for background work.
Keep persistent state in the 2 KB store, saving only when it changes; preserve the
current scene through `app_session_save()` and `app_session_load()`. Timers run only on
screen. Alarms can run in the background but must be restored after an app update when
needed. Check the Perinkpheral OS application contract for exact limits and lifecycle semantics.

## Build and verify

Install Java 21 or later, wasi-sdk (verified with 34), a C compiler and Make. With a
sibling `../perinkpheral-os` checkout:

```sh
export WASI_SDK_PATH=/path/to/wasi-sdk
../perinkpheral-os/tools/perinkpheral build apps/<name> --out /tmp/<name>
../perinkpheral-os/tools/perinkpheral sim apps/<name>
../perinkpheral-os/tools/perinkpheral app-check apps
../perinkpheral-os/tools/perinkpheral sim-test tests
./build.sh
```

`PERINKPHERAL` overrides the SDK checkout for `build.sh`. For CLI commands, use the
matching checkout's `tools/perinkpheral`. The tests require no live service: declare mocks
for network answers. See [the test reference](tests/README.md).

**A new application is checked by doing nothing.** `app-check` runs the SDK's capability
checks - sessions, store, timers, network, drawing - on every application of `apps/`,
choosing by what the application imports; there is no list to add it to and no test to
write for what the contract promises. Two things are the application's own, both in
`apps/<name>/test/`:

- `check.json`, when it asks the network for anything or needs settings to show anything:
  the time, the store and the answers it is checked with - a simulator scenario's `at`,
  `tz`, `store` and `mocks`, and usually the scenario's own mocks file by name.
- `<anything>.c`, when it has logic of its own - a calculation, a parser, an ordering: a
  program that includes the application's source and asserts on it. No script runs it;
  `app-check` finds it.

A new or changed screen needs a simulator scenario. Use `sim-test tests --update` only
when the intended rendering changed, inspect every changed PNG, then run again without
`--update`. Check English and Russian in both font families. Run `app-check apps` and the catalog build before
publishing.

## Delivery

Bump an application's manifest version when its delivered behavior or appearance
changes. `build.sh` recreates the catalog; commit affected packages and `catalog.json`
with the source change. A documentation or fixture move should leave catalog bytes
unchanged. `glyphs/` and `catalog/glyphs/` are intentional input/output copies.

CI rebuilds and compares the committed catalog, runs `app-check apps` and `sim-test tests`, then
releases a catalog archive on each successful push to `main`. The live device catalog
is served directly from `catalog/` on `main`; branch builds do not publish it.
