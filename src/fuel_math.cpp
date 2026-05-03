#include "fuel_math.h"

#include <Arduino.h>
#include <math.h>

namespace fuel_math
{
namespace
{
constexpr float FUEL_SAMPLE_MIN = 0.10f;
constexpr float FUEL_SAMPLE_MAX = 100.0f;
constexpr float REFUEL_DETECT_THRESHOLD = 2.0f;
constexpr float LIVE_ESTIMATE_MIN_PROGRESS = 0.25f;
constexpr float LIVE_ESTIMATE_MAX_PROGRESS = 0.95f;
constexpr unsigned long FUEL_LAP_MIN_MS = 30000UL;

float roundToTwoDecimals(float value)
{
    return roundf(value * 100.0f) / 100.0f;
}

void clearLapSamples(FuelState &state)
{
    // Keep fuelPerLap so range can use the previous stint average until a new sample is ready.
    state.totalFuelPerLap = 0.0f;
    state.lapSampleCount = 0;
}

void setLapBaseline(FuelState &state, float fuelLevel)
{
    state.lapStartFuel = fuelLevel;
    state.lastFuelLevel = fuelLevel;
    state.lapStartMs = millis();
}

void addLapSample(FuelState &state, float usedPerLap)
{
    state.totalFuelPerLap += usedPerLap;
    state.lapSampleCount++;
    state.fuelPerLap = state.totalFuelPerLap / (float)state.lapSampleCount;
}

void updateAverageLapTime(FuelState &state, unsigned long lapElapsedMs, int lapDelta)
{
    if (lapDelta != 1 || lapElapsedMs < 30000UL)
    {
        return;
    }

    if (state.averageLapMs <= 0.0f)
    {
        state.averageLapMs = (float)lapElapsedMs;
    }
    else
    {
        state.averageLapMs = state.averageLapMs * 0.70f + (float)lapElapsedMs * 0.30f;
    }
}

float getLiveFuelPerLapEstimate(const FuelState &state, float fuelLevel)
{
    if (state.averageLapMs <= 0.0f || state.lapStartFuel < 0.0f)
    {
        return 0.0f;
    }

    unsigned long elapsedMs = millis() - state.lapStartMs;
    float progress = (float)elapsedMs / state.averageLapMs;
    if (progress < LIVE_ESTIMATE_MIN_PROGRESS || progress > LIVE_ESTIMATE_MAX_PROGRESS)
    {
        return 0.0f;
    }

    float used = state.lapStartFuel - fuelLevel;
    if (used <= FUEL_SAMPLE_MIN)
    {
        return 0.0f;
    }

    float estimate = used / progress;
    if (estimate <= FUEL_SAMPLE_MIN || estimate > FUEL_SAMPLE_MAX)
    {
        return 0.0f;
    }

    return estimate;
}
} // namespace

void reset(FuelState &state)
{
    state.lapStartFuel = -1.0f;
    state.lastFuelLevel = -1.0f;
    state.fuelPerLap = 0.0f;
    state.lapStartMs = 0;
    state.averageLapMs = 0.0f;
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

    unsigned long lapElapsedMs = millis() - state.lapStartMs;
    updateAverageLapTime(state, lapElapsedMs, lapDelta);

    if (state.lapStartFuel < 0.0f)
    {
        setLapBaseline(state, fuelAfter);
        return;
    }

    if (state.lastFuelLevel >= 0.0f && fuelAfter > state.lastFuelLevel + REFUEL_DETECT_THRESHOLD)
    {
        clearLapSamples(state);
        setLapBaseline(state, fuelAfter);
        return;
    }

    float used = state.lapStartFuel - fuelAfter;
    if (lapDelta != 1 || lapElapsedMs < FUEL_LAP_MIN_MS || used <= FUEL_SAMPLE_MIN || used > FUEL_SAMPLE_MAX)
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

    float fuelPerLapForDisplay = state.fuelPerLap;
    if (fuelPerLapForDisplay <= 0.01f)
    {
        float liveEstimate = getLiveFuelPerLapEstimate(state, fuelLevel);
        if (liveEstimate > 0.01f)
        {
            fuelPerLapForDisplay = liveEstimate;
            snapshot.liveEstimate = true;
        }
    }

    snapshot.fuelPerLap = roundToTwoDecimals(fuelPerLapForDisplay);
    if (fuelPerLapForDisplay > 0.01f)
    {
        snapshot.lapOnFuel = roundToTwoDecimals(constrain(fuelLevel / fuelPerLapForDisplay, 0.0f, 99.0f));
    }
    else
    {
        snapshot.lapOnFuel = 0.0f;
    }

    return snapshot;
}
} // namespace fuel_math
