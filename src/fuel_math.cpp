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
constexpr float EMA_ALPHA = 0.30f;
constexpr unsigned long FUEL_LAP_MIN_MS = 5000UL;
constexpr float FUEL_DROP_REJECT_RATIO = 0.40f;
constexpr int FUEL_DROP_CONFIRM_FRAMES = 3;

float roundToTwoDecimals(float value)
{
    return roundf(value * 100.0f) / 100.0f;
}

void setLapBaseline(FuelState &state, float fuelLevel)
{
    state.lapStartFuel = fuelLevel;
    state.lastFuelLevel = fuelLevel;
    state.lastStableFuelLevel = fuelLevel;
    state.suspiciousFuelFrames = 0;
    state.lapStartMs = millis();
}

float sanitizeFuelLevel(FuelState &state, float fuelLevel)
{
    if (fuelLevel < 0.0f)
    {
        return fuelLevel;
    }

    if (state.lastStableFuelLevel < 0.0f)
    {
        state.lastStableFuelLevel = fuelLevel;
        state.suspiciousFuelFrames = 0;
        return fuelLevel;
    }

    bool suspiciousDrop = false;
    if (state.lastStableFuelLevel > 0.1f && fuelLevel <= 0.01f)
    {
        suspiciousDrop = true;
    }
    else if (state.lastStableFuelLevel > 0.1f && fuelLevel < state.lastStableFuelLevel * FUEL_DROP_REJECT_RATIO)
    {
        suspiciousDrop = true;
    }

    if (suspiciousDrop)
    {
        if (state.suspiciousFuelFrames + 1 < FUEL_DROP_CONFIRM_FRAMES)
        {
            state.suspiciousFuelFrames++;
            return state.lastStableFuelLevel;
        }

        state.suspiciousFuelFrames = 0;
        state.lastStableFuelLevel = fuelLevel;
        return fuelLevel;
    }

    state.suspiciousFuelFrames = 0;
    state.lastStableFuelLevel = fuelLevel;
    return fuelLevel;
}

void updateAverageLapTime(FuelState &state, unsigned long lapTimeMs)
{
    if (lapTimeMs < FUEL_LAP_MIN_MS)
    {
        return;
    }

    if (state.averageLapMs <= 0.0f)
    {
        state.averageLapMs = (float)lapTimeMs;
    }
    else
    {
        state.averageLapMs = state.averageLapMs * (1.0f - EMA_ALPHA) + (float)lapTimeMs * EMA_ALPHA;
    }
}

void updateConsumptionAverage(FuelState &state, float exactFuelThisLap)
{
    if (exactFuelThisLap <= FUEL_SAMPLE_MIN || exactFuelThisLap > FUEL_SAMPLE_MAX)
    {
        return;
    }

    if (state.lapSampleCount == 0)
    {
        state.fuelPerLap = exactFuelThisLap;
        state.lapSampleCount = 1;
        return;
    }

    state.fuelPerLap = state.fuelPerLap * (1.0f - EMA_ALPHA) + exactFuelThisLap * EMA_ALPHA;
    state.lapSampleCount++;
}

float getLiveEstimate(const FuelState &state, float fuelLevel)
{
    if (state.averageLapMs <= 0.0f || state.lapStartFuel < 0.0f)
    {
        return 0.0f;
    }

    unsigned long elapsedMs = millis() - state.lapStartMs;
    float progress = (float)elapsedMs / state.averageLapMs;
    if (progress < 0.20f || progress > 0.98f)
    {
        return 0.0f;
    }

    float usedSinceLapStart = state.lapStartFuel - fuelLevel;
    if (usedSinceLapStart <= FUEL_SAMPLE_MIN)
    {
        return 0.0f;
    }

    float estimate = usedSinceLapStart / progress;
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
    state.lastStableFuelLevel = -1.0f;
    state.suspiciousFuelFrames = 0;
    state.fuelPerLap = 0.0f;
    state.lapSampleCount = 0;
    state.lapStartMs = 0;
    state.averageLapMs = 0.0f;
    state.refueling = false;
}

void beginLap(FuelState &state, float fuelLevel)
{
    if (fuelLevel > FUEL_SAMPLE_MIN)
    {
        float stableFuelLevel = sanitizeFuelLevel(state, fuelLevel);
        if (stableFuelLevel > FUEL_SAMPLE_MIN)
        {
            setLapBaseline(state, stableFuelLevel);
            state.refueling = false;
        }
    }
}

void update(FuelState &state, float fuelLevel)
{
    if (fuelLevel < 0.0f)
    {
        return;
    }

    fuelLevel = sanitizeFuelLevel(state, fuelLevel);

    if (state.lapStartFuel < 0.0f)
    {
        if (fuelLevel > FUEL_SAMPLE_MIN)
        {
            setLapBaseline(state, fuelLevel);
        }
        return;
    }

    if (fuelLevel > state.lastFuelLevel + REFUEL_DETECT_THRESHOLD)
    {
        state.refueling = true;
        state.lapStartFuel = fuelLevel;
        state.lapStartMs = millis();
        state.lastFuelLevel = fuelLevel;
        return;
    }

    if (state.refueling && fuelLevel <= state.lastFuelLevel)
    {
        state.refueling = false;
        setLapBaseline(state, fuelLevel);
        return;
    }

    state.lastFuelLevel = fuelLevel;
}

void onLapChange(FuelState &state, float fuelAfter, int lapDelta, int lapTimeMs)
{
    if (lapDelta <= 0 || fuelAfter < 0.0f)
    {
        return;
    }

    fuelAfter = sanitizeFuelLevel(state, fuelAfter);

    if (state.lapStartFuel < 0.0f)
    {
        if (fuelAfter > FUEL_SAMPLE_MIN)
        {
            setLapBaseline(state, fuelAfter);
        }
        return;
    }

    if (lapTimeMs <= 0)
    {
        lapTimeMs = (int)(millis() - state.lapStartMs);
    }

    updateAverageLapTime(state, (unsigned long)lapTimeMs);

    if (state.refueling || fuelAfter > state.lastFuelLevel + REFUEL_DETECT_THRESHOLD)
    {
        state.refueling = false;
        setLapBaseline(state, fuelAfter);
        return;
    }

    float exactFuelThisLap = state.lapStartFuel - fuelAfter;
    if (exactFuelThisLap <= FUEL_SAMPLE_MIN || exactFuelThisLap > FUEL_SAMPLE_MAX)
    {
        setLapBaseline(state, fuelAfter);
        return;
    }

    if (lapDelta == 1 && lapTimeMs < (int)FUEL_LAP_MIN_MS)
    {
        setLapBaseline(state, fuelAfter);
        return;
    }

    updateConsumptionAverage(state, exactFuelThisLap / (float)lapDelta);
    setLapBaseline(state, fuelAfter);
}

FuelSnapshot evaluate(const FuelState &state, float fuelLevel, float fuelCapacity)
{
    FuelSnapshot snapshot{};

    if (fuelLevel >= 0.0f && state.lastStableFuelLevel > 0.0f && fuelLevel <= 0.01f)
    {
        fuelLevel = state.lastStableFuelLevel;
    }

    if (fuelCapacity > 0.01f)
    {
        snapshot.fuelLevelMapped = (int)constrain((fuelLevel / fuelCapacity) * 100.0f, 0.0f, 100.0f);
    }
    else if (fuelLevel > 0.01f)
    {
        snapshot.fuelLevelMapped = (int)constrain(fuelLevel, 0.0f, 100.0f);
    }

    float fuelPerLapForDisplay = state.fuelPerLap;
    if (fuelPerLapForDisplay <= FUEL_SAMPLE_MIN)
    {
        float liveEstimate = getLiveEstimate(state, fuelLevel);
        if (liveEstimate > FUEL_SAMPLE_MIN)
        {
            fuelPerLapForDisplay = liveEstimate;
            snapshot.liveEstimate = true;
        }
    }

    snapshot.fuelPerLap = roundToTwoDecimals(fuelPerLapForDisplay);
    if (fuelPerLapForDisplay > FUEL_SAMPLE_MIN)
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
