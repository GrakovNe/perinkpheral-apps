/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
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
#define VIDEO_WIDTH     200
#define VIDEO_HEIGHT    150
#define VIDEO_ROW_BYTES (VIDEO_WIDTH / 8)
#define VIDEO_BYTES     (VIDEO_ROW_BYTES * VIDEO_HEIGHT)
#define OUTPUT_SCALE    2
#define OUTPUT_WIDTH    (VIDEO_WIDTH * OUTPUT_SCALE)
#define OUTPUT_HEIGHT   (VIDEO_HEIGHT * OUTPUT_SCALE)
#define OUTPUT_ROW_BYTES (OUTPUT_WIDTH / 8)
#define OUTPUT_BYTES    (OUTPUT_ROW_BYTES * OUTPUT_HEIGHT)
#define FRAME_BYTES_MAX 2048
#define HEADER_BYTES    12
#define OFFSET_BYTES    2
#define BITS_PER_BYTE   8

static const char FRAMES_FILE[] = "frames.bin";
#define FRAMES_FILE_LENGTH ((int32_t)sizeof FRAMES_FILE - 1)

static int32_t frame_count, frames_per_10_s, current_frame, cached_frame;
static bool has_video, is_over, read_failed;
static int32_t draws, draw_ms_total, draw_ms_worst;
static uint8_t frame_bytes[FRAME_BYTES_MAX];
static int32_t frame_bits, next_bit;
static uint8_t pixels[VIDEO_BYTES];
static uint8_t bitmap[OUTPUT_BYTES];

static uint32_t little_endian(const uint8_t *bytes)
{
    return bytes[0] | (uint32_t)bytes[1] << BITS_PER_BYTE;
}

static void read_header(void)
{
    uint8_t header[HEADER_BYTES];
    has_video = false;
    if (file_read(FRAMES_FILE, FRAMES_FILE_LENGTH, 0, header, sizeof header) != sizeof header) return;
    if (header[0] != 'B' || header[1] != 'A' || header[2] != '0' || header[3] != '1' ||
        little_endian(header + 4) != VIDEO_WIDTH || little_endian(header + 6) != VIDEO_HEIGHT) return;
    frame_count = (int32_t)little_endian(header + 8);
    frames_per_10_s = (int32_t)little_endian(header + 10);
    has_video = frame_count > 0 && frames_per_10_s > 0 &&
                HEADER_BYTES + (frame_count + 1) * OFFSET_BYTES < 65536;
}

static bool load_frame(int32_t frame)
{
    uint8_t offsets[2 * OFFSET_BYTES];
    if (file_read(FRAMES_FILE, FRAMES_FILE_LENGTH, HEADER_BYTES + frame * OFFSET_BYTES,
                  offsets, sizeof offsets) != sizeof offsets) return false;
    int32_t start = (int32_t)little_endian(offsets);
    int32_t length = (int32_t)little_endian(offsets + OFFSET_BYTES) - start;
    if (start < HEADER_BYTES + (frame_count + 1) * OFFSET_BYTES || length <= 0 ||
        length > FRAME_BYTES_MAX ||
        file_read(FRAMES_FILE, FRAMES_FILE_LENGTH, start, frame_bytes, length) != length) return false;
    frame_bits = length * BITS_PER_BYTE;
    next_bit = 0;
    return true;
}

static int32_t read_bit(void)
{
    if (next_bit >= frame_bits) return -1;
    int32_t bit = frame_bytes[next_bit / BITS_PER_BYTE] >> (7 - next_bit % BITS_PER_BYTE) & 1;
    ++next_bit;
    return bit;
}

/* Positive Exp-Golomb runs, with the order stored once per frame. */
static int32_t read_run(int32_t order)
{
    int32_t more_bits = order;
    int32_t bit;
    while ((bit = read_bit()) == 0)
        if (++more_bits > 16) return -1;
    if (bit < 0) return -1;
    uint32_t number = 1;
    for (int32_t index = 0; index < more_bits; ++index) {
        bit = read_bit();
        if (bit < 0) return -1;
        number = number << 1 | (uint32_t)bit;
    }
    return (int32_t)number - (1 << order) + 1;
}

static void ink_run(int32_t first, int32_t count)
{
    while (count && first % BITS_PER_BYTE) {
        pixels[first / BITS_PER_BYTE] |= (uint8_t)(0x80 >> (first % BITS_PER_BYTE));
        ++first;
        --count;
    }
    while (count >= BITS_PER_BYTE) {
        pixels[first / BITS_PER_BYTE] = 0xff;
        first += BITS_PER_BYTE;
        count -= BITS_PER_BYTE;
    }
    while (count--) {
        pixels[first / BITS_PER_BYTE] |= (uint8_t)(0x80 >> (first % BITS_PER_BYTE));
        ++first;
    }
}

static uint32_t double_pixels(uint32_t byte)
{
    byte = (byte | byte << 4) & 0x0f0f;
    byte = (byte | byte << 2) & 0x3333;
    byte = (byte | byte << 1) & 0x5555;
    return byte | byte << 1;
}

static bool unpack_frame(void)
{
    for (int32_t index = 0; index < VIDEO_BYTES; ++index) pixels[index] = 0;
    int32_t order = 0;
    for (int32_t index = 0; index < 3; ++index) {
        int32_t bit = read_bit();
        if (bit < 0) return false;
        order = order << 1 | bit;
    }
    int32_t ink = read_bit();
    if (ink < 0) return false;
    for (int32_t first = 0; first < VIDEO_WIDTH * VIDEO_HEIGHT;) {
        int32_t count = read_run(order);
        if (count <= 0 || count > VIDEO_WIDTH * VIDEO_HEIGHT - first) return false;
        if (ink) ink_run(first, count);
        first += count;
        ink ^= 1;
    }
    for (int32_t index = VIDEO_ROW_BYTES; index < VIDEO_BYTES; ++index)
        pixels[index] ^= pixels[index - VIDEO_ROW_BYTES];
    for (int32_t row = 0; row < VIDEO_HEIGHT; ++row) {
        int32_t output = row * OUTPUT_SCALE * OUTPUT_ROW_BYTES;
        for (int32_t column = 0; column < VIDEO_ROW_BYTES; ++column) {
            uint32_t doubled = double_pixels(pixels[row * VIDEO_ROW_BYTES + column]);
            bitmap[output + column * 2] = bitmap[output + OUTPUT_ROW_BYTES + column * 2] = (uint8_t)(doubled >> 8);
            bitmap[output + column * 2 + 1] = bitmap[output + OUTPUT_ROW_BYTES + column * 2 + 1] = (uint8_t)doubled;
        }
    }
    return true;
}

static void start_over(void)
{
    timer_set(FRAME_TIMER, 0);
    is_over = read_failed = false;
    draws = draw_ms_total = draw_ms_worst = 0;
    current_frame = 0;
    cached_frame = -1;
}

void app_on_focus(void)
{
    read_header();
    start_over();
}

void app_on_timer(int32_t id)
{
    if (id != FRAME_TIMER || !has_video || is_over || read_failed || cached_frame != current_frame) return;
    /* Advance once even if the timer arrived late. The next timer is armed
     * only after this frame is drawn, so a pending redraw cannot skip it. */
    if (++current_frame == frame_count) is_over = true;
    screen_redraw(is_over ? HOST_REDRAW_CLEAN : HOST_REDRAW_NO_BLINK);
}

void app_on_button(int32_t event)
{
    if (event == BTN_OK_PRESSED) {
        start_over();
        screen_redraw(HOST_REDRAW_NO_BLINK);
    } else if (event == BTN_UP_PRESSED) {
        is_over = true;
        timer_set(FRAME_TIMER, 0);
        screen_redraw(HOST_REDRAW_CLEAN);
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
    gfx_draw_text(MARGIN, MARGIN + line * LINE_HEIGHT, HOST_FONT_TEXT, buffer, app_text_length(buffer));
}

static void draw_numbers(void)
{
    int32_t shown = draws > 0 ? draws : 1;
    draw_line(0, "Frames: ", frame_count, "");
    draw_line(1, "Drawn: ", draws, "");
    draw_line(2, "Pixels per frame: ", VIDEO_WIDTH * VIDEO_HEIGHT, "");
    draw_line(3, "Draw avg: ", draw_ms_total / shown, " ms");
    draw_line(4, "Draw max: ", draw_ms_worst, " ms");
    draw_line(5, "Frame interval: ", MS_PER_10_S / frames_per_10_s, " ms");
    draw_line(6, "Up: stop   OK: restart", 0, 0);
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
    if (!has_video) { draw_line(0, "frames.bin not found.", 0, 0); return; }
    if (is_over) { draw_numbers(); return; }
    bool new_frame = cached_frame != current_frame;
    int64_t before_ms = time_uptime_ms();
    if (new_frame) {
        if (!load_frame(current_frame) || !unpack_frame()) {
            read_failed = true;
            timer_set(FRAME_TIMER, 0);
            draw_line(0, "Read error.", 0, 0);
            return;
        }
        cached_frame = current_frame;
    }
    gfx_draw_bitmap((gfx_width() - OUTPUT_WIDTH) / 2, (gfx_height() - OUTPUT_HEIGHT) / 2,
                    OUTPUT_WIDTH, OUTPUT_HEIGHT, bitmap);
    if (new_frame) {
        int32_t took_ms = (int32_t)(time_uptime_ms() - before_ms);
        ++draws;
        draw_ms_total += took_ms;
        if (took_ms > draw_ms_worst) draw_ms_worst = took_ms;
        int32_t remaining_ms = MS_PER_10_S / frames_per_10_s - took_ms;
        timer_set(FRAME_TIMER, remaining_ms > 0 ? remaining_ms : 1);
    }
}
