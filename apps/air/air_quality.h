// SPDX-License-Identifier: MIT
// Copyright (c) 2026 GrakovNe
#pragma once

#include <stdint.h>

enum {
    AIR_OZONE, AIR_NO2, AIR_SO2, AIR_CO, AIR_DUST,
    AIR_ALDER, AIR_BIRCH, AIR_GRASS, AIR_MUGWORT, AIR_RAGWEED, AIR_OLIVE,
    AIR_IRRITANTS
};
enum { AIR_MISSING = -1, AIR_TILES = 3, AIR_DECIMAL_SCALE = 10, AIR_BANDS = 4 };

/* Lower bounds of Moderate, High and Very high, in ug/m3 for gases and
 * dust, grains/m3 for pollen. Readings are tenths of these units.
 *
 * O3, NO2, SO2: EEA's revised 2024 bands; Good/Fair become Low,
 * Moderate stays Moderate, Poor becomes High, the last two Very high.
 * Dust uses the same grouping of PM10 as a proxy, not a dust index.
 * https://airindex.eea.europa.eu/AQI/?webgl=0
 * CO: the historical UK four bands (8-hour mean), converted from mg/m3.
 * https://uk-air.defra.gov.uk/reports/cat13/0910081142_AURN_QA_QC_Manual_Sep_09_FINAL.pdf
 * Alder, birch, grass, mugwort, ragweed: MeteoSwiss daily pollen classes.
 * https://www.meteoswiss.admin.ch/dam/jcr:f3d0942c-b3ab-4de6-882e-5df909faed9c/threshold-values-for-pollen-load-classes-of-allergenic-pollen-types.pdf
 * Olive: REA's 50/200 boundaries, with a local Very high class at 400.
 * https://www.uco.es/rea/fenologia/fenologia.html/?page_id=262
 *
 * These comparisons rank current model readings for this dashboard;
 * CO/pollen are not the sources' averaged exposure indices.
 */
static const int32_t AIR_THRESHOLDS[AIR_IRRITANTS][AIR_BANDS - 1] = {
    [AIR_OZONE]   = {   100,   120,   160 },
    [AIR_NO2]     = {    25,    60,   100 },
    [AIR_SO2]     = {    40,   125,   190 },
    [AIR_CO]      = { 11600, 17400, 23200 },
    [AIR_DUST]    = {    45,   120,   195 },
    [AIR_ALDER]   = {    11,    70,   250 },
    [AIR_BIRCH]   = {    11,    70,   300 },
    [AIR_GRASS]   = {    20,    50,   150 },
    [AIR_MUGWORT] = {     6,    15,    50 },
    [AIR_RAGWEED] = {     6,    11,    40 },
    [AIR_OLIVE]   = {    50,   200,   400 }
};

static inline int32_t air_band(int32_t irritant, int32_t value)
{
    int32_t band = 0;
    while (band < AIR_BANDS - 1 && value >= AIR_THRESHOLDS[irritant][band] * AIR_DECIMAL_SCALE) {
        band++;
    }
    return band;
}

static inline int32_t air_band_start(int32_t irritant, int32_t band)
{
    return band == 0 ? 0 : AIR_THRESHOLDS[irritant][band - 1] * AIR_DECIMAL_SCALE;
}

static inline int32_t air_band_span(int32_t irritant, int32_t band)
{
    /* Very high has no next threshold: compare excess over its start in
     * units of the preceding band's width, without capping it. */
    if (band == AIR_BANDS - 1) { band--; }
    return AIR_THRESHOLDS[irritant][band] * AIR_DECIMAL_SCALE - air_band_start(irritant, band);
}

static inline int air_worse(const int32_t values[AIR_IRRITANTS], int32_t left, int32_t right)
{
    int32_t left_band = air_band(left, values[left]);
    int32_t right_band = air_band(right, values[right]);
    if (left_band != right_band) { return left_band > right_band; }
    /* Compare progress towards the next threshold, not raw concentrations
     * or distances in incompatible units. Exact ties keep service order. */
    int64_t left_progress = values[left] - air_band_start(left, left_band);
    int64_t right_progress = values[right] - air_band_start(right, right_band);
    return left_progress * air_band_span(right, right_band) >
           right_progress * air_band_span(left, left_band);
}

static inline int32_t air_worst(const int32_t values[AIR_IRRITANTS], int32_t selected[AIR_TILES])
{
    int32_t count = 0;
    for (int32_t irritant = 0; irritant < AIR_IRRITANTS; irritant++) {
        if (values[irritant] == AIR_MISSING) { continue; }
        int32_t place = 0;
        while (place < count && !air_worse(values, irritant, selected[place])) { place++; }
        if (place >= AIR_TILES) { continue; }
        if (count < AIR_TILES) { count++; }
        for (int32_t index = count - 1; index > place; index--) { selected[index] = selected[index - 1]; }
        selected[place] = irritant;
    }
    return count;
}
