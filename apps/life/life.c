// SPDX-License-Identifier: MIT
// Copyright (c) 2026 GrakovNe
/*
 * Conway's Game of Life on a torus. Two packed fields hold generations;
 * drawing expands the current field to one screen bitmap, making one
 * call to Perinkpheral OS for all cells. Three unpacked rows supply each generation without
 * repeatedly looking up individual packed neighbours.
 * The packed field stays in the scene while the instance is temporarily unloaded.
 * OK runs or pauses; DOWN steps (held: ten); UP reseeds (held: patterns).
 */
#include "app_draw.h"
#include "app_state.h"
#include "app_text.h"
#include "host_api.h"
#include "strings.h"

#define WIDTH 400
#define HEIGHT 300
#define FIELD_BYTES ((100 * 75 + 7) / 8)
#define BITMAP_BYTES (WIDTH / 8 * HEIGHT)
#define STEP_TIMER 0
#define SCENE_VERSION 2
#define COLUMNS_MAX (WIDTH / 4)
#define STATUS_MARGIN 6
#define STATUS_PADDING 5

enum { PAUSED, EMPTY };
static uint8_t fields[2][FIELD_BYTES];
static uint8_t bitmap[BITMAP_BYTES];
static int32_t current, columns, rows, cell, generation, reason;
static bool running;
static uint32_t random_state;

typedef struct {
    int32_t version, cell, generation, reason;
    uint32_t random_state;
    bool running;
    uint8_t field[FIELD_BYTES];
} life_scene_state_t;

_Static_assert(sizeof(life_scene_state_t) <= HOST_SCENE_STATE_MAX, "Life field fits scene storage");

static int32_t cell_size(void)
{
    return app_setting_is("cell", "4 px") ? 4 : app_setting_is("cell", "8 px") ? 8 : 5;
}
static int32_t period(void) { return app_setting_is("speed", "Fast") ? 600 : 2000; }
static uint32_t random_number(void)
{
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}
static bool alive(int32_t x, int32_t y)
{
    int32_t bit = y * columns + x;
    return (fields[current][bit / 8] >> (7 - bit % 8) & 1) != 0;
}
static void put(uint8_t *field, int32_t x, int32_t y)
{
    int32_t bit = y * columns + x;
    field[bit / 8] |= (uint8_t)(0x80u >> (bit % 8));
}
static void clear_fields(void)
{
    for (int32_t i = 0; i < FIELD_BYTES; i++) { fields[0][i] = fields[1][i] = 0; }
    current = generation = 0; running = false; reason = PAUSED;
    timer_set(STEP_TIMER, 0);
}
static void new_field(bool reseed)
{
    cell = cell_size(); columns = WIDTH / cell; rows = HEIGHT / cell;
    clear_fields();
    if (reseed) {
        int32_t seed;
        random_state = app_setting_number("seed", 0, &seed) ? (uint32_t)seed : (uint32_t)time_uptime_ms() ^ time_boot_id();
        if (!random_state) { random_state = 0x6d2b79f5u; }
    }
    for (int32_t y = 0; y < rows; y++) {
        for (int32_t x = 0; x < columns; x++) {
            if (random_number() % 3 == 0) { put(fields[current], x, y); }
        }
    }
}

typedef struct { uint8_t x, y; } point_t;
static void pattern(const point_t *points, int32_t count, int32_t x, int32_t y)
{
    for (int32_t i = 0; i < count; i++) {
        int32_t px = x + points[i].x, py = y + points[i].y;
        if (px >= 0 && py >= 0 && px < columns && py < rows) { put(fields[current], px, py); }
    }
}
#define PATTERN(points, x, y) pattern(points, (int32_t)(sizeof points / sizeof points[0]), x, y)
static void classic_patterns(void)
{
    static const point_t glider[] = { {1,0}, {2,1}, {0,2}, {1,2}, {2,2} };
    static const point_t gun[] = {
        {24,0}, {22,1}, {24,1}, {12,2}, {13,2}, {20,2}, {21,2}, {34,2}, {35,2},
        {11,3}, {15,3}, {20,3}, {21,3}, {34,3}, {35,3}, {0,4}, {1,4}, {10,4}, {16,4}, {20,4}, {21,4},
        {0,5}, {1,5}, {10,5}, {14,5}, {16,5}, {17,5}, {22,5}, {24,5},
        {10,6}, {16,6}, {24,6}, {11,7}, {15,7}, {12,8}, {13,8}
    };
    /* A pulsar is four reflections of this quadrant. */
    static const point_t quadrant[] = { {2,0}, {3,0}, {4,0}, {0,2}, {5,2}, {0,3}, {5,3}, {0,4}, {5,4}, {2,5}, {3,5}, {4,5} };
    clear_fields();
    PATTERN(gun, 2, 2);
    PATTERN(glider, columns - 8, rows - 8);
    PATTERN(glider, columns - 8, 3);
    for (int32_t i = 0; i < (int32_t)(sizeof quadrant / sizeof quadrant[0]); i++) {
        int32_t x = quadrant[i].x, y = quadrant[i].y;
        put(fields[current], 5 + x, rows - 16 + y);
        put(fields[current], 5 + 12 - x, rows - 16 + y);
        put(fields[current], 5 + x, rows - 16 + 12 - y);
        put(fields[current], 5 + 12 - x, rows - 16 + 12 - y);
    }
}
static void pause(int32_t why)
{
    running = false; reason = why; timer_set(STEP_TIMER, 0);
}
static void unpack_row(uint8_t *output, int32_t row)
{
    int32_t bit = row * columns;
    const uint8_t *source = fields[current] + bit / 8;
    uint8_t mask = (uint8_t)(0x80u >> (bit % 8));
    for (int32_t x = 0; x < columns; x++) {
        output[x] = (*source & mask) != 0;
        mask >>= 1;
        if (!mask) { mask = 0x80; source++; }
    }
}

static void step(void)
{
    uint8_t *next = fields[1 - current];
    for (int32_t i = 0; i < FIELD_BYTES; i++) { next[i] = 0; }
    uint8_t row_data[3][COLUMNS_MAX], sums[COLUMNS_MAX];
    uint8_t *above = row_data[0], *middle = row_data[1], *below = row_data[2];
    unpack_row(above, rows - 1);
    unpack_row(middle, 0);
    unpack_row(below, 1);
    int32_t population = 0;
    int32_t output_byte = 0;
    uint8_t output_mask = 0x80;
    for (int32_t y = 0; y < rows; y++) {
        for (int32_t x = 0; x < columns; x++) {
            sums[x] = above[x] + middle[x] + below[x];
        }
        int32_t left_sum = sums[columns - 1], middle_sum = sums[0];
        for (int32_t x = 0; x < columns; x++) {
            int32_t right_sum = sums[x + 1 == columns ? 0 : x + 1];
            int32_t neighbours = left_sum + middle_sum + right_sum - middle[x];
            if (neighbours == 3 || (neighbours == 2 && middle[x])) {
                next[output_byte] |= output_mask;
                population++;
            }
            output_mask >>= 1;
            if (!output_mask) { output_mask = 0x80; output_byte++; }
            left_sum = middle_sum;
            middle_sum = right_sum;
        }
        uint8_t *reused = above;
        above = middle;
        middle = below;
        below = reused;
        if (y + 1 < rows) { unpack_row(below, y + 2 == rows ? 0 : y + 2); }
    }
    current = 1 - current;
    /* The counter may saturate after years; the field keeps evolving. */
    if (generation < INT32_MAX) { generation++; }
    if (!population) { pause(EMPTY); }
}
void app_on_focus(void)
{
    life_scene_state_t saved;
    if (scene_state_get(&saved, sizeof saved) != sizeof saved || saved.version != SCENE_VERSION ||
        saved.cell != cell_size()) {
        new_field(true);
        return;
    }
    cell = saved.cell;
    columns = WIDTH / cell;
    rows = HEIGHT / cell;
    current = 0;
    generation = saved.generation;
    reason = saved.reason;
    random_state = saved.random_state;
    running = saved.running;
    for (int32_t index = 0; index < FIELD_BYTES; index++) { fields[current][index] = saved.field[index]; }
    if (running) { timer_set(STEP_TIMER, period()); }
}

void app_on_blur(void)
{
    life_scene_state_t saved = {
        .version = SCENE_VERSION, .cell = cell, .generation = generation, .reason = reason,
        .random_state = random_state, .running = running
    };
    for (int32_t index = 0; index < FIELD_BYTES; index++) { saved.field[index] = fields[current][index]; }
    scene_state_set(&saved, sizeof saved);
}
void app_on_settings(void)
{
    if (cell != cell_size()) { new_field(true); screen_redraw(HOST_REDRAW_CLEAN); }
    else if (running) { timer_set(STEP_TIMER, period()); }
}
void app_on_button(int32_t event)
{
    switch (event) {
    case BTN_OK_PRESSED:
        if (running) { pause(PAUSED); screen_redraw(HOST_REDRAW_CLEAN); }
        else {
            running = true; reason = PAUSED; timer_set(STEP_TIMER, period()); screen_redraw(HOST_REDRAW_NO_BLINK);
        }
        break;
    case BTN_DOWN_PRESSED:
    case BTN_DOWN_LONG_PRESSED:
        if (running) { return; }
        reason = PAUSED;
        for (int32_t i = 0; i < (event == BTN_DOWN_LONG_PRESSED ? 10 : 1); i++) {
            step(); if (reason != PAUSED) { break; }
        }
        screen_redraw(reason == PAUSED ? HOST_REDRAW_NORMAL : HOST_REDRAW_CLEAN);
        break;
    case BTN_UP_PRESSED:
        new_field(false); screen_redraw(HOST_REDRAW_CLEAN); break;
    case BTN_UP_LONG_PRESSED:
        classic_patterns(); screen_redraw(HOST_REDRAW_CLEAN); break;
    default: break;
    }
}
void app_on_timer(int32_t id)
{
    if (id != STEP_TIMER || !running) { return; }
    step();
    screen_redraw(running ? HOST_REDRAW_NO_BLINK : HOST_REDRAW_CLEAN);
    if (running) { timer_set(STEP_TIMER, period()); }
}
static void make_bitmap(void)
{
    for (int32_t i = rows * cell * (WIDTH / 8); i < BITMAP_BYTES; i++) { bitmap[i] = 0; }
    for (int32_t y = 0; y < rows; y++) {
        int32_t pixel_row = y * cell;
        uint8_t *line = bitmap + pixel_row * (WIDTH / 8);
        for (int32_t byte = 0; byte < WIDTH / 8; byte++) { line[byte] = 0; }
        for (int32_t x = 0; x < columns; x++) {
            if (!alive(x, y)) { continue; }
            int32_t pixel = x * cell;
            int32_t shift = pixel % 8;
            uint16_t mask = (uint16_t)(((1u << cell) - 1) << (16 - cell - shift));
            line[pixel / 8] |= (uint8_t)(mask >> 8);
            if (shift + cell > 8) { line[pixel / 8 + 1] |= (uint8_t)mask; }
        }
        for (int32_t dy = 1; dy < cell; dy++) {
            for (int32_t b = 0; b < WIDTH / 8; b++) {
                bitmap[(pixel_row + dy) * (WIDTH / 8) + b] = bitmap[pixel_row * (WIDTH / 8) + b];
            }
        }
    }
}
static int32_t status_caption(void)
{
    switch (reason) {
    case EMPTY: return STR_EMPTY;
    default: return running ? STR_RUNNING : STR_PAUSED;
    }
}

static void draw_status(void)
{
    char line[96];
    app_text_t text;
    app_text_start(&text, line, sizeof line);
    app_text_add(&text, app_string(STR_GENERATION));
    app_text_add(&text, " ");
    app_text_add_number(&text, generation);
    app_text_add(&text, "  ");
    app_text_add(&text, app_string(status_caption()));

    int32_t font = HOST_FONT_SMALL | HOST_FONT_TABULAR;
    int32_t status_width = app_text_width(font, line);
    const char *hint = running ? app_string(STR_PAUSE_ACTION)
        : app_string(STR_PAUSED_ACTIONS);
    int32_t hint_width = app_text_width(HOST_FONT_SMALL, hint);
    if (status_width + hint_width + 5 * STATUS_PADDING > WIDTH - 2 * STATUS_MARGIN) {
        hint_width = 0;
    }
    int32_t width = status_width + 2 * STATUS_PADDING + (hint_width ? hint_width + 3 * STATUS_PADDING : 0);
    int32_t height = gfx_font_height(font) + 2 * STATUS_PADDING;
    int32_t left = WIDTH - STATUS_MARGIN - width;
    int32_t top = HEIGHT - STATUS_MARGIN - height;

    gfx_set_color(HOST_PAPER);
    gfx_draw_rect(left - 1, top - 1, width + 2, height + 2, HOST_RECT_FILLED);
    gfx_set_color(HOST_INK);
    gfx_draw_rect(left, top, width, height, 0);
    if (hint_width) {
        app_draw_text(left + STATUS_PADDING, top + STATUS_PADDING, HOST_FONT_SMALL, hint);
        int32_t divider = left + hint_width + 2 * STATUS_PADDING;
        gfx_draw_line(divider, top + STATUS_PADDING, divider, top + height - STATUS_PADDING - 1);
    }
    app_draw_text(left + width - STATUS_PADDING - status_width, top + STATUS_PADDING, font, line);
}

void app_on_draw(void)
{
    make_bitmap();
    gfx_draw_bitmap(0, 0, WIDTH, HEIGHT, bitmap);
    draw_status();
}
