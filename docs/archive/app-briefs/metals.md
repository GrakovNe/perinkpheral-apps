# Initial brief: `metals` - Bank of Russia metals

Archived design brief for an application that has shipped. This records the original
proposal, not current behavior or outstanding work. See the [archive index](README.md)
and [contribution guide](../../../CONTRIBUTING.md).

Reference implementations: `apps/stocks` (explicit HTTP requests with
streamed responses, numbers with decimals, a chart, a packaged certificate-authority
root in `"certificates"`, and one item per screen with UP / DOWN navigation).

## What it is

The Bank of Russia's accounting prices of precious metals - gold, silver, platinum, palladium -
in rubles a gram: one metal per screen, its latest price, its change since the previous published
day, and its month as a chart. Official public data of the Bank of Russia; no logo, no other
bank, the source named in words.

```
  1/4 Bank of Russia                       [host header]

  Gold
  10 926.51 RUB/g
  +1.4 %

  ▁▂▂▃▄▅▅▆▅▆▇▇█▇▆▆▇█▇▆▅▆▇▇██▇▇█         11 266.86
                                          10 774.32
                     17 Nov 2026
```

## The data

`GET https://www.cbr.ru/scripts/xml_metall.asp?date_req1=DD/MM/YYYY&date_req2=DD/MM/YYYY` -
XML, no key. `www.cbr.ru` is certified by the Russian Trusted Root CA only: the device does not
trust it unless the package brings that authority. Real answers are in
`tests/fixtures/metals/` (`month.xml` - a month, `days.xml` - three days); use these recorded
responses for offline tests.

```xml
<?xml version="1.0" encoding="windows-1251"?>
<Metall FromDate="20261110" ToDate="20261117" name="Precious metals quotations">
  <Record Date="10.11.2026" Code="1"><Buy>10774,32</Buy><Sell>10774,32</Sell></Record>
  <Record Date="10.11.2026" Code="2"><Buy>138,4</Buy><Sell>138,4</Sell></Record>
  ...
```
- `Code` 1 gold, 2 silver, 3 platinum, 4 palladium. A day the bank does not work has no records.
- Prices have a decimal comma. Buy and Sell are the same; use `Buy`.
- The answer for 31 days is about 7 KB: it comes in pieces of up to 1 KB to `app_on_data()`
  (`flags` say first and last). Parse it as it comes with a small scanner of your own - it is
  only `<Record Date="..." Code="n"><Buy>number</Buy>`: keep a little state across pieces (a
  record may be cut anywhere), no general XML parser. Only `LAST` makes the answer whole; a
  `FAILED` drops what came.
- The application builds the URL itself (the dates are today's and 31 days before, from
  `time_local`) and asks with `http_send(tag, HOST_HTTP_GET, url, ...)`; the answer comes to
  `app_on_data` under that tag. Ask when the screen opens and on an alarm every 6 hours
  (`alarm_set_every`; set it again in `app_on_updated` and in the `started` intent). No poll in
  the manifest: the dates change.

## Manifest

- `name` `metals`, `title` "Bank of Russia metals" / "Металлы Банка России", `description`
  "Bank of Russia prices of gold, silver, platinum and palladium." / "Цены Банка России на
  золото, серебро, платину и палладий.".
- `hosts`: `["www.cbr.ru"]`.
- `certificates`: `["russian-root-ca.pem"]` - copy `apps/stocks/files/russian-root-ca.pem` into
  `apps/metals/files/`. Read in `CLAUDE.md` (perinkpheral) what the device does with it: it is
  trusted for this application's requests only, and the page names it before installing.
- `intents`: `["started"]`.
- `settings`:
  - `metal`, `choice`, "Metal" / "Металл", options `["Gold", "Silver", "Platinum",
    "Palladium"]`, default `"Gold"`, with Russian labels.
- An icon: an ingot (a trapezoid bar), 11x11, in the manner of the others.

## State

In the store, one struct with a version first: the last good month - up to 31 days, for each
day the date (days since 1970, `app_days.h`) and the four prices as fixed-point integers with
2 decimals (`app_fixed_parse`, after turning the comma into a point). 31 x (4 + 4 x 4) bytes is
about 620 bytes: fine in the 2 KB. Written only when a whole answer differs from what is kept.
The metal on the screen: the setting supplies the first metal, and UP / DOWN change it for the
moment (kept in the session, not written to the store). A changed setting selects that metal.

## Behaviour

- Match `apps/stocks`: the metal's name at the standard margin in `HOST_FONT_TEXT`, then
  its latest price large with `ui_draw_value` in `HOST_FONT_MEDIUM` and "RUB/g" / "руб/г" beside it.
  Below the price, small: its change against the previous published day, in percent with one
  decimal and a sign.
- Below that, the metal's month as an area chart with `ui_draw_chart`, a mark on the last value,
  and the greatest and least as captions on the right. Draw the captions with the same
  localized formatting as the price. As in stocks, omit the chart with fewer than two points.
- Centre the date of the latest price below the chart, small (`time_text`, `HOST_TIME_LONG_DATE`).
- In the header's left text: the position ("1/4", as stocks counts tickers), then
  "Bank of Russia" / "Банк России" - the source, in words.
- UP and DOWN select the previous and next metal, wrapping at both ends as stocks does;
  the name, price, change and chart all follow the selection. OK asks now; BACK leaves.
- Nothing stored and the request failed: `ui_draw_reason(status)`; a later failure keeps what is
  shown.
- Thousands are separated by a thin space or a space ("10 926.51"); decimals by a point in
  English and a comma in Russian, as the device's language is (`sys_setting_get` is not open to
  this application: take the language from which strings table was loaded - put a string
  `decimal_point` "." / "," in strings and use it).

## Test

`tests/metals` and `tests/metals-ru`: the mocks give `month.xml` for any `https://www.cbr.ru/*`
(a mock file as `tests/weather/mocks.json`, the body the XML as a JSON string); `wait 3`,
`shot metals` (gold); `press 3`, `shot silver` (the whole screen moved to silver). Both set
`metal` to `Gold`. A third sets the same first metal with the request failing and no saved
month: `wait 3`, `shot failed`, `press 3`, `shot failed-next`.

## Original shared conventions

No floating point, no C library (use `sdk/lib`), draw only in `app_on_draw`, state in the store,
the session for where the user is, strings in `strings/en.json` and `strings/ru.json` with the
manifest's texts under `manifest` in `ru.json`, English and plain hyphens in code and comments,
SPDX headers, interface text short. Check: `../perinkpheral/tools/perinkpheral build apps/metals
--out /tmp/metals`, `../perinkpheral/tools/perinkpheral sim-test tests`, `./build.sh`.
