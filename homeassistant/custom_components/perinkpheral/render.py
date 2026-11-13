# SPDX-License-Identifier: MIT
"""A screen as HA Companion reads it: a few lines of text.

Nothing of Home Assistant is imported here: a state is anything with
`state`, `attributes` and `entity_id`, and the test of this file runs
without Home Assistant.
"""
from .const import (LAYOUT_HERO, LAYOUT_LIST, NAME_MAX, PICTURE_AUTO, SCREEN_MAX_BYTES, SLOTS)

NOTHING = "--"
DEGREE = "°"
TEMPERATURE_UNITS = ("°C", "°F", "K")
UNAVAILABLE = ("unavailable", "unknown", "none", "")
# The picture a kind of sensor gets when the user chose none.
PICTURE_OF_CLASS = {
    "temperature": "thermometer", "humidity": "drop", "moisture": "drop",
    "carbon_dioxide": "co2", "pressure": "gauge", "atmospheric_pressure": "gauge",
    "voltage": "plug", "power": "bolt", "energy": "bolt", "current": "bolt",
    "battery": "battery", "data_rate": "network", "wind_speed": "wind",
}
PICTURE_OF_DOMAIN = {"light": "bulb", "binary_sensor": "door", "media_player": "play"}


def ascii_only(text):
    """The device's fonts are ASCII; the degree sign is the one thing it draws besides."""
    return "".join(character if " " <= character <= "~" or character == DEGREE else "?"
                   for character in str(text)).replace("|", "/")


def number_text(value):
    """A number as short as it reads well: no decimals from a hundred up, one below."""
    if abs(value) >= 100:
        return "%d" % round(value)
    text = "%.1f" % value
    return text[:-2] if text.endswith(".0") else text


def value_and_unit(state):
    """What an entity shows: its value and, apart, the unit it is in."""
    if state is None or str(state.state).lower() in UNAVAILABLE:
        return NOTHING, ""
    if state.entity_id.startswith("weather."):
        temperature = state.attributes.get("temperature")
        return (number_text(float(temperature)) if temperature is not None else NOTHING), DEGREE
    unit = state.attributes.get("unit_of_measurement") or ""
    try:
        value = number_text(float(state.state))
    except (TypeError, ValueError):
        return ascii_only(state.state), ""
    return value, DEGREE if unit in TEMPERATURE_UNITS else ascii_only(unit)


def joined(value, unit):
    """A value with its unit in one piece: "22.8°", "34%", "228 V"."""
    if value == NOTHING or not unit:
        return value
    return value + unit if unit in (DEGREE, "%") else "%s %s" % (value, unit)


def picture_of(state, chosen):
    if chosen and chosen != PICTURE_AUTO:
        return "" if chosen == "none" else chosen
    if state is None:
        return ""
    if state.entity_id.startswith("weather."):
        return ascii_only(state.state)  # the device knows the states of weather by name
    found = PICTURE_OF_CLASS.get(state.attributes.get("device_class"))
    return found or PICTURE_OF_DOMAIN.get(state.entity_id.split(".")[0], "")


def name_of(state, entity_id, label):
    if label:
        return ascii_only(label)[:NAME_MAX]
    friendly = state.attributes.get("friendly_name") if state is not None else None
    if friendly and all(" " <= character <= "~" for character in friendly):
        return friendly[:NAME_MAX]
    return entity_id.split(".")[-1].replace("_", " ").capitalize()[:NAME_MAX]


def render(layout, options, state_of):
    """The lines of the screen. `options` holds, for every place, `<place>_entity`,
    `<place>_label` and `<place>_picture`; `state_of(entity_id)` gives a state or None."""
    lines = [] if layout == LAYOUT_HERO else ["layout|" + layout]
    for slot in SLOTS[layout]:
        entity_id = options.get(slot + "_entity")
        if not entity_id:
            continue
        state = state_of(entity_id)
        value, unit = value_and_unit(state)
        picture = "" if layout == LAYOUT_LIST else picture_of(state, options.get(slot + "_picture"))
        name = name_of(state, entity_id, options.get(slot + "_label"))
        if slot == "hero":
            # Two characters are the largest the device has: a whole number.
            whole = value.split(".")[0] if unit == DEGREE else value
            lines.append("hero|%s|%s" % (joined(whole, unit if unit == DEGREE else ""), picture))
        elif slot.startswith("detail"):
            lines.append("detail|%s|%s|%s" % (joined(value, unit if unit == DEGREE else ""),
                                              "" if unit == DEGREE else unit, picture))
        elif slot.startswith("column"):
            lines.append("column|%s|%s|%s" % (name, picture, joined(value, unit)))
        else:
            lines.append("item|%s|%s|%s" % (name, picture, joined(value, unit)))
    screen = "\n".join(lines)
    # Whole lines only: a screen cut in the middle of one would draw half a value.
    while len(screen.encode("utf-8")) > SCREEN_MAX_BYTES and "\n" in screen:
        screen = screen[:screen.rindex("\n")]
    return screen
