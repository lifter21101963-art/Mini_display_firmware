#ifndef UPDATE_MANAGER_H
#define UPDATE_MANAGER_H

#include <Arduino.h>
#include <LilyGo_AMOLED.h>

namespace update_manager
{
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
