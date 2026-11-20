// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
#define HOST_IMPORT(name, ...) __VA_ARGS__
#include "../../apps/iss/iss.c"

static int32_t strokes, ink;
int64_t time_now(void) { return 600; }
int32_t gfx_set_color(int32_t color) { ink = color; return HOST_OK; }
int32_t gfx_draw_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    assert(x0 == 100 + strokes * TRACK_DASH_PERIOD);
    assert(x1 == x0 + TRACK_DASH_LENGTH - 1 && y0 == 100 && y1 == 100);
    strokes++;
    return HOST_OK;
}

int main(void)
{
    state.track = (orbit_track_t){0};
    orbit_track_add(&state.track, 0, 0, -9000);
    orbit_track_add(&state.track, 5, 0, -7200);
    orbit_track_add(&state.track, 10, 0, -5400);
    draw_track();
    assert(strokes == 5 && ink == HOST_INK);
    state.track = (orbit_track_t){0}; strokes = 0;
    orbit_track_add(&state.track, 5, 0, 17900);
    orbit_track_add(&state.track, 10, 0, -17900);
    draw_track();
    assert(strokes == 0);
    state.track = (orbit_track_t){0};
    orbit_track_add(&state.track, -5, 0, -9000);
    orbit_track_add(&state.track, 10, 0, -5400);
    draw_track();
    assert(strokes == 0);
    puts("ISS track: dash spacing survives sample boundaries; date line and missing fixes passed");
}
