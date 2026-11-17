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
