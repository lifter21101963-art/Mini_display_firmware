#ifndef APP_UI_H
#define APP_UI_H

#include <Arduino.h>
#include <LilyGo_AMOLED.h>

#include "telemetry_view.h"

namespace app_ui
{
void displayMessage(const String &msg, LilyGo_Class &amoled);
void showSplashScreen(LilyGo_Class &amoled);
void renderTelemetryDashboard(const TelemetryViewData &view);
} // namespace app_ui

#endif
