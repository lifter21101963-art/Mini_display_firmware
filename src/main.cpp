#include <Arduino.h>
#include <LilyGo_AMOLED.h>
#include <LV_Helper.h>
#include "GT7UDPParser.h"
#include "ui.h"

#include "app_ui.h"
#include "delta_timing.h"
#include "portal.h"
#include "telemetry_parsing.h"
#include "telemetry_processor.h"
#include "wind_heading.h"

constexpr int CALIB_BUTTON = 0; // GPIO0 / BOOT
constexpr unsigned long HEARTBEAT_INTERVAL_MS = 500;

unsigned long previousHeartbeatMs = 0;

GT7_UDP_Parser gt7Telem;
LilyGo_Class amoled;
wind_heading::WindState windState;

void setup()
{
    Serial.begin(115200);

    if (!amoled.begin())
    {
        while (1)
        {
            Serial.println("Nie wykryto wyswietlacza!");
            delay(1000);
        }
    }

    beginLvglHelper(amoled);
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_black(), 0);

    app_ui::showSplashScreen(amoled);

    pinMode(CALIB_BUTTON, INPUT_PULLUP);

    if (digitalRead(CALIB_BUTTON) == LOW)
    {
        portal::startWiFiConfigPortal(amoled);
    }

    portal::connectWiFiWithUI(amoled);

    IPAddress psIP;
    while (psIP == IPAddress(0, 0, 0, 0))
    {
        psIP = portal::discoverPlaystation(amoled);
    }

    gt7Telem.begin(psIP);
    gt7Telem.sendHeartbeat();
    ui_init();
    app_ui::initializeDeltaHistory();

    telemetry::resetTelemetryState();
    wind_heading::reset(windState);
}

void loop()
{
    lv_task_handler();
    delay(10);

    Packet packetContent = gt7Telem.readData();
    telemetry_parsing::TelemetryFrame frame = telemetry_parsing::parseTelemetryFrame(packetContent);

    float azimuth = wind_heading::quaternionToAzimuth(
        frame.rotation[0],
        frame.rotation[1],
        frame.rotation[2],
        frame.rotation[3]);

    if (digitalRead(CALIB_BUTTON) == LOW)
    {
        wind_heading::calibrateWind(windState, azimuth);
        app_ui::displayMessage("Wiatr skalibrowany!", amoled);
        delay(500);
    }

    wind_heading::updateArrow(windState, azimuth);
    telemetry::processTelemetryFrame(frame);

    if (millis() - previousHeartbeatMs >= HEARTBEAT_INTERVAL_MS)
    {
        previousHeartbeatMs = millis();
        gt7Telem.sendHeartbeat();
    }
}
