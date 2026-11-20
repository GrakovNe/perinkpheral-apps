// SPDX-License-Identifier: MIT
/* The projection uses hundredths of a degree. */
#include "orbit.h"

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
