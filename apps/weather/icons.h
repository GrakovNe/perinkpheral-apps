/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * The weather is the host's icons, drawn at three sizes: 88 pixels for the
 * weather now, 44 for the days ahead and 22 for the small print beside the
 * temperature. The host draws each from its finest drawing at that size,
 * in lines of two pixels, as fine as a line of the text beside it.
 */
#pragma once

#include "host_api.h"

#define LARGE_ICON_CELL  8 /* pixels a cell of the icon's eleven: 88 */
#define SMALL_ICON_CELL  4 /* 44 */
#define DETAIL_ICON_CELL 2 /* 22, the size of the text */
#define ICON_SIDE(cell)  (HOST_ICON_CELLS * (cell))

typedef enum {
    ICON_CLEAR,
    ICON_PARTLY_CLOUDY,
    ICON_CLOUDY,
    ICON_RAIN,
    ICON_SNOW,
    ICON_THUNDER,
} icon_t;

/* Every one of them fills its square from top to bottom, so that a row of
 * days does not jump: a cloud with something falling from it, and for an
 * overcast sky two clouds, one behind the other. */
static const int32_t WEATHER_ICONS[] = {
    [ICON_CLEAR] = HOST_ICON_SUN,
    [ICON_PARTLY_CLOUDY] = HOST_ICON_PARTLY_CLOUDY,
    [ICON_CLOUDY] = HOST_ICON_CLOUD,
    [ICON_RAIN] = HOST_ICON_RAIN,
    [ICON_SNOW] = HOST_ICON_SNOW,
    [ICON_THUNDER] = HOST_ICON_THUNDER,
};

/* The small print beside the temperature: what a value is, without a word. */
typedef enum {
    DETAIL_RAIN,
    DETAIL_WIND,
} detail_t;

static const int32_t DETAIL_ICONS[] = {
    [DETAIL_RAIN] = HOST_ICON_UMBRELLA,
    [DETAIL_WIND] = HOST_ICON_WIND,
};
