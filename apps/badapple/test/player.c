/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HOST_IMPORT(name, ...) __VA_ARGS__
#include "../badapple.c"
#include "app_text.c"

static uint8_t video[65536];
static size_t video_size;
static int64_t uptime;
static int32_t timer_delay, redraw_flags;
static int bitmap_calls;
static FILE *output;

int32_t file_read(const char *name, int32_t name_length, int32_t offset, void *buffer, int32_t length)
{
    assert(name_length == 10 && !memcmp(name, "frames.bin", 10));
    if (offset < 0 || length < 0 || (size_t)offset + (size_t)length > video_size) return HOST_ERR_NOT_FOUND;
    memcpy(buffer, video + offset, (size_t)length);
    return length;
}
int64_t time_uptime_ms(void) { return uptime; }
int32_t timer_set(int32_t id, int32_t milliseconds)
{
    assert(id == FRAME_TIMER);
    timer_delay = milliseconds;
    return HOST_OK;
}
void screen_redraw(int32_t flags) { redraw_flags = flags; }
int32_t gfx_width(void) { return 400; }
int32_t gfx_height(void) { return 300; }
int32_t gfx_draw_bitmap(int32_t x, int32_t y, int32_t width, int32_t height, const uint8_t *bits)
{
    assert(x == 0 && y == 0 && width == 400 && height == 300);
    assert(bits == bitmap);
    ++bitmap_calls;
    return HOST_OK;
}
int32_t gfx_draw_rect(int32_t x, int32_t y, int32_t width, int32_t height, int32_t fill)
{
    (void)x; (void)y; (void)width; (void)height; (void)fill;
    assert(is_over);
    return HOST_OK;
}
int32_t gfx_set_color(int32_t color) { (void)color; return HOST_OK; }
int32_t gfx_draw_text(int32_t x, int32_t y, int32_t font, const char *text, int32_t length)
{
    (void)x; (void)y; (void)font; (void)text; (void)length;
    return HOST_OK;
}

int main(int argc, char **argv)
{
    assert(argc == 3);
    frame_bytes[0] = 0xd3;
    frame_bytes[1] = 0xa6;
    frame_bytes[2] = 0x5c;
    frame_bits = 24;
    for (int32_t offset = 0; offset < BITS_PER_BYTE; ++offset) {
        for (int32_t count = 0; count <= 16; ++count) {
            next_bit = offset;
            uint32_t expected = (0xd3a65cu >> (24 - offset - count)) & ((1u << count) - 1);
            assert(read_bits(count) == (int32_t)expected && next_bit == offset + count);
        }
    }
    next_bit = frame_bits - 1;
    assert(read_bits(2) == -1 && next_bit == frame_bits - 1);
    next_bit = frame_bits;
    assert(read_bits(0) == 0 && read_bits(1) == -1);
    frame_bytes[0] = 0;
    frame_bits = BITS_PER_BYTE;
    next_bit = 0;
    assert(read_run(0) == -1);
    FILE *input = fopen(argv[1], "rb");
    assert(input);
    video_size = fread(video, 1, sizeof video, input);
    assert(!ferror(input) && feof(input));
    fclose(input);
    output = fopen(argv[2], "wb");
    assert(output);
    app_on_focus();
    assert(has_video && frame_count == 165 && frames_per_10_s == 8 && timer_delay == 0);
    /* A slow initial full refresh must still begin with frame zero. */
    uptime = 9000;
    for (int32_t frame = 0; frame < frame_count; ++frame) {
        assert(current_frame == frame);
        app_on_draw();
        assert(cached_frame == frame && draws == frame + 1 && timer_delay == 1250);
        assert(fwrite(bitmap, 1, sizeof bitmap, output) == sizeof bitmap);
        int32_t previous_draws = draws;
        app_on_draw();
        assert(draws == previous_draws && timer_delay == 1250);
        /* Timers expire once after a blocking panel refresh, not once per
         * elapsed interval. A very late event must advance only one frame. */
        uptime += frame % 3 == 0 ? 7500 : 1250;
        timer_delay = 0;
        app_on_timer(FRAME_TIMER);
        assert(current_frame == frame + 1 && timer_delay == 0);
        app_on_timer(FRAME_TIMER);
        assert(current_frame == frame + 1);
    }
    assert(is_over && draws == 165 && redraw_flags == HOST_REDRAW_CLEAN);
    assert(bitmap_calls == 330);
    app_on_draw();
    fclose(output);
    app_on_button(BTN_OK_PRESSED);
    assert(current_frame == 0 && cached_frame == -1 && !is_over && timer_delay == 0);
    app_on_draw();
    assert(current_frame == 0 && draws == 1 && timer_delay == 1250);
    app_on_button(BTN_UP_PRESSED);
    assert(is_over && timer_delay == 0 && redraw_flags == HOST_REDRAW_CLEAN);
    app_on_timer(FRAME_TIMER);
    assert(current_frame == 0);
    app_on_button(BTN_OK_PRESSED);
    size_t saved_size = video_size;
    video_size = HEADER_BYTES;
    app_on_draw();
    assert(read_failed && timer_delay == 0);
    video_size = saved_size;
    app_on_button(BTN_OK_PRESSED);
    app_on_draw();
    assert(!read_failed && draws == 1);
    video[0] = 0;
    app_on_focus();
    assert(!has_video && timer_delay == 0);
    app_on_button(BTN_UP_PRESSED);
    app_on_draw();
    puts("Bad Apple: 165 frames, delayed/coalesced timers, repaint, stop/restart and read errors passed");
    return 0;
}
