#include "telemetry_sender.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>

namespace telemetry_sender
{
namespace
{
    constexpr const char *PC_TELEMETRY_HOST = "gt7positions.duckdns.org";
    constexpr uint16_t PC_TELEMETRY_PORT = 5005;
    constexpr unsigned long SEND_INTERVAL_MS = 50;

    WiFiUDP udp;
    bool isStarted = false;
    bool hasDestination = false;
    bool hasPosition = false;
    float currentX = 0.0f;
    float currentY = 0.0f;
    float currentZ = 0.0f;
    String deviceMac;
    IPAddress destinationIP;
    unsigned long lastSendMs = 0;

    String formatFloat(float value)
    {
        if (isnan(value) || isinf(value))
        {
            return String("0.000");
        }
        return String(value, 3);
    }

    void sendCurrentPosition()
    {
        if (!hasPosition || WiFi.status() != WL_CONNECTED)
        {
            return;
        }

        if (!hasDestination)
        {
            return;
        }

        String payload;
        payload.reserve(96);
        payload += "{\"x\":";
        payload += formatFloat(currentX);
        payload += ",\"y\":";
        payload += formatFloat(currentY);
        payload += ",\"z\":";
        payload += formatFloat(currentZ);
        payload += ",\"mac\":\"";
        payload += deviceMac;
        payload += "\"";
        payload += "}";

        udp.beginPacket(destinationIP, PC_TELEMETRY_PORT);
        udp.write(reinterpret_cast<const uint8_t *>(payload.c_str()), payload.length());
        udp.endPacket();
    }
} // namespace

void begin()
{
    if (isStarted)
    {
        return;
    }

    deviceMac = WiFi.macAddress();
    hasDestination = WiFi.hostByName(PC_TELEMETRY_HOST, destinationIP);
    udp.begin(0);
    isStarted = true;
}

void updatePosition(float x, float y, float z)
{
    currentX = x;
    currentY = y;
    currentZ = z;
    hasPosition = true;
}

void handle()
{
    if (!isStarted)
    {
        return;
    }

    unsigned long now = millis();
    if (now - lastSendMs >= SEND_INTERVAL_MS)
    {
        lastSendMs = now;
        sendCurrentPosition();
    }
}
} // namespace telemetry_sender
