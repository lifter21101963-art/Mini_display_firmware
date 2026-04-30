#ifndef WIND_HEADING_H
#define WIND_HEADING_H

#include <lvgl.h>

namespace wind_heading
{
struct WindState
{
    float windDirectionWorld = -1.0f;
    bool calibrated = false;
    int lastAzimuth = -100;
};

void reset(WindState &state);
float quaternionToAzimuth(float w, float x, float y, float z);
void updateArrow(WindState &state, float carAz);
void calibrateWind(WindState &state, float carAz);
} // namespace wind_heading

#endif
