# ISS

The station's current position, the solar terminator and up to 90 minutes of observed positions.
A picked poll runs once a minute. OK also asks for an update. The visible position follows each good
fix. The track samples at five-minute intervals, and the app checkpoints the readings and track at
that pace and when the last request status changes. The newest visible fix
survives temporary unloads in scene memory. Old points expire by age, and the line breaks at the
date line and over gaps longer than ten minutes.

The 400×200 equirectangular map leaves room under it for coordinates, altitude, speed and the age
of the displayed fix. The crosshair's dashed axes intersect at the position. Fine dots follow
the solar terminator, found from changes in the sign of the solar dot product. The calculation
uses Q14 integer sines in tenths of a degree.

`files/land.bin` is a raw, MSB-first 400×200 bitmap, 50 bytes per row, derived from
`tasks/samples/world-land-400x200.png`. Natural Earth land is public domain. An ink pixel
is retained only where both x and y are even, producing a fine stipple. The host draws
this 10 KB package file with `gfx_draw_bitmap_file`, keeping it out of WASM memory.
`sine.h` contains rounded `16384 * sin(angle)` values for 0–90 degrees at 0.1-degree steps.
