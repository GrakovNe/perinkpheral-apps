# SPDX-License-Identifier: MIT
"""What a screen of the device is made of."""

DOMAIN = "perinkpheral"

CONF_LAYOUT = "layout"

LAYOUT_HERO = "hero"
LAYOUT_TILES = "tiles"
LAYOUT_SINGLE = "single"
LAYOUT_LIST = "list"

# The places of every layout, in the order the device fills them.
SLOTS = {
    LAYOUT_HERO: ["hero", "detail_1", "detail_2", "column_1", "column_2", "column_3", "column_4"],
    LAYOUT_TILES: ["tile_1", "tile_2", "tile_3", "tile_4"],
    LAYOUT_SINGLE: ["value"],
    LAYOUT_LIST: ["row_%d" % number for number in range(1, 9)],
}

# The pictures HA Companion draws, by the names it knows them under.
PICTURE_AUTO = "auto"
PICTURES = [
    PICTURE_AUTO, "none", "thermometer", "drop", "co2", "gauge", "plug", "bolt", "chip",
    "network", "battery", "bulb", "door", "home", "moon", "umbrella", "wind", "bell",
    "clock", "check", "warning", "info", "play",
]

# What the device keeps of a screen, in bytes.
SCREEN_MAX_BYTES = 1000
NAME_MAX = 23
