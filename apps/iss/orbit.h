// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stdint.h>
enum { TRACK_POINTS = 90, MAP_WIDTH = 400, MAP_HEIGHT = 200 };
typedef struct { int32_t minute; int16_t latitude, longitude; } orbit_point_t;
typedef struct { int32_t count, next; orbit_point_t points[TRACK_POINTS]; } orbit_track_t;
int32_t orbit_sine(int32_t tenths);
int32_t orbit_x(int32_t longitude);
int32_t orbit_y(int32_t latitude);
bool orbit_night(int32_t latitude, int32_t longitude, int32_t solar_latitude, int32_t solar_longitude);
bool orbit_track_add(orbit_track_t *track, int32_t minute, int32_t latitude, int32_t longitude);
