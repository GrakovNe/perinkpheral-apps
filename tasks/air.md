# Task: `air` - air quality

Read `tasks/README.md` first. The composition follows `apps/ha/ha.c`: a hero with a large
picture, two details beside it, and four columns under the rule. ABI 3, version 0.1.0.

## Data and settings

Open-Meteo and CAMS, no key or account. The poll requests current values only:

```
https://air-quality-api.open-meteo.com/v1/air-quality?latitude={{lat}}&longitude={{lon}}
  &current=european_aqi,us_aqi,pm2_5,pm10,uv_index,ozone,nitrogen_dioxide,sulphur_dioxide,carbon_monoxide,dust,alder_pollen,birch_pollen,grass_pollen,mugwort_pollen,ragweed_pollen,olive_pollen
  &timezone=auto
```

The sixteen `pick` paths are `current.<name>`, in that exact order. `app_on_picked()` receives
both indices, PM2.5, PM10, UV and eleven irritants. No hourly request or chart. Values are
fixed-point tenths except the two whole-number indices. Pollen that is null or absent is not
available, rather than zero. The four gases and dust must all be present for a new snapshot.
Keep the last good snapshot on a failed or incomplete answer; invalidate it when the place
changes. The state has a version first and the coordinates it belongs to.

Settings stay as before: the `city` place with `lat` and `lon`, `scale` (European or US),
`every` (15 min, 30 min or 1 h) and `header_city`. Changing scale uses the stored answer.
OK calls `poll_run(0)`; no `app_on_back` is needed. The chosen city goes in the right header
with `header_set_text()`. Use `ui_draw_reason()` for an unset place or a failed first answer.

## Screen

The index is large, with its scale's word under the hero. The picture is LEAF for European
Good/Fair and US Good, GAS for European Moderate and US Moderate/Sensitive, MASK for European
Poor or worse and US Unhealthy or worse. The original index bands remain:

- European: through 20 Good, 40 Fair, 60 Moderate, 80 Poor, 100 Very poor, then Extremely poor.
- US: through 50 Good, 100 Moderate, 150 Sensitive, 200 Unhealthy, 300 Very unhealthy,
  then Hazardous.

Two stacked details on the right show PM2.5 and PM10, each with PARTICLES and ug/m3.
Below the rule are four equally spaced tiles: UV always first, with SUN, then the three
worst available irritants. Gases use GAS, dust DUST, all pollen POLLEN. A tile shows its
name, picture, value and band word. Labels and words are translated into English and Russian;
NO2, SO2 and CO are chemical names in both. Each column fits its own text to its width.

## Irritant bands

`apps/air/air_quality.h` holds the table, source URLs and pure ranking. The values below
are the lower boundaries of Moderate, High and Very high; below the first is Low.

| Irritant | Moderate | High | Very high | Unit |
|---|---:|---:|---:|---|
| Ozone | 100 | 120 | 160 | ug/m3 |
| Nitrogen dioxide | 25 | 60 | 100 | ug/m3 |
| Sulphur dioxide | 40 | 125 | 190 | ug/m3 |
| Carbon monoxide | 11600 | 17400 | 23200 | ug/m3 |
| Dust | 45 | 120 | 195 | ug/m3 |
| Alder | 11 | 70 | 250 | grains/m3 |
| Birch | 11 | 70 | 300 | grains/m3 |
| Grass | 20 | 50 | 150 | grains/m3 |
| Mugwort | 6 | 15 | 50 | grains/m3 |
| Ragweed | 6 | 11 | 40 | grains/m3 |
| Olive | 50 | 200 | 400 | grains/m3 |

The gases except CO combine the revised 2024 EEA Good/Fair into Low, retain Moderate,
map Poor to High and combine Very poor/Extremely poor into Very high. Dust uses PM10 as
a proxy. CO uses the historical UK four bands, converted from mg/m3. The first five pollen
species use MeteoSwiss classes; olive uses REA's 50/200 boundaries with a local Very high
extension at 400. These are dashboard comparisons of current model readings, not the
averaged exposure indices of the CO and pollen sources.

Rank by band first. Within a band, compare `(value - band_start) / band_width`, using integer
cross multiplication. Very high has no next threshold: compare excess beyond its start in
units of the preceding band's width. Exact ties keep the service order. Null pollen is
excluded; the five non-pollen candidates keep the row full. UV uses 3/6/8, with Extreme
included in Very high, and never participates in the selection.

## Tests

`tests/air`, `air-us`, `air-ru`, `air-error`, `air-unset` carry complete current-response mocks.
The European examples exercise seasonal pollen and different hero bands; US has no pollen.
The two November samples in `tasks/samples/` include all sixteen fields with realistic values.
Run `../perinkpheral/tools/perinkpheral sim-test tests --update`, inspect the five Air pictures,
and then compare them without `--update`.

The pure ranking regression test also runs without the SDK or simulator:

```
cc -std=c11 -Wall -Wextra -Werror tests/air/ranking.c -o /tmp/air-ranking
/tmp/air-ranking
```
