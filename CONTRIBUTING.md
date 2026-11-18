# Developing applications

The [firmware repository](https://github.com/GrakovNe/perinkpheral) owns the SDK and
shared tools. Read its [host contract](https://github.com/GrakovNe/perinkpheral/blob/main/sdk/host_api.h)
and [application guide](https://github.com/GrakovNe/perinkpheral/releases/latest/download/perinkpheral-app-guide.pdf)
before changing an application. ABI 3 requires firmware 0.1.30 or later.

## Layout

- `apps/<name>/`: C sources, manifest, translations, pictures and package files.
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
needed. Check the host contract for exact limits and lifecycle semantics.

## Build and verify

Install Java 21 or later, wasi-sdk (verified with 34), a C compiler and Make. With a
sibling `../perinkpheral` checkout:

```sh
export WASI_SDK_PATH=/path/to/wasi-sdk
../perinkpheral/tools/perinkpheral build apps/<name> --out /tmp/<name>
../perinkpheral/tools/perinkpheral sim apps/<name>
tests/check-logic.sh
tests/life/check-session.sh
tests/stopwatch/check-resume.sh
../perinkpheral/tools/perinkpheral sim-test tests
./build.sh
```

`PERINKPHERAL` overrides the SDK checkout for `build.sh` and the shell tests. For CLI
commands, use the matching checkout's `tools/perinkpheral`. The tests require no live
service: declare mocks for network answers. See [the test reference](tests/README.md).

A new or changed screen needs a simulator scenario. Use `sim-test tests --update` only
when the intended rendering changed, inspect every changed PNG, then run again without
`--update`. Check English and Russian in both font families. Validate the relevant
pure logic tests and the catalog build before publishing.

## Delivery

Bump an application's manifest version when its delivered behavior or appearance
changes. `build.sh` recreates the catalog; commit affected packages and `catalog.json`
with the source change. A documentation or fixture move should leave catalog bytes
unchanged. `glyphs/` and `catalog/glyphs/` are intentional input/output copies.

CI rebuilds and compares the committed catalog, runs host and simulator checks, then
releases a catalog archive on each successful push to `main`. The live device catalog
is served directly from `catalog/` on `main`; branch builds do not publish it.
