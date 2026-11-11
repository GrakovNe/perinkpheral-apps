# perINKpheral applications

The official applications for [perINKpheral](https://github.com/GrakovNe/perinkpheral),
the desk e-paper companion: the ones a user may or may not want. A device
holds five applications at a time; this is where the rest of them wait.

| Application | What it is |
|---|---|
| `clock` | the time, the day of the week and the date, across the whole screen |
| `weather` | now and three days, from Open-Meteo: no key, no account |
| `timer` | a countdown that ends wherever you have gone, and takes the screen back |
| `badapple` | a 1-bit video: a test of the panel and of the interpreter |
| `hello` | the SDK sample, from the perinkpheral repository |

## Installing

A device knows this catalog out of the box. Open the device's page, go to
Applications, and install what you want; remove one and its settings stay
for when it comes back.

The catalog is the `catalog` folder of this repository, served as it is:

    https://raw.githubusercontent.com/GrakovNe/perinkpheral-apps/HEAD/catalog

## A catalog of your own

A catalog is static files: an index, `catalog.json`, and a folder for
every application with its `app.wasm`, its `manifest.json` and whatever
data it brings. It works the way a Debian repository does - the device
reads the index, and nothing but the index tells it what is there - and
any hosting that serves files over HTTPS will do: a git hosting's raw
files, its pages, a server of your own. Add the address of the folder on
the device's page, next to this one.

To make one, put your applications into a folder and run
`tools/catalog/mkcatalog.py <folder> --out <catalog>` from the
perinkpheral repository. `build.sh` here does exactly that for `apps/`.

## Writing an application

The contract, the SDK and the tools are in the
[perinkpheral](https://github.com/GrakovNe/perinkpheral) repository:
`sdk/host_api.h` is the whole API, `sdk/apps/hello` the smallest sample,
and `tools/devtool/devtool.py run <folder>` puts an application onto a
device over the network in ten seconds. The applications here are the
larger samples: `weather` for data from the network and settings,
`timer` for working while not loaded.

## Building

    WASI_SDK_PATH=<wasi-sdk> ./build.sh

rebuilds `catalog/` from `apps/`. The catalog is committed: a device
installs from the repository as it stands.

MIT.
