#ifndef FUEL_MATH_H
#define FUEL_MATH_H

namespace fuel_math
{
struct FuelState
{
    float lapStartFuel = -1.0f;
    float lastFuelLevel = -1.0f;
    float lastStableFuelLevel = -1.0f;
    int suspiciousFuelFrames = 0;
    float fuelPerLap = 0.0f;
    int lapSampleCount = 0;
    unsigned long lapStartMs = 0;
    float averageLapMs = 0.0f;
    bool refueling = false;
};

struct FuelSnapshot
{
    int fuelLevelMapped = 0;
    float lapOnFuel = 0.0f;
    float fuelPerLap = 0.0f;
    bool liveEstimate = false;
};

void reset(FuelState &state);
void beginLap(FuelState &state, float fuelLevel);
void update(FuelState &state, float fuelLevel);
void onLapChange(FuelState &state, float fuelAfter, int lapDelta, int lapTimeMs);
FuelSnapshot evaluate(const FuelState &state, float fuelLevel, float fuelCapacity);
} // namespace fuel_math

#endif
