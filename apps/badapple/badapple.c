/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * A video on e-paper: not something to watch, a test. It asks the panel
 * for a new picture as often as the host lets it and asks for none of
 * them to become a full refresh, which shows what the panel does under
 * two hundred partial refreshes in a row; and it unpacks every frame
 * pixel by pixel and draws it as a few hundred rectangles, which shows
 * what the interpreter costs. When the video ends, the numbers are on
 * the screen.
 *
 * The frames are files/frames.bin, made by tools/mkvideo; the format is
 * described there.
 */
#include <stdbool.h>

#include "app_text.h"
#include "host_api.h"

#define FRAME_TIMER     0
#define MS_PER_10_S     10000
#define LINE_HEIGHT     30
#define MARGIN          16
#define GREYS_TOP       232
#define GREYS_HEIGHT    52
#define WIDTH_MAX       400
#define FRAME_BYTES_MAX 2048
#define HEADER_BYTES    8
#define START_BYTES     4 /* of one entry of the table of where the frames start */
#define BITS_PER_BYTE   8

static const char FRAMES_FILE[] = "frames.bin";
#define FRAMES_FILE_LENGTH ((int32_t)sizeof FRAMES_FILE - 1)

/* What the file says about itself. */
static int32_t width, height, frame_count, frames_per_10_s;
static bool    has_video;

static int64_t started_ms;
static bool    is_over;

/* What the test measures. */
static int32_t draws, rectangles, draw_ms_total, draw_ms_worst;

/* The frame being unpacked: its bytes, and the row above the one in hand. */
static uint8_t frame_bytes[FRAME_BYTES_MAX];
static int32_t frame_bits, next_bit;
static uint8_t row_above[WIDTH_MAX]; /* 1 where there is ink */

static uint32_t little_endian(const uint8_t *bytes, int32_t count)
{
    uint32_t value = 0;
    for (int32_t i = count - 1; i >= 0; i--) {
        value = value << BITS_PER_BYTE | bytes[i];
    }
    return value;
}

static void read_header(void)
{
    uint8_t header[HEADER_BYTES];
    if (file_read(FRAMES_FILE, FRAMES_FILE_LENGTH, 0, header, sizeof header) != sizeof header) {
        return;
    }
    width = (int32_t)little_endian(header, 2);
    height = (int32_t)little_endian(header + 2, 2);
    frame_count = (int32_t)little_endian(header + 4, 2);
    frames_per_10_s = (int32_t)little_endian(header + 6, 2);
    has_video = width > 0 && width <= WIDTH_MAX && height > 0 && frame_count > 0 &&
                frames_per_10_s > 0;
}

/* False when the frame is not there or is larger than this player takes. */
static bool load_frame(int32_t frame)
{
    uint8_t starts[2 * START_BYTES]; /* where it starts, and where the next one does */
    int32_t entry_at = HEADER_BYTES + frame * START_BYTES;
    if (file_read(FRAMES_FILE, FRAMES_FILE_LENGTH, entry_at, starts, sizeof starts) !=
        sizeof starts) {
        return false;
    }
    int32_t start = (int32_t)little_endian(starts, START_BYTES);
    int32_t length = (int32_t)little_endian(starts + START_BYTES, START_BYTES) - start;
    if (length <= 0 || length > FRAME_BYTES_MAX ||
        file_read(FRAMES_FILE, FRAMES_FILE_LENGTH, start, frame_bytes, length) != length) {
        return false;
    }
    frame_bits = length * BITS_PER_BYTE;
    next_bit = 0;
    return true;
}

/* A frame cut short reads as zeros: the picture goes wrong, nothing else. */
static uint32_t read_bit(void)
{
    if (next_bit >= frame_bits) {
        return 0;
    }
    uint32_t bit = frame_bytes[next_bit / BITS_PER_BYTE] >> (7 - next_bit % BITS_PER_BYTE) & 1;
    next_bit++;
    return bit;
}

/* An Elias gamma code: as many zeros as the number has bits after its
 * first, then the number. The runs are stored as their length plus one. */
static int32_t read_run(void)
{
    int32_t more_bits = 0;
    while (read_bit() == 0) {
        if (++more_bits >= 31 || next_bit >= frame_bits) {
            return width * height; /* garbage: let it end the frame */
        }
    }
    uint32_t number = 1;
    for (int32_t i = 0; i < more_bits; i++) {
        number = number << 1 | read_bit();
    }
    return (int32_t)number - 1;
}

static void draw_frame(void)
{
    int32_t pixel_size = gfx_width() / width;
    if (gfx_height() / height < pixel_size) {
        pixel_size = gfx_height() / height;
    }
    int32_t left = (gfx_width() - width * pixel_size) / 2;
    int32_t top = (gfx_height() - height * pixel_size) / 2;
    for (int32_t column = 0; column < width; column++) {
        row_above[column] = 0;
    }

    /* The runs go on from one row into the next. */
    int32_t run_left = read_run();
    uint8_t run_differs = 0;
    for (int32_t row = 0; row < height; row++) {
        int32_t ink_from = -1; /* where the run of ink in hand began */
        for (int32_t column = 0; column <= width; column++) {
            uint8_t ink = 0;
            if (column < width) {
                while (run_left == 0) {
                    run_differs ^= 1;
                    run_left = read_run();
                }
                run_left--;
                ink = row_above[column] ^ run_differs;
                row_above[column] = ink;
            }
            if (ink && ink_from < 0) {
                ink_from = column;
            } else if (!ink && ink_from >= 0) {
                /* A run of ink in a row is one rectangle. */
                gfx_rect(left + ink_from * pixel_size, top + row * pixel_size,
                         (column - ink_from) * pixel_size, pixel_size, HOST_RECT_FILLED);
                rectangles++;
                ink_from = -1;
            }
        }
    }
}

static void start_over(void)
{
    is_over = false;
    draws = rectangles = draw_ms_total = draw_ms_worst = 0;
    started_ms = time_uptime_ms();
    timer_set(FRAME_TIMER, 1);
}

void app_on_focus(void)
{
    read_header();
    start_over();
}

void app_on_timer(int32_t id)
{
    (void)id;
    if (!has_video) {
        return;
    }
    /* Nobody pressed anything, it is wanted now, and it is one of a row. */
    request_redraw(HOST_REDRAW_NO_BLINK);
    timer_set(FRAME_TIMER, MS_PER_10_S / frames_per_10_s);
}

void app_on_button(int32_t event)
{
    switch (event) {
    case BTN_BACK_PRESSED:
        request_exit();
        break;
    case BTN_OK_PRESSED:
        start_over();
        break;
    case BTN_UP_PRESSED:
        /* Enough: to the numbers at once. */
        started_ms = time_uptime_ms() - (int64_t)frame_count * MS_PER_10_S / frames_per_10_s;
        request_redraw(HOST_REDRAW_NORMAL);
        break;
    }
}

static void draw_line(int32_t line, const char *label, int32_t value, const char *unit)
{
    char buffer[40];
    app_text_t text;
    app_text_start(&text, buffer, sizeof buffer);
    app_text_add(&text, label);
    if (unit != 0) {
        app_text_add_number(&text, value);
        app_text_add(&text, unit);
    }
    gfx_text(MARGIN, MARGIN + line * LINE_HEIGHT, HOST_FONT_TEXT, buffer, app_text_length(buffer));
}

static void draw_numbers(void)
{
    int32_t shown = draws > 0 ? draws : 1;
    draw_line(0, "Frames in the video: ", frame_count, "");
    draw_line(1, "Frames drawn: ", draws, "");
    draw_line(2, "A frame is ", width * height, " pixels");
    draw_line(3, "Draw, average: ", draw_ms_total / shown, " ms");
    draw_line(4, "Draw, worst: ", draw_ms_worst, " ms");
    draw_line(5, "Rectangles a frame: ", rectangles / shown, "");
    draw_line(6, "Up stop, Ok again, Back", 0, 0);

    /* Every grey the host can make, paper to ink: the video itself has
     * next to none, so this is where the panel shows what they look like. */
    int32_t step_width = (gfx_width() - 2 * MARGIN) / (HOST_INK + 1);
    for (int32_t shade = HOST_PAPER; shade <= HOST_INK; shade++) {
        gfx_color(shade);
        gfx_rect(MARGIN + shade * step_width, GREYS_TOP, step_width, GREYS_HEIGHT,
                 HOST_RECT_FILLED);
    }
    gfx_color(HOST_INK);
    gfx_rect(MARGIN, GREYS_TOP, (HOST_INK + 1) * step_width, GREYS_HEIGHT, HOST_RECT_OUTLINE);
}

void app_on_draw(void)
{
    if (!has_video) {
        draw_line(0, "No frames.bin to play.", 0, 0);
        return;
    }
    /* The video keeps real time: what the panel could not show is skipped,
     * not played late. Any frame can be drawn without the ones before. */
    int64_t before_ms = time_uptime_ms();
    int32_t due_frame = (int32_t)((before_ms - started_ms) * frames_per_10_s / MS_PER_10_S);
    if (due_frame >= frame_count) {
        if (!is_over) {
            is_over = true;
            timer_set(FRAME_TIMER, 0);
        }
        draw_numbers();
        return;
    }
    if (!load_frame(due_frame)) {
        draw_line(0, "A frame cannot be read.", 0, 0);
        return;
    }
    draw_frame();
    int32_t took_ms = (int32_t)(time_uptime_ms() - before_ms);
    draws++;
    draw_ms_total += took_ms;
    if (took_ms > draw_ms_worst) {
        draw_ms_worst = took_ms;
    }
}
