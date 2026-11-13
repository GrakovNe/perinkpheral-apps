# SPDX-License-Identifier: MIT
"""Setting a screen up: its name and layout first, then what stands in every place."""
import voluptuous as vol

from homeassistant.config_entries import ConfigEntry, ConfigFlow, OptionsFlow
from homeassistant.core import callback
from homeassistant.helpers import selector

from .const import (CONF_LAYOUT, DOMAIN, LAYOUT_HERO, LAYOUT_LIST, LAYOUT_SINGLE, LAYOUT_TILES,
                    NAME_MAX, PICTURE_AUTO, PICTURES, SLOTS)

CONF_NAME = "name"
DEFAULT_NAME = "perinkpheral"
LAYOUTS = [LAYOUT_HERO, LAYOUT_TILES, LAYOUT_SINGLE, LAYOUT_LIST]


class PerinkpheralConfigFlow(ConfigFlow, domain=DOMAIN):
    """A new screen."""

    VERSION = 1

    async def async_step_user(self, user_input=None):
        if user_input is not None:
            return self.async_create_entry(title=user_input[CONF_NAME],
                                           data={CONF_LAYOUT: user_input[CONF_LAYOUT]})
        schema = vol.Schema({
            vol.Required(CONF_NAME, default=DEFAULT_NAME): selector.TextSelector(),
            vol.Required(CONF_LAYOUT, default=LAYOUT_HERO): selector.SelectSelector(
                selector.SelectSelectorConfig(options=LAYOUTS, translation_key=CONF_LAYOUT,
                                              mode=selector.SelectSelectorMode.LIST)),
        })
        return self.async_show_form(step_id="user", data_schema=schema)

    @staticmethod
    @callback
    def async_get_options_flow(config_entry: ConfigEntry) -> OptionsFlow:
        return PerinkpheralOptionsFlow()


class PerinkpheralOptionsFlow(OptionsFlow):
    """What stands in the places of the screen's layout."""

    async def async_step_init(self, user_input=None):
        if user_input is not None:
            # A field that was cleared is not in the answer at all: the place is empty.
            return self.async_create_entry(title="", data=user_input)
        layout = self.config_entry.data.get(CONF_LAYOUT, LAYOUT_HERO)
        fields = {}
        for slot in SLOTS[layout]:
            fields[vol.Optional(slot + "_entity")] = selector.EntitySelector()
            if slot != "hero" and not slot.startswith("detail"):
                fields[vol.Optional(slot + "_label")] = selector.TextSelector()
            if layout != LAYOUT_LIST:
                fields[vol.Optional(slot + "_picture", default=PICTURE_AUTO)] = (
                    selector.SelectSelector(selector.SelectSelectorConfig(
                        options=PICTURES, translation_key="picture",
                        mode=selector.SelectSelectorMode.DROPDOWN)))
        schema = self.add_suggested_values_to_schema(vol.Schema(fields),
                                                     self.config_entry.options)
        return self.async_show_form(step_id="init", data_schema=schema)
