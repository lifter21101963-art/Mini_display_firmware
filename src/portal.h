#ifndef PORTAL_H
#define PORTAL_H

#include <Arduino.h>
#include <IPAddress.h>
#include <LilyGo_AMOLED.h>

namespace portal
{
void saveWiFiCredentials(const String &ssid, const String &password);
bool loadWiFiCredentials(String &ssid, String &password);
void handleRoot();
void handleSave();
void startWiFiConfigPortal(LilyGo_Class &amoled);
void connectWiFiWithUI(LilyGo_Class &amoled);
IPAddress discoverPlaystation(LilyGo_Class &amoled);
} // namespace portal

#endif
