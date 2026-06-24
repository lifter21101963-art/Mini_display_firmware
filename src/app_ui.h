#ifndef APP_UI_H
#define APP_UI_H

#include <Arduino.h>
#include <LilyGo_AMOLED.h>

#include "telemetry_view.h"
#include "battery_status.h"

namespace app_ui
{
void displayMessage(const String &msg, LilyGo_Class &amoled);
void showSplashScreen(LilyGo_Class &amoled);
void initializeDeltaHistory();
void clearDeltaDisplay();
void initializeBatteryStatus();
void renderBatteryStatus(const battery_status::BatterySnapshot &battery);
void renderTelemetryWaiting();
void renderTelemetryDashboard(const TelemetryViewData &view);
} // namespace app_ui

#endif
