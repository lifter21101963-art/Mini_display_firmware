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

bool isRaceFinished(const telemetry_parsing::TelemetryFrame &frame)
{
    // GT7 reports the current lap number; the final lap is still live when lapCount == totalLaps.
    return frame.totalLaps > 0 && frame.lapCount > frame.totalLaps;
}
} // namespace

void resetTelemetryState()
{
    fuel_math::reset(fuelState);
    delta_timing::reset();
    lastLapCount = 0;
    haveLastLapCount = false;
}

void processTelemetryFrame(const telemetry_parsing::TelemetryFrame &frame)
{
    if (!telemetry_parsing::isRaceActive(frame))
    {
        resetTelemetryState();
        app_ui::renderTelemetryWaiting();
        return;
    }

    if (isRaceFinished(frame))
    {
        resetTelemetryState();
        app_ui::renderTelemetryWaiting();
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
            delta_timing::onLapChange(lapDelta, frame.lastLaptime);
            fuel_math::onLapChange(fuelState, frame.fuelLevel, lapDelta, frame.lastLaptime);
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
        view.deltaHistoryColors[i] = deltaSnapshot.historyColors[i];
        view.deltaHistoryValid[i] = deltaSnapshot.historyValid[i];
    }
    view.liveDeltaColor = deltaSnapshot.liveColor;
    view.liveDeltaValid = deltaSnapshot.liveColorValid;

    app_ui::renderTelemetryDashboard(view);
}
} // namespace telemetry
