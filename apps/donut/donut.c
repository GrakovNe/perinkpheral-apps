/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * The spinning torus of Andy Sloane's donut.c, as a test of the panel: a
 * full frame of dithered greys, redrawn as fast as the panel takes them.
 *
 * A frame is made in four steps:
 *   1. the torus is turned - its three axes, from the frame's number;
 *   2. a mesh of RING_STEPS x TUBE_STEPS points of its surface is put on
 *      the screen in perspective, each with the light it gets;
 *   3. the mesh is filled slice by slice of the ring, the far slices
 *      first, so that the near ones cover them: there is no room for a
 *      depth of every pixel;
 *   4. the light, changing evenly inside each triangle, is dithered: a
 *      pixel is paper where the light exceeds the threshold of its place.
 * The light is worked out for cells of 2 x 2 pixels, a quarter of the
 * work; the dither is of single pixels. The background is ink, as a
 * terminal's is under donut.c.
 *
 * Numbers are integers. Sines and the parts of unit vectors are Q14
 * (16384 is 1), lengths Q12 with the tube's radius as 1, places on the
 * screen Q4 (a sixteenth of a cell), light Q8 in levels of the dither.
 *
 * OK starts over; UP stops and shows what the drawing took.
 */
#include <stdbool.h>

#include "app_state.h"
#include "app_text.h"
#include "host_api.h"
#include "strings.h"

#define WIDTH         400
#define HEIGHT        300
#define BITS_PER_BYTE 8
#define ROW_BYTES     (WIDTH / BITS_PER_BYTE)

#define CELL           2 /* pixels a side */
#define COLUMNS        (WIDTH / CELL)
#define ROWS           (HEIGHT / CELL)
#define CELLS_PER_BYTE (BITS_PER_BYTE / CELL)
#define CELL_BITS      0xc0 /* the first cell of a byte */

#define Q4  4
#define Q8  8
#define Q14 14
#define Q16 16

/* ---- Sines ---- */

#define TURN    256 /* angles are counted in 256ths of a turn */
#define QUARTER (TURN / 4)

/* sin(i * 90 / 64 degrees) * 16384. */
static const int16_t QUARTER_SINE[QUARTER + 1] = {
    0, 402, 804, 1205, 1606, 2006, 2404, 2801, 3196, 3590, 3981, 4370, 4756,
    5139, 5520, 5897, 6270, 6639, 7005, 7366, 7723, 8076, 8423, 8765, 9102,
    9434, 9760, 10080, 10394, 10702, 11003, 11297, 11585, 11866, 12140,
    12406, 12665, 12916, 13160, 13395, 13623, 13842, 14053, 14256, 14449,
    14635, 14811, 14978, 15137, 15286, 15426, 15557, 15679, 15791, 15893,
    15986, 16069, 16143, 16207, 16261, 16305, 16340, 16364, 16379, 16384
};

static int32_t sine(uint32_t angle)
{
    angle %= TURN;
    if (angle >= TURN / 2) return -sine(angle - TURN / 2);
    return QUARTER_SINE[angle <= QUARTER ? angle : TURN / 2 - angle];
}

static int32_t cosine(uint32_t angle)
{
    return sine(angle + QUARTER);
}

/* ---- The dither ---- */

/* The levels of light: 0 is ink, 64 paper. */
#define LEVELS 64

/* Bayer's 8 x 8 ordered dither. */
static const uint8_t THRESHOLD[8][8] = {
    { 0, 32,  8, 40,  2, 34, 10, 42},
    {48, 16, 56, 24, 50, 18, 58, 26},
    {12, 44,  4, 36, 14, 46,  6, 38},
    {60, 28, 52, 20, 62, 30, 54, 22},
    { 3, 35, 11, 43,  1, 33,  9, 41},
    {51, 19, 59, 27, 49, 17, 57, 25},
    {15, 47,  7, 39, 13, 45,  5, 37},
    {63, 31, 55, 23, 61, 29, 53, 21},
};

/* The same as bytes of the bitmap: eight pixels of a row of the screen,
 * by the row's place in the matrix and the level. Made at load. */
static uint8_t pattern[8][LEVELS + 1];

static void make_patterns(void)
{
    for (int32_t row = 0; row < 8; ++row) {
        for (int32_t level = 0; level <= LEVELS; ++level) {
            int32_t ink = 0;
            for (int32_t column = 0; column < 8; ++column)
                if (level <= THRESHOLD[row][column]) ink |= 0x80 >> column;
            pattern[row][level] = (uint8_t)ink;
        }
    }
}

/* ---- The torus ---- */

#define RING_STEPS   32 /* slices around the ring */
#define TUBE_STEPS   16 /* points around the tube */
#define TUBE_RADIUS  4096
#define RING_RADIUS  (2 * TUBE_RADIUS) /* the tube's centre line; donut.c's proportion */
#define Q12_FROM_Q14 2

/* The eye is on the z axis, this far before the centre, looking along z;
 * x is to the right, y up. */
#define EYE_DISTANCE (10 * TUBE_RADIUS)
/* Cells of a length at the centre's depth, by its size there: the torus
 * comes within ten pixels of the screen's top and bottom, no nearer. */
#define SCALE        224
#define SUBCELLS     (1 << Q4)
#define CELL_CENTRE  (SUBCELLS / 2)

/* Of a turn, a frame: 11 and 7 degrees. The panel shows three frames in
 * two seconds at best, and steps much smaller do not read as motion. */
#define TILT_START 40
#define TILT_STEP  8
#define SPIN_STEP  5

/* The side away from the light is not left ink: it would be lost in the
 * background. */
#define SHADOW      (5 << Q8)
#define LIGHT_RANGE ((LEVELS << Q8) - SHADOW)

typedef struct {
    int32_t x, y, z;
} vector_t;

/* To the light: above, to the left and before the torus. A unit vector. */
static const vector_t TO_LIGHT = {-8241, 11534, -8241};

typedef struct {
    int16_t x, y;  /* on the screen, Q4 */
    int16_t light; /* Q8 */
} vertex_t;

/* One more of each than steps: the seam is stored twice, and no index wraps. */
static vertex_t mesh[RING_STEPS + 1][TUBE_STEPS + 1];
/* How far beyond the torus's centre the tube's is at each ring step. */
static int32_t ring_depth[RING_STEPS + 1];
static uint8_t bitmap[ROW_BYTES * HEIGHT];

static int32_t dot(const vector_t *a, const vector_t *b)
{
    return (a->x * b->x + a->y * b->y + a->z * b->z) >> Q14;
}

/* a * p + b * q, for Q14 a and b. */
static vector_t combine(int32_t a, const vector_t *p, int32_t b, const vector_t *q)
{
    vector_t sum = {
        (a * p->x + b * q->x) >> Q14,
        (a * p->y + b * q->y) >> Q14,
        (a * p->z + b * q->z) >> Q14,
    };
    return sum;
}

/* Steps 1 and 2. The torus lies in the plane of `across` and `along` with
 * `axis` through its hole: the x, z and y axes turned by `tilt` about x and
 * then by `spin` about z, as donut.c turns it. */
static void place_mesh(uint32_t frame)
{
    uint32_t tilt = TILT_START + frame * TILT_STEP, spin = frame * SPIN_STEP;
    int32_t sin_tilt = sine(tilt), cos_tilt = cosine(tilt);
    int32_t sin_spin = sine(spin), cos_spin = cosine(spin);
    vector_t across = {cos_spin, sin_spin, 0};
    vector_t along = {sin_tilt * sin_spin >> Q14, -(sin_tilt * cos_spin >> Q14), cos_tilt};
    vector_t axis = {-(cos_tilt * sin_spin >> Q14), cos_tilt * cos_spin >> Q14, sin_tilt};
    int32_t axis_light = dot(&axis, &TO_LIGHT);

    for (int32_t ring = 0; ring <= RING_STEPS; ++ring) {
        uint32_t ring_angle = (uint32_t)ring * (TURN / RING_STEPS);
        /* From the torus's centre to the tube's, a unit vector. */
        vector_t outward = combine(cosine(ring_angle), &across, sine(ring_angle), &along);
        int32_t outward_light = dot(&outward, &TO_LIGHT);
        ring_depth[ring] = outward.z;

        for (int32_t tube = 0; tube <= TUBE_STEPS; ++tube) {
            uint32_t tube_angle = (uint32_t)tube * (TURN / TUBE_STEPS);
            int32_t cos_tube = cosine(tube_angle), sin_tube = sine(tube_angle);
            /* The surface faces cos * outward + sin * axis here; the
             * point is the tube's centre and a radius that way. */
            int32_t from_axis = RING_RADIUS + (cos_tube >> Q12_FROM_Q14);
            int32_t above = sin_tube >> Q12_FROM_Q14;
            vector_t point = combine(from_axis, &outward, above, &axis);
            int32_t facing_light = (cos_tube * outward_light + sin_tube * axis_light) >> Q14;

            int32_t depth = EYE_DISTANCE + point.z;
            vertex_t *vertex = &mesh[ring][tube];
            vertex->x = (int16_t)(COLUMNS / 2 * SUBCELLS + SCALE * SUBCELLS * point.x / depth);
            vertex->y = (int16_t)(ROWS / 2 * SUBCELLS - SCALE * SUBCELLS * point.y / depth);
            vertex->light = (int16_t)(SHADOW + (facing_light > 0 ? facing_light * LIGHT_RANGE >> Q14 : 0));
        }
    }
}

/* ---- Step 4: one triangle ---- */

/* Twice the triangle's area in Q4 cells; positive when a, b, c go
 * clockwise on the screen. */
static int32_t doubled_area(const vertex_t *a, const vertex_t *b, const vertex_t *c)
{
    return (b->x - a->x) * (c->y - a->y) - (c->x - a->x) * (b->y - a->y);
}

/* The first cell whose centre is at or after a Q4 place. */
static int32_t first_cell(int32_t place)
{
    return (place + SUBCELLS - 1 - CELL_CENTRE) >> Q4;
}

static int32_t clamp(int64_t value, int32_t low, int32_t high)
{
    return value < low ? low : value > high ? high : (int32_t)value;
}

/* The light over a triangle is a plane: its value at a vertex and how
 * much it grows a sixteenth of a cell to the right and down, Q16. A
 * triangle seen nearly from its edge has a plane too steep for 32 bits. */
typedef struct {
    const vertex_t *at;
    int64_t per_x, per_y;
} light_plane_t;
#define LIGHT_MAX (LEVELS << Q16)

/* An edge followed down the rows: where it crosses the middle of the
 * row, Q4 with 16 bits more, and how far it moves in a row. */
typedef struct {
    int64_t x, per_row;
} edge_t;

/* Always counted from the upper end, so two triangles that share an edge
 * get the same crossings and leave no gap. */
static edge_t edge_at_row(const vertex_t *upper, const vertex_t *lower, int32_t row)
{
    int64_t per_y = (lower->x - upper->x) * (int64_t)(1 << Q16) / (lower->y - upper->y);
    edge_t edge = {
        upper->x * (int64_t)(1 << Q16) + per_y * (row * SUBCELLS + CELL_CENTRE - upper->y),
        per_y * SUBCELLS,
    };
    return edge;
}

/* The cells of one row from `column` to before `column_end`: two rows of
 * pixels, each cell two bits of both. */
static void fill_span(int32_t row, int32_t column, int32_t column_end, int32_t light, int32_t light_per_cell)
{
    uint8_t *upper = bitmap + row * CELL * ROW_BYTES, *lower = upper + ROW_BYTES;
    const uint8_t *upper_pattern = pattern[row * CELL % 8], *lower_pattern = pattern[(row * CELL + 1) % 8];
    for (; column < column_end; ++column) {
        int32_t level = clamp(light >> Q16, 0, LEVELS);
        int32_t byte = column / CELLS_PER_BYTE;
        int32_t mask = CELL_BITS >> (column % CELLS_PER_BYTE * CELL);
        /* Ink as well as paper: a near slice is drawn over a far one. */
        upper[byte] = (uint8_t)((upper[byte] & ~mask) | (upper_pattern[level] & mask));
        lower[byte] = (uint8_t)((lower[byte] & ~mask) | (lower_pattern[level] & mask));
        light += light_per_cell;
    }
}

/* The rows from `row` to before `row_end`, between two edges that both
 * cross every one of them. */
static void fill_rows(int32_t row, int32_t row_end, const vertex_t *upper, const vertex_t *lower,
                      const vertex_t *other_upper, const vertex_t *other_lower, const light_plane_t *plane)
{
    if (row >= row_end) return;
    edge_t one = edge_at_row(upper, lower, row), other = edge_at_row(other_upper, other_lower, row);
    for (; row < row_end; ++row) {
        int32_t x_one = (int32_t)(one.x >> Q16), x_other = (int32_t)(other.x >> Q16);
        /* Clamped, so that no size of the torus can write outside the bitmap. */
        int32_t column = clamp(first_cell(x_one < x_other ? x_one : x_other), 0, COLUMNS);
        int32_t column_end = clamp(first_cell(x_one < x_other ? x_other : x_one), 0, COLUMNS);
        int32_t x = column * SUBCELLS + CELL_CENTRE, y = row * SUBCELLS + CELL_CENTRE;
        int64_t light = plane->at->light * (1 << Q8) + plane->per_x * (x - plane->at->x) +
                        plane->per_y * (y - plane->at->y);
        /* Inside the triangle the light is between its vertices'; bounded
         * for the cell an edge's rounding lets in, and so that a span
         * cannot count past 32 bits. */
        fill_span(row, column, column_end, clamp(light, 0, LIGHT_MAX),
                  clamp(plane->per_x * SUBCELLS, -LIGHT_MAX, LIGHT_MAX));
        one.x += one.per_row;
        other.x += other.per_row;
    }
}

static void fill_triangle(const vertex_t *a, const vertex_t *b, const vertex_t *c)
{
    int32_t area = doubled_area(a, b, c);
    /* The surface's outer side goes clockwise; the inner side is never seen. */
    if (area <= 0) return;

    int64_t light_ab = b->light - a->light, light_ac = c->light - a->light;
    light_plane_t plane = {
        a,
        (light_ab * (c->y - a->y) - light_ac * (b->y - a->y)) * (1 << Q8) / area,
        (light_ac * (b->x - a->x) - light_ab * (c->x - a->x)) * (1 << Q8) / area,
    };

    /* From here a is the top and c the bottom: a to c is the long side,
     * and the other two follow each other opposite it. */
    const vertex_t *swap;
    if (b->y < a->y) { swap = a; a = b; b = swap; }
    if (c->y < a->y) { swap = a; a = c; c = swap; }
    if (c->y < b->y) { swap = b; b = c; c = swap; }
    int32_t top = clamp(first_cell(a->y), 0, ROWS);
    int32_t middle = clamp(first_cell(b->y), 0, ROWS);
    int32_t bottom = clamp(first_cell(c->y), 0, ROWS);
    fill_rows(top, middle, a, c, a, b, &plane);
    fill_rows(middle, bottom, a, c, b, c, &plane);
}

/* ---- Step 3: the slices, far to near ---- */

static void fill_slice(int32_t ring)
{
    for (int32_t tube = 0; tube < TUBE_STEPS; ++tube) {
        const vertex_t *here = &mesh[ring][tube], *around = &mesh[ring][tube + 1];
        const vertex_t *next = &mesh[ring + 1][tube], *opposite = &mesh[ring + 1][tube + 1];
        fill_triangle(here, opposite, next);
        fill_triangle(here, around, opposite);
    }
}

static void render_frame(uint32_t frame)
{
    place_mesh(frame);
    for (int32_t byte = 0; byte < (int32_t)sizeof bitmap; ++byte) bitmap[byte] = 0xff;

    /* A slice is as far as the tube's centre at its two ends. The tube
     * is round, so where two slices are on one pixel, the one whose
     * centre is nearer is the one seen. */
    uint8_t order[RING_STEPS];
    for (int32_t count = 0; count < RING_STEPS; ++count) {
        int32_t depth = ring_depth[count] + ring_depth[count + 1];
        int32_t place = count;
        for (; place > 0; --place) {
            int32_t before = order[place - 1];
            if (ring_depth[before] + ring_depth[before + 1] >= depth) break;
            order[place] = (uint8_t)before;
        }
        order[place] = (uint8_t)count;
    }
    for (int32_t place = 0; place < RING_STEPS; ++place) fill_slice(order[place]);
}

/* ---- The frames in a row ---- */

#define FRAME_TIMER       0
/* What the panel needs for a frame: it sets the pace, not this. */
#define FRAME_INTERVAL_MS 500
#define SESSION_VERSION   1
#define NEVER             (-1)

static enum { SPINNING, STOPPED } state;
static int32_t frame;
/* False from the moment `frame` changes until app_on_draw() has rendered
 * it into `bitmap`. */
static bool frame_is_rendered;
static int32_t draws, draw_ms_worst, intervals;
static int64_t draw_ms_total, interval_ms_total;
/* When the frame before this one began to be drawn by this instance. */
static int64_t previous_draw_at;

/* Hidden settings. `interval` is the pause between frames. `tick` is for
 * the simulator's scenarios, whose pictures must repeat: the clock is not
 * read but counted, that many milliseconds a reading. */
static int32_t interval_ms, tick_ms;
static int64_t counted_ms;

typedef struct {
    int32_t version, state, frame, draws, draw_ms_worst, intervals;
    int64_t draw_ms_total, interval_ms_total;
} session_t;
APP_SESSION_FITS(session_t);

static int64_t clock_ms(void)
{
    if (tick_ms <= 0) return time_uptime_ms();
    counted_ms += tick_ms;
    return counted_ms;
}

static void read_settings(void)
{
    if (!app_setting_number("interval", 0, &interval_ms) || interval_ms <= 0) interval_ms = FRAME_INTERVAL_MS;
    if (!app_setting_number("tick", 0, &tick_ms)) tick_ms = 0;
}

static void start_over(void)
{
    timer_set(FRAME_TIMER, 0);
    state = SPINNING;
    frame = 0;
    frame_is_rendered = false;
    draws = draw_ms_worst = intervals = 0;
    draw_ms_total = interval_ms_total = 0;
    previous_draw_at = NEVER;
}

void app_on_load(void)
{
    make_patterns();
}

/* An instance unloaded in the background comes back to the frame and the
 * counts it left; the frame's timer is set again when it is drawn. */
void app_on_focus(void)
{
    session_t kept;
    read_settings();
    start_over();
    if (!app_session_load(&kept, sizeof kept) || kept.version != SESSION_VERSION) return;
    state = kept.state == STOPPED ? STOPPED : SPINNING;
    frame = kept.frame;
    draws = kept.draws;
    draw_ms_worst = kept.draw_ms_worst;
    intervals = kept.intervals;
    draw_ms_total = kept.draw_ms_total;
    interval_ms_total = kept.interval_ms_total;
}

void app_on_blur(void)
{
    session_t kept = {
        .version = SESSION_VERSION, .state = state, .frame = frame, .draws = draws,
        .draw_ms_worst = draw_ms_worst, .intervals = intervals,
        .draw_ms_total = draw_ms_total, .interval_ms_total = interval_ms_total,
    };
    app_session_save(&kept, sizeof kept);
}

void app_on_settings(void)
{
    read_settings();
}

void app_on_timer(int32_t id)
{
    if (id != FRAME_TIMER || state != SPINNING || !frame_is_rendered) return;
    /* One frame a timer, however late it came: frames are not skipped.
     * The turns repeat after TURN frames. */
    frame = (frame + 1) % TURN;
    frame_is_rendered = false;
    screen_redraw(HOST_REDRAW_NO_BLINK);
}

void app_on_button(int32_t event)
{
    if (event == BTN_OK_PRESSED) {
        /* After the summary the panel is cleaned of it first. */
        int32_t redraw = state == STOPPED ? HOST_REDRAW_CLEAN : HOST_REDRAW_NO_BLINK;
        start_over();
        screen_redraw(redraw);
    } else if (event == BTN_UP_PRESSED) {
        timer_set(FRAME_TIMER, 0);
        state = STOPPED;
        screen_redraw(HOST_REDRAW_CLEAN);
    }
}

/* ---- Drawing ---- */

#define MARGIN       16
#define LINE_HEIGHT  30
#define GREYS_TOP    232
#define GREYS_HEIGHT 52

static void draw_bitmap(void)
{
    gfx_draw_bitmap((gfx_width() - WIDTH) / 2, (gfx_height() - HEIGHT) / 2, WIDTH, HEIGHT, bitmap);
}

/* Renders and draws `frame`, then sets the timer for the next one. */
static void draw_new_frame(void)
{
    int64_t began_at = clock_ms();
    render_frame((uint32_t)frame);
    frame_is_rendered = true;
    draw_bitmap();
    int32_t took_ms = (int32_t)(clock_ms() - began_at);
    ++draws;
    draw_ms_total += took_ms;
    if (took_ms > draw_ms_worst) draw_ms_worst = took_ms;
    if (previous_draw_at != NEVER) {
        ++intervals;
        interval_ms_total += began_at - previous_draw_at;
    }
    previous_draw_at = began_at;
    /* The interval runs from the start of this draw, so rendering does
     * not slow the row. Zero would cancel the timer. */
    int32_t remaining_ms = interval_ms - took_ms;
    timer_set(FRAME_TIMER, remaining_ms > 0 ? remaining_ms : 1);
}

static void draw_text_line(int32_t line, const char *text)
{
    gfx_draw_text(MARGIN, MARGIN + line * LINE_HEIGHT, HOST_FONT_TEXT, text, app_text_length(text));
}

static void draw_number_line(int32_t line, int32_t label, int32_t value, const char *unit)
{
    char buffer[64];
    app_text_t text;
    app_text_start(&text, buffer, sizeof buffer);
    app_text_add(&text, app_string(label));
    app_text_add_number(&text, value);
    app_text_add(&text, unit);
    draw_text_line(line, buffer);
}

/* What the row of frames took, and every grey of the panel as a strip. */
static void draw_summary(void)
{
    const char *ms = app_string(STR_MILLISECONDS);
    draw_number_line(0, STR_FRAMES, draws, "");
    draw_number_line(1, STR_DRAW_AVERAGE, draws > 0 ? (int32_t)(draw_ms_total / draws) : 0, ms);
    draw_number_line(2, STR_DRAW_WORST, draw_ms_worst, ms);
    /* Measured between the starts of two draws: the panel's pace. */
    draw_number_line(3, STR_FRAME_INTERVAL, intervals > 0 ? (int32_t)(interval_ms_total / intervals) : 0, ms);
    draw_text_line(4, app_string(STR_RESTART));
    int32_t step_width = (gfx_width() - 2 * MARGIN) / (HOST_INK + 1);
    for (int32_t shade = HOST_PAPER; shade <= HOST_INK; ++shade) {
        gfx_set_color(shade);
        gfx_draw_rect(MARGIN + shade * step_width, GREYS_TOP, step_width, GREYS_HEIGHT, HOST_RECT_FILLED);
    }
    gfx_set_color(HOST_INK);
    gfx_draw_rect(MARGIN, GREYS_TOP, (HOST_INK + 1) * step_width, GREYS_HEIGHT, HOST_RECT_OUTLINE);
}

void app_on_draw(void)
{
    if (state == STOPPED) {
        draw_summary();
        return;
    }
    /* Perinkpheral OS may ask for the same frame again. */
    if (frame_is_rendered) draw_bitmap();
    else draw_new_frame();
}
