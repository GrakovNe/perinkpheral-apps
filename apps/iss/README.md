# ISS

The station's current position, day and night, and up to 90 minutes of observed positions.
A picked poll runs once a minute. OK also asks for an update. The visible position follows each good
fix. The track samples at five-minute intervals, and the app checkpoints the readings and track at
that pace and when the last request status changes. The newest visible fix
survives temporary unloads in scene memory. Old points expire by age, and the line breaks at the
date line and over gaps longer than ten minutes.

The 400×200 equirectangular map leaves room under it for altitude on the left and speed on
the right, with a 64-pixel gap between the two readings. Altitude is rounded to whole metres
from the API's kilometres; speed is in km/h. Saved version 1 readings migrate from kilometres
to metres and keep their track. Coordinates are rounded to tenths of a degree, with at least two integer digits
(for example, `04.2°`), in two small inverted badges over
the map. They stay 12 pixels below its top and 8 pixels from the outer edge: on the left
for western longitudes, on the right for eastern longitudes and the prime meridian.
Their size is fixed for the selected font and language, including the hemisphere labels
(`N`, `S`, `E`, `W` in English; `С.Ш`, `Ю.Ш`, `В.Д`, `З.Д` in Russian).
The crosshair's dashed axes intersect at the position. The whole screen, including the system
header and telemetry, is dark when the station is over the Earth's night side and light when
it is over the day side. This follows the ground position, not whether the station itself is
in Earth's shadow. The solar dot product uses Q14 integer sines once per draw.
The observed track uses thin four-pixel dashes with four-pixel gaps, continued across samples.

`files/land.bin` is a raw, MSB-first 400×200 bitmap, 50 bytes per row, derived from
[the source bitmap](assets/world-land-400x200.png). Natural Earth land is public domain.
An ink pixel is retained only where both x and y are even, producing a fine stipple. The app reads
this 10 KB package file into a bitmap and draws the light palette. Perinkpheral OS inverts
the composed screen for the night theme through `screen_set_inverted` (firmware 0.1.54 or later).
`sine.h` contains rounded `16384 * sin(angle)` values for 0–90 degrees at 0.1-degree steps.
