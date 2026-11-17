// SPDX-License-Identifier: MIT
// Copyright (c) 2026 GrakovNe
/*
 * Conway's Game of Life on a torus. Two packed fields hold generations;
 * drawing expands the current field to one screen bitmap, making one host
 * call for all cells. Three recent hashes detect still lives and short
 * oscillators. Animation ends with a clean refresh, at most 500 steps.
 * No field is persisted: receiving the screen starts a new paused field.
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
#define GENERATIONS_MAX 500

enum { PAUSED, STILL, EMPTY, LIMIT };
static uint8_t fields[2][FIELD_BYTES];
static uint8_t bitmap[BITMAP_BYTES];
static int32_t current, columns, rows, cell, generation, reason;
static bool running;
static uint32_t random_state, hashes[3];
static int32_t hash_count, hash_next;

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
static uint32_t field_hash(void)
{
    uint32_t hash = 2166136261u;
    int32_t bytes = (columns * rows + 7) / 8;
    for (int32_t i = 0; i < bytes; i++) { hash = (hash ^ fields[current][i]) * 16777619u; }
    return hash;
}
static void reset_hashes(void)
{
    hashes[0] = field_hash(); hash_count = 1; hash_next = 1;
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
    reset_hashes();
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
    reset_hashes();
}
static void pause(int32_t why)
{
    running = false; reason = why; timer_set(STEP_TIMER, 0);
}
static void step(void)
{
    if (generation >= GENERATIONS_MAX) { pause(LIMIT); return; }
    uint8_t *next = fields[1 - current];
    for (int32_t i = 0; i < FIELD_BYTES; i++) { next[i] = 0; }
    int32_t population = 0;
    for (int32_t y = 0; y < rows; y++) {
        int32_t above = y == 0 ? rows - 1 : y - 1, below = y + 1 == rows ? 0 : y + 1;
        for (int32_t x = 0; x < columns; x++) {
            int32_t left = x == 0 ? columns - 1 : x - 1, right = x + 1 == columns ? 0 : x + 1;
            int32_t neighbours = alive(left, above) + alive(x, above) + alive(right, above)
                + alive(left, y) + alive(right, y) + alive(left, below) + alive(x, below) + alive(right, below);
            if (neighbours == 3 || (neighbours == 2 && alive(x, y))) {
                put(next, x, y); population++;
            }
        }
    }
    current = 1 - current; generation++;
    uint32_t hash = field_hash();
    bool repeats = false;
    for (int32_t i = 0; i < hash_count; i++) { repeats |= hashes[i] == hash; }
    hashes[hash_next] = hash; hash_next = (hash_next + 1) % 3;
    if (hash_count < 3) { hash_count++; }
    if (!population) { pause(EMPTY); }
    else if (generation >= GENERATIONS_MAX) { pause(LIMIT); }
    else if (repeats) { pause(STILL); }
}
void app_on_focus(void) { new_field(true); }
void app_on_blur(void) { pause(PAUSED); }
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
        else if (generation < GENERATIONS_MAX) {
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
    for (int32_t i = 0; i < BITMAP_BYTES; i++) { bitmap[i] = 0; }
    for (int32_t y = 0; y < rows; y++) {
        int32_t pixel_row = y * cell;
        for (int32_t x = 0; x < columns; x++) {
            if (!alive(x, y)) { continue; }
            for (int32_t dx = 0; dx < cell; dx++) {
                int32_t px = x * cell + dx;
                bitmap[pixel_row * (WIDTH / 8) + px / 8] |= (uint8_t)(0x80u >> (px % 8));
            }
        }
        for (int32_t dy = 1; dy < cell; dy++) {
            for (int32_t b = 0; b < WIDTH / 8; b++) {
                bitmap[(pixel_row + dy) * (WIDTH / 8) + b] = bitmap[pixel_row * (WIDTH / 8) + b];
            }
        }
    }
}
void app_on_draw(void)
{
    make_bitmap(); gfx_draw_bitmap(0, 0, WIDTH, HEIGHT, bitmap);
    char line[96]; app_text_t text;
    app_text_start(&text, line, sizeof line);
    app_text_add(&text, app_string(STR_GENERATION)); app_text_add(&text, " "); app_text_add_number(&text, generation); app_text_add(&text, "  ");
    app_text_add(&text, app_string(running ? STR_RUNNING : reason == STILL ? STR_STILL : reason == EMPTY ? STR_EMPTY : reason == LIMIT ? STR_LIMIT : STR_PAUSED));
    int32_t width = app_text_width(HOST_FONT_SMALL | HOST_FONT_TABULAR, line);
    int32_t height = gfx_font_height(HOST_FONT_SMALL), x = WIDTH - width - 8, y = HEIGHT - height - 4;
    gfx_set_color(HOST_PAPER); gfx_draw_rect(x - 4, y - 2, width + 12, height + 6, HOST_RECT_FILLED);
    gfx_set_color(HOST_INK); app_draw_text(x, y, HOST_FONT_SMALL | HOST_FONT_TABULAR, line);
}
