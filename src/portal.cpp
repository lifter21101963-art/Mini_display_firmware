#include "portal.h"

#include <Arduino.h>
#include <LV_Helper.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiUdp.h>

#include "app_ui.h"
#include "update_manager.h"
#include "version.h"

namespace portal
{
    namespace
    {
        constexpr unsigned int LOCAL_PORT = 33740;
        constexpr unsigned int REMOTE_PORT = 33739;
        constexpr char HEARTBEAT_MSG = 'A';
        constexpr byte DNS_PORT = 53;

        Preferences preferences;
        WebServer server(80);
        DNSServer dnsServer;
        WiFiUDP discoveryUdp;
        String wifi_ssid;
        String wifi_pass;
        update_manager::UpdateSettings updateSettings;

        const String html_head =
            "<!DOCTYPE html><html><head><meta charset='utf-8'>"
            "<meta name='viewport' content='width=device-width, initial-scale=1'>"
            "<style>"
            "body { background:#000; color:#fff; font-family:Arial,Helvetica,sans-serif; text-align:center; }"
            ".box { margin:30px auto; padding:18px; width:92%; max-width:420px; border:1px solid #444; border-radius:10px; background:#0b0b0b; }"
            "h2 { margin:8px 0 18px 0; }"
            "input { width:86%; padding:10px; margin:8px 0; border-radius:6px; border:1px solid #666; background:#111; color:#fff; font-size:16px; }"
            "button { padding:12px 28px; font-size:16px; border:0; border-radius:6px; background:#1473FF; color:white; }"
            "a { color:#8fb7ff; text-decoration:none; }"
            "</style></head><body>";

        const String html_footer = "</body></html>";

        String escapeHtml(const String &input)
        {
            String output;
            output.reserve(input.length() + 16);
            for (size_t i = 0; i < input.length(); ++i)
            {
                char c = input[i];
                switch (c)
                {
                case '&':
                    output += "&amp;";
                    break;
                case '<':
                    output += "&lt;";
                    break;
                case '>':
                    output += "&gt;";
                    break;
                case '"':
                    output += "&quot;";
                    break;
                case '\'':
                    output += "&#39;";
                    break;
                default:
                    output += c;
                    break;
                }
            }
            return output;
        }

    } // namespace

    void saveWiFiCredentials(const String &ssid, const String &password)
    {
        preferences.begin("wifi", false);
        preferences.putString("ssid", ssid);
        preferences.putString("pass", password);
        preferences.end();
    }

    bool loadWiFiCredentials(String &ssid, String &password)
    {
        preferences.begin("wifi", true);
        ssid = preferences.getString("ssid", "");
        password = preferences.getString("pass", "");
        preferences.end();
        return ssid.length() > 0;
    }

    void handleRoot()
    {
        update_manager::loadSettings(updateSettings);
        if (updateSettings.manifestUrl.length() == 0)
        {
            updateSettings.manifestUrl = update_manager::DEFAULT_UPDATE_SOURCE_URL;
        }
        if (updateSettings.assetName.length() == 0)
        {
            updateSettings.assetName = update_manager::DEFAULT_UPDATE_ASSET_NAME;
        }

        String page = html_head;
        page += "<div class='box'><h2>GT7 Telemetry - Konfiguracja</h2>";
        page += "<p style='font-size:12px;color:#999;margin:0 0 10px 0;'>Wersja firmware: " + String(APP_VERSION) + "</p>";
        page += "<form action='/save' method='POST'>";
        page += "<h3 style='margin:18px 0 10px 0;'>Wi-Fi</h3>";
        page += "<input name='ssid' placeholder='Nazwa sieci Wi-Fi' value='" + escapeHtml(wifi_ssid) + "'><br>";
        page += "<input name='pass' placeholder='Haslo Wi-Fi' type='password' value=''><br>";
        page += "<h3 style='margin:18px 0 10px 0;'>Aktualizacje</h3>";
        page += "<input name='manifest_url' placeholder='URL manifestu lub repo GitHub' value='" + escapeHtml(updateSettings.manifestUrl) + "'><br>";
        page += "<input name='asset_name' placeholder='Nazwa assetu .bin' value='" + escapeHtml(updateSettings.assetName) + "'><br>";
        page += "<label style='display:block;margin:8px 0 12px 0;font-size:14px;'><input type='checkbox' name='auto_update' value='1' ";
        if (updateSettings.enabled)
        {
            page += "checked";
        }
        page += "> Automatycznie sprawdzaj aktualizacje po polaczeniu z Wi-Fi</label>";
        page += "<button type='submit'>Zapisz ustawienia</button>";
        page += "</form>";
        page += "<p style='font-size:12px;color:#999;margin-top:12px;'>Po zapisaniu ESP sie zrestartuje i polaczy z twoja siecia.</p>";
        page += "<p style='font-size:12px;color:#999;margin-top:6px;'>Mozesz wpisac adres repo GitHub, /releases/latest albo URL manifestu.</p>";
        page += "<p style='font-size:12px;color:#999;margin-top:6px;'>Domyslna nazwa assetu OTA to firmware.bin. merged-firmware.bin zostaje tylko do pelnego flashowania.</p>";
        page += "</div>";
        page += html_footer;
        server.send(200, "text/html", page);
    }

    void handleSave()
    {
        String ssid = server.arg("ssid");
        String pass = server.arg("pass");
        String manifestUrl = server.arg("manifest_url");
        String assetName = server.arg("asset_name");
        bool autoUpdateEnabled = server.hasArg("auto_update");

        if (ssid.length() == 0)
        {
            server.send(200, "text/html", html_head + "<div class='box'><h2>Blad: SSID jest puste!</h2></div>" + html_footer);
            return;
        }

        saveWiFiCredentials(ssid, pass);
        updateSettings.enabled = autoUpdateEnabled;
        updateSettings.manifestUrl = manifestUrl;
        updateSettings.assetName = assetName;
        updateSettings.manifestUrl.trim();
        updateSettings.assetName.trim();
        if (updateSettings.manifestUrl.length() == 0)
        {
            updateSettings.manifestUrl = update_manager::DEFAULT_UPDATE_SOURCE_URL;
        }
        if (updateSettings.assetName.length() == 0)
        {
            updateSettings.assetName = update_manager::DEFAULT_UPDATE_ASSET_NAME;
        }
        if (updateSettings.assetName.equalsIgnoreCase("merged-firmware.bin"))
        {
            updateSettings.assetName = update_manager::DEFAULT_UPDATE_ASSET_NAME;
        }
        update_manager::saveSettings(updateSettings);

        String resp = html_head;
        resp += "<div class='box'><h2>Zapisano ustawienia.</h2><p>Restartuje urzadzenie...</p></div>";
        resp += html_footer;
        server.send(200, "text/html", resp);
        delay(1200);
        ESP.restart();
    }

    void startWiFiConfigPortal(LilyGo_Class &amoled)
    {
        WiFi.disconnect(true);
        delay(100);

        WiFi.mode(WIFI_AP);
        WiFi.softAP("GT7_Telemetry_Setup", "12345678");

        IPAddress apIP(192, 168, 4, 1);
        WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));

        app_ui::displayMessage("Tryb konfiguracji Wi-Fi!\nPolacz sie z 'GT7_Telemetry_Setup'", amoled);

        dnsServer.start(DNS_PORT, "*", apIP);
        server.on("/", HTTP_GET, handleRoot);
        server.on("/save", HTTP_POST, handleSave);
        server.begin();

        while (true)
        {
            dnsServer.processNextRequest();
            server.handleClient();
            lv_task_handler();
            delay(5);
        }
    }

    void connectWiFiWithUI(LilyGo_Class &amoled)
    {
        if (!loadWiFiCredentials(wifi_ssid, wifi_pass))
        {
            startWiFiConfigPortal(amoled);
        }

        app_ui::displayMessage("Laczenie z Wi-Fi:\n" + wifi_ssid, amoled);
        WiFi.mode(WIFI_STA);
        WiFi.begin(wifi_ssid.c_str(), wifi_pass.c_str());

        unsigned long startTime = millis();
        unsigned long lastUpdate = 0;
        while (WiFi.status() != WL_CONNECTED && millis() - startTime < 20000)
        {
            delay(300);
            unsigned long now = millis();
            if (now - lastUpdate > 800)
            {
                lastUpdate = now;
                app_ui::displayMessage("Laczenie z Wi-Fi:\n" + wifi_ssid, amoled);
            }
            lv_task_handler();
        }

        if (WiFi.status() == WL_CONNECTED)
        {
            app_ui::displayMessage("Polaczono!\nIP: " + WiFi.localIP().toString(), amoled);
            delay(900);
        }
        else
        {
            app_ui::displayMessage("Blad polaczenia.\nUruchamiam konfiguracje...", amoled);
            delay(500);
            startWiFiConfigPortal(amoled);
        }
    }

    IPAddress discoverPlaystation(LilyGo_Class &amoled)
    {
        discoveryUdp.begin(LOCAL_PORT);
        IPAddress broadcastIP(255, 255, 255, 255);

        discoveryUdp.beginPacket(broadcastIP, REMOTE_PORT);
        discoveryUdp.write(HEARTBEAT_MSG);
        discoveryUdp.endPacket();

        app_ui::displayMessage("Szukam konsoli z GT7...", amoled);

        unsigned long start = millis();
        while (millis() - start < 1500)
        {
            int packetSize = discoveryUdp.parsePacket();
            if (packetSize > 0)
            {
                IPAddress sender = discoveryUdp.remoteIP();
                discoveryUdp.stop();
                app_ui::displayMessage("Znaleziono konsole:\n" + sender.toString(), amoled);
                delay(250);
                return sender;
            }

            delay(5);
        }

        discoveryUdp.stop();
        app_ui::displayMessage("Brak odpowiedzi z konsoli!", amoled);
        delay(1000);
        return IPAddress(0, 0, 0, 0);
    }
} // namespace portal
