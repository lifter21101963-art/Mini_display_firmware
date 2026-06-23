#include "delta_timing.h"

#include <Arduino.h>
#include <math.h>
#include <esp_heap_caps.h>

namespace delta_timing
{
namespace
{
constexpr size_t LIVE_DELTA_MAX_SAMPLES = 8192;
constexpr float DELTA_SMOOTHING_ALPHA = 0.15f;
constexpr uint32_t REFERENCE_LAP_MIN_MS = 30000UL;
constexpr float REFERENCE_LAP_MIN_DISTANCE = 500.0f;

struct LapSample
{
    float distance;
    uint32_t elapsedMs;
};

LapSample *currentLapSamples = nullptr;
LapSample *referenceLapSamples = nullptr;
size_t currentLapSampleCount = 0;
size_t referenceLapSampleCount = 0;
float currentLapDistance = 0.0f;
float currentSmoothedDelta = 0.0f;
uint32_t currentLapStartMs = 0;
bool haveSmoothedDelta = false;
bool haveLastTrackPosition = false;
float lastTrackX = 0.0f;
float lastTrackY = 0.0f;
float lastTrackZ = 0.0f;
bool liveDeltaArmed = false;
bool buffersReady = false;
bool buffersAttempted = false;
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

void copyDeltaHistory(DeltaSnapshot &snapshot)
{
    for (int i = 0; i < DELTA_HISTORY_SLOTS; ++i)
    {
        snapshot.historyColors[i] = deltaHistoryColors[i];
        snapshot.historyValid[i] = deltaHistoryValid[i];
    }
}

bool ensureBuffers()
{
    if (buffersReady)
    {
        return true;
    }

    if (buffersAttempted)
    {
        return false;
    }
    buffersAttempted = true;

    const size_t bufferBytes = LIVE_DELTA_MAX_SAMPLES * sizeof(LapSample);

    currentLapSamples = static_cast<LapSample *>(heap_caps_malloc(bufferBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (currentLapSamples == nullptr)
    {
        currentLapSamples = static_cast<LapSample *>(heap_caps_malloc(bufferBytes, MALLOC_CAP_8BIT));
    }

    referenceLapSamples = static_cast<LapSample *>(heap_caps_malloc(bufferBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (referenceLapSamples == nullptr)
    {
        referenceLapSamples = static_cast<LapSample *>(heap_caps_malloc(bufferBytes, MALLOC_CAP_8BIT));
    }

    buffersReady = (currentLapSamples != nullptr && referenceLapSamples != nullptr);
    if (!buffersReady)
    {
        if (currentLapSamples != nullptr)
        {
            heap_caps_free(currentLapSamples);
            currentLapSamples = nullptr;
        }
        if (referenceLapSamples != nullptr)
        {
            heap_caps_free(referenceLapSamples);
            referenceLapSamples = nullptr;
        }
    }

    return buffersReady;
}

void resetCurrentLapTracking()
{
    currentLapSampleCount = 0;
    currentLapDistance = 0.0f;
    haveLastTrackPosition = false;
    currentLapStartMs = millis();
    currentSmoothedDelta = 0.0f;
    haveSmoothedDelta = false;
}

void resetLiveDeltaTracking()
{
    referenceLapSampleCount = 0;
    liveDeltaArmed = false;
    previousCompletedLapTimeMs = -1;
    havePreviousCompletedLapTimeMs = false;
    clearDeltaHistory();
}

void captureCurrentLapAsReference()
{
    if (!ensureBuffers())
    {
        return;
    }

    referenceLapSampleCount = currentLapSampleCount;
    for (size_t i = 0; i < currentLapSampleCount; ++i)
    {
        referenceLapSamples[i] = currentLapSamples[i];
    }
}

void appendLapSample(float distance, uint32_t elapsedMs)
{
    if (!ensureBuffers())
    {
        return;
    }

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
    if (!ensureBuffers())
    {
        return false;
    }

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

    return false;
}
} // namespace

void reset()
{
    ensureBuffers();
    resetLiveDeltaTracking();
    resetCurrentLapTracking();
}

void onLapChange(int lapDelta, int32_t completedLapTimeMs)
{
    if (lapDelta <= 0)
    {
        return;
    }

    if (lapDelta == 1)
    {
        uint32_t lapColor = 0x404040;
        bool lapColorValid = false;
        if (completedLapTimeMs > 0 && havePreviousCompletedLapTimeMs && previousCompletedLapTimeMs > 0)
        {
            float lapDeltaSeconds = ((float)completedLapTimeMs - (float)previousCompletedLapTimeMs) / 1000.0f;
            lapColor = deltaColorToRgb888(lapDeltaSeconds);
            lapColorValid = true;
        }
        shiftDeltaHistoryUp(lapColor, lapColorValid);
        previousCompletedLapTimeMs = completedLapTimeMs;
        havePreviousCompletedLapTimeMs = true;
    }

    uint32_t lapElapsedMs = millis() - currentLapStartMs;
    if (lapDelta == 1 &&
        lapElapsedMs >= REFERENCE_LAP_MIN_MS &&
        currentLapDistance >= REFERENCE_LAP_MIN_DISTANCE &&
        currentLapSampleCount >= 2)
    {
        captureCurrentLapAsReference();
        liveDeltaArmed = true;
    }

    resetCurrentLapTracking();
}

DeltaSnapshot update(float currentX, float currentY, float currentZ)
{
    DeltaSnapshot snapshot{};
    copyDeltaHistory(snapshot);

    if (!ensureBuffers())
    {
        return snapshot;
    }

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
            if (haveSmoothedDelta)
            {
                currentSmoothedDelta = (currentSmoothedDelta * (1.0f - DELTA_SMOOTHING_ALPHA)) + (rawDeltaSeconds * DELTA_SMOOTHING_ALPHA);
            }
            else
            {
                currentSmoothedDelta = rawDeltaSeconds;
                haveSmoothedDelta = true;
            }
            snapshot.deltaSeconds = currentSmoothedDelta;
            snapshot.valid = true;
            snapshot.liveColor = deltaColorToRgb888(snapshot.deltaSeconds);
            snapshot.liveColorValid = true;
        }
    }

    return snapshot;
}
} // namespace delta_timing
