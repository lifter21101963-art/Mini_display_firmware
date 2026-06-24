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

bool isHttpsUrl(const String &url)
{
    if (url.length() < 8)
    {
        return false;
    }

    String scheme = url.substring(0, 8);
    scheme.toLowerCase();
    return scheme == "https://";
}

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

    String lowerUrl = url;
    lowerUrl.toLowerCase();

    if (lowerUrl.startsWith("https://api.github.com/repos/") || lowerUrl.startsWith("http://api.github.com/repos/"))
    {
        return url;
    }

    if (lowerUrl.startsWith("https://github.com/") || lowerUrl.startsWith("http://github.com/"))
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

String extractGitHubRepoPath(const String &url)
{
    String normalized = url;
    normalized.trim();

    int schemePos = normalized.indexOf("://");
    int pathStart = schemePos >= 0 ? normalized.indexOf('/', schemePos + 3) : normalized.indexOf('/');
    if (pathStart < 0 || pathStart + 1 >= (int)normalized.length())
    {
        return "";
    }

    String path = normalized.substring(pathStart + 1);
    if (path.startsWith("repos/"))
    {
        path = path.substring(6);
    }

    int firstSlash = path.indexOf('/');
    if (firstSlash < 0)
    {
        return "";
    }

    int secondSlash = path.indexOf('/', firstSlash + 1);
    String owner = path.substring(0, firstSlash);
    String repo = secondSlash >= 0 ? path.substring(firstSlash + 1, secondSlash) : path.substring(firstSlash + 1);
    owner.trim();
    repo.trim();

    if (owner.length() == 0 || repo.length() == 0)
    {
        return "";
    }

    return owner + "/" + repo;
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

String buildGitHubAssetUrl(const String &repoPath, const String &tag, const String &assetName)
{
    if (repoPath.length() == 0 || tag.length() == 0 || assetName.length() == 0)
    {
        return "";
    }

    String url = "https://github.com/";
    url += repoPath;
    url += "/releases/download/";
    url += tag;
    url += "/";
    url += assetName;
    return url;
}

bool readHttpText(const String &url, String &body)
{
    WiFiClient client;
    WiFiClientSecure secureClient;
    HTTPClient http;

    WiFiClient *transport = &client;
    if (isHttpsUrl(url))
    {
        secureClient.setInsecure();
        transport = &secureClient;
    }

    if (!http.begin(*transport, url))
    {
        return false;
    }

    http.setTimeout(30000);
    http.useHTTP10(true);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
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
    if (isHttpsUrl(binUrl))
    {
        secureClient.setInsecure();
        transport = &secureClient;
    }

    if (!http.begin(*transport, binUrl))
    {
        app_ui::displayMessage("Blad: nie moge otworzyc URL", amoled);
        Serial.println("[OTA] http.begin(binUrl) failed");
        return false;
    }

    http.setTimeout(60000);
    http.useHTTP10(true);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.addHeader("User-Agent", GITHUB_USER_AGENT);
    int code = http.GET();
    Serial.print("[OTA] firmware HTTP code: ");
    Serial.println(code);
    if (code != HTTP_CODE_OK)
    {
        String msg = "Blad pobierania:\nHTTP ";
        msg += code;
        app_ui::displayMessage(msg, amoled);
        http.end();
        return false;
    }

    Serial.print("[OTA] content length header: ");
    Serial.println(http.getSize());
    Serial.print("[OTA] final download URL: ");
    Serial.println(binUrl);

    if (!Update.begin((size_t)http.getSize()))
    {
        app_ui::displayMessage("Blad: brak miejsca na OTA", amoled);
        Serial.println("[OTA] Update.begin() failed");
        http.end();
        return false;
    }
    Serial.println("[OTA] Update.begin() ok");

    app_ui::displayMessage("Pobieram OTA...", amoled);
    WiFiClient *stream = http.getStreamPtr();
    size_t totalBytes = (size_t)http.getSize();
    size_t written = 0;
    uint8_t buffer[1024];
    unsigned int lastReportedBucket = 101;

    while (http.connected() && (totalBytes == 0 || written < totalBytes))
    {
        size_t available = stream->available();
        if (available == 0)
        {
            delay(1);
            continue;
        }

        size_t toRead = available;
        if (toRead > sizeof(buffer))
        {
            toRead = sizeof(buffer);
        }

        size_t bytesRead = stream->readBytes(buffer, toRead);
        if (bytesRead == 0)
        {
            delay(1);
            continue;
        }

        size_t bytesWritten = Update.write(buffer, bytesRead);
        written += bytesWritten;

        if (totalBytes > 0)
        {
            unsigned int percent = (unsigned int)((written * 100U) / totalBytes);
            unsigned int bucket = percent / 10U;
            if (percent == 100U || bucket != lastReportedBucket)
            {
                lastReportedBucket = bucket;
                Serial.print("[OTA] progress: ");
                Serial.print(written);
                Serial.print("/");
                Serial.print(totalBytes);
                Serial.print(" (");
                Serial.print(percent);
                Serial.println("%)");

                String msg = "Pobieram OTA...\n";
                msg += percent;
                msg += "%";
                app_ui::displayMessage(msg, amoled);
            }
        }
    }

    Serial.print("[OTA] written bytes: ");
    Serial.println(written);
    Serial.print("[OTA] Update progress finished flag: ");
    Serial.println(Update.isFinished() ? "yes" : "no");
    if (!Update.end())
    {
        String msg = "OTA blad: ";
        msg += Update.errorString();
        app_ui::displayMessage(msg, amoled);
        Serial.print("[OTA] Update.end() failed: ");
        Serial.println(Update.errorString());
        http.end();
        return false;
    }

    if (!Update.isFinished() || written == 0)
    {
        app_ui::displayMessage("OTA nie zostala zakonczona", amoled);
        Serial.print("[OTA] finished: ");
        Serial.println(Update.isFinished() ? "yes" : "no");
        Serial.print("[OTA] error string: ");
        Serial.println(Update.errorString());
        http.end();
        return false;
    }

    Serial.println("[OTA] firmware transfer completed successfully");
    http.end();
    return true;
}
} // namespace

void loadSettings(UpdateSettings &settings)
{
    preferences.begin(PREF_NAMESPACE, true);
    settings.enabled = preferences.getBool(KEY_ENABLED, true);
    settings.manifestUrl = preferences.getString(KEY_MANIFEST, DEFAULT_UPDATE_SOURCE_URL);
    settings.assetName = preferences.getString(KEY_ASSET, DEFAULT_GITHUB_ASSET_NAME);
    preferences.end();
    settings.manifestUrl.trim();
    settings.assetName.trim();
    if (settings.manifestUrl.length() == 0)
    {
        settings.manifestUrl = DEFAULT_UPDATE_SOURCE_URL;
    }
    if (settings.assetName.length() == 0)
    {
        settings.assetName = DEFAULT_GITHUB_ASSET_NAME;
    }
    if (settings.assetName.equalsIgnoreCase("merged-firmware.bin"))
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
        Serial.println("[OTA] WiFi not connected");
        return false;
    }

    UpdateSettings settings;
    loadSettings(settings);
    if (!settings.enabled)
    {
        Serial.println("[OTA] Auto-update disabled");
        return false;
    }

    if (settings.manifestUrl.length() == 0)
    {
        app_ui::displayMessage("Auto-update wlaczony,\nbrak adresu manifestu.", amoled);
        Serial.println("[OTA] Missing manifest URL");
        return false;
    }

    app_ui::displayMessage("Sprawdzam aktualizacje...", amoled);

    String sourceUrl = normalizeReleaseUrl(settings.manifestUrl);
    Serial.print("[OTA] sourceUrl: ");
    Serial.println(sourceUrl);
    String repoPath = extractGitHubRepoPath(sourceUrl);

    String manifest;
    if (!readHttpText(sourceUrl, manifest))
    {
        app_ui::displayMessage("Brak odpowiedzi\nserwera aktualizacji.", amoled);
        Serial.println("[OTA] Manifest download failed");
        return false;
    }
    Serial.print("[OTA] manifest bytes: ");
    Serial.println(manifest.length());

    String remoteVersion = extractValue(manifest, "version");
    String browserDownloadUrl = extractValue(manifest, "browser_download_url");
    String binUrl = extractValue(manifest, "bin_url");
    String manifestAssetName = extractValue(manifest, "asset_name");
    String releaseTag = extractValue(manifest, "tag_name");
    Serial.print("[OTA] version: ");
    Serial.println(remoteVersion);
    Serial.print("[OTA] browser_download_url: ");
    Serial.println(browserDownloadUrl);
    Serial.print("[OTA] bin_url: ");
    Serial.println(binUrl);
    Serial.print("[OTA] tag_name: ");
    Serial.println(releaseTag);
    if (binUrl.length() == 0 && browserDownloadUrl.length() > 0)
    {
        binUrl = browserDownloadUrl;
    }
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
        if (releaseTag.length() == 0)
        {
            releaseTag = extractValue(manifest, "name");
        }
        remoteVersion = releaseTag;
        String expectedAssetName = manifestAssetName.length() > 0 ? manifestAssetName : settings.assetName;
        if (expectedAssetName.length() == 0)
        {
            expectedAssetName = DEFAULT_GITHUB_ASSET_NAME;
        }
        binUrl = buildGitHubAssetUrl(repoPath, releaseTag, expectedAssetName);
        Serial.print("[OTA] derived version from tag_name: ");
        Serial.println(remoteVersion);
    }

    remoteVersion = trimVersionToken(remoteVersion);
    binUrl.trim();
    Serial.print("[OTA] normalized version: ");
    Serial.println(remoteVersion);
    Serial.print("[OTA] final binUrl: ");
    Serial.println(binUrl);

    if (remoteVersion.length() == 0 || binUrl.length() == 0)
    {
        String msg = "Brak assetu:\n";
        msg += settings.assetName.length() > 0 ? settings.assetName : DEFAULT_GITHUB_ASSET_NAME;
        app_ui::displayMessage(msg, amoled);
        Serial.println("[OTA] Missing version or bin URL");
        return false;
    }

    String debugMsg = "Tag: ";
    debugMsg += remoteVersion;
    debugMsg += "\nAsset: ";
    debugMsg += settings.assetName.length() > 0 ? settings.assetName : DEFAULT_GITHUB_ASSET_NAME;
    app_ui::displayMessage(debugMsg, amoled);
    delay(700);
    Serial.print("[OTA] APP_VERSION: ");
    Serial.println(APP_VERSION);

    // compareVersions() returns > 0 when the remote version is newer.
    if (compareVersions(APP_VERSION, remoteVersion) <= 0)
    {
        String msg = "Aktualna wersja\n";
        msg += APP_VERSION;
        msg += " jest OK";
        app_ui::displayMessage(msg, amoled);
        Serial.println("[OTA] Current version is up to date");
        return false;
    }

    String msg = "Nowa wersja ";
    msg += remoteVersion;
    msg += "\nPobieram...";
    app_ui::displayMessage(msg, amoled);

    if (!flashFirmware(binUrl, amoled))
    {
        Serial.println("[OTA] Flash failed");
        return false;
    }

    app_ui::displayMessage("Aktualizacja OK.\nRestart...", amoled);
    Serial.println("[OTA] Flash OK, restarting");
    delay(1200);
    ESP.restart();
    return true;
}
} // namespace update_manager
