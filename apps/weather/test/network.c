// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "../weather.c"
#include "standin.h"

static int values[13] = { 12, 1, 5, 1, 2, 3, 15, 16, 17, 8, 9, 10, 20 };

/* Every kv_set the application tried, refused or not. */
static int writes(void) { return standin()->store_writes + standin()->refused_writes; }

/* An answer with `values` in the manifest's pick order, through the SDK's picker. */
static void picked(int32_t status)
{
    char body[320];
    int32_t length = snprintf(body, sizeof body,
        "{\"current\":{\"temperature_2m\":%d,\"weather_code\":%d,\"wind_speed_10m\":%d},"
        "\"daily\":{\"weather_code\":[%d,%d,%d],\"temperature_2m_max\":[%d,%d,%d],"
        "\"temperature_2m_min\":[%d,%d,%d],\"precipitation_probability_max\":[%d]}}",
        values[0], values[1], values[2], values[3], values[4], values[5], values[6], values[7], values[8],
        values[9], values[10], values[11], values[12]);
    standin_picked(0, -1, status, status >= 0 ? body : NULL, status >= 0 ? length : 0);
}
int main(void)
{
    standin_reset();
    standin_handlers.on_picked = app_on_picked;
    standin_set("lat", "55.75"); standin_set("lon", "37.62"); standin_set("units", "celsius");
    app_on_load(); picked(200);
    assert(has_forecast && forecast.temperature == 12);
    int first_writes = writes();
    for (int poll = 0; poll < 96; poll++) picked(200);
    assert(writes() == first_writes);
    standin_set("units", "fahrenheit"); app_on_settings();
    assert(has_forecast && shown_degrees(forecast.temperature) == 54);
    standin_set("header_city", "0"); app_on_settings();
    standin_set("every", "1 hour"); app_on_settings();
    picked(HOST_FAILED_NO_NETWORK);
    assert(has_forecast && forecast.temperature == 12);
    standin_set("lat", "40.7"); app_on_settings();
    assert(!has_forecast && standin_value("forecast", NULL) == NULL);
    picked(200);
    assert(has_forecast && same_place());
    values[0] = 13; standin()->refuse_writes = HOST_ERR_NO_SPACE; picked(200);
    assert(forecast_dirty);
    standin()->refuse_writes = HOST_OK; picked(200);
    assert(!forecast_dirty);
    forecast = (forecast_t){0}; has_forecast = false; app_on_load();
    assert(has_forecast && forecast.temperature == 13);
    /* The cache as the version before this one kept it: shorter, without the place. */
    forecast_t cache;
    int32_t cache_size = 0;
    const uint8_t *kept = standin_value("forecast", &cache_size);
    assert(kept != NULL && cache_size == sizeof cache);
    memcpy(&cache, kept, sizeof cache);
    cache.version = PREVIOUS_FORECAST_VERSION;
    assert(kv_set("forecast", 8, &cache, offsetof(forecast_t, lat)) == HOST_OK);
    app_on_load(); assert(has_forecast && forecast.version == FORECAST_VERSION && same_place());
    standin_set("lon", "-74.0"); app_on_load();
    /* A migrated old cache uses its current settings; new caches are source-bound. */
    picked(200); standin_set("lon", "0"); app_on_load(); assert(!has_forecast);
    puts("weather: offline settings, source binding, unchanged polls, failed save retry and migration passed");
}
