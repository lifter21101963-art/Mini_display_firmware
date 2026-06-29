#ifndef TELEMETRY_SENDER_H
#define TELEMETRY_SENDER_H

namespace telemetry_sender
{
void begin();
void updatePosition(float x, float y, float z);
void handle();
} // namespace telemetry_sender

#endif
