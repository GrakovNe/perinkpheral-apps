// SPDX-License-Identifier: MIT
// Copyright (c) 2026 GrakovNe
#include <assert.h>
#include <limits.h>
#include <stdio.h>

#include "../air_quality.h"

static void gases_only(int32_t values[AIR_IRRITANTS])
{
    for (int32_t irritant = 0; irritant < AIR_IRRITANTS; irritant++) {
        values[irritant] = irritant < AIR_ALDER ? 0 : AIR_MISSING;
    }
}

int main(void)
{
    int32_t values[AIR_IRRITANTS], selected[AIR_TILES];
    gases_only(values);
    values[AIR_OZONE] = 990;
    values[AIR_NO2] = 200;
    values[AIR_SO2] = 100;
    values[AIR_CO] = 50000;
    assert(air_worst(values, selected) == 3);
    assert(selected[0] == AIR_OZONE && selected[1] == AIR_NO2 && selected[2] == AIR_CO);

    /* A near-threshold Low value loses to even the start of Moderate. */
    values[AIR_GRASS] = 200;
    air_worst(values, selected);
    assert(selected[0] == AIR_GRASS);
    assert(air_band(AIR_GRASS, 199) == 0);
    assert(air_band(AIR_GRASS, 200) == 1);
    assert(air_band(AIR_MUGWORT, 149) == 1);
    assert(air_band(AIR_MUGWORT, 150) == 2);
    assert(air_band(AIR_RAGWEED, 400) == 3);

    /* Different units: compare progress within the band, not raw size.
     * Ozone is halfway through Moderate; NO2 is three quarters through. */
    gases_only(values);
    values[AIR_OZONE] = 1100;
    values[AIR_NO2] = 512;
    values[AIR_CO] = 120000;
    air_worst(values, selected);
    assert(selected[0] == AIR_NO2 && selected[1] == AIR_OZONE && selected[2] == AIR_CO);

    /* Every pollen species can displace the gases; null cannot, zero is
     * a real Low candidate. All missing is safe for an incomplete caller. */
    const int32_t very_high[] = { 2500, 3000, 1500, 500, 400, 4000 };
    for (int32_t pollen = AIR_ALDER; pollen < AIR_IRRITANTS; pollen++) {
        gases_only(values);
        values[pollen] = very_high[pollen - AIR_ALDER];
        air_worst(values, selected);
        assert(selected[0] == pollen);
    }
    for (int32_t irritant = 0; irritant < AIR_IRRITANTS; irritant++) { values[irritant] = AIR_MISSING; }
    assert(air_worst(values, selected) == 0);
    values[AIR_BIRCH] = 0;
    assert(air_worst(values, selected) == 1 && selected[0] == AIR_BIRCH);

    /* At the top there is no next threshold. Normalized excess is
     * uncapped and keeps ordering even at the fixed-point range limit. */
    gases_only(values);
    values[AIR_OZONE] = 2000;
    values[AIR_NO2] = 1200;
    values[AIR_SO2] = 2550;
    air_worst(values, selected);
    assert(selected[0] == AIR_OZONE && selected[1] == AIR_SO2 && selected[2] == AIR_NO2);
    values[AIR_CO] = INT32_MAX;
    air_worst(values, selected);
    assert(selected[0] == AIR_CO);

    /* Exact equality is stable, including all-zero gases. */
    gases_only(values);
    air_worst(values, selected);
    assert(selected[0] == AIR_OZONE && selected[1] == AIR_NO2 && selected[2] == AIR_SO2);
    puts("Air ranking: passed");
    return 0;
}
