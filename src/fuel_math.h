#ifndef FUEL_MATH_H
#define FUEL_MATH_H

namespace fuel_math
{
struct FuelState
{
    float lapStartFuel = -1.0f;
    float lastFuelLevel = -1.0f;
    float fuelPerLap = 0.0f;
    float lapSamples[5] = {};
    int lapSampleCount = 0;
    int nextLapSample = 0;
};

struct FuelSnapshot
{
    int fuelLevelMapped = 0;
    float lapOnFuel = 0.0f;
    float fuelPerLap = 0.0f;
};

void reset(FuelState &state);
void beginLap(FuelState &state, float fuelLevel);
void update(FuelState &state, float fuelLevel);
void onLapChange(FuelState &state, float fuelAfter, int lapDelta);
FuelSnapshot evaluate(const FuelState &state, float fuelLevel, float fuelCapacity);
} // namespace fuel_math

#endif
