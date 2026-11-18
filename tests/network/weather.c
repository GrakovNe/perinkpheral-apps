// SPDX-License-Identifier: MIT
#include "support.h"
#include "../../apps/weather/weather.c"
static int values[13] = { 12, 1, 5, 1, 2, 3, 15, 16, 17, 8, 9, 10, 20 };
int32_t answer_value(int32_t index, char *out, int32_t capacity)
{
    assert(index >= 0 && index < 13);
    return snprintf(out, capacity, "%d", values[index]);
}
int main(void)
{
    setting("lat", "55.75"); setting("lon", "37.62"); setting("units", "celsius");
    app_on_load(); app_on_picked(0, 200);
    assert(has_forecast && forecast.temperature == 12);
    int first_writes = writes;
    for (int poll = 0; poll < 96; poll++) app_on_picked(0, 200);
    assert(writes == first_writes);
    setting("units", "fahrenheit"); app_on_settings();
    assert(has_forecast && shown_degrees(forecast.temperature) == 54);
    setting("header_city", "0"); app_on_settings();
    setting("every", "1 hour"); app_on_settings();
    app_on_picked(0, HOST_FAILED_NO_NETWORK);
    assert(has_forecast && forecast.temperature == 12);
    setting("lat", "40.7"); app_on_settings();
    assert(!has_forecast && !record("forecast", 8)->size);
    app_on_picked(0, 200);
    assert(has_forecast && same_place());
    values[0] = 13; write_result = HOST_ERR_NO_SPACE; app_on_picked(0, 200);
    assert(forecast_dirty);
    write_result = HOST_OK; app_on_picked(0, 200);
    assert(!forecast_dirty);
    forecast = (forecast_t){0}; has_forecast = false; app_on_load();
    assert(has_forecast && forecast.temperature == 13);
    record_t *cache = record("forecast", 8);
    ((forecast_t *)cache->data)->version = PREVIOUS_FORECAST_VERSION;
    cache->size = offsetof(forecast_t, lat);
    app_on_load(); assert(has_forecast && forecast.version == FORECAST_VERSION && same_place());
    setting("lon", "-74.0"); app_on_load();
    /* A migrated old cache uses its current settings; new caches are source-bound. */
    app_on_picked(0, 200); setting("lon", "0"); app_on_load(); assert(!has_forecast);
    puts("weather: offline settings, source binding, unchanged polls, failed save retry and migration passed");
}
