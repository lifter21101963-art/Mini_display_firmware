#ifndef UPDATE_MANAGER_H
#define UPDATE_MANAGER_H

#include <Arduino.h>
#include <LilyGo_AMOLED.h>

namespace update_manager
{
constexpr char DEFAULT_UPDATE_SOURCE_URL[] = "https://github.com/lifter21101963-art/Mini_display_firmware";
constexpr char DEFAULT_UPDATE_ASSET_NAME[] = "merged-firmware.bin";

struct UpdateSettings
{
    bool enabled = false;
    String manifestUrl;
    String assetName;
};

void loadSettings(UpdateSettings &settings);
void saveSettings(const UpdateSettings &settings);
bool checkForUpdate(LilyGo_Class &amoled);
} // namespace update_manager

#endif
