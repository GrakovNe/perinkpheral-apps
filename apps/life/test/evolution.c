// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../life.c"
#include "standin.h"

static uint8_t reference[75][100], expected[75][100];
static int comparisons;

static int32_t timer_ms(void)
{
    assert((standin_timers() & ~(1u << STEP_TIMER)) == 0);
    return standin_timer_left(STEP_TIMER);
}

static void take_reference(void)
{
    for (int y = 0; y < rows; y++) {
        for (int x = 0; x < columns; x++) { reference[y][x] = alive(x, y); }
    }
}

static void reference_step(void)
{
    for (int y = 0; y < rows; y++) {
        for (int x = 0; x < columns; x++) {
            int neighbours = 0;
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    if (dx || dy) neighbours += reference[(y + dy + rows) % rows][(x + dx + columns) % columns];
                }
            }
            expected[y][x] = neighbours == 3 || (neighbours == 2 && reference[y][x]);
        }
    }
    memcpy(reference, expected, sizeof reference);
}

static void compare_step(void)
{
    reference_step();
    step();
    for (int y = 0; y < rows; y++) {
        for (int x = 0; x < columns; x++) { assert(alive(x, y) == reference[y][x]); }
    }
    make_bitmap();
    for (int y = 0; y < HEIGHT; y++) {
        for (int x = 0; x < WIDTH; x++) {
            bool pixel = (bitmap[y * (WIDTH / 8) + x / 8] & (0x80 >> (x % 8))) != 0;
            bool expected_pixel = y / cell < rows && reference[y / cell][x / cell];
            assert(pixel == expected_pixel);
        }
    }
    comparisons++;
}

static void choose_size(int size)
{
    cell = size; columns = WIDTH / cell; rows = HEIGHT / cell;
    clear_fields();
}

static void randomized_boards(void)
{
    const int sizes[] = {4, 5, 8};
    for (size_t size = 0; size < sizeof sizes / sizeof sizes[0]; size++) {
        choose_size(sizes[size]);
        for (int seed = 1; seed <= 4; seed++) {
            clear_fields(); random_state = (uint32_t)seed;
            for (int y = 0; y < rows; y++) {
                for (int x = 0; x < columns; x++) {
                    if (random_number() % 3 == 0) put(fields[current], x, y);
                }
            }
            take_reference();
            for (int tick = 0; tick < 30; tick++) compare_step();
        }
    }
}

static void keeps_running(void)
{
    choose_size(4);
    put(fields[current], 0, rows - 1);
    put(fields[current], 0, 0);
    put(fields[current], 0, 1);
    take_reference();
    running = true;
    for (int tick = 0; tick < 600; tick++) { compare_step(); assert(running); }
    assert(generation == 600);
    app_on_button(BTN_OK_PRESSED);
    assert(!running && timer_ms() == 0);
    app_on_button(BTN_OK_PRESSED);
    assert(running && timer_ms() == 600);

    choose_size(8);
    const point_t glider[] = {{1,0},{2,1},{0,2},{1,2},{2,2}};
    PATTERN(glider, columns - 3, rows - 3);
    take_reference();
    running = true;
    for (int tick = 0; tick < 600; tick++) { compare_step(); assert(running); }
    generation = INT32_MAX - 1;
    compare_step(); compare_step();
    assert(running && generation == INT32_MAX);
    app_on_button(BTN_UP_PRESSED);
    assert(!running && generation == 0);
}

int main(void)
{
    standin_reset();
    standin_set("cell", "4 px");
    standin_set("speed", "Fast");
    standin_set("seed", "1");
    randomized_boards();
    keeps_running();
    printf("Life: %d generations and full bitmaps match reference; cycles, limits and overflow checked\n", comparisons);
    return 0;
}
