// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
#include "../iss.c"
#include "../orbit.c"
#include "standin.h"

/* The stand-in host keeps a frame as a hash of its calls and their numbers,
 * so the strokes are checked by drawing the expected ones and comparing. */
static int32_t expected_strokes;

static void track_frame(void) { draw_track(); }

static void expected_frame(void)
{
    gfx_set_color(HOST_INK);
    for (int32_t stroke = 0; stroke < expected_strokes; stroke++) {
        int32_t x0 = 100 + stroke * TRACK_DASH_PERIOD;
        gfx_draw_line(x0, 100, x0 + TRACK_DASH_LENGTH - 1, 100);
    }
}

/* What draw_track() drew is the ink and exactly `strokes` dashes on the equator. */
static bool drew(int32_t strokes)
{
    standin_handlers.on_draw = track_frame;
    uint64_t drawn = standin_draw();
    int32_t calls = standin()->draw_calls;
    expected_strokes = strokes;
    standin_handlers.on_draw = expected_frame;
    return calls == strokes + 1 && drawn == standin_draw() && standin()->refused_draws == 0;
}

int main(void)
{
    standin_reset();
    standin_clock(600);
    state.track = (orbit_track_t){0};
    orbit_track_add(&state.track, 0, 0, -9000);
    orbit_track_add(&state.track, 5, 0, -7200);
    orbit_track_add(&state.track, 10, 0, -5400);
    assert(drew(5));
    state.track = (orbit_track_t){0};
    orbit_track_add(&state.track, 5, 0, 17900);
    orbit_track_add(&state.track, 10, 0, -17900);
    assert(drew(0));
    state.track = (orbit_track_t){0};
    orbit_track_add(&state.track, -5, 0, -9000);
    orbit_track_add(&state.track, 10, 0, -5400);
    assert(drew(0));
    puts("ISS track: dash spacing survives sample boundaries; date line and missing fixes passed");
}
