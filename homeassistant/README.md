# perINKpheral for Home Assistant

Screens for the [perINKpheral](https://github.com/GrakovNe/perinkpheral) desk display,
put together in Home Assistant's own interface: choose a layout, put entities into its
places. No YAML and no templates.

It works with the **HA Companion** application on the device. A screen is a sensor whose
`screen` attribute is the text the application reads; the device asks Home Assistant for
it at the pace set in the application's settings, and on OK.

**Status: written and not yet run inside Home Assistant.** The text of a screen is covered
by `tests/test_render.py`; the forms and the sensor are not tested at all. Tell us what
breaks.

## Install

Copy `custom_components/perinkpheral` into the `custom_components` folder of your Home
Assistant configuration and restart it. (As a custom repository in HACS it needs a
repository of its own, which it does not have yet.)

## Set up

1. **Settings - Devices & services - Add integration - perINKpheral.** Give the screen a
   name and choose a layout:

   | Layout | What it is |
   |---|---|
   | One large value, two details, up to four columns | the weather's composition |
   | Up to four tiles | one, two halves or four quarters |
   | One value | as large as it goes |
   | A list | up to eight names and values |

2. **Configure** on the integration's card: an entity for each place, a name where the
   layout shows one, a picture (Automatic takes it from what kind of sensor it is). An
   empty place is left out.
3. On the device's page, in HA Companion's settings: the server's address, a long-lived
   access token, and as **Sensor** the entity the integration made - `sensor.<name>`.

The names on the device are ASCII: a name in another script is replaced by the entity's
own id unless you type one.

## Several screens

Add the integration again for each. The device shows one - the sensor its settings name.
