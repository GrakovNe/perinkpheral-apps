<img src="https://raw.githubusercontent.com/GrakovNe/perinkpheral/main/assets/mark/mark-on-paper-512.png" width="96" alt="The perINKpheral mark">

# perINKpheral applications

Official applications and the static catalog for [perINKpheral](https://perinkpheral.dev).

[Installation](https://perinkpheral.dev/start#apps) -
[Developer documentation](https://perinkpheral.dev/sdk) -
[Catalog files](https://github.com/GrakovNe/perinkpheral-apps/tree/main/catalog).
Application guide (PDF):
[English](https://github.com/GrakovNe/perinkpheral/releases/latest/download/perinkpheral-app-guide.pdf),
[Russian](https://github.com/GrakovNe/perinkpheral/releases/latest/download/perinkpheral-app-guide.ru.pdf).

## Build

Requires Java 21 or later, wasi-sdk and a sibling `../perinkpheral` checkout.
`PERINKPHERAL` selects another SDK checkout.

```sh
export WASI_SDK_PATH=/path/to/wasi-sdk
./build.sh
../perinkpheral/tools/perinkpheral sim-test tests
```

Simulator tests also require a C compiler and Make.
CI builds and tests every push. Each successful push to `main` releases a catalog archive.

## Repository

- `apps/` - 15 applications: `air`, `badapple`, `calendar`, `clock`, `ha`,
  `kit`, `life`, `metals`, `pomodoro`, `rates`, `stocks`, `stopwatch`, `sun`,
  `timer`, `weather`.
- `catalog/` - committed application packages and `catalog.json`.
- `tests/` - simulator scenarios, network mocks and reference images.
- `glyphs/` - glyph packs copied into the catalog by `build.sh`.

## Application authors

The catalog is served at
`https://raw.githubusercontent.com/GrakovNe/perinkpheral-apps/HEAD/catalog`.
`build.sh` rebuilds it from `apps/`. Every application requires a test in `tests/`.
The application guide linked above covers the SDK and package format.

## License

[MIT](LICENSE).
