<img src="https://raw.githubusercontent.com/GrakovNe/perinkpheral/main/assets/mark/mark-on-paper-512.png" width="96" alt="The perINKpheral mark">

# perINKpheral applications

The official applications for [perINKpheral](https://github.com/GrakovNe/perinkpheral),
the desk e-paper companion: the ones a user may or may not want. A device
holds as many as it has room for, up to 24; this is where the rest of them wait.

| Application | What it is |
|---|---|
| `clock` | the time, the day of the week and the date, across the whole screen |
| `weather` | now and three days, from Open-Meteo: no key, no account |
| `timer` | a countdown that ends wherever you have gone, and takes the screen back; the minutes left stay in the header |
| `badapple` | a 1-bit video: a test of the panel and of the interpreter |
| `stopwatch` | minutes and seconds for the first hour, hours and minutes after; it goes on counting wherever you have gone |
| `kit` | every part of the host's look an application may ask for, each on a page of its own |
| `stocks` | prices and the day's chart of tickers from New York, Tokyo and elsewhere (Yahoo Finance), the Moscow Exchange (`MOEX:SBER`) and the SPB Exchange (`SPB:SPBE`), in one list; the last trusts its own certificate authority, the Russian Trusted Root CA, which the page names before it installs |
| `sun` | sunrise, sunset and the length of the day where you are, and the moon's phase; worked out on the device, nothing is asked of the network |
| `calendar` | what is coming in the next two weeks from any calendar that gives an address of itself (Google, iCloud, Outlook, Nextcloud), and a reminder before it begins |
| `ha` | a screen described in Home Assistant and drawn on the device: a large value, details and columns, tiles or a list |

## Installing

A device knows this catalog out of the box. Open the device's page, go to
Applications, and install what you want; remove one and its settings stay
for when it comes back. Every application says the oldest firmware it runs
on (`firmware` in its manifest and in the index); a device older than that
is not offered it - update the firmware first.

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
`tools/perinkpheral catalog <folder> --out <catalog>` from the
perinkpheral repository. `build.sh` here does exactly that for `apps/`.

## Writing an application

The contract, the SDK and the tools are in the
[perinkpheral](https://github.com/GrakovNe/perinkpheral) repository:
`sdk/host_api.h` is the whole API, `sdk/apps/hello` the smallest sample,
and `tools/perinkpheral run <folder>` puts an application onto a
device over the network in ten seconds. The applications here are the
larger samples: `weather` for data from the network and settings,
`timer` for working while not loaded.

## Building

    WASI_SDK_PATH=<wasi-sdk> ./build.sh

rebuilds `catalog/` from `apps/`. The catalog is committed: a device
installs from the repository as it stands. The build stamps each
application with the firmware its host calls need, not the SDK's own
version, so a rebuild asks no more of a device than the code does.

MIT.
