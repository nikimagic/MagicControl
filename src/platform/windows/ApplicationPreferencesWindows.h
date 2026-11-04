#pragma once
#include "MagicControl/PortableSettings.h"
#include <string>

namespace mc::windows {
class ConfigurationRestore;
// Reads only app-settings.ini and keys-settings.ini using the existing schema.
// Never replays Bluetooth or any other key action.
struct ApplicationPreferencesBootstrap {
    std::string applicationError;
    std::string keysError;
};
ApplicationPreferencesBootstrap loadApplicationPreferencesForStartup(ApplicationPreferences&);
// Application-owned OSD only; native Copilot belongs to KeysLifecycle.
void readKeysApplicationPreferences(ApplicationPreferences&);
bool readStoredApplicationPreferences(ApplicationPreferences&,std::string& error);
std::filesystem::path applicationPreferencesPath();
void saveApplicationPreferencesIfChanged(const std::filesystem::path&,const ApplicationPreferences& previous,const ApplicationPreferences& next);

// Exact existing hardware calls; caller retains power-event ordering and UI callbacks.
void suspendKeyboardForPower();
bool resumeKeyboardForPower();
void restoreNativeBatteryForPower(const std::string& reason);
// True asks the caller to restore its UI-owned colour-cycle scheduler.
bool restoreDeviceConfiguration(const std::string& reason,ConfigurationRestore*);
}
