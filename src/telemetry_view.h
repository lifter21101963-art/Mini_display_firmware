#ifndef TELEMETRY_VIEW_H
#define TELEMETRY_VIEW_H

struct TelemetryViewData
{
    int tyreTemps[4];
    int fuelLevelMapped;
    float fuelPerLap;
    float lapOnFuel;
    bool fuelLiveEstimate;
    float deltaSeconds;
    bool deltaValid;
};

#endif
