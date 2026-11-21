// SPDX-License-Identifier: MIT
/*
 * The donut's renderer and handlers on this computer, with the sanitizers.
 * The pictures are held against a torus traced ray by ray in floating
 * point, which shares nothing with the application but its constants.
 */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The stand-in host keeps a frame as a hash and its clock stands while one is drawn.
 * The application's three drawing calls go through these, which check what the frame
 * is made of, let a draw take time, and hand it on to the host. */
#define gfx_draw_bitmap checked_draw_bitmap
#define gfx_draw_text   checked_draw_text
#define gfx_draw_rect   checked_draw_rect
#include "../donut.c"
#undef gfx_draw_bitmap
#undef gfx_draw_text
#undef gfx_draw_rect
#include "standin.h"

int32_t gfx_draw_bitmap(int32_t x, int32_t y, int32_t width, int32_t height, const uint8_t *bits);
int32_t gfx_draw_text(int32_t x, int32_t y, int32_t font, const char *text, int32_t length);
int32_t gfx_draw_rect(int32_t x, int32_t y, int32_t width, int32_t height, int32_t fill);

/* ---- Perinkpheral OS: the stand-in host, and what it does not tell ---- */

#define LINES 5

/* What the clock moves on while a frame is put on the screen. */
static int32_t draw_takes_ms;
static int32_t bitmap_calls, rect_calls;
static char lines[LINES][HOST_TEXT_MAX + 1];

int32_t checked_draw_bitmap(int32_t x, int32_t y, int32_t width, int32_t height, const uint8_t *bits)
{
    assert(state == SPINNING && frame_is_rendered);
    assert(x == 0 && y == 0 && width == WIDTH && height == HEIGHT && bits == bitmap);
    ++bitmap_calls;
    standin_pass(draw_takes_ms);
    return gfx_draw_bitmap(x, y, width, height, bits);
}

int32_t checked_draw_text(int32_t x, int32_t y, int32_t font, const char *text, int32_t length)
{
    int32_t line = (y - MARGIN) / LINE_HEIGHT;
    assert(state == STOPPED && x == MARGIN && font == HOST_FONT_TEXT);
    assert(line >= 0 && line < LINES && length <= HOST_TEXT_MAX);
    memcpy(lines[line], text, (size_t)length);
    lines[line][length] = '\0';
    return gfx_draw_text(x, y, font, text, length);
}

int32_t checked_draw_rect(int32_t x, int32_t y, int32_t width, int32_t height, int32_t fill)
{
    assert(state == STOPPED && x >= 0 && y >= 0 && x + width <= WIDTH && y + height <= HEIGHT);
    ++rect_calls;
    return gfx_draw_rect(x, y, width, height, fill);
}

/* app_on_draw() in a frame of the host, which refuses nothing of it: every colour is one. */
static void draw(void)
{
    standin_handlers.on_draw = app_on_draw;
    standin_draw();
    assert(standin()->refused_draws == 0 && standin()->stray_draws == 0);
}

/* What the frame's timer waits; 0 when it does not. No other timer is the application's. */
static int32_t timer_ms(void)
{
    assert((standin_timers() & ~(1u << FRAME_TIMER)) == 0);
    return standin_timer_left(FRAME_TIMER);
}

static void uptime_is(int64_t milliseconds)
{
    standin_pass(milliseconds - standin()->uptime_ms);
}

/* ---- The renderer ---- */

#define FRAMES       TURN /* the turns repeat after as many */
#define EDGE_MARGIN  8    /* pixels the torus keeps from the screen's edges */
#define BLOCK        8    /* pixels a side: the dither's matrix, so a level is so many paper pixels of it */
#define PI           3.14159265358979323846
/* How far a block's paper may be from the traced light, in levels: the
 * mesh is flat between its points and the light even inside a triangle. */
#define LEVELS_APART 8

static const double TORUS_REACH = (RING_RADIUS + TUBE_RADIUS) / (double)TUBE_RADIUS;

static bool is_paper(int32_t x, int32_t y)
{
    return (bitmap[y * ROW_BYTES + x / BITS_PER_BYTE] & (0x80 >> x % BITS_PER_BYTE)) == 0;
}

static uint64_t bitmap_hash(void)
{
    uint64_t hash = 0xcbf29ce484222325u;
    for (size_t byte = 0; byte < sizeof bitmap; ++byte) hash = (hash ^ bitmap[byte]) * 0x100000001b3u;
    return hash;
}

/* The picture as it was when it was looked at and kept. */
static void expect_hash(uint32_t number, uint64_t expected)
{
    render_frame(number);
    if (bitmap_hash() == expected) return;
    printf("frame %u is now %016llx\n", (unsigned)number, (unsigned long long)bitmap_hash());
    abort();
}

static void check_sines(void)
{
    for (uint32_t angle = 0; angle < 3 * TURN; ++angle) {
        assert(fabs(sine(angle) - (1 << Q14) * sin(angle * 2 * PI / TURN)) <= 0.5);
        assert(fabs(cosine(angle) - (1 << Q14) * cos(angle * 2 * PI / TURN)) <= 0.5);
    }
}

typedef struct {
    double x, y, z;
} point_t;

static double product(point_t a, point_t b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

/* The light the eye sees at a place of the screen, in levels; a negative
 * number where it looks past the torus. Lengths are in tube radii. */
static double traced_light(uint32_t number, double column, double row)
{
    double tilt = (TILT_START + number * TILT_STEP) * 2 * PI / TURN, spin = number * SPIN_STEP * 2 * PI / TURN;
    point_t across = {cos(spin), sin(spin), 0};
    point_t along = {sin(tilt) * sin(spin), -sin(tilt) * cos(spin), cos(tilt)};
    point_t axis = {-cos(tilt) * sin(spin), cos(tilt) * cos(spin), sin(tilt)};
    double eye = EYE_DISTANCE / (double)TUBE_RADIUS, ring = RING_RADIUS / (double)TUBE_RADIUS;
    point_t ray = {(column - COLUMNS / 2) / SCALE, -(row - ROWS / 2) / SCALE, 1};
    double length = sqrt(product(ray, ray));

    for (double along_ray = eye - TORUS_REACH - 1; along_ray < eye + TORUS_REACH + 1;) {
        point_t at = {ray.x / length * along_ray, ray.y / length * along_ray, ray.z / length * along_ray - eye};
        double a = product(at, across), b = product(at, along), h = product(at, axis);
        double from_axis = sqrt(a * a + b * b);
        double from_surface = sqrt((from_axis - ring) * (from_axis - ring) + h * h) - 1;
        if (from_surface < 1e-6) {
            double outward = (from_axis - ring) / from_axis;
            point_t facing = {
                outward * (a * across.x + b * along.x) + h * axis.x,
                outward * (a * across.y + b * along.y) + h * axis.y,
                outward * (a * across.z + b * along.z) + h * axis.z,
            };
            double one = 1 << Q14;
            point_t to_light = {TO_LIGHT.x / one, TO_LIGHT.y / one, TO_LIGHT.z / one};
            double lit = product(facing, to_light);
            return (SHADOW + (lit > 0 ? lit * LIGHT_RANGE : 0)) / (1 << Q8);
        }
        along_ray += from_surface;
    }
    return -1;
}

/* Every block of the picture that is wholly torus or wholly background
 * by the traced one has the paper of the traced light, or none. */
static void check_against_traced(uint32_t number)
{
    int32_t compared = 0;
    double worst = 0;
    render_frame(number);
    for (int32_t top = 0; top + BLOCK <= HEIGHT; top += BLOCK) {
        for (int32_t left = 0; left + BLOCK <= WIDTH; left += BLOCK) {
            int32_t paper = 0, cells = 0, cells_on_torus = 0;
            double light = 0;
            for (int32_t y = top; y < top + BLOCK; ++y)
                for (int32_t x = left; x < left + BLOCK; ++x) paper += is_paper(x, y);
            /* A cell's corners too: a block the outline only touches is left out. */
            for (double row = top / CELL; row <= (top + BLOCK) / CELL; row += 0.5) {
                for (double column = left / CELL; column <= (left + BLOCK) / CELL; column += 0.5) {
                    double here = traced_light(number, column, row);
                    ++cells;
                    if (here >= 0) {
                        ++cells_on_torus;
                        light += here;
                    }
                }
            }
            if (cells_on_torus == 0) {
                assert(paper == 0);
                ++compared;
            } else if (cells_on_torus == cells) {
                double apart = fabs(paper - light / cells);
                if (apart > worst) worst = apart;
                ++compared;
            }
        }
    }
    assert(compared > WIDTH / BLOCK * (HEIGHT / BLOCK) * 2 / 3 && worst <= LEVELS_APART);
}

static void check_renderer(void)
{
    static uint8_t kept[sizeof bitmap];
    check_sines();

    /* A frame is its number's and nothing else's. */
    render_frame(37);
    memcpy(kept, bitmap, sizeof kept);
    render_frame(38);
    assert(memcmp(kept, bitmap, sizeof kept) != 0);
    render_frame(37);
    assert(memcmp(kept, bitmap, sizeof kept) == 0);
    render_frame(37 + FRAMES);
    assert(memcmp(kept, bitmap, sizeof kept) == 0);

    /* Every frame is inside the screen, with a margin, and fills most of it. */
    for (uint32_t number = 0; number < FRAMES; ++number) {
        int32_t paper = 0, top = HEIGHT, bottom = 0, left = WIDTH, right = 0;
        render_frame(number);
        for (int32_t y = 0; y < HEIGHT; ++y) {
            for (int32_t x = 0; x < WIDTH; ++x) {
                if (!is_paper(x, y)) continue;
                ++paper;
                if (y < top) top = y;
                if (y > bottom) bottom = y;
                if (x < left) left = x;
                if (x > right) right = x;
            }
        }
        assert(paper > 0 && top >= EDGE_MARGIN && bottom < HEIGHT - EDGE_MARGIN);
        assert(left >= EDGE_MARGIN && right < WIDTH - EDGE_MARGIN);
        /* From corner to corner what is seen is never under 80% of the screen's height. */
        assert(hypot(bottom - top, right - left) >= HEIGHT * 4 / 5);
    }

    expect_hash(0, 0x08c276e1023deb73u);
    expect_hash(100, 0xa7eee73eb65c63d9u);

    static const uint32_t traced[] = {0, 5, 12, 28, 45, 77, 100, 131, 164, 200, 229, 250};
    for (size_t index = 0; index < sizeof traced / sizeof traced[0]; ++index) check_against_traced(traced[index]);
}

/* ---- The handlers ---- */

/* A newly loaded instance. What an earlier one left in memory must not matter. */
static void load_instance(void)
{
    state = STOPPED;
    frame = draws = draw_ms_worst = intervals = -7;
    draw_ms_total = interval_ms_total = previous_draw_at = -7;
    frame_is_rendered = true;
    counted_ms = 0;
    timer_set(FRAME_TIMER, 0); /* an instance's timers end with it */
    app_on_load();
    app_on_focus();
}

static void expect_summary(const char *frames, const char *average, const char *worst, const char *interval)
{
    rect_calls = 0;
    memset(lines, 0, sizeof lines);
    draw();
    assert(!strcmp(lines[0], frames) && !strcmp(lines[1], average) && !strcmp(lines[2], worst));
    assert(!strcmp(lines[3], interval) && !strcmp(lines[4], "OK: restart"));
    /* Every grey and the strip's outline. */
    assert(rect_calls == HOST_INK + 2);
}

static void check_handlers(void)
{
    static uint8_t drawn[sizeof bitmap];

    standin_forget_session();
    load_instance();
    assert(state == SPINNING && frame == 0 && !frame_is_rendered && draws == 0 && timer_ms() == 0);

    /* A slow first refresh still begins with frame zero. */
    uptime_is(9000);
    draw_takes_ms = 120;
    draw();
    assert(frame == 0 && frame_is_rendered && draws == 1 && intervals == 0 && bitmap_calls == 1);
    assert(timer_ms() == FRAME_INTERVAL_MS - 120);
    memcpy(drawn, bitmap, sizeof drawn);
    render_frame(0);
    assert(memcmp(drawn, bitmap, sizeof drawn) == 0);

    /* A repaint is not a frame: nothing is rendered, counted or timed anew - the timer that
     * waits for the next frame is left as it is, neither set again nor taken back. */
    int64_t due_ms = standin()->timer_due_ms[FRAME_TIMER];
    int32_t timer_calls = standin()->timer_sets;
    assert(due_ms != 0);
    draw();
    assert(draws == 1 && bitmap_calls == 2 && draw_ms_total == 120);
    assert(standin()->timer_sets == timer_calls && standin()->timer_due_ms[FRAME_TIMER] == due_ms);
    timer_set(FRAME_TIMER, 0); /* what follows starts with no timer waiting, as before */

    /* Timers come once after the panel was busy, not once an interval: a
     * late one is one frame, and nothing moves until that frame is drawn. */
    app_on_timer(FRAME_TIMER + 1);
    assert(frame == 0);
    uptime_is(20000);
    standin()->redraw_flags = -1;
    app_on_timer(FRAME_TIMER);
    assert(frame == 1 && !frame_is_rendered && standin()->redraw_flags == HOST_REDRAW_NO_BLINK);
    app_on_timer(FRAME_TIMER);
    assert(frame == 1);
    draw_takes_ms = 800;
    draw();
    assert(draws == 2 && draw_ms_worst == 800 && draw_ms_total == 920);
    assert(intervals == 1 && interval_ms_total == 20000 - 9000);
    /* The draw outlasted the interval: the next frame at once, the timer not cancelled. */
    assert(timer_ms() == 1);
    memcpy(drawn, bitmap, sizeof drawn);
    render_frame(1);
    assert(memcmp(drawn, bitmap, sizeof drawn) == 0);

    draw_takes_ms = 100;
    for (int32_t next = 2; next < 6; ++next) {
        standin_pass(500);
        app_on_timer(FRAME_TIMER);
        draw();
        assert(frame == next && draws == next + 1 && timer_ms() == FRAME_INTERVAL_MS - 100);
    }
    /* From the start of a draw to the start of the next: 800 and 500, then 100 and 500. */
    assert(intervals == 5 && interval_ms_total == 11000 + 1300 + 3 * 600);

    /* UP stops; the summary is the measures, and survives an unload. */
    app_on_button(BTN_UP_PRESSED);
    assert(state == STOPPED && timer_ms() == 0 && standin()->redraw_flags == HOST_REDRAW_CLEAN);
    app_on_timer(FRAME_TIMER);
    assert(frame == 5);
    expect_summary("Frames: 6", "Draw avg: 220 ms", "Draw max: 800 ms", "Frame interval: 2820 ms");
    app_on_blur();
    load_instance();
    assert(state == STOPPED && frame == 5 && timer_ms() == 0);
    expect_summary("Frames: 6", "Draw avg: 220 ms", "Draw max: 800 ms", "Frame interval: 2820 ms");

    /* OK starts over: after the summary with a clean panel, mid-row without. */
    app_on_button(BTN_OK_PRESSED);
    assert(state == SPINNING && frame == 0 && !frame_is_rendered && draws == 0 && intervals == 0);
    assert(timer_ms() == 0 && standin()->redraw_flags == HOST_REDRAW_CLEAN);
    draw();
    app_on_timer(FRAME_TIMER);
    draw();
    assert(frame == 1 && draws == 2 && intervals == 1);
    app_on_button(BTN_OK_PRESSED);
    assert(frame == 0 && draws == 0 && timer_ms() == 0 && standin()->redraw_flags == HOST_REDRAW_NO_BLINK);

    /* A row comes back to its frame; the time away is nobody's interval. */
    for (int32_t next = 0; next < 3; ++next) {
        draw();
        app_on_timer(FRAME_TIMER);
    }
    assert(frame == 3 && !frame_is_rendered && draws == 3 && intervals == 2);
    app_on_blur();
    load_instance();
    assert(state == SPINNING && frame == 3 && !frame_is_rendered && draws == 3 && intervals == 2);
    standin_pass(60000);
    draw();
    assert(frame == 3 && draws == 4 && intervals == 2 && timer_ms() == FRAME_INTERVAL_MS - 100);
    memcpy(drawn, bitmap, sizeof drawn);
    render_frame(3);
    assert(memcmp(drawn, bitmap, sizeof drawn) == 0);

    /* A session of another shape is none. */
    app_on_blur();
    int32_t other_version = SESSION_VERSION + 1;
    memcpy(standin()->session, &other_version, sizeof other_version);
    load_instance();
    assert(state == SPINNING && frame == 0 && draws == 0);
    app_on_blur();
    standin()->session_length -= 1;
    load_instance();
    assert(frame == 0 && draws == 0);

    /* Stopped before a frame was drawn: nothing to divide by. */
    app_on_button(BTN_UP_PRESSED);
    expect_summary("Frames: 0", "Draw avg: 0 ms", "Draw max: 0 ms", "Frame interval: 0 ms");

    /* The hidden settings: another interval, and a counted clock. */
    standin_set("interval", "2000");
    app_on_settings();
    app_on_button(BTN_OK_PRESSED);
    draw();
    assert(timer_ms() == 2000 - 100);
    standin_set("tick", "150");
    standin_forget_session();
    load_instance();
    for (int32_t next = 0; next < 3; ++next) {
        standin_pass(12345);
        draw();
        app_on_timer(FRAME_TIMER);
    }
    assert(timer_ms() == 2000 - 150);
    app_on_button(BTN_UP_PRESSED);
    expect_summary("Frames: 3", "Draw avg: 150 ms", "Draw max: 150 ms", "Frame interval: 300 ms");
    standin_set("interval", NULL);
    standin_set("tick", NULL);
}

int main(void)
{
    standin_reset();
    standin()->is_session_writable = true; /* app_on_blur() is called as for the instance on the screen */
    app_on_load();
    check_renderer();
    check_handlers();
    puts("Donut: sines, 256 frames, 12 of them against a traced torus, timers, repaint, stop, restart and unload passed");
    return 0;
}
