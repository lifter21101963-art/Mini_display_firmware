#ifndef DELTA_TIMING_H
#define DELTA_TIMING_H

#include <stdint.h>

constexpr int DELTA_HISTORY_SLOTS = 5;

namespace delta_timing
{
struct DeltaSnapshot
{
    float deltaSeconds = 0.0f;
    bool valid = false;
    uint32_t historyColors[DELTA_HISTORY_SLOTS] = {};
    bool historyValid[DELTA_HISTORY_SLOTS] = {};
    uint32_t liveColor = 0;
    bool liveColorValid = false;
};

void reset();
void onLapChange(int lapDelta, int32_t completedLapTimeMs);
DeltaSnapshot update(float currentX, float currentY, float currentZ);
} // namespace delta_timing

#endif
