/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * The pictures a screen may name. The weather's are the weather
 * application's own, in its three sizes; the things of a home are on 11
 * cells like the host's icons and are drawn at any of them.
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

/* ---- the things of a home ---- */

#define THING_CELLS 11

typedef enum {
    THING_GAUGE,
    THING_THERMOMETER,
    THING_BATTERY,
    THING_BULB,
    THING_DOOR,
    THING_HOME,
    THING_MOON,
    THING_PLUG,
    THING_CHIP,
    THING_NETWORK,
    THING_BELL,
    THING_CLOCK,
    THING_CHECK,
    THING_WARNING,
    THING_INFO,
    THING_PLAY,
    THING_COUNT,
} thing_t;

static const uint8_t *const THING_ICONS[] = {
    [THING_GAUGE] = PICTURE_THING_GAUGE,
    [THING_THERMOMETER] = PICTURE_THING_THERMOMETER,
    [THING_BATTERY] = PICTURE_THING_BATTERY,
    [THING_BULB] = PICTURE_THING_BULB,
    [THING_DOOR] = PICTURE_THING_DOOR,
    [THING_HOME] = PICTURE_THING_HOME,
    [THING_MOON] = PICTURE_THING_MOON,
    [THING_PLUG] = PICTURE_THING_PLUG,
    [THING_CHIP] = PICTURE_THING_CHIP,
    [THING_NETWORK] = PICTURE_THING_NETWORK,
    [THING_BELL] = PICTURE_THING_BELL,
    [THING_CLOCK] = PICTURE_THING_CLOCK,
    [THING_CHECK] = PICTURE_THING_CHECK,
    [THING_WARNING] = PICTURE_THING_WARNING,
    [THING_INFO] = PICTURE_THING_INFO,
    [THING_PLAY] = PICTURE_THING_PLAY,
};

/* ---- the same on 22 cells, for what a coarse grid makes rough ---- */

#define FINE_CELLS 22

typedef enum {
    FINE_DROP,
    FINE_CO2,
    FINE_BOLT,
    FINE_COUNT,
} fine_t;

static const uint8_t *const FINE_ICONS[] = {
    [FINE_DROP] = PICTURE_FINE_DROP,
    [FINE_CO2] = PICTURE_FINE_CO2,
    [FINE_BOLT] = PICTURE_FINE_BOLT,
};

/* ---- and one drawn for the hero's place, on the weather's 44 ---- */

#define LARGE_THERMOMETER PICTURE_LARGE_THERMOMETER
