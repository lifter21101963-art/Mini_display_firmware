#ifndef DELTA_TIMING_H
#define DELTA_TIMING_H

namespace delta_timing
{
struct DeltaSnapshot
{
    float deltaSeconds = 0.0f;
    bool valid = false;
};

void reset();
void onLapChange(int lapDelta);
DeltaSnapshot update(float currentX, float currentY, float currentZ);
} // namespace delta_timing

#endif
