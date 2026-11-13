/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * The weather, in two drawings of each, both in outline: a large one for
 * the weather now, 44 x 44 cells, and a small one for the days ahead,
 * 22 x 22. '#' is ink. A cell is two pixels in both - the font's own -
 * so a line here is as fine as a line of the text beside it. Right
 * angles only; a curve is an honest staircase.
 */
#pragma once

#include "pictures.h" /* made of the `pictures` folder by tools/mkapp */

#define LARGE_ICON_CELLS 44
#define SMALL_ICON_CELLS 22
#define ICON_CELL_PIXELS 2

typedef enum {
    ICON_CLEAR,
    ICON_PARTLY_CLOUDY,
    ICON_CLOUDY,
    ICON_RAIN,
    ICON_SNOW,
    ICON_THUNDER,
    ICON_COUNT,
} icon_t;

static const uint8_t *const LARGE_ICONS[] = {
    [ICON_CLEAR] = PICTURE_LARGE_CLEAR,
    [ICON_PARTLY_CLOUDY] = PICTURE_LARGE_PARTLY_CLOUDY,
    [ICON_CLOUDY] = PICTURE_LARGE_CLOUDY,
    [ICON_RAIN] = PICTURE_LARGE_RAIN,
    [ICON_SNOW] = PICTURE_LARGE_SNOW,
    [ICON_THUNDER] = PICTURE_LARGE_THUNDER,
};

/* Every small icon fills its cell from top to bottom, so that a row of days
 * does not jump: a cloud with something falling from it, and for an
 * overcast sky two clouds, one behind the other. */
static const uint8_t *const SMALL_ICONS[] = {
    [ICON_CLEAR] = PICTURE_SMALL_CLEAR,
    [ICON_PARTLY_CLOUDY] = PICTURE_SMALL_PARTLY_CLOUDY,
    [ICON_CLOUDY] = PICTURE_SMALL_CLOUDY,
    [ICON_RAIN] = PICTURE_SMALL_RAIN,
    [ICON_SNOW] = PICTURE_SMALL_SNOW,
    [ICON_THUNDER] = PICTURE_SMALL_THUNDER,
};


/* The small print beside the temperature: what a value is, without a word. */
#define DETAIL_ICON_CELLS 11

typedef enum {
    DETAIL_RAIN,
    DETAIL_WIND,
    DETAIL_COUNT,
} detail_t;

static const uint8_t *const DETAIL_ICONS[] = {
    [DETAIL_RAIN] = PICTURE_DETAIL_RAIN,
    [DETAIL_WIND] = PICTURE_DETAIL_WIND,
};
