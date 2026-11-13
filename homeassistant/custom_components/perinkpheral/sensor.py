# SPDX-License-Identifier: MIT
"""The screen as a sensor: its `screen` attribute is what the device asks for."""
from homeassistant.components.sensor import SensorEntity
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import Event, HomeAssistant, callback
from homeassistant.helpers.entity_platform import AddEntitiesCallback
from homeassistant.helpers.event import async_track_state_change_event

from .const import CONF_LAYOUT, LAYOUT_HERO, SLOTS
from .render import render


async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry,
                            async_add_entities: AddEntitiesCallback) -> None:
    async_add_entities([ScreenSensor(entry)])


class ScreenSensor(SensorEntity):
    """One screen of the device."""

    _attr_should_poll = False
    _attr_icon = "mdi:tablet-dashboard"
    # The text changes with every reading of every entity in it: not history.
    _unrecorded_attributes = frozenset({"screen"})

    def __init__(self, entry: ConfigEntry) -> None:
        self._entry = entry
        self._attr_name = entry.title
        self._attr_unique_id = entry.entry_id
        self._screen = ""

    @property
    def _layout(self) -> str:
        return self._entry.data.get(CONF_LAYOUT, LAYOUT_HERO)

    @property
    def _entities(self) -> list[str]:
        found = [self._entry.options.get(slot + "_entity") for slot in SLOTS[self._layout]]
        return [entity_id for entity_id in found if entity_id]

    @property
    def native_value(self) -> str:
        return "ok" if self._screen else "empty"

    @property
    def extra_state_attributes(self) -> dict:
        return {"screen": self._screen, "layout": self._layout}

    def _put_together(self) -> None:
        self._screen = render(self._layout, self._entry.options, self.hass.states.get)

    async def async_added_to_hass(self) -> None:
        self._put_together()

        @callback
        def changed(event: Event) -> None:
            self._put_together()
            self.async_write_ha_state()

        if self._entities:
            self.async_on_remove(
                async_track_state_change_event(self.hass, self._entities, changed))
