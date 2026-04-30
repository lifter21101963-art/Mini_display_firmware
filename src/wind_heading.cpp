#include "wind_heading.h"

#include <Arduino.h>
#include <math.h>

#include "ui.h"

namespace wind_heading
{
void reset(WindState &state)
{
    state.windDirectionWorld = -1.0f;
    state.calibrated = false;
    state.lastAzimuth = -100;
}

float quaternionToAzimuth(float w, float x, float y, float z)
{
    float siny_cosp = 2.0f * (w * y + x * z);
    float cosy_cosp = 1.0f - 2.0f * (y * y + z * z);
    float yaw = atan2f(siny_cosp, cosy_cosp);
    float deg = yaw * 180.0f / PI;
    if (deg < 0)
    {
        deg += 360.0f;
    }
    return deg;
}

void updateArrow(WindState &state, float carAz)
{
    if (state.calibrated)
    {
        float delta = state.windDirectionWorld - carAz;
        while (delta > 180.0f)
        {
            delta -= 360.0f;
        }
        while (delta < -180.0f)
        {
            delta += 360.0f;
        }
        lv_img_set_angle(ui_Arrow, (int16_t)(delta * 10.0f));
        return;
    }

    if (abs((int)carAz - state.lastAzimuth) >= 1)
    {
        state.lastAzimuth = (int)carAz;
        lv_img_set_angle(ui_Arrow, (int16_t)(carAz * 10.0f));
    }
}

void calibrateWind(WindState &state, float carAz)
{
    state.windDirectionWorld = carAz;
    state.calibrated = true;
    lv_obj_set_style_img_recolor_opa(ui_Arrow, LV_OPA_COVER, 0);
    lv_obj_set_style_img_recolor(ui_Arrow, lv_color_hex(0xffffff), 0);
}
} // namespace wind_heading
