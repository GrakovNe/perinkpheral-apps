# SPDX-License-Identifier: MIT
"""perINKpheral: screens for the desk display, put together in Home Assistant.

A screen is a layout and the entities in its places. The integration keeps
it as a sensor whose `screen` attribute is the text the device's HA
Companion application reads - so the device is set up once, with the
sensor's name, and what it shows is changed here.
"""
from homeassistant.config_entries import ConfigEntry
from homeassistant.const import Platform
from homeassistant.core import HomeAssistant

PLATFORMS = [Platform.SENSOR]


async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    await hass.config_entries.async_forward_entry_setups(entry, PLATFORMS)
    entry.async_on_unload(entry.add_update_listener(_reload))
    return True


async def async_unload_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    return await hass.config_entries.async_unload_platforms(entry, PLATFORMS)


async def _reload(hass: HomeAssistant, entry: ConfigEntry) -> None:
    """The places were changed: the sensor is made anew with them."""
    await hass.config_entries.async_reload(entry.entry_id)
