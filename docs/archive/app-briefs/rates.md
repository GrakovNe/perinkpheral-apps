# Initial brief: `rates` - exchange rates of the Bank of Russia

Archived design brief for an application that has shipped. This records the original
proposal, not current behavior or outstanding work. See the [archive index](README.md)
and [contribution guide](../../../CONTRIBUTING.md).

The shared conventions are in [CONTRIBUTING.md](../../../CONTRIBUTING.md). Model to copy: `apps/stocks` (a list setting, a list on the screen,
numbers with decimals, a request read in pieces).

## What it is

The official exchange rates of the Central Bank of Russia for the currencies the user chose: the
rate in rubles and how it changed since the previous day. One screen, a row a currency.

```
 USD   Доллар США             92.5432   +0.32
 EUR   Евро                  100.1204   -0.15
 CNY   Китайский юань         12.7011   +0.02
 JPY   Японская иена (100)    61.0350    0.00
                                       на 17.11
```

## The data

`GET https://www.cbr-xml-daily.ru/daily_json.js` - JSON, no key, no account, updated once a day
on working days. Look at it first (`curl -s https://www.cbr-xml-daily.ru/daily_json.js | head -c 1500`):

```json
{ "Date": "2026-11-17T11:30:00+03:00", "PreviousDate": "...",
  "Valute": { "USD": { "CharCode": "USD", "Nominal": 1, "Name": "Доллар США",
                       "Value": 92.5432, "Previous": 92.2211 },
              "JPY": { "CharCode": "JPY", "Nominal": 100, "Name": "Японских иен",
                       "Value": 61.035, "Previous": 61.035 }, ... } }
```
`Value` is the price of `Nominal` units in rubles: show the nominal when it is not 1.

The answer is about 15 KB: do **not** ask the host to pick it (`pick` paths are fixed in the
manifest, and the currencies are the user's). Make it a poll without `pick`; the answer then comes
in pieces to `app_on_data()`, and the application feeds them to a picker of its own
(`sdk/lib/json_pick.h`) with paths it builds from the settings: `Valute.USD.Value`,
`Valute.USD.Previous`, `Valute.USD.Nominal`, `Date`. `apps/stocks` does the same for one of its
sources: read it.

## Manifest

- `name` `rates`, `title` "Exchange rates" / "Курсы валют", `description` "Exchange rates of the
  Bank of Russia." / "Курсы валют ЦБ РФ.".
- `polls`: one, `https://www.cbr-xml-daily.ru/daily_json.js`, `every` `"{{every}}"`.
- `hosts`: `["www.cbr-xml-daily.ru"]`.
- `settings`:
  - `codes`, `list`, "Currencies" / "Валюты", `default` `["USD", "EUR", "CNY"]`. Items are
    three-letter codes as the bank writes them.
  - `every`, `choice`, "Refresh" / "Обновление", options `["1 h", "6 h", "1 d"]`, default `"6 h"`
    (the bank publishes once a day: more often is waste).
- An icon: something that reads as money or exchange, 11x11, drawn in the manner of the others.

## Behaviour

- Keep the last good answer in the store: for each currency (at most 8; say so in the manifest's
  help of `codes`: "Up to 8.") its code, nominal, value and previous value as fixed-point
  integers with 4 decimals (`app_fixed_parse(text, 4, &value)`), and the date. One struct,
  `app_state_save`, written only when an answer came whole and differs. 8 currencies x a few
  numbers is far under the 2 KB.
- A code the bank does not have: its row says "No data" / "Нет данных" instead of a number.
- An answer that failed: keep showing what is stored; when nothing is stored, the screen is
  `ui_draw_reason(status)` (Loading, No network, No answer - the host's words).
- The change is `value - previous`, signed, with 2 decimals; "0.00" when equal.
- The names: the bank's `Name` comes in Russian. Do not show it in an English device; show the
  code only. In Russian show the code and the bank's name, cut to fit (`gfx_fit_text`).
- The date of the rates under the list, small, "17 Nov" style (`time_text` with
  `HOST_TIME_LONG_DATE` after `time_from_local` of the date's day, or build it from the date's
  digits).

## Screen

- Not full screen: the host's header stays. Rows with `ui_draw_list_row(list, i, label, value, icon)`
  - label the code (and the name in Russian), value the rate and the change; or draw them by hand
  in the text font like `apps/stocks` - columns aligned right, digits tabular
  (`HOST_FONT_TEXT | HOST_FONT_TABULAR`).
- More currencies than fit: UP and DOWN scroll (`app_list_button`, a list without a selection),
  a long press a page.
- OK: ask now (`poll_run(0)`), the picture redrawn when the answer comes (`HOST_REDRAW_SOON`).
- Redraw from data with `HOST_REDRAW_NORMAL` - once a day changes nothing worth hurrying for.

## Test

`tests/rates`: mocks with a short `daily_json.js` of four currencies (one with `Nominal` 100),
store empty, `wait 3`, `shot rates`; and `tests/rates-ru` the same in Russian. A third with the
network failing (`"status": 500` in the mock) and nothing stored: the reason on the screen.
