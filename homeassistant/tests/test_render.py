# SPDX-License-Identifier: MIT
"""The text of a screen, checked without Home Assistant: python3 tests/test_render.py"""
import importlib.util
import os
import sys
import types

HERE = os.path.dirname(os.path.abspath(__file__))
PACKAGE = os.path.join(HERE, "..", "custom_components", "perinkpheral")


def load(name):
    """A module of the integration without its __init__, which needs Home Assistant."""
    spec = importlib.util.spec_from_file_location("perinkpheral." + name,
                                                  os.path.join(PACKAGE, name + ".py"))
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


sys.modules["perinkpheral"] = types.ModuleType("perinkpheral")
sys.modules["perinkpheral"].__path__ = [PACKAGE]
load("const")
render = load("render").render


class State:
    def __init__(self, entity_id, state, **attributes):
        self.entity_id, self.state, self.attributes = entity_id, state, attributes


STATES = {state.entity_id: state for state in [
    State("weather.home", "cloudy", temperature=2.2, friendly_name="Home"),
    State("sensor.room", "22.85", unit_of_measurement="°C", device_class="temperature",
          friendly_name="Room temperature"),
    State("sensor.humidity", "34.02", unit_of_measurement="%", device_class="humidity"),
    State("sensor.co2", "850", unit_of_measurement="ppm", device_class="carbon_dioxide",
          friendly_name="Углекислый газ"),
    State("sensor.input", "228.0", unit_of_measurement="V", device_class="voltage"),
    State("sensor.fridge", "unavailable", unit_of_measurement="°C"),
    State("binary_sensor.door", "off", friendly_name="Front door"),
]}

failed = 0


def check(got, wanted):
    global failed
    if got != wanted:
        failed += 1
        print("FAIL\n  got:    %r\n  wanted: %r" % (got, wanted))


check(render("hero", {
    "hero_entity": "weather.home",
    "detail_1_entity": "sensor.humidity",
    "detail_2_entity": "sensor.co2",
    "column_1_entity": "sensor.room", "column_1_label": "Room",
    "column_2_entity": "sensor.fridge", "column_2_label": "Fridge",
    "column_3_entity": "sensor.input", "column_3_picture": "none",
}, STATES.get), "\n".join([
    "hero|2°|cloudy",
    "detail|34|%|drop",
    "detail|850|ppm|co2",
    "column|Room|thermometer|22.9°",   # 22.85 as a float is a hair above
    "column|Fridge||--°",
    "column|Input||228 V",
]))

check(render("tiles", {
    "tile_1_entity": "sensor.room",
    "tile_2_entity": "sensor.co2",                # a name that is not ASCII: the entity's own
    "tile_4_entity": "binary_sensor.door",        # the third place is empty
    "tile_4_picture": "door",
}, STATES.get), "\n".join([
    "layout|tiles",
    "item|Room temperature|thermometer|22.9°",
    "item|Co2|co2|850 ppm",
    "item|Front door|door|off",
]))

check(render("single", {"value_entity": "sensor.gone", "value_label": "a|b"}, STATES.get),
      "layout|single\nitem|a/b||--")
check(render("list", {"row_1_entity": "sensor.humidity", "row_1_label": "Humidity",
                      "row_1_picture": "drop"}, STATES.get),
      "layout|list\nitem|Humidity||34%")
check(render("list", {}, STATES.get), "layout|list")

# Never more than the device keeps, and never half a line.
many = {"row_%d_entity" % n: "sensor.room" for n in range(1, 9)}
many.update({"row_%d_label" % n: "x" * 200 for n in range(1, 9)})
long_screen = render("list", many, STATES.get)
check(len(long_screen.encode()) <= 1000 and long_screen.count("\n") == 8, True)

print("%d failed" % failed)
sys.exit(1 if failed else 0)
