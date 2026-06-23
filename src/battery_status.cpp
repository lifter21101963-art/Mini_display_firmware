#include "battery_status.h"

namespace battery_status
{
namespace
{
constexpr uint16_t BATTERY_PRESENT_MIN_MV = 3000;
constexpr uint16_t BATTERY_EMPTY_MV = 3300;
constexpr uint16_t BATTERY_FULL_MV = 4200;

int voltageToPercent(uint16_t voltageMv)
{
    if (voltageMv <= BATTERY_EMPTY_MV)
    {
        return 0;
    }

    if (voltageMv >= BATTERY_FULL_MV)
    {
        return 100;
    }

    return (int)lroundf(((float)(voltageMv - BATTERY_EMPTY_MV) * 100.0f) / (float)(BATTERY_FULL_MV - BATTERY_EMPTY_MV));
}
} // namespace

BatterySnapshot read(LilyGo_Class &amoled)
{
    BatterySnapshot snapshot{};
    snapshot.voltageMv = amoled.getBattVoltage();
    snapshot.connected = snapshot.voltageMv >= BATTERY_PRESENT_MIN_MV;

    if (!snapshot.connected)
    {
        return snapshot;
    }

    snapshot.charging = amoled.isCharging();
    snapshot.percent = voltageToPercent(snapshot.voltageMv);
    return snapshot;
}
} // namespace battery_status
