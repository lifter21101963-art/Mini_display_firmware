#ifndef TELEMETRY_VIEW_H
#define TELEMETRY_VIEW_H

#include "delta_timing.h"

constexpr int DELTA_MARKER_COUNT = 6;

struct TelemetryViewData
{
    int tyreTemps[4];
    int fuelLevelMapped;
    float fuelPerLap;
    float lapOnFuel;
    bool fuelLiveEstimate;
    float deltaSeconds;
    bool deltaValid;
    uint32_t deltaHistoryColors[DELTA_HISTORY_SLOTS];
    bool deltaHistoryValid[DELTA_HISTORY_SLOTS];
    uint32_t liveDeltaColor = 0;
    bool liveDeltaValid = false;
};

#endif
