# ISS

The station's current position, solar terminator and last 90 minutes of observed positions.
A picked poll runs once a minute; OK also asks for an update. One versioned store value holds
the current readings and a 90-point ring. Samples in the same minute replace that minute's
point; older answers cannot move history backward. Old points expire by age, including
after gaps. Track segments break at the date line and across gaps over two minutes.

The 400x200 equirectangular map fits under the host header, leaving 60 pixels for one line
of telemetry. Coordinates use hundredths of degrees, altitude and speed are rounded to
whole units, and direction, units and visibility are localized. A ring and central dot with
four ticks mark the station. Dotted 30-degree graticules stand over separated land dots;
sparser dots indicate night. The terminator is the sign of the solar dot product, computed
with Q14 integer sines in tenths of a degree. No floating point runs on the device.

`files/land.bin` is a raw, MSB-first 400x200 bitmap, 50 bytes per row, derived from
`tasks/samples/world-land-400x200.png`. Natural Earth land is public domain. An ink pixel
is retained only where both x and y are even, producing a fine stipple. The host draws
this 10 KB package file with `gfx_draw_bitmap_file`; it does not occupy WASM memory.
`sine.h` contains rounded `16384 * sin(angle)` values for 0-90 degrees at 0.1-degree steps.
