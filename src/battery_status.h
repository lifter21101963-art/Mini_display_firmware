#ifndef BATTERY_STATUS_H
#define BATTERY_STATUS_H

#include <Arduino.h>
#include <LilyGo_AMOLED.h>

namespace battery_status
{
struct BatterySnapshot
{
    bool connected = false;
    bool charging = false;
    uint16_t voltageMv = 0;
    int percent = 0;
};

BatterySnapshot read(LilyGo_Class &amoled);
} // namespace battery_status

#endif
