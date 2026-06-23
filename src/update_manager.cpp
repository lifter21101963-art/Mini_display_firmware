#include "update_manager.h"

#include <HTTPClient.h>
#include <Preferences.h>
#include <Update.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "app_ui.h"
#include "version.h"

namespace update_manager
{
namespace
{
constexpr char PREF_NAMESPACE[] = "update";
constexpr char KEY_ENABLED[] = "enabled";
constexpr char KEY_MANIFEST[] = "manifest";
constexpr char KEY_ASSET[] = "asset";
constexpr size_t MAX_MANIFEST_URL_LEN = 240;
constexpr char GITHUB_USER_AGENT[] = "GT7_V2_Project";
constexpr char DEFAULT_GITHUB_ASSET_NAME[] = "merged-firmware.bin";

Preferences preferences;

String trimVersionToken(String value)
{
    value.trim();
    if (value.startsWith("v") || value.startsWith("V"))
    {
        value.remove(0, 1);
    }
    return value;
}

String normalizeReleaseUrl(String url)
{
    url.trim();

    if (url.startsWith("https://api.github.com/repos/") || url.startsWith("http://api.github.com/repos/"))
    {
        return url;
    }

    if (url.startsWith("https://github.com/") || url.startsWith("http://github.com/"))
    {
        int schemeEnd = url.indexOf("://");
        int hostEnd = url.indexOf('/', schemeEnd + 3);
        if (hostEnd < 0)
        {
            return url;
        }

        String path = url.substring(hostEnd + 1);
        int firstSlash = path.indexOf('/');
        if (firstSlash < 0)
        {
            return url;
        }

        int secondSlash = path.indexOf('/', firstSlash + 1);
        String owner = path.substring(0, firstSlash);
        String repo = secondSlash >= 0 ? path.substring(firstSlash + 1, secondSlash) : path.substring(firstSlash + 1);

        if (owner.length() == 0 || repo.length() == 0)
        {
            return url;
        }

        String apiUrl = "https://api.github.com/repos/";
        apiUrl += owner;
        apiUrl += "/";
        apiUrl += repo;
        apiUrl += "/releases/latest";
        return apiUrl;
    }

    return url;
}

String extractValue(const String &text, const String &key)
{
    String quotedKey = "\"" + key + "\"";
    int keyPos = text.indexOf(quotedKey);
    if (keyPos >= 0)
    {
        int colonPos = text.indexOf(':', keyPos + quotedKey.length());
        if (colonPos >= 0)
        {
            int start = colonPos + 1;
            while (start < (int)text.length() && isspace((unsigned char)text[start]))
            {
                ++start;
            }
            bool quoted = start < (int)text.length() && text[start] == '"';
            if (quoted)
            {
                ++start;
            }

            int end = start;
            while (end < (int)text.length())
            {
                char c = text[end];
                if (quoted)
                {
                    if (c == '"')
                    {
                        break;
                    }
                }
                else if (c == ',' || c == '\n' || c == '\r' || c == '}')
                {
                    break;
                }
                ++end;
            }

            String value = text.substring(start, end);
            value.trim();
            return value;
        }
    }

    String kvKey = key + "=";
    int kvPos = text.indexOf(kvKey);
    if (kvPos >= 0)
    {
        int start = kvPos + kvKey.length();
        int end = text.indexOf('\n', start);
        if (end < 0)
        {
            end = text.length();
        }
        String value = text.substring(start, end);
        value.trim();
        return value;
    }

    return "";
}

String extractGithubAssetUrl(const String &text, const String &expectedName)
{
    int searchPos = 0;

    while (true)
    {
        int keyPos = text.indexOf("\"browser_download_url\"", searchPos);
        if (keyPos < 0)
        {
            break;
        }

        int colonPos = text.indexOf(':', keyPos);
        if (colonPos < 0)
        {
            break;
        }

        String assetName;
        int nameKeyPos = text.lastIndexOf("\"name\"", keyPos);
        if (nameKeyPos >= 0)
        {
            int nameColonPos = text.indexOf(':', nameKeyPos);
            if (nameColonPos >= 0 && nameColonPos < keyPos)
            {
                int nameStart = nameColonPos + 1;
                while (nameStart < keyPos && isspace((unsigned char)text[nameStart]))
                {
                    ++nameStart;
                }
                if (nameStart < keyPos && text[nameStart] == '"')
                {
                    ++nameStart;
                    int nameEnd = nameStart;
                    while (nameEnd < keyPos && text[nameEnd] != '"')
                    {
                        ++nameEnd;
                    }
                    assetName = text.substring(nameStart, nameEnd);
                    assetName.trim();
                }
            }
        }

        int start = colonPos + 1;
        while (start < (int)text.length() && isspace((unsigned char)text[start]))
        {
            ++start;
        }

        if (start >= (int)text.length() || text[start] != '"')
        {
            searchPos = colonPos + 1;
            continue;
        }

        ++start;
        int end = start;
        while (end < (int)text.length() && text[end] != '"')
        {
            ++end;
        }

        String candidate = text.substring(start, end);
        candidate.trim();

        if (expectedName.length() == 0 || assetName.equalsIgnoreCase(expectedName))
        {
            return candidate;
        }

        searchPos = end + 1;
    }

    return "";
}

bool readHttpText(const String &url, String &body)
{
    WiFiClient client;
    WiFiClientSecure secureClient;
    HTTPClient http;

    WiFiClient *transport = &client;
    if (url.startsWith("https://"))
    {
        secureClient.setInsecure();
        transport = &secureClient;
    }

    if (!http.begin(*transport, url))
    {
        return false;
    }

    http.setTimeout(15000);
    http.addHeader("User-Agent", GITHUB_USER_AGENT);
    http.addHeader("Accept", "application/vnd.github+json");
    int code = http.GET();
    if (code != HTTP_CODE_OK)
    {
        http.end();
        return false;
    }

    body = http.getString();
    http.end();
    return body.length() > 0;
}

int compareVersions(String current, String remote)
{
    current = trimVersionToken(current);
    remote = trimVersionToken(remote);

    int currentIndex = 0;
    int remoteIndex = 0;

    for (int part = 0; part < 4; ++part)
    {
        int currentValue = 0;
        int remoteValue = 0;

        while (currentIndex < (int)current.length() && isdigit((unsigned char)current[currentIndex]))
        {
            currentValue = currentValue * 10 + (current[currentIndex] - '0');
            ++currentIndex;
        }

        while (remoteIndex < (int)remote.length() && isdigit((unsigned char)remote[remoteIndex]))
        {
            remoteValue = remoteValue * 10 + (remote[remoteIndex] - '0');
            ++remoteIndex;
        }

        if (remoteValue != currentValue)
        {
            return remoteValue > currentValue ? 1 : -1;
        }

        while (currentIndex < (int)current.length() && !isdigit((unsigned char)current[currentIndex]))
        {
            if (current[currentIndex] == '.' || current[currentIndex] == '-' || current[currentIndex] == '_')
            {
                ++currentIndex;
                break;
            }
            ++currentIndex;
        }

        while (remoteIndex < (int)remote.length() && !isdigit((unsigned char)remote[remoteIndex]))
        {
            if (remote[remoteIndex] == '.' || remote[remoteIndex] == '-' || remote[remoteIndex] == '_')
            {
                ++remoteIndex;
                break;
            }
            ++remoteIndex;
        }
    }

    return 0;
}

bool flashFirmware(const String &binUrl, LilyGo_Class &amoled)
{
    WiFiClient client;
    WiFiClientSecure secureClient;
    HTTPClient http;

    WiFiClient *transport = &client;
    if (binUrl.startsWith("https://"))
    {
        secureClient.setInsecure();
        transport = &secureClient;
    }

    if (!http.begin(*transport, binUrl))
    {
        app_ui::displayMessage("Blad: nie moge otworzyc URL", amoled);
        return false;
    }

    http.setTimeout(20000);
    http.addHeader("User-Agent", GITHUB_USER_AGENT);
    int code = http.GET();
    if (code != HTTP_CODE_OK)
    {
        app_ui::displayMessage("Blad pobierania aktualizacji", amoled);
        http.end();
        return false;
    }

    if (!Update.begin(UPDATE_SIZE_UNKNOWN))
    {
        app_ui::displayMessage("Blad: brak miejsca na OTA", amoled);
        http.end();
        return false;
    }

    WiFiClient *stream = http.getStreamPtr();
    size_t written = Update.writeStream(*stream);
    if (!Update.end())
    {
        String msg = "OTA blad: ";
        msg += Update.errorString();
        app_ui::displayMessage(msg, amoled);
        http.end();
        return false;
    }

    if (!Update.isFinished() || written == 0)
    {
        app_ui::displayMessage("OTA nie zostala zakonczona", amoled);
        http.end();
        return false;
    }

    http.end();
    return true;
}
} // namespace

void loadSettings(UpdateSettings &settings)
{
    preferences.begin(PREF_NAMESPACE, true);
    settings.enabled = preferences.getBool(KEY_ENABLED, false);
    settings.manifestUrl = preferences.getString(KEY_MANIFEST, "");
    settings.assetName = preferences.getString(KEY_ASSET, DEFAULT_GITHUB_ASSET_NAME);
    preferences.end();
    settings.manifestUrl.trim();
    settings.assetName.trim();
    if (settings.assetName.length() == 0)
    {
        settings.assetName = DEFAULT_GITHUB_ASSET_NAME;
    }
}

void saveSettings(const UpdateSettings &settings)
{
    preferences.begin(PREF_NAMESPACE, false);
    preferences.putBool(KEY_ENABLED, settings.enabled);
    preferences.putString(KEY_MANIFEST, settings.manifestUrl.substring(0, MAX_MANIFEST_URL_LEN));
    preferences.putString(KEY_ASSET, settings.assetName.substring(0, MAX_MANIFEST_URL_LEN));
    preferences.end();
}

bool checkForUpdate(LilyGo_Class &amoled)
{
    if (WiFi.status() != WL_CONNECTED)
    {
        return false;
    }

    UpdateSettings settings;
    loadSettings(settings);
    if (!settings.enabled)
    {
        return false;
    }

    if (settings.manifestUrl.length() == 0)
    {
        app_ui::displayMessage("Auto-update wlaczony,\nbrak adresu manifestu.", amoled);
        return false;
    }

    app_ui::displayMessage("Sprawdzam aktualizacje...", amoled);

    String sourceUrl = normalizeReleaseUrl(settings.manifestUrl);

    String manifest;
    if (!readHttpText(sourceUrl, manifest))
    {
        app_ui::displayMessage("Brak odpowiedzi\nserwera aktualizacji.", amoled);
        return false;
    }

    String remoteVersion = extractValue(manifest, "version");
    String binUrl = extractValue(manifest, "bin_url");
    String assetName = extractValue(manifest, "asset_name");
    if (binUrl.length() == 0)
    {
        binUrl = extractValue(manifest, "firmware_url");
    }
    if (binUrl.length() == 0)
    {
        binUrl = extractValue(manifest, "url");
    }

    if (remoteVersion.length() == 0 || binUrl.length() == 0)
    {
        remoteVersion = extractValue(manifest, "tag_name");
        if (remoteVersion.length() == 0)
        {
            remoteVersion = extractValue(manifest, "name");
        }
        if (assetName.length() == 0)
        {
            assetName = settings.assetName;
        }
        binUrl = extractGithubAssetUrl(manifest, assetName.length() ? assetName : DEFAULT_GITHUB_ASSET_NAME);
    }

    remoteVersion = trimVersionToken(remoteVersion);
    binUrl.trim();
    assetName.trim();

    if (remoteVersion.length() == 0 || binUrl.length() == 0)
    {
        app_ui::displayMessage("Niepoprawny manifest\naktualizacji.", amoled);
        return false;
    }

    if (compareVersions(APP_VERSION, remoteVersion) >= 0)
    {
        String msg = "Aktualna wersja\n";
        msg += APP_VERSION;
        msg += " jest OK";
        app_ui::displayMessage(msg, amoled);
        return false;
    }

    String msg = "Nowa wersja ";
    msg += remoteVersion;
    msg += "\nPobieram...";
    app_ui::displayMessage(msg, amoled);

    if (!flashFirmware(binUrl, amoled))
    {
        return false;
    }

    app_ui::displayMessage("Aktualizacja OK.\nRestart...", amoled);
    delay(1200);
    ESP.restart();
    return true;
}
} // namespace update_manager
