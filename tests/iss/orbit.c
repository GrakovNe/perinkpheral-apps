// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../apps/iss/orbit.h"

static void check_shade(void)
{
    const int32_t latitudes[] = { -9000, -2350, -1, 0, 1, 2350, 9000 };
    const int32_t longitudes[] = { -18000, -17999, -8107, 0, 8107, 17999, 18000 };
    orbit_shade_t shade;
    uint8_t pixels[MAP_WIDTH * MAP_HEIGHT / 8];
    for (unsigned latitude = 0; latitude < sizeof latitudes / sizeof *latitudes; latitude++) {
        for (unsigned longitude = 0; longitude < sizeof longitudes / sizeof *longitudes; longitude++) {
            orbit_shade_build(&shade, latitudes[latitude], longitudes[longitude]);
            memset(pixels, 0x5a, sizeof pixels);
            orbit_shade_invert(&shade, pixels);
            for (int32_t y = 0; y < MAP_HEIGHT; y++) {
                for (int32_t x = 0; x < MAP_WIDTH; x++) {
                    bool expected = orbit_night(9000 - y * 18000 / MAP_HEIGHT,
                        x * 36000 / MAP_WIDTH - 18000, latitudes[latitude], longitudes[longitude]);
                    assert(orbit_shade_at(&shade, x, y) == expected);
                    unsigned bit = 0x80u >> (x % 8);
                    assert(((pixels[y * MAP_WIDTH / 8 + x / 8] ^ 0x5a) & bit) == (expected ? bit : 0));
                }
            }
            for (int32_t left = 0; left <= 300; left += 100) {
                int32_t count = 0;
                for (int32_t y = 12; y < 39; y++) {
                    for (int32_t x = left; x < left + 100; x++) {
                        count += orbit_night(9000 - y * 18000 / MAP_HEIGHT,
                            x * 36000 / MAP_WIDTH - 18000, latitudes[latitude], longitudes[longitude]);
                    }
                }
                assert(orbit_shade_majority(&shade, left, 12, 100, 27) == (count * 2 > 2700));
            }
        }
    }
    memset(&shade, 0, sizeof shade);
    shade.rows[0] = (orbit_span_t){ 398, 4 };
    assert(!orbit_shade_majority(&shade, 0, 0, 4, 1)); /* Exactly half is day. */
    assert(orbit_shade_majority(&shade, 0, 0, 3, 1));
    assert(orbit_shade_majority(&shade, 397, 0, 3, 1));
    assert(!orbit_shade_at(&shade, -1, 0));
    assert(!orbit_shade_at(&shade, 400, 0));
}
int main(void)
{
    check_shade();
    assert(orbit_sine(0) == 0 && orbit_sine(900) == 16384);
    assert(orbit_sine(1800) == 0 && orbit_sine(2700) == -16384);
    assert(orbit_sine(-900) == -16384 && orbit_sine(4500) == 16384);
    assert(orbit_x(-18000) == 0 && orbit_x(18000) == 399 && orbit_x(0) == 200);
    assert(orbit_y(9000) == 0 && orbit_y(-9000) == 199 && orbit_y(0) == 100);
    assert(!orbit_night(0, 0, 0, 0));
    assert(orbit_night(0, 18000, 0, 0));
    assert(!orbit_night(9000, 0, 2300, 0));
    assert(orbit_night(-9000, 0, 2300, 0));
    assert(orbit_night(0, 0, 0, 19300) == orbit_night(0, 0, 0, -16700));
    orbit_track_t track = {0};
    for (int32_t minute = 1; minute <= 100; minute++) { assert(orbit_track_add(&track, minute, minute, -minute)); }
    assert(track.count == 90 && track.next == 10);
    assert(track.points[track.next].minute == 11);
    assert(!orbit_track_add(&track, 100, 100, -100));
    assert(orbit_track_add(&track, 100, 101, -99));
    assert(track.count == 90 && track.next == 10);
    assert(!orbit_track_add(&track, 99, 99, -99));
    assert(orbit_track_add(&track, 200, 0, 17990));
    assert(track.count == 1);
    assert(orbit_track_add(&track, 201, 0, -17990));
    assert(track.count == 2);
    orbit_track_t sampled = {0};
    assert(orbit_track_sample(&sampled, 1, 50, -30, 5));
    for (int32_t minute = 2; minute <= 5; minute++) {
        assert(!orbit_track_sample(&sampled, minute, 51, -29, 5));
    }
    assert(orbit_track_sample(&sampled, 6, 52, -28, 5));
    assert(!orbit_track_sample(&sampled, 5, 51, -29, 5));
    assert(sampled.count == 2 && sampled.points[0].latitude == 50);
    assert(orbit_track_sample(&sampled, 96, -20, 40, 5));
    assert(sampled.count == 1 && sampled.points[2].minute == 96);
    puts("ISS orbit: fixed-point projection, terminator and sampled history passed");
}
