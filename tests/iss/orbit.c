// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
#include "../../apps/iss/orbit.h"
int main(void)
{
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
