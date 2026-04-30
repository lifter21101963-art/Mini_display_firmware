#include "fuel_math.h"

#include <Arduino.h>
#include <math.h>

namespace fuel_math
{
namespace
{
constexpr float FUEL_SAMPLE_MIN = 0.10f;
constexpr float FUEL_SAMPLE_MAX = 20.0f;
constexpr float REFUEL_DETECT_THRESHOLD = 0.25f;
constexpr int FUEL_LAP_SAMPLE_COUNT = 5;

float roundToTwoDecimals(float value)
{
    return roundf(value * 100.0f) / 100.0f;
}

void clearLapSamples(FuelState &state)
{
    for (int i = 0; i < FUEL_LAP_SAMPLE_COUNT; ++i)
    {
        state.lapSamples[i] = 0.0f;
    }
    state.lapSampleCount = 0;
    state.nextLapSample = 0;
}

void setLapBaseline(FuelState &state, float fuelLevel)
{
    state.lapStartFuel = fuelLevel;
    state.lastFuelLevel = fuelLevel;
}

void addLapSample(FuelState &state, float usedPerLap)
{
    state.lapSamples[state.nextLapSample] = usedPerLap;
    state.nextLapSample = (state.nextLapSample + 1) % FUEL_LAP_SAMPLE_COUNT;
    if (state.lapSampleCount < FUEL_LAP_SAMPLE_COUNT)
    {
        state.lapSampleCount++;
    }

    float total = 0.0f;
    for (int i = 0; i < state.lapSampleCount; ++i)
    {
        total += state.lapSamples[i];
    }
    state.fuelPerLap = total / (float)state.lapSampleCount;
}
} // namespace

void reset(FuelState &state)
{
    state.lapStartFuel = -1.0f;
    state.lastFuelLevel = -1.0f;
    state.fuelPerLap = 0.0f;
    clearLapSamples(state);
}

void beginLap(FuelState &state, float fuelLevel)
{
    if (fuelLevel >= 0.0f)
    {
        setLapBaseline(state, fuelLevel);
    }
}

void update(FuelState &state, float fuelLevel)
{
    if (fuelLevel < 0.0f)
    {
        return;
    }

    if (state.lapStartFuel < 0.0f)
    {
        setLapBaseline(state, fuelLevel);
        return;
    }

    if (state.lastFuelLevel >= 0.0f && fuelLevel > state.lastFuelLevel + REFUEL_DETECT_THRESHOLD)
    {
        clearLapSamples(state);
        state.fuelPerLap = 0.0f;
        setLapBaseline(state, fuelLevel);
        return;
    }

    state.lastFuelLevel = fuelLevel;
}

void onLapChange(FuelState &state, float fuelAfter, int lapDelta)
{
    if (lapDelta <= 0)
    {
        return;
    }

    if (fuelAfter < 0.0f)
    {
        return;
    }

    if (state.lapStartFuel < 0.0f)
    {
        setLapBaseline(state, fuelAfter);
        return;
    }

    if (state.lastFuelLevel >= 0.0f && fuelAfter > state.lastFuelLevel + REFUEL_DETECT_THRESHOLD)
    {
        clearLapSamples(state);
        state.fuelPerLap = 0.0f;
        setLapBaseline(state, fuelAfter);
        return;
    }

    float used = state.lapStartFuel - fuelAfter;
    if (used <= FUEL_SAMPLE_MIN || used > FUEL_SAMPLE_MAX)
    {
        setLapBaseline(state, fuelAfter);
        return;
    }

    float usedPerLap = used / (float)lapDelta;
    if (usedPerLap <= FUEL_SAMPLE_MIN || usedPerLap > FUEL_SAMPLE_MAX)
    {
        setLapBaseline(state, fuelAfter);
        return;
    }

    addLapSample(state, usedPerLap);
    setLapBaseline(state, fuelAfter);
}

FuelSnapshot evaluate(const FuelState &state, float fuelLevel, float fuelCapacity)
{
    FuelSnapshot snapshot{};

    if (fuelCapacity > 0.01f)
    {
        snapshot.fuelLevelMapped = (int)constrain((fuelLevel / fuelCapacity) * 100.0f, 0.0f, 100.0f);
    }
    else if (fuelLevel > 0.01f)
    {
        snapshot.fuelLevelMapped = (int)constrain(fuelLevel, 0.0f, 100.0f);
    }

    snapshot.fuelPerLap = roundToTwoDecimals(state.fuelPerLap);
    if (state.fuelPerLap > 0.01f)
    {
        snapshot.lapOnFuel = roundToTwoDecimals(constrain(fuelLevel / state.fuelPerLap, 0.0f, 99.0f));
    }
    else
    {
        snapshot.lapOnFuel = 0.0f;
    }

    return snapshot;
}
} // namespace fuel_math
