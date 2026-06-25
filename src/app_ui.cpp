#include "app_ui.h"

#include <Arduino.h>
#include <math.h>
#include "ui.h"
#include "version.h"

extern const lv_img_dsc_t logo;

namespace app_ui
{
namespace
{
lv_obj_t *messageLabel = nullptr;
lv_obj_t *deltaHistoryMarkers[DELTA_MARKER_COUNT] = {nullptr};
lv_obj_t *batteryLabel = nullptr;
lv_obj_t *batteryBar = nullptr;
lv_obj_t *batteryFill = nullptr;
bool deltaHistoryUiReady = false;
constexpr int BATTERY_BAR_X = 526;
constexpr int BATTERY_BAR_Y = 12;

struct RgbColor
{
    uint8_t red;
    uint8_t green;
    uint8_t blue;
};

uint8_t lerpChannel(uint8_t from, uint8_t to, float ratio)
{
    ratio = constrain(ratio, 0.0f, 1.0f);
    return (uint8_t)lroundf((float)from + ((float)to - (float)from) * ratio);
}

lv_color_t lerpColor(const RgbColor &from, const RgbColor &to, float ratio)
{
    return lv_color_make(
        lerpChannel(from.red, to.red, ratio),
        lerpChannel(from.green, to.green, ratio),
        lerpChannel(from.blue, to.blue, ratio));
}

lv_color_t fromPackedColor(uint32_t packedColor)
{
    uint8_t red = (uint8_t)((packedColor >> 16) & 0xFF);
    uint8_t green = (uint8_t)((packedColor >> 8) & 0xFF);
    uint8_t blue = (uint8_t)(packedColor & 0xFF);
    return lv_color_make(red, green, blue);
}

lv_opa_t getFadeOpacity(unsigned long phaseMs)
{
    constexpr float MIN_OPA = 0.35f;
    constexpr float MAX_OPA = 1.0f;
    constexpr float FADE_PERIOD_MS = 1600.0f;
    constexpr float FADE_TWO_PI = 6.28318530f;

    float angle = ((float)(phaseMs % (unsigned long)FADE_PERIOD_MS) / FADE_PERIOD_MS) * FADE_TWO_PI;
    float wave = (sinf(angle) + 1.0f) * 0.5f;
    float opacity = MIN_OPA + (MAX_OPA - MIN_OPA) * wave;
    return (lv_opa_t)lroundf(opacity * 255.0f);
}

lv_color_t getDeltaColor(float deltaSeconds)
{
    constexpr float STRONG_FAST_DELTA = -0.50f;
    constexpr float NEUTRAL_DELTA = 0.0f;
    constexpr float STRONG_SLOW_DELTA = 0.50f;

    const RgbColor fastPurple = {0xB0, 0x00, 0xFF};
    const RgbColor fastGreen = {0x00, 0xFF, 0x00};
    const RgbColor neutralWhite = {0xFF, 0xFF, 0xFF};
    const RgbColor slowRed = {0xFF, 0x00, 0x00};

    if (deltaSeconds <= STRONG_FAST_DELTA)
    {
        return lv_color_make(fastPurple.red, fastPurple.green, fastPurple.blue);
    }

    if (deltaSeconds < NEUTRAL_DELTA)
    {
        float ratio = (deltaSeconds - STRONG_FAST_DELTA) / (NEUTRAL_DELTA - STRONG_FAST_DELTA);
        return lerpColor(fastGreen, neutralWhite, ratio);
    }

    if (deltaSeconds >= STRONG_SLOW_DELTA)
    {
        return lv_color_make(slowRed.red, slowRed.green, slowRed.blue);
    }

    return lerpColor(neutralWhite, slowRed, deltaSeconds / STRONG_SLOW_DELTA);
}

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
    if (temp <= 80)
    {
        float ratio = (temp - 60) / 20.0f;
        uint8_t r = ratio * 255;
        uint8_t g = 255;
        uint8_t b = 0;
        return lv_color_make(r, g, b);
    }
    if (temp <= 100)
    {
        float ratio = (temp - 80) / 20.0f;
        uint8_t r = 255;
        uint8_t g = 255 - ratio * 255;
        uint8_t b = 0;
        return lv_color_make(r, g, b);
    }

    return lv_color_make(255, 0, 0);
}

void updateTyreTile(int temp, int &lastTemp, lv_obj_t *label, lv_obj_t *tile)
{
    if (abs(temp - lastTemp) < 1)
    {
        return;
    }

    lastTemp = temp;
    lv_label_set_text(label, String(temp).c_str());
    if (tile)
    {
        lv_obj_set_style_bg_color(tile, getTyreColor(temp - 3), LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

void renderDeltaDisplay(float deltaSeconds, bool deltaValid)
{
    if (!ui_AzimuthAngle)
    {
        return;
    }

    char deltaBuf[16];
    if (!deltaValid)
    {
        snprintf(deltaBuf, sizeof(deltaBuf), "--.--");
        lv_label_set_text(ui_AzimuthAngle, deltaBuf);
        lv_obj_set_style_text_color(ui_AzimuthAngle, lv_color_hex(0xF7FF00), 0);
        lv_obj_set_style_text_opa(ui_AzimuthAngle, getFadeOpacity(millis()), 0);
        return;
    }

    snprintf(deltaBuf, sizeof(deltaBuf), "%+.2fs", deltaSeconds);
    lv_label_set_text(ui_AzimuthAngle, deltaBuf);
    lv_obj_set_style_text_color(ui_AzimuthAngle, getDeltaColor(deltaSeconds), 0);
    lv_obj_set_style_text_opa(ui_AzimuthAngle, LV_OPA_COVER, 0);
}

void ensureDeltaHistoryUi()
{
    if (deltaHistoryUiReady || !ui_Screen1)
    {
        return;
    }

    constexpr int markerSize = 20;
    constexpr int markerTopMargin = 12;
    constexpr int markerBottomMargin = 12;
    constexpr int markerX = 233;
    lv_color_t baseMarkerColor = lv_color_hex(0x404040);
    constexpr int usableHeight = 240 - markerTopMargin - markerBottomMargin;
    constexpr int markerStep = (usableHeight - markerSize) / (DELTA_MARKER_COUNT - 1);

    for (int i = 0; i < DELTA_MARKER_COUNT; ++i)
    {
        deltaHistoryMarkers[i] = lv_obj_create(ui_Screen1);
        lv_obj_clear_flag(deltaHistoryMarkers[i], LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(deltaHistoryMarkers[i], markerSize, markerSize);
        lv_obj_set_align(deltaHistoryMarkers[i], LV_ALIGN_CENTER);
        lv_obj_set_x(deltaHistoryMarkers[i], markerX);
        lv_obj_set_y(deltaHistoryMarkers[i], -120 + markerTopMargin + (i * markerStep) + (markerSize / 2));
        lv_obj_set_style_radius(deltaHistoryMarkers[i], LV_RADIUS_CIRCLE, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(deltaHistoryMarkers[i], 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_all(deltaHistoryMarkers[i], 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(deltaHistoryMarkers[i], LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(deltaHistoryMarkers[i], baseMarkerColor, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_move_foreground(deltaHistoryMarkers[i]);
    }

    deltaHistoryUiReady = true;
}

void renderDeltaHistory(const TelemetryViewData &view)
{
    ensureDeltaHistoryUi();
    if (!deltaHistoryUiReady)
    {
        return;
    }

    for (int i = 0; i < DELTA_HISTORY_SLOTS; ++i)
    {
        lv_color_t markerColor = view.deltaHistoryValid[i] ? fromPackedColor(view.deltaHistoryColors[i]) : lv_color_hex(0x404040);
        lv_obj_set_style_bg_color(deltaHistoryMarkers[i], markerColor, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(deltaHistoryMarkers[i], LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    }

    lv_color_t liveColor = view.liveDeltaValid ? fromPackedColor(view.liveDeltaColor) : lv_color_hex(0x404040);
    lv_obj_set_style_bg_color(deltaHistoryMarkers[DELTA_MARKER_COUNT - 1], liveColor, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(deltaHistoryMarkers[DELTA_MARKER_COUNT - 1], LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
}

void ensureBatteryStatusUi()
{
    if (batteryLabel || batteryBar || batteryFill || !ui_Screen1)
    {
        return;
    }

    batteryLabel = lv_label_create(ui_Screen1);
    lv_obj_set_width(batteryLabel, 1);
    lv_label_set_long_mode(batteryLabel, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(batteryLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(batteryLabel, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(batteryLabel, lv_color_hex(0xF7FF00), 0);
    lv_obj_set_pos(batteryLabel, BATTERY_BAR_X, BATTERY_BAR_Y);
    lv_obj_add_flag(batteryLabel, LV_OBJ_FLAG_HIDDEN);

    batteryBar = lv_obj_create(ui_Screen1);
    lv_obj_set_size(batteryBar, 12, 42);
    lv_obj_clear_flag(batteryBar, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(batteryBar, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(batteryBar, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(batteryBar, lv_color_hex(0x404040), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(batteryBar, lv_color_hex(0x101010), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(batteryBar, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_flag(batteryBar, LV_OBJ_FLAG_HIDDEN);

    batteryFill = lv_obj_create(batteryBar);
    lv_obj_clear_flag(batteryFill, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(batteryFill, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(batteryFill, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(batteryFill, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(batteryFill, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_pos(batteryBar, BATTERY_BAR_X, BATTERY_BAR_Y);
    lv_obj_add_flag(batteryLabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(batteryFill, LV_OBJ_FLAG_HIDDEN);
}
} // namespace

void initializeDeltaHistory()
{
    ensureDeltaHistoryUi();
}

void initializeBatteryStatus()
{
    ensureBatteryStatusUi();
}

void displayMessage(const String &msg, LilyGo_Class &amoled)
{
    if (!messageLabel)
    {
        messageLabel = lv_label_create(lv_scr_act());
        lv_obj_set_width(messageLabel, amoled.width());
        lv_label_set_long_mode(messageLabel, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(messageLabel, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(messageLabel, &lv_font_montserrat_32, 0);
        lv_obj_align(messageLabel, LV_ALIGN_CENTER, 0, 0);
    }

    lv_obj_set_style_text_color(messageLabel, lv_color_white(), 0);
    lv_label_set_text(messageLabel, msg.c_str());
    lv_task_handler();
}

void showSplashScreen(LilyGo_Class &amoled)
{
    lv_obj_t *splash = lv_img_create(lv_scr_act());
    lv_img_set_src(splash, &logo);
    lv_obj_align(splash, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *versionLabel = lv_label_create(lv_scr_act());
    lv_label_set_text_fmt(versionLabel, "v%s", APP_VERSION);
    lv_obj_set_style_text_font(versionLabel, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(versionLabel, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(versionLabel, 180, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(versionLabel, LV_ALIGN_BOTTOM_LEFT, 8, -6);

    lv_task_handler();
    delay(2000);
    lv_obj_del(versionLabel);
    lv_obj_del(splash);
    lv_task_handler();
}

void clearDeltaDisplay()
{
    renderDeltaDisplay(0.0f, false);
}

void renderBatteryStatus(const battery_status::BatterySnapshot &battery)
{
    ensureBatteryStatusUi();
    if (!batteryLabel || !batteryBar || !batteryFill)
    {
        return;
    }

    int innerHeight = 36;
    int fillHeight = (int)lroundf((float)innerHeight * constrain((float)battery.percent / 100.0f, 0.0f, 1.0f));
    if (fillHeight < 2)
    {
        fillHeight = 2;
    }

    lv_obj_set_size(batteryFill, 8, fillHeight);
    lv_obj_set_style_bg_color(
        batteryFill,
        battery.percent <= 10 ? lv_color_hex(0xFF3B30)
                              : battery.percent <= 35 ? lv_color_hex(0xFFB000)
                                                      : battery.charging ? lv_color_hex(0x00D084) : lv_color_hex(0xF7FF00),
        LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_add_flag(batteryLabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(batteryBar, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(batteryFill, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(batteryBar);
}

void renderFuelDisplay(const TelemetryViewData &view)
{
    lv_slider_set_value(ui_Slider1, view.fuelLevelMapped, LV_ANIM_OFF);

    bool fuelValueReady = view.fuelPerLap > 0.01f;
    int whole = 0;
    int fraction = 0;
    if (fuelValueReady)
    {
        whole = (int)view.lapOnFuel;
        fraction = (int)lroundf((view.lapOnFuel - (float)whole) * 100.0f);
        if (fraction >= 100)
        {
            whole += 1;
            fraction = 0;
        }
        if (whole > 99)
        {
            whole = 99;
            fraction = 99;
        }
    }

    lv_color_t fuelColor = lv_color_hex(0xF7FF00);
    lv_opa_t fuelOpacity = LV_OPA_COVER;
    if (!fuelValueReady || view.fuelLiveEstimate)
    {
        fuelOpacity = getFadeOpacity(millis());
    }

    lv_obj_set_style_text_color(ui_LAPCOUNTERbase, fuelColor, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_LAPCOUNTERbase1, fuelColor, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_LAPCOUNTERbase, fuelOpacity, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_LAPCOUNTERbase1, fuelOpacity, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_AVGfuel, fuelOpacity, LV_PART_MAIN | LV_STATE_DEFAULT);

    char buf[5];
    char buf1[5];
    if (!fuelValueReady)
    {
        snprintf(buf, sizeof(buf), ",--");
        snprintf(buf1, sizeof(buf1), "--");
    }
    else
    {
        snprintf(buf, sizeof(buf), ",%02d", fraction);
        snprintf(buf1, sizeof(buf1), "%02d", whole);
    }
    lv_label_set_text(ui_LAPCOUNTERbase, buf1);
    lv_label_set_text(ui_LAPCOUNTERbase1, buf);

    char fuelPerLapBuf[12];
    if (!fuelValueReady)
    {
        snprintf(fuelPerLapBuf, sizeof(fuelPerLapBuf), "-,--");
    }
    else
    {
        snprintf(fuelPerLapBuf, sizeof(fuelPerLapBuf), "%.2f", roundf(view.fuelPerLap * 100.0f) / 100.0f);
        for (size_t i = 0; i < sizeof(fuelPerLapBuf); ++i)
        {
            if (fuelPerLapBuf[i] == '.')
            {
                fuelPerLapBuf[i] = ',';
                break;
            }
        }
    }
    lv_label_set_text(ui_AVGfuel, fuelPerLapBuf);
}

void renderTelemetryWaiting()
{
    TelemetryViewData view{};
    renderFuelDisplay(view);
    renderDeltaDisplay(0.0f, false);
    renderDeltaHistory(view);
}

void renderTelemetryDashboard(const TelemetryViewData &view)
{
    static int lastTyreTemps[4] = {-100, -100, -100, -100};

    updateTyreTile(view.tyreTemps[0], lastTyreTemps[0], ui_FLTIRETEMPwhite, ui_LFtire);
    updateTyreTile(view.tyreTemps[1], lastTyreTemps[1], ui_FRTIRETEMPwhite, ui_RFtire);
    updateTyreTile(view.tyreTemps[2], lastTyreTemps[2], ui_RLTIRETEMPwhite, ui_LRtire);
    updateTyreTile(view.tyreTemps[3], lastTyreTemps[3], ui_RRTIRETEMPwhite, ui_RRtire);

    renderFuelDisplay(view);

    renderDeltaDisplay(view.deltaSeconds, view.deltaValid);
    renderDeltaHistory(view);
}
} // namespace app_ui
