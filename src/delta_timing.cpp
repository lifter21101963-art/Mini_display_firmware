#include "delta_timing.h"

#include <Arduino.h>
#include <math.h>

namespace delta_timing
{
namespace
{
constexpr size_t LIVE_DELTA_MAX_SAMPLES = 2048;
constexpr float DELTA_SMOOTHING_ALPHA = 0.15f;

struct LapSample
{
    float distance;
    uint32_t elapsedMs;
};

LapSample currentLapSamples[LIVE_DELTA_MAX_SAMPLES];
LapSample referenceLapSamples[LIVE_DELTA_MAX_SAMPLES];
size_t currentLapSampleCount = 0;
size_t referenceLapSampleCount = 0;
float currentLapDistance = 0.0f;
float currentSmoothedDelta = 0.0f;
uint32_t currentLapStartMs = 0;
bool haveLastTrackPosition = false;
float lastTrackX = 0.0f;
float lastTrackY = 0.0f;
float lastTrackZ = 0.0f;
bool liveDeltaArmed = false;

void resetCurrentLapTracking()
{
    currentLapSampleCount = 0;
    currentLapDistance = 0.0f;
    haveLastTrackPosition = false;
    currentLapStartMs = millis();
    currentSmoothedDelta = 0.0f;
}

void resetLiveDeltaTracking()
{
    referenceLapSampleCount = 0;
    liveDeltaArmed = false;
}

void captureCurrentLapAsReference()
{
    referenceLapSampleCount = currentLapSampleCount;
    for (size_t i = 0; i < currentLapSampleCount; ++i)
    {
        referenceLapSamples[i] = currentLapSamples[i];
    }
}

void appendLapSample(float distance, uint32_t elapsedMs)
{
    if (currentLapSampleCount >= LIVE_DELTA_MAX_SAMPLES)
    {
        return;
    }

    if (currentLapSampleCount > 0)
    {
        const LapSample &lastSample = currentLapSamples[currentLapSampleCount - 1];
        if ((distance - lastSample.distance) < 1.0f && (elapsedMs - lastSample.elapsedMs) < 100)
        {
            return;
        }
    }

    currentLapSamples[currentLapSampleCount++] = {distance, elapsedMs};
}

bool getReferenceTimeAtDistance(float distance, float &referenceTimeMs)
{
    if (referenceLapSampleCount < 2)
    {
        return false;
    }

    if (distance <= referenceLapSamples[0].distance)
    {
        referenceTimeMs = (float)referenceLapSamples[0].elapsedMs;
        return true;
    }

    for (size_t i = 1; i < referenceLapSampleCount; ++i)
    {
        const LapSample &prev = referenceLapSamples[i - 1];
        const LapSample &next = referenceLapSamples[i];

        if (distance <= next.distance)
        {
            float span = next.distance - prev.distance;
            if (span <= 0.001f)
            {
                referenceTimeMs = (float)next.elapsedMs;
            }
            else
            {
                float ratio = (distance - prev.distance) / span;
                referenceTimeMs = (float)prev.elapsedMs + ratio * (float)(next.elapsedMs - prev.elapsedMs);
            }
            return true;
        }
    }

    referenceTimeMs = (float)referenceLapSamples[referenceLapSampleCount - 1].elapsedMs;
    return true;
}
} // namespace

void reset()
{
    resetLiveDeltaTracking();
    resetCurrentLapTracking();
}

void onLapChange(int lapDelta)
{
    if (lapDelta <= 0)
    {
        return;
    }

    if (currentLapSampleCount >= 2)
    {
        captureCurrentLapAsReference();
        liveDeltaArmed = true;
    }

    resetCurrentLapTracking();
}

DeltaSnapshot update(float currentX, float currentY, float currentZ)
{
    DeltaSnapshot snapshot{};

    if (!haveLastTrackPosition)
    {
        lastTrackX = currentX;
        lastTrackY = currentY;
        lastTrackZ = currentZ;
        haveLastTrackPosition = true;
    }
    else
    {
        float dx = currentX - lastTrackX;
        float dy = currentY - lastTrackY;
        float dz = currentZ - lastTrackZ;
        float stepDistance = sqrtf(dx * dx + dy * dy + dz * dz);

        if (stepDistance > 0.0f && stepDistance < 100.0f)
        {
            currentLapDistance += stepDistance;
        }

        lastTrackX = currentX;
        lastTrackY = currentY;
        lastTrackZ = currentZ;
    }

    uint32_t currentLapElapsedMs = millis() - currentLapStartMs;
    appendLapSample(currentLapDistance, currentLapElapsedMs);

    if (liveDeltaArmed)
    {
        float referenceTimeMs = 0.0f;
        if (getReferenceTimeAtDistance(currentLapDistance, referenceTimeMs))
        {
            float rawDeltaSeconds = ((float)currentLapElapsedMs - referenceTimeMs) / 1000.0f;
            currentSmoothedDelta = (currentSmoothedDelta * (1.0f - DELTA_SMOOTHING_ALPHA)) + (rawDeltaSeconds * DELTA_SMOOTHING_ALPHA);
            snapshot.deltaSeconds = currentSmoothedDelta;
            snapshot.valid = true;
        }
    }

    return snapshot;
}
} // namespace delta_timing
