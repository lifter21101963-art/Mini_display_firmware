#include "telemetry_parsing.h"

#include <Arduino.h>

namespace telemetry_parsing
{
TelemetryFrame parseTelemetryFrame(const Packet &packet)
{
    const GT7Packet &raw = packet.packetContent;
    TelemetryFrame frame{};

    frame.tyreTemps[0] = (int)constrain(raw.tyreTemp[0], 0, 120);
    frame.tyreTemps[1] = (int)constrain(raw.tyreTemp[1], 0, 120);
    frame.tyreTemps[2] = (int)constrain(raw.tyreTemp[2], 0, 120);
    frame.tyreTemps[3] = (int)constrain(raw.tyreTemp[3], 0, 120);

    frame.fuelLevel = raw.fuelLevel;
    frame.fuelCapacity = raw.fuelCapacity;
    frame.lapCount = raw.lapCount;
    frame.preRaceNumCars = raw.preRaceNumCars;

    frame.position[0] = raw.position[0];
    frame.position[1] = raw.position[1];
    frame.position[2] = raw.position[2];

    frame.rotation[0] = raw.rotation[0];
    frame.rotation[1] = raw.rotation[1];
    frame.rotation[2] = raw.rotation[2];
    frame.rotation[3] = raw.orientationRelativeToNorth;

    return frame;
}

bool isRaceActive(const TelemetryFrame &frame)
{
    return frame.preRaceNumCars >= 0;
}
} // namespace telemetry_parsing
