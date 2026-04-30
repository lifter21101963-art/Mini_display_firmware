#ifndef TELEMETRY_PROCESSOR_H
#define TELEMETRY_PROCESSOR_H

#include "telemetry_parsing.h"

namespace telemetry
{
void resetTelemetryState();
void processTelemetryFrame(const telemetry_parsing::TelemetryFrame &frame);
} // namespace telemetry

#endif
