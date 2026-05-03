#ifndef TELEMETRY_PARSING_H
#define TELEMETRY_PARSING_H

#include "GT7UDPParser.h"

namespace telemetry_parsing
{
struct TelemetryFrame
{
    int tyreTemps[4];
    float fuelLevel;
    float fuelCapacity;
    int lapCount;
    int totalLaps;
    int lastLaptime;
    int preRaceNumCars;
    float position[3];
    float rotation[4];
};

TelemetryFrame parseTelemetryFrame(const Packet &packet);
bool isRaceActive(const TelemetryFrame &frame);
} // namespace telemetry_parsing

#endif
