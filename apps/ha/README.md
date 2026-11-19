# HA Companion

A Home Assistant dashboard for perINKpheral. The
[Home Assistant integration](https://github.com/perinkpheral/perinkpheral-hacs)
chooses the layout and entities; this application draws their values on the display.

## Set up

1. Install **HA Companion** from the application catalog on the device's page.
2. Install the perINKpheral integration through HACS and restart Home Assistant.
3. In Home Assistant, open **Settings → Devices & services → Add integration →
   perINKpheral**. Enter the display address and a Home Assistant URL reachable
   from the display.
4. Choose the layout and entities in the integration options.
5. Open HA Companion on the display.

The integration writes the server address and its own pairing secret to the
application. These fields are hidden on the device's page; no long-lived Home
Assistant access token or template sensor is required. The application's visible
setting is the refresh interval. Opening it or pressing OK refreshes immediately.

## Screen format

The integration serves the rendered screen at `/api/perinkpheral/screen`.
The application sends the pairing secret in `Authorization: Bearer <secret>`.
This is the integration's endpoint, not Home Assistant's template-sensor API.

Each line describes a place on the screen, with parts separated by `|`:

```text
hero|21°|cloudy
detail|54|%|drop
detail|1013|hPa|gauge
column|Room|thermometer|22.5°
column|Fridge|thermometer|4°
column|CO2|co2|650
```

| Place | Parts | What it is |
|---|---|---|
| `hero` | value, picture | the main value; two characters use the largest size |
| `detail` | value, unit, picture | up to two, beside the hero |
| `column` | name, picture, value | up to four, below the rule |

## Layouts

The default layout uses the places above. Other layouts begin with
`layout|<name>` and use `item|name|picture|value` lines:

| Layout | What it is |
|---|---|
| `tiles` | up to four items; one fills the screen, two use halves, three or four use quarters |
| `single` | the first item, as large as it fits |
| `list` | up to twelve rows; UP and DOWN scroll |

```text
layout|tiles
item|Living room|thermometer|22.5°
item|CO2|co2|650
item|Input|plug|5V
item|Network|network|100
```

A picture may be empty (`item|Door||Closed`). Pictures include weather states
(`sunny`, `cloudy`, `rainy`, `snowy`, `lightning`, `partlycloudy`, `clear-night`),
and `drop`, `co2`, `gauge`, `thermometer`, `battery`, `bulb`, `door`, `home`,
`moon`, `plug`, `bolt`, `chip`, `network`, `umbrella`, `wind`, `bell`,
`clock`, `check`, `warning`, `info`, `play`.

Text is UTF-8. Character coverage depends on the selected font and installed glyph
packs. The complete response is limited to 512 bytes.
