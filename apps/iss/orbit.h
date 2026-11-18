// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stdint.h>
enum { TRACK_POINTS = 90, MAP_WIDTH = 400, MAP_HEIGHT = 200 };
typedef struct { int32_t minute; int16_t latitude, longitude; } orbit_point_t;
typedef struct { int32_t count, next; orbit_point_t points[TRACK_POINTS]; } orbit_track_t;
typedef struct { uint16_t start, count; } orbit_span_t;
typedef struct { orbit_span_t rows[MAP_HEIGHT]; } orbit_shade_t;
void orbit_shade_build(orbit_shade_t *shade, int32_t solar_latitude, int32_t solar_longitude);
bool orbit_shade_at(const orbit_shade_t *shade, int32_t x, int32_t y);
bool orbit_shade_majority(const orbit_shade_t *shade, int32_t x, int32_t y, int32_t width, int32_t height);
void orbit_shade_invert(const orbit_shade_t *shade, uint8_t pixels[MAP_WIDTH * MAP_HEIGHT / 8]);
int32_t orbit_sine(int32_t tenths);
int32_t orbit_x(int32_t longitude);
int32_t orbit_y(int32_t latitude);
bool orbit_night(int32_t latitude, int32_t longitude, int32_t solar_latitude, int32_t solar_longitude);
bool orbit_track_add(orbit_track_t *track, int32_t minute, int32_t latitude, int32_t longitude);
bool orbit_track_sample(orbit_track_t *track, int32_t minute, int32_t latitude, int32_t longitude, int32_t spacing);
