#pragma once

#include <windows.h>
#include <memory>
#include <functional>
#include <string>
#include "MagicControl/ResourceControl.h"
#include "MagicControl/SettingsResourceControl.h"

namespace mc {
class EcAccess;
struct BatteryReading;
namespace windows { class FanController; }
}

namespace mc::gui {
class OsdPopup;
using mc::TraySettingsState;using mc::TraySetting;

/// SETs page: battery limit, EC performance presets and keyboard lighting.
/// The page owns only UI state; all EC transactions are serialized by EcAccess
/// and run away from the window thread.
class SettingsPage {
public:
    SettingsPage(HWND parent, EcAccess* ec, OsdPopup* osd=nullptr,
                 std::function<void(std::string)> log={}, std::shared_ptr<mc::windows::FanController> controller={},
                 ResourceWorker::ThreadFactory workerFactory={});
    ~SettingsPage();
    SettingsPage(const SettingsPage&) = delete;
    SettingsPage& operator=(const SettingsPage&) = delete;

    void arrange(const RECT& area, UINT dpi, bool visible);
    bool preTranslate(MSG& message);
    void observeKeyboardLevel(int level);
    [[nodiscard]] TraySettingsState trayState() const;
    void refreshForTray(bool invalidateObservation=false);
    void applyFromTray(TraySetting setting, int value);
    void refreshCommandAvailability();
    void restoreKeyboardCycle();
#ifdef MAGICCONTROL_WINDOWS_SETTINGS_TEST
    void testSeedBatteryProjection(const BatteryReading&);
    bool testBatteryProjectionAvailable() const;
#endif

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace mc::gui
