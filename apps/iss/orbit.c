// SPDX-License-Identifier: MIT
/* The projection and solar dot product use hundredths of a degree and
 * Q14 sines. Products stay below 2^29, including the terminator's sum. */
#include "orbit.h"
#include "sine.h"

int32_t orbit_sine(int32_t tenths)
{
    tenths %= 3600;
    if (tenths < 0) { tenths += 3600; }
    int32_t sign = tenths >= 1800 ? -1 : 1;
    tenths %= 1800;
    if (tenths > 900) { tenths = 1800 - tenths; }
    return sign * SINE[tenths];
}
int32_t orbit_x(int32_t longitude)
{
    int32_t x = (longitude + 18000) * MAP_WIDTH / 36000;
    return x < 0 ? 0 : x >= MAP_WIDTH ? MAP_WIDTH - 1 : x;
}
int32_t orbit_y(int32_t latitude)
{
    int32_t y = (9000 - latitude) * MAP_HEIGHT / 18000;
    return y < 0 ? 0 : y >= MAP_HEIGHT ? MAP_HEIGHT - 1 : y;
}
bool orbit_night(int32_t latitude, int32_t longitude, int32_t solar_latitude, int32_t solar_longitude)
{
    int32_t sin_lat = orbit_sine(latitude / 10), cos_lat = orbit_sine(latitude / 10 + 900);
    int32_t sin_sun = orbit_sine(solar_latitude / 10), cos_sun = orbit_sine(solar_latitude / 10 + 900);
    int32_t cos_delta = orbit_sine((longitude - solar_longitude) / 10 + 900);
    return sin_lat * sin_sun + (cos_lat * cos_sun / 16384) * cos_delta < 0;
}

static int32_t wrap_column(int32_t column)
{
    if (column < 0) { return column + MAP_WIDTH; }
    if (column >= MAP_WIDTH) { return column - MAP_WIDTH; }
    return column;
}

static int32_t first_night(const int32_t cosine[MAP_WIDTH], int32_t peak,
                           int32_t distance, int32_t direction, int32_t threshold)
{
    int32_t daylight = 0, night = distance;
    while (night - daylight > 1) {
        int32_t middle = (daylight + night) / 2;
        if (cosine[wrap_column(peak + direction * middle)] < threshold) { night = middle; }
        else { daylight = middle; }
    }
    return wrap_column(peak + direction * night);
}

void orbit_shade_build(orbit_shade_t *shade, int32_t solar_latitude, int32_t solar_longitude)
{
    int32_t cosine[MAP_WIDTH], peak = 0, trough = 0;
    int32_t sun_sine = orbit_sine(solar_latitude / 10);
    int32_t sun_cosine = orbit_sine(solar_latitude / 10 + 900);
    for (int32_t x = 0; x < MAP_WIDTH; x++) {
        cosine[x] = orbit_sine((x * 36000 / MAP_WIDTH - 18000 - solar_longitude) / 10 + 900);
        if (cosine[x] > cosine[peak]) { peak = x; }
        if (cosine[x] < cosine[trough]) { trough = x; }
    }
    /* Along either arc from noon to midnight, cosine is monotonic. Searching the
     * integer dot-product threshold preserves the original pixel boundary. */
    for (int32_t y = 0; y < MAP_HEIGHT; y++) {
        int32_t latitude = (9000 - y * 18000 / MAP_HEIGHT) / 10;
        int32_t offset = orbit_sine(latitude) * sun_sine;
        int32_t factor = orbit_sine(latitude + 900) * sun_cosine / 16384;
        if (!factor) {
            shade->rows[y] = (orbit_span_t){ 0, offset < 0 ? MAP_WIDTH : 0 };
            continue;
        }
        int32_t threshold = -offset / factor;
        if (-offset % factor > 0) { threshold++; }
        if (cosine[peak] < threshold) {
            shade->rows[y] = (orbit_span_t){ 0, MAP_WIDTH };
        } else if (cosine[trough] >= threshold) {
            shade->rows[y] = (orbit_span_t){ 0, 0 };
        } else {
            int32_t start = first_night(cosine, peak, wrap_column(trough - peak), 1, threshold);
            int32_t end = first_night(cosine, peak, wrap_column(peak - trough), -1, threshold);
            shade->rows[y] = (orbit_span_t){ (uint16_t)start, (uint16_t)(wrap_column(end - start) + 1) };
        }
    }
}

bool orbit_shade_at(const orbit_shade_t *shade, int32_t x, int32_t y)
{
    if (x < 0 || x >= MAP_WIDTH || y < 0 || y >= MAP_HEIGHT) { return false; }
    return wrap_column(x - shade->rows[y].start) < shade->rows[y].count;
}

static int32_t overlap_width(int32_t first, int32_t end, int32_t left, int32_t right)
{
    int32_t start = first > left ? first : left;
    int32_t finish = end < right ? end : right;
    return finish > start ? finish - start : 0;
}

bool orbit_shade_majority(const orbit_shade_t *shade, int32_t x, int32_t y, int32_t width, int32_t height)
{
    if (x < 0 || y < 0 || width <= 0 || height <= 0 || x > MAP_WIDTH - width || y > MAP_HEIGHT - height) {
        return false;
    }
    int32_t pixels = 0;
    for (int32_t row = y; row < y + height; row++) {
        int32_t start = shade->rows[row].start, end = start + shade->rows[row].count;
        pixels += overlap_width(start, end, x, x + width);
        if (end > MAP_WIDTH) { pixels += overlap_width(0, end - MAP_WIDTH, x, x + width); }
    }
    return pixels * 2 > width * height;
}

static void invert_span(uint8_t *row, int32_t start, int32_t end)
{
    while (start < end && start % 8) {
        row[start / 8] ^= (uint8_t)(0x80 >> (start % 8));
        start++;
    }
    while (start + 8 <= end) { row[start / 8] ^= 0xff; start += 8; }
    while (start < end) {
        row[start / 8] ^= (uint8_t)(0x80 >> (start % 8));
        start++;
    }
}

void orbit_shade_invert(const orbit_shade_t *shade, uint8_t pixels[MAP_WIDTH * MAP_HEIGHT / 8])
{
    for (int32_t y = 0; y < MAP_HEIGHT; y++) {
        int32_t start = shade->rows[y].start, end = start + shade->rows[y].count;
        uint8_t *row = pixels + y * (MAP_WIDTH / 8);
        invert_span(row, start, end < MAP_WIDTH ? end : MAP_WIDTH);
        if (end > MAP_WIDTH) { invert_span(row, 0, end - MAP_WIDTH); }
    }
}
bool orbit_track_add(orbit_track_t *track, int32_t minute, int32_t latitude, int32_t longitude)
{
    if (track->count) {
        int32_t last = (track->next + TRACK_POINTS - 1) % TRACK_POINTS;
        if (minute < track->points[last].minute) { return false; }
        if (minute == track->points[last].minute) {
            bool changed = track->points[last].latitude != latitude || track->points[last].longitude != longitude;
            track->points[last] = (orbit_point_t){ minute, (int16_t)latitude, (int16_t)longitude };
            return changed;
        }
    }
    while (track->count) {
        int32_t first = (track->next + TRACK_POINTS - track->count) % TRACK_POINTS;
        if (track->points[first].minute > minute - TRACK_POINTS) { break; }
        track->count--;
    }
    track->points[track->next] = (orbit_point_t){ minute, (int16_t)latitude, (int16_t)longitude };
    track->next = (track->next + 1) % TRACK_POINTS;
    if (track->count < TRACK_POINTS) { track->count++; }
    return true;
}

bool orbit_track_sample(orbit_track_t *track, int32_t minute, int32_t latitude, int32_t longitude, int32_t spacing)
{
    if (track->count) {
        int32_t last = (track->next + TRACK_POINTS - 1) % TRACK_POINTS;
        if (minute - track->points[last].minute < spacing) { return false; }
    }
    return orbit_track_add(track, minute, latitude, longitude);
}
