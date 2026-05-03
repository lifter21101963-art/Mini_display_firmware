#include "telemetry_processor.h"

#include <Arduino.h>

#include "app_ui.h"
#include "delta_timing.h"
#include "fuel_math.h"
#include "telemetry_view.h"

namespace telemetry
{
namespace
{
fuel_math::FuelState fuelState;
int lastLapCount = 0;
bool haveLastLapCount = false;
int32_t previousCompletedLapTimeMs = -1;
bool havePreviousCompletedLapTimeMs = false;
uint32_t deltaHistoryColors[DELTA_HISTORY_SLOTS] = {};
bool deltaHistoryValid[DELTA_HISTORY_SLOTS] = {};

uint32_t deltaColorToRgb888(float deltaSeconds)
{
    constexpr float STRONG_FAST_DELTA = -0.50f;
    constexpr float NEUTRAL_DELTA = 0.0f;
    constexpr float STRONG_SLOW_DELTA = 0.50f;

    uint8_t red = 0;
    uint8_t green = 0;
    uint8_t blue = 0;

    if (deltaSeconds <= STRONG_FAST_DELTA)
    {
        red = 0xB0;
        green = 0x00;
        blue = 0xFF;
    }
    else if (deltaSeconds < NEUTRAL_DELTA)
    {
        float ratio = (deltaSeconds - STRONG_FAST_DELTA) / (NEUTRAL_DELTA - STRONG_FAST_DELTA);
        red = (uint8_t)lroundf((float)0x00 + ((float)0xFF - 0x00) * ratio);
        green = 0xFF;
        blue = (uint8_t)lroundf((float)0x00 + ((float)0xFF - 0x00) * ratio);
    }
    else if (deltaSeconds >= STRONG_SLOW_DELTA)
    {
        red = 0xFF;
        green = 0x00;
        blue = 0x00;
    }
    else
    {
        float ratio = deltaSeconds / STRONG_SLOW_DELTA;
        red = 0xFF;
        green = (uint8_t)lroundf((float)0xFF + ((float)0x00 - 0xFF) * ratio);
        blue = 0x00;
    }

    return ((uint32_t)red << 16) | ((uint32_t)green << 8) | (uint32_t)blue;
}

void clearDeltaHistory()
{
    for (int i = 0; i < DELTA_HISTORY_SLOTS; ++i)
    {
        deltaHistoryColors[i] = 0;
        deltaHistoryValid[i] = false;
    }
}

void shiftDeltaHistoryUp(uint32_t color, bool valid)
{
    for (int i = 0; i < DELTA_HISTORY_SLOTS - 1; ++i)
    {
        deltaHistoryColors[i] = deltaHistoryColors[i + 1];
        deltaHistoryValid[i] = deltaHistoryValid[i + 1];
    }
    deltaHistoryColors[DELTA_HISTORY_SLOTS - 1] = color;
    deltaHistoryValid[DELTA_HISTORY_SLOTS - 1] = valid;
}

bool isRaceFinished(const telemetry_parsing::TelemetryFrame &frame)
{
    return frame.totalLaps > 0 && frame.lapCount >= frame.totalLaps;
}
} // namespace

void resetTelemetryState()
{
    fuel_math::reset(fuelState);
    delta_timing::reset();
    lastLapCount = 0;
    haveLastLapCount = false;
    previousCompletedLapTimeMs = -1;
    havePreviousCompletedLapTimeMs = false;
    clearDeltaHistory();
}

void processTelemetryFrame(const telemetry_parsing::TelemetryFrame &frame)
{
    if (!telemetry_parsing::isRaceActive(frame))
    {
        resetTelemetryState();
        app_ui::clearDeltaDisplay();
        return;
    }

    if (isRaceFinished(frame))
    {
        resetTelemetryState();
        app_ui::clearDeltaDisplay();
        return;
    }

    if (!haveLastLapCount)
    {
        lastLapCount = frame.lapCount;
        haveLastLapCount = true;
        fuel_math::beginLap(fuelState, frame.fuelLevel);
        delta_timing::reset();
    }
    else if (frame.lapCount != lastLapCount)
    {
        int lapDelta = frame.lapCount - lastLapCount;
        if (lapDelta != 0)
        {
            if (lapDelta == 1)
            {
                uint32_t lapColor = 0x404040;
                bool lapColorValid = false;
                if (frame.lastLaptime > 0 && havePreviousCompletedLapTimeMs && previousCompletedLapTimeMs > 0)
                {
                    float lapDeltaSeconds = ((float)frame.lastLaptime - (float)previousCompletedLapTimeMs) / 1000.0f;
                    lapColor = deltaColorToRgb888(lapDeltaSeconds);
                    lapColorValid = true;
                }
                shiftDeltaHistoryUp(lapColor, lapColorValid);
                previousCompletedLapTimeMs = frame.lastLaptime;
                havePreviousCompletedLapTimeMs = true;
            }
            delta_timing::onLapChange(lapDelta);
            fuel_math::onLapChange(fuelState, frame.fuelLevel, lapDelta);
            lastLapCount = frame.lapCount;
        }
    }
    else
    {
        fuel_math::update(fuelState, frame.fuelLevel);
    }

    delta_timing::DeltaSnapshot deltaSnapshot = delta_timing::update(frame.position[0], frame.position[1], frame.position[2]);
    fuel_math::FuelSnapshot fuelSnapshot = fuel_math::evaluate(fuelState, frame.fuelLevel, frame.fuelCapacity);

    TelemetryViewData view{};
    view.tyreTemps[0] = frame.tyreTemps[0];
    view.tyreTemps[1] = frame.tyreTemps[1];
    view.tyreTemps[2] = frame.tyreTemps[2];
    view.tyreTemps[3] = frame.tyreTemps[3];
    view.fuelLevelMapped = fuelSnapshot.fuelLevelMapped;
    view.fuelPerLap = fuelSnapshot.fuelPerLap;
    view.lapOnFuel = fuelSnapshot.lapOnFuel;
    view.fuelLiveEstimate = fuelSnapshot.liveEstimate;
    view.deltaSeconds = deltaSnapshot.deltaSeconds;
    view.deltaValid = deltaSnapshot.valid;
    for (int i = 0; i < DELTA_HISTORY_SLOTS; ++i)
    {
        view.deltaHistoryColors[i] = deltaHistoryColors[i];
        view.deltaHistoryValid[i] = deltaHistoryValid[i];
    }

    if (deltaSnapshot.valid)
    {
        view.liveDeltaColor = deltaColorToRgb888(deltaSnapshot.deltaSeconds);
        view.liveDeltaValid = true;
    }

    app_ui::renderTelemetryDashboard(view);
}
} // namespace telemetry
