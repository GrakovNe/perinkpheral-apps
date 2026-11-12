# HA Companion

A screen for Home Assistant: what it shows is described in Home Assistant,
how it looks is drawn on the device.

## In Home Assistant

One template sensor with an attribute `screen` - a line for every place on
the screen, its parts divided by `|`:

```yaml
template:
  - sensor:
      - name: perinkpheral
        state: ok
        attributes:
          screen: |
            hero|{{ state_attr('weather.home','temperature') | round(0, default='--') }}°|{{ states('weather.home') }}
            detail|{{ state_attr('weather.home','humidity') }}|%|drop
            detail|{{ state_attr('weather.home','pressure') | round(0, default='--') }}|hPa|gauge
            column|Room|thermometer|{{ states('sensor.room_temperature') | round(1, default='--') }}°
            column|Fridge|thermometer|{{ states('sensor.fridge_temperature') }}°
            column|CO2|co2|{{ states('sensor.co2') }}
```

A `round` without `default` fails when its entity is unavailable, and one
failed value leaves the whole screen empty.

| Place | Parts | What it is |
|---|---|---|
| `hero` | value, picture | the one thing the screen is for: two characters are the largest |
| `detail` | value, unit, picture | up to two, beside the hero |
| `column` | name, picture, value | up to four, in a row under the rule |

Pictures: the states of a weather entity as they are (`sunny`, `cloudy`,
`rainy`, `snowy`, `lightning`, `partlycloudy`, `clear-night`, ...), and
`drop`, `co2`, `gauge`, `thermometer`, `battery`, `bulb`, `door`, `home`,
`moon`, `plug`, `bolt`, `chip`, `network`, `umbrella`, `wind`, `bell`,
`clock`, `check`, `warning`, `info`, `play`.

Text is ASCII; `°` is drawn. The whole of it is at most 1000 bytes.

## On the device

Settings of the application, on the device's page: the server's address,
a long-lived access token (Home Assistant, your profile, Security), the
sensor's name and how often to refresh. OK on the device refreshes now.
