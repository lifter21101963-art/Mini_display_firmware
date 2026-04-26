// GT7 Telemetry + WiFi Config Portal (single-file .ino)
// Integracja: Twój oryginalny kod + portal konfiguracji Wi-Fi (Captive Portal)
// Wymuszenie wejścia do konfiguracji: BOOT (GPIO0) przy starcie

#include <LilyGo_AMOLED.h>
#include <LV_Helper.h>
#include "ui.h"
#include "GT7UDPParser.h"

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>

extern const lv_img_dsc_t logo;

// --- Stałe i zmienne globalne ---
constexpr unsigned int localPort = 33740;
constexpr unsigned int remotePort = 33739;
constexpr char heartbeatMsg = 'A';
unsigned long previousT = 0;
const long interval = 500;

int FLtyretemp = 0, FRtyretemp = 0, RLtyretemp = 0, RRtyretemp = 0;
float FuelLevel = 0;
float LapOnFuel = 0;

// nowe obliczenia paliwa
static float lastLapFuel = -1.0f; // paliwo na końcu poprzedniego okrążenia
static float fuelPerLap = 0.0f;   // wygładzona średnia spalania
static int lastLapCount = 0;
constexpr float FUEL_SAMPLE_MIN = 0.10f;
constexpr float FUEL_SAMPLE_MAX = 20.0f;
constexpr float FUEL_SMOOTHING_ALPHA = 0.30f;
struct LapSample
{
    float distance;
    uint32_t elapsedMs;
};

constexpr size_t LIVE_DELTA_MAX_SAMPLES = 2048;
static LapSample currentLapSamples[LIVE_DELTA_MAX_SAMPLES];
static LapSample referenceLapSamples[LIVE_DELTA_MAX_SAMPLES];
static size_t currentLapSampleCount = 0;
static size_t referenceLapSampleCount = 0;
static float currentLapDistance = 0.0f;
static float smoothedDelta = 0.0f;

constexpr float DELTA_SMOOTHING_ALPHA = 0.15f; // FILTR DELTA - imituje bardziej "płynne" wskazanie, mniej skaczące przy małych różnicach

static uint32_t currentLapStartMs = 0;
static bool haveLastTrackPosition = false;
static float lastTrackX = 0.0f;
static float lastTrackY = 0.0f;
static float lastTrackZ = 0.0f;
static bool liveDeltaArmed = false;

static float roundToTwoDecimals(float value)
{
    return roundf(value * 100.0f) / 100.0f;
}

static void updateFuelPerLapFromLap(float fuelAfter, int lapDelta)
{
    if (lapDelta <= 0)
    {
        return;
    }

    if (lastLapFuel < 0.0f)
    {
        lastLapFuel = fuelAfter;
        return;
    }

    float used = lastLapFuel - fuelAfter;
    if (used <= FUEL_SAMPLE_MIN || used > FUEL_SAMPLE_MAX)
    {
        lastLapFuel = fuelAfter;
        return;
    }

    float usedPerLap = used / (float)lapDelta;
    if (usedPerLap <= FUEL_SAMPLE_MIN || usedPerLap > FUEL_SAMPLE_MAX)
    {
        lastLapFuel = fuelAfter;
        return;
    }

    if (fuelPerLap <= 0.0f)
    {
        fuelPerLap = usedPerLap;
    }
    else
    {
        fuelPerLap = fuelPerLap * (1.0f - FUEL_SMOOTHING_ALPHA) + usedPerLap * FUEL_SMOOTHING_ALPHA;
    }

    fuelPerLap = roundToTwoDecimals(fuelPerLap);
    lastLapFuel = fuelAfter;
}

int last_FL = -100, last_FR = -100, last_RL = -100, last_RR = -100;
int last_az = -100;

GT7_UDP_Parser gt7Telem;
Packet packetContent;
LilyGo_Class amoled;
WiFiUDP discoveryUdp;

lv_obj_t *ui_messageLabel = nullptr;

const int CALIB_BUTTON = 0; // GPIO0 - BOOT (wymuszenie konfiguracji)
float windDirectionWorld = -1;
bool windCalibrated = false;

// --- Wi-Fi portal ---
Preferences preferences;
WebServer server(80);
DNSServer dnsServer;
const byte DNS_PORT = 53;
String wifi_ssid = "";
String wifi_pass = "";

// --- HTML portal style ---
String html_head =
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

String html_footer = "</body></html>";

// --- Forward declarations (ułatwiają porządek funkcji) ---
void displayMessage(const String &msg);
void showSplashScreen();
void saveWiFiCredentials(const String &ssid, const String &password);
bool loadWiFiCredentials(String &ssid, String &password);
void handle_Root();
void handle_Save();
void startWiFiConfigPortal();
void connectWiFiWithUI();
IPAddress discoverPlaystation();
lv_color_t getTyreColor(int temp);
float quaternionToAzimuth(float w, float x, float y, float z);
void updateArrow(float carAz);
void calibrateWind(float carAz);

// --- Wyświetlanie komunikatów ---
static void resetCurrentLapTracking()
{
    currentLapSampleCount = 0;
    currentLapDistance = 0.0f;
    haveLastTrackPosition = false;
    currentLapStartMs = millis();
    smoothedDelta = 0.0f;
}

static void resetLiveDeltaTracking()
{
    referenceLapSampleCount = 0;
    liveDeltaArmed = false;
}

static void captureCurrentLapAsReference()
{
    referenceLapSampleCount = currentLapSampleCount;
    for (size_t i = 0; i < currentLapSampleCount; ++i)
    {
        referenceLapSamples[i] = currentLapSamples[i];
    }
}

static void appendLapSample(float distance, uint32_t elapsedMs)
{
    if (currentLapSampleCount >= LIVE_DELTA_MAX_SAMPLES)
    {
        return;
    }

    if (currentLapSampleCount > 0)
    {
        const LapSample &lastSample = currentLapSamples[currentLapSampleCount - 1];
        if ((distance - lastSample.distance) < 1.0f && (elapsedMs - lastSample.elapsedMs) < 100)
        {
            return;
        }
    }

    currentLapSamples[currentLapSampleCount++] = {distance, elapsedMs};
}

static bool getReferenceTimeAtDistance(float distance, float &referenceTimeMs)
{
    if (referenceLapSampleCount < 2)
    {
        return false;
    }

    if (distance <= referenceLapSamples[0].distance)
    {
        referenceTimeMs = (float)referenceLapSamples[0].elapsedMs;
        return true;
    }

    for (size_t i = 1; i < referenceLapSampleCount; ++i)
    {
        const LapSample &prev = referenceLapSamples[i - 1];
        const LapSample &next = referenceLapSamples[i];

        if (distance <= next.distance)
        {
            float span = next.distance - prev.distance;
            if (span <= 0.001f)
            {
                referenceTimeMs = (float)next.elapsedMs;
            }
            else
            {
                float ratio = (distance - prev.distance) / span;
                referenceTimeMs = (float)prev.elapsedMs + ratio * (float)(next.elapsedMs - prev.elapsedMs);
            }
            return true;
        }
    }

    referenceTimeMs = (float)referenceLapSamples[referenceLapSampleCount - 1].elapsedMs;
    return true;
}
static void updateLiveDeltaLabel(float deltaSeconds, bool valid)
{
    char buf[16];
    if (!valid)
    {
        snprintf(buf, sizeof(buf), "--.--");
        if (ui_AzimuthAngle)
        {
            lv_label_set_text(ui_AzimuthAngle, buf);
            lv_obj_set_style_text_color(ui_AzimuthAngle, lv_color_hex(0x505050), 0); // Szary dla braku danych
        }
        return;
    }

    // Formatowanie tekstu: zawsze znak (+/-) i 2 miejsca po przecinku
    snprintf(buf, sizeof(buf), "%+.2fs", deltaSeconds);

    if (ui_AzimuthAngle)
    {
        lv_color_t color;

        // Logika kolorów (Deadzone + Progi)
        if (deltaSeconds < -0.50f)
        {
            color = lv_color_hex(0xB000FF); // Fioletowy - miażdżysz rekord
        }
        else if (deltaSeconds < -0.05f)
        {
            color = lv_color_hex(0x00FF00); // Zielony - poprawa
        }
        else if (deltaSeconds > 0.05f)
        {
            color = lv_color_hex(0xFF0000); // Czerwony - strata
        }
        else
        {
            color = lv_color_hex(0xFFFFFF); // Biały - martwa strefa (poniżej 0.05s różnicy)
        }

        lv_label_set_text(ui_AzimuthAngle, buf);
        lv_obj_set_style_text_color(ui_AzimuthAngle, color, 0);
    }
}

void displayMessage(const String &msg)
{
    if (!ui_messageLabel)
    {
        ui_messageLabel = lv_label_create(lv_scr_act());
        lv_obj_set_width(ui_messageLabel, amoled.width());
        lv_label_set_long_mode(ui_messageLabel, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(ui_messageLabel, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(ui_messageLabel, &lv_font_montserrat_32, 0);
        lv_obj_align(ui_messageLabel, LV_ALIGN_CENTER, 0, 0);
    }
    lv_obj_set_style_text_color(ui_messageLabel, lv_color_white(), 0);
    lv_label_set_text(ui_messageLabel, msg.c_str());
    lv_task_handler();
}

// --- Splash screen ---
void showSplashScreen()
{
    lv_obj_t *splash = lv_img_create(lv_scr_act());
    lv_img_set_src(splash, &logo);
    lv_obj_align(splash, LV_ALIGN_CENTER, 0, 0);
    lv_task_handler();
    delay(2000);
    lv_obj_del(splash);
    lv_task_handler();
}

// --- Funkcje do zapisu i odczytu Wi-Fi ---
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

// --- Strona główna portalu ---
void handle_Root()
{
    String page = html_head;
    page += "<div class='box'><h2>GT7 Telemetry - Konfiguracja Wi-Fi</h2>";
    page += "<form action='/save' method='POST'>";
    page += "<input name='ssid' placeholder='Nazwa sieci Wi-Fi' value='" + wifi_ssid + "'><br>";
    page += "<input name='pass' placeholder='Haslo Wi-Fi' type='password' value=''><br>";
    page += "<button type='submit'>Zapisz</button>";
    page += "</form>";
    page += "<p style='font-size:12px;color:#999;margin-top:12px;'>Po zapisaniu ESP sie zrestartuje i polaczy z twoja siecia.</p>";
    page += "</div>";
    page += html_footer;
    server.send(200, "text/html", page);
}

// --- Obsługa zapisu Wi-Fi ---
void handle_Save()
{
    // Pobierz dane (może to być POST)
    String ssid = server.arg("ssid");
    String pass = server.arg("pass");

    if (ssid.length() == 0)
    {
        server.send(200, "text/html", html_head + "<div class='box'><h2>Bląd: SSID jest puste!</h2></div>" + html_footer);
        return;
    }

    saveWiFiCredentials(ssid, pass);
    String resp = html_head;
    resp += "<div class='box'><h2>Zapisano ustawienia.</h2><p>Restartuje urządzenie...</p></div>";
    resp += html_footer;
    server.send(200, "text/html", resp);
    delay(1200);
    ESP.restart();
}

// --- Start konfiguracji Wi-Fi przez AP z Captive Portal ---
void startWiFiConfigPortal()
{
    WiFi.disconnect(true);
    delay(100);

    WiFi.mode(WIFI_AP);
    WiFi.softAP("GT7_Telemetry_Setup", "12345678");

    IPAddress apIP(192, 168, 4, 1);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));

    displayMessage("Tryb konfiguracji Wi-Fi!\nPolacz sia z 'GT7_Telemetry_Setup'");

    // Uruchom DNS (captive portal) - wszystkie zapytania wskazują na apIP
    dnsServer.start(DNS_PORT, "*", apIP);

    // Serwer WWW - trasy
    server.on("/", HTTP_GET, handle_Root);
    server.on("/save", HTTP_POST, handle_Save);
    server.begin();

    // pętla konfiguracji (blokująca) - utrzymuj LVGL task handler, aby ekran nie zawiesil się
    while (true)
    {
        dnsServer.processNextRequest();
        server.handleClient();
        lv_task_handler();
        delay(5);
    }
}

// --- Funkcja łączenia z Wi-Fi z obsługą przycisku BOOT (GPIO0) ---
void connectWiFiWithUI()
{
    pinMode(CALIB_BUTTON, INPUT_PULLUP); // upewnij się, że BOOT jest pull-up

    // sprawdź, czy przycisk BOOT był wciśnięty podczas startu
    bool forceConfig = (digitalRead(CALIB_BUTTON) == LOW);
    if (forceConfig)
    {
        displayMessage("BOOT wcisnisty!\nWchodze do konfiguracji...");
        delay(800);
        startWiFiConfigPortal();
    }

    // Jeżeli brak zapisanych danych → portal konfiguracji
    if (!loadWiFiCredentials(wifi_ssid, wifi_pass))
    {
        startWiFiConfigPortal();
    }

    // Spróbuj połączyć się z zapisanym WiFi
    displayMessage("Laczenie z Wi-Fi:\n" + wifi_ssid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(wifi_ssid.c_str(), wifi_pass.c_str());

    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startTime < 20000)
    {
        delay(300);
        // aktualizuj komunikat co pewien czas
        static unsigned long lastUpdate = 0;
        unsigned long now = millis();
        if (now - lastUpdate > 800)
        {
            lastUpdate = now;
            displayMessage("Laczenie z Wi-Fi:\n" + wifi_ssid);
        }
        lv_task_handler();
    }

    if (WiFi.status() == WL_CONNECTED)
    {
        displayMessage("Polaczono!\nIP: " + WiFi.localIP().toString());
        delay(900);
    }
    else
    {
        displayMessage("Blad polaczenia.\nUruchamiam konfiguracje...");
        delay(500);
        startWiFiConfigPortal();
    }
}

// --- Wykrywanie IP konsoli ---
IPAddress discoverPlaystation()
{
    discoveryUdp.begin(localPort);
    IPAddress broadcastIP(255, 255, 255, 255);

    discoveryUdp.beginPacket(broadcastIP, remotePort);
    discoveryUdp.write(heartbeatMsg);
    discoveryUdp.endPacket();

    displayMessage("Szukam konsoli z GT7...");

    unsigned long start = millis();
    while (millis() - start < 1500)
    {
        int packetSize = discoveryUdp.parsePacket();
        if (packetSize > 0)
        {
            IPAddress sender = discoveryUdp.remoteIP();
            discoveryUdp.stop();
            displayMessage("Znaleziono konsole:\n" + sender.toString());
            delay(250);
            return sender;
        }
        delay(5);
    }

    discoveryUdp.stop();
    displayMessage("Brak odpowiedzi od konsoli!");
    delay(1000);
    return IPAddress(0, 0, 0, 0);
}

// --- Kolor opony ---
lv_color_t getTyreColor(int temp)
{
    temp = constrain(temp, 0, 120);
    if (temp <= 60)
    {
        float ratio = temp / 60.0f;
        uint8_t r = 224 - ratio * 224;
        uint8_t g = 255;
        uint8_t b = 246 - ratio * 246;
        return lv_color_make(r, g, b);
    }
    else if (temp <= 80)
    {
        float ratio = (temp - 60) / 20.0f;
        uint8_t r = ratio * 255;
        uint8_t g = 255;
        uint8_t b = 0;
        return lv_color_make(r, g, b);
    }
    else if (temp <= 100)
    {
        float ratio = (temp - 80) / 20.0f;
        uint8_t r = 255;
        uint8_t g = 255 - ratio * 255;
        uint8_t b = 0;
        return lv_color_make(r, g, b);
    }
    else
    {
        return lv_color_make(255, 0, 0);
    }
}

// --- Quaternion → Azymut ---
float quaternionToAzimuth(float w, float x, float y, float z)
{
    float siny_cosp = 2.0f * (w * y + x * z);
    float cosy_cosp = 1.0f - 2.0f * (y * y + z * z);
    float yaw = atan2(siny_cosp, cosy_cosp);
    float deg = yaw * 180.0f / PI;
    if (deg < 0)
        deg += 360.0f;
    return deg;
}

// --- Aktualizacja strzałki ---
void updateArrow(float carAz)
{
    if (windCalibrated)
    {
        float delta = windDirectionWorld - carAz;
        while (delta > 180)
            delta -= 360;
        while (delta < -180)
            delta += 360;
        lv_img_set_angle(ui_Arrow, (int16_t)(delta * 10));
    }
    else
    {
        if (abs((int)carAz - last_az) >= 1)
        {
            last_az = (int)carAz;
            int16_t angle10 = (int16_t)(carAz * 10.0f);
            lv_img_set_angle(ui_Arrow, angle10);
        }
    }
}

// --- Kalibracja wiatru ---
void calibrateWind(float carAz)
{
    windDirectionWorld = carAz;
    windCalibrated = true;
    lv_obj_set_style_img_recolor_opa(ui_Arrow, LV_OPA_COVER, 0);
    lv_obj_set_style_img_recolor(ui_Arrow, lv_color_hex(0xffffff), 0);
    displayMessage("Wiatr skalibrowany!");
}

// --- Setup ---
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
    showSplashScreen();

    // zapewnij pull-up na BOOT
    pinMode(CALIB_BUTTON, INPUT_PULLUP);

    // po splash - uruchom konfigurację Wi-Fi (może wejść do portalu)
    connectWiFiWithUI();

    // discovery PS (blokuje do czasu znalezienia lub powrotu IP 0.0.0.0)
    IPAddress psIP;
    while (psIP == IPAddress(0, 0, 0, 0))
    {
        psIP = discoverPlaystation();
    }

    gt7Telem.begin(psIP);
    gt7Telem.sendHeartbeat();
    ui_init();

    resetLiveDeltaTracking();
    resetCurrentLapTracking();
}

// --- Loop ---
void loop()
{
    lv_task_handler();
    delay(10);

    packetContent = gt7Telem.readData();

    int racestart = packetContent.packetContent.preRaceNumCars;

    if (racestart < 0)
    {
        FLtyretemp = FRtyretemp = RLtyretemp = RRtyretemp = 0;
        FuelLevel = 0;
        LapOnFuel = 0;
        lastLapFuel = -1.0f;
        fuelPerLap = 0.0f;
        lastLapCount = 0;
        resetLiveDeltaTracking();
        resetCurrentLapTracking();
    }

    float az_deg = quaternionToAzimuth(
        packetContent.packetContent.rotation[0],
        packetContent.packetContent.rotation[1],
        packetContent.packetContent.rotation[2],
        packetContent.packetContent.rotation[3]);

    if (digitalRead(CALIB_BUTTON) == LOW)
    {
        calibrateWind(az_deg);
        delay(500);
    }

    updateArrow(az_deg);

    FLtyretemp = constrain(packetContent.packetContent.tyreTemp[0], 0, 120);
    FRtyretemp = constrain(packetContent.packetContent.tyreTemp[1], 0, 120);
    RLtyretemp = constrain(packetContent.packetContent.tyreTemp[2], 0, 120);
    RRtyretemp = constrain(packetContent.packetContent.tyreTemp[3], 0, 120);

    if (abs(FLtyretemp - last_FL) >= 1)
    {
        last_FL = FLtyretemp;
        lv_label_set_text(ui_FLTIRETEMPwhite, String(FLtyretemp).c_str());
        lv_obj_set_style_bg_color(ui_LFtire, getTyreColor(FLtyretemp - 3), LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    if (abs(FRtyretemp - last_FR) >= 1)
    {
        last_FR = FRtyretemp;
        lv_label_set_text(ui_FRTIRETEMPwhite, String(FRtyretemp).c_str());
        lv_obj_set_style_bg_color(ui_RFtire, getTyreColor(FRtyretemp - 3), LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    if (abs(RLtyretemp - last_RL) >= 1)
    {
        last_RL = RLtyretemp;
        lv_label_set_text(ui_RLTIRETEMPwhite, String(RLtyretemp).c_str());
        lv_obj_set_style_bg_color(ui_LRtire, getTyreColor(RLtyretemp - 3), LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    if (abs(RRtyretemp - last_RR) >= 1)
    {
        last_RR = RRtyretemp;
        lv_label_set_text(ui_RRTIRETEMPwhite, String(RRtyretemp).c_str());
        lv_obj_set_style_bg_color(ui_RRtire, getTyreColor(RRtyretemp - 3), LV_PART_MAIN | LV_STATE_DEFAULT);
    }

    FuelLevel = packetContent.packetContent.fuelLevel;
    float FuelCapacity = packetContent.packetContent.fuelCapacity;
    int LapCounter = packetContent.packetContent.lapCount;
    float currentX = packetContent.packetContent.position[0];
    float currentY = packetContent.packetContent.position[1];
    float currentZ = packetContent.packetContent.position[2];

    if (LapCounter != lastLapCount)
    {
        int lapDelta = LapCounter - lastLapCount;
        if (currentLapSampleCount >= 2)
        {
            captureCurrentLapAsReference();
            liveDeltaArmed = true;
        }
        lastLapCount = LapCounter;
        updateFuelPerLapFromLap(FuelLevel, lapDelta);
        resetCurrentLapTracking();
    }

    if (!haveLastTrackPosition)
    {
        lastTrackX = currentX;
        lastTrackY = currentY;
        lastTrackZ = currentZ;
        haveLastTrackPosition = true;
    }
    else
    {
        float dx = currentX - lastTrackX;
        float dy = currentY - lastTrackY;
        float dz = currentZ - lastTrackZ;
        float stepDistance = sqrtf(dx * dx + dy * dy + dz * dz);

        if (stepDistance > 0.0f && stepDistance < 100.0f)
        {
            currentLapDistance += stepDistance;
        }

        lastTrackX = currentX;
        lastTrackY = currentY;
        lastTrackZ = currentZ;
    }

    uint32_t currentLapElapsedMs = millis() - currentLapStartMs;
    appendLapSample(currentLapDistance, currentLapElapsedMs);

    if (liveDeltaArmed)
    {
        float referenceTimeMs = 0.0f;
        if (getReferenceTimeAtDistance(currentLapDistance, referenceTimeMs))
        {
            // Obliczamy surową różnicę
            float rawDeltaSeconds = ((float)currentLapElapsedMs - referenceTimeMs) / 1000.0f;

            // Wygładzamy (Filtr EMA)
            smoothedDelta = (smoothedDelta * (1.0f - DELTA_SMOOTHING_ALPHA)) + (rawDeltaSeconds * DELTA_SMOOTHING_ALPHA);

            // Wysyłamy wygładzoną wartość do etykiety
            updateLiveDeltaLabel(smoothedDelta, true);
        }
        else
        {
            updateLiveDeltaLabel(0.0f, false);
        }
    }
    else
    {
        updateLiveDeltaLabel(0.0f, false);
    }
    // pasek paliwa
    int FuelLevelMapped = 0;
    if (FuelCapacity > 0.01f)
    {
        FuelLevelMapped = (int)constrain((FuelLevel / FuelCapacity) * 100.0f, 0.0f, 100.0f);
    }
    lv_slider_set_value(ui_Slider1, FuelLevelMapped, LV_ANIM_OFF);

    // ===== nowe liczenie spalania =====
#if 0

    if (LapCounter != lastLapCount)
    {
        // nowe okrążenie
        int lapDelta = LapCounter - lastLapCount;
        lastLapCount = LapCounter;

        if(lastLapFuel >= 0 && lapDelta > 0)
        {
           float used = lastLapFuel - FuelLevel;
           float usedPerLap = used / (float)lapDelta;

           // filtr:
            // >0.1 ignoruje szum
           // <20 ignoruje pit refuel
            if(usedPerLap > 0.1f && usedPerLap < 20.0f)
            {
               // exponential moving average
                if(fuelPerLap == 0)
                   fuelPerLap = usedPerLap;
               else
                   fuelPerLap =
                    fuelPerLap*0.7f +
                    usedPerLap*0.3f;
            }
        }

        lastLapFuel = FuelLevel;
    }

    // przewidywana ilość okrążeń
#endif

    if (fuelPerLap > 0.01f)
    {
        LapOnFuel = FuelLevel / fuelPerLap;
    }
    else
    {
        LapOnFuel = 0;
    }

    LapOnFuel = roundToTwoDecimals(constrain(LapOnFuel, 0.0f, 99.0f));

    // --- wyświetlanie ---

    int calkowita = (int)LapOnFuel;
    int ulamkowa = (int)lroundf((LapOnFuel - (float)calkowita) * 100.0f);
    if (ulamkowa >= 100)
    {
        calkowita += 1;
        ulamkowa = 0;
    }
    if (calkowita > 99)
    {
        calkowita = 99;
        ulamkowa = 99;
    }

    lv_color_t fuelColor = (calkowita < 2) ? lv_color_hex(0xff0000) : lv_color_hex(0xF7FF00);
    lv_obj_set_style_text_color(ui_LAPCOUNTERbase, fuelColor, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_LAPCOUNTERbase1, fuelColor, LV_PART_MAIN | LV_STATE_DEFAULT);

    char buf[4];
    char buf1[4];

    snprintf(buf, sizeof(buf), "%02d", ulamkowa);
    snprintf(buf1, sizeof(buf1), "%02d", calkowita);

    lv_label_set_text(ui_LAPCOUNTERbase, buf1);
    lv_label_set_text(ui_LAPCOUNTERbase1, buf);

    // pokaz spalanie / lap
    char fuelPerLapBuf[12];
    snprintf(fuelPerLapBuf, sizeof(fuelPerLapBuf), "%.2f", roundToTwoDecimals(fuelPerLap));
    lv_label_set_text(ui_AVGfuel, fuelPerLapBuf);

    if (millis() - previousT >= interval)
    {
        previousT = millis();
        gt7Telem.sendHeartbeat();
    }
}
