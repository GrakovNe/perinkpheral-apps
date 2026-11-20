<img src="https://raw.githubusercontent.com/Perinkpheral/perinkpheral/main/assets/mark/mark-on-paper-512.png" width="96" alt="The perINKpheral mark">

# perINKpheral applications

Official applications and the static catalog for [perINKpheral](https://perinkpheral.dev).

[Installation](https://perinkpheral.dev/setup#apps) -
[Developer documentation](https://perinkpheral.dev/sdk) -
[Catalog files](https://github.com/perinkpheral/perinkpheral-apps/tree/main/catalog).
Application guide (PDF):
[English](https://github.com/perinkpheral/perinkpheral/releases/latest/download/perinkpheral-app-guide.pdf),
[Russian](https://github.com/perinkpheral/perinkpheral/releases/latest/download/perinkpheral-app-guide.ru.pdf).

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

- `apps/` - 17 applications: `air`, `badapple`, `calendar`, `clock`, `ha`,
  `iss`, `kit`, `life`, `metals`, `pomodoro`, `rates`, `status`, `stocks`, `stopwatch`, `sun`,
  `timer`, `weather`.
- `catalog/` - committed application packages and `catalog.json`.
- [tests/](tests/README.md) - host checks, simulator scenarios, network fixtures and reference images.
- `glyphs/` - glyph packs copied into the catalog by `build.sh`.

## Application authors

The catalog is served at
`https://raw.githubusercontent.com/Perinkpheral/perinkpheral-apps/HEAD/catalog`.
`build.sh` rebuilds it from `apps/`. Every application requires a test in `tests/`.
The application guide linked above covers the SDK and package format.
[CONTRIBUTING.md](CONTRIBUTING.md) describes source layout, validation and catalog delivery.
Original briefs for shipped applications are retained in [the design archive](docs/archive/app-briefs/README.md).

## Related repositories

- [Firmware, SDK and tools](https://github.com/perinkpheral/perinkpheral).
- [CLion plugin](https://github.com/perinkpheral/perinkpheral-clion-plugin).
- [Home Assistant integration](https://github.com/perinkpheral/perinkpheral-hacs), used with [HA Companion](apps/ha/README.md).
- [Portal source](https://github.com/perinkpheral/perinkpheral-dev-portal), including the installation guide.

## License

[MIT](LICENSE).
