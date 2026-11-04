#pragma once

#include "MagicControl/Monitoring.h"
#include "MagicControl/ResourceControl.h"
#include "MagicControl/BoardIdentity.h"
#include "MagicControl/DisplayContinuity.h"
#include "MagicControl/OsdMessage.h"
#include "SettingsObservationEpoch.h"
#include "MagicControl/BatteryResourceState.h"
#include "MagicControl/BatteryUiPolicy.h"
#include "MagicControl/LaptopControl.h"
#include "MagicControl/SettingsPreview.h"
#include "SettingsWorkerLinux.h"
#include <string>
#include <string_view>
#include <optional>

namespace mc { struct ApplicationPreferences; }
namespace mc::linux_platform {
// Process-wide application-control ownership shared by primary GUI and write
// CLI, matching Windows refusal to control concurrently. Read-only CLI stays free.
std::shared_ptr<void> acquireApplicationControlLease();
// Presentation readiness only; the typed setter still performs admission and
// readback. An unknown backend must not expose Honor battery actions.
bool batteryControlsAvailable(const LaptopControl*,const LaptopControlSnapshot&) noexcept;
// These services use only the already-admitted typed LaptopControl backend.
// Negative command values mean no change; the UI retains queue and lifecycle.
int loadSelectedCustomPreset(std::string_view profile);
// App preferences remain in the existing app-settings.ini schema; startup
// creates a default only when the file is absent without a read error.
void loadOrCreateApplicationSettings(ApplicationPreferences& preferences, std::string& error);
bool saveApplicationLanguage(unsigned language, std::string& error);
// Read the same sysfs online attributes used by the tray label, without
// changing power state or inferring a battery charging status.
bool hasAnyOnlinePowerSupply();
std::string readHostnameFallback();
struct StartupSettingsPreparation {
    std::optional<std::string> portableError;
    std::optional<std::string> keysError;
};
// Preserves migration -> startup identity history -> KEYs migration ordering.
StartupSettingsPreparation prepareStartupSettings();
void collectSettingsReadings(MonitoringReader& reader, LaptopControl* control,
                             BatteryReading& battery, BatteryReading& osBattery,
                             EcBatteryReading& ecBattery, BatteryPowerPolicy& policy,
                             LaptopControlSnapshot& snapshot);
bool settingsReadbackMatches(const LaptopControlSnapshot& snapshot, int upper,
                             int restart, int keyboardLevel, int keyboardTimeout);
// Route a typed SETs request to its registered resource handler. The actual
// backend still owns admission, validation, ordered writes and readback.
struct SettingsReadingOutputs {
    BatteryReading& battery;
    BatteryReading& osBattery;
    EcBatteryReading& ecBattery;
    BatteryPowerPolicy& policy;
    LaptopControlSnapshot& snapshot;
};
// Invoked on the caller's worker thread; owns neither reader nor backend.
// Retains the admitted LaptopControl and the original read/write ordering.
SettingsWorkerResult executeSettingsOperation(MonitoringReader& reader, LaptopControl* control,
                                              SettingsWorkerRequest request, SettingsReadingOutputs output);
struct SettingsPageResult {
    std::string message;bool success{},accepted{},workerEscaped{},startFailed{},superseded{},retry{};
    std::uint64_t token{};mc::gui::osd::Message notice;
};
class SettingsOperationsLinux {
public:
    SettingsOperationsLinux(BoardDescriptor,bool preview);
    ~SettingsOperationsLinux(){shutdown();}
private:
    const BoardDescriptor pageBoard;bool preview{};int brightnessMaximum{};SettingsPreviewState previewState_;
    BatteryCardView batteryView;int selectedCustomPreset{};
    std::unique_ptr<MonitoringReader> reader;BatteryReading reading;mc::BatteryResourceState batteryResources;
    EcBatteryDisplayCache ecDisplayCache;LaptopDisplayCache oemDisplayCache;
    BatteryReading osBattery;EcBatteryReading ecBattery;BatteryPowerPolicy batteryPolicy;
    std::unique_ptr<LaptopControl> oem;LaptopControlSnapshot oemState;
    SettingsObservationEpoch observationEpoch;
public:
    // UI-thread read bindings: no copied cache, backend handle or writable state.
    struct Presentation {
        const decltype(SettingsOperationsLinux::pageBoard)& pageBoard;
        const decltype(SettingsOperationsLinux::preview)& preview;
        const decltype(SettingsOperationsLinux::brightnessMaximum)& brightnessMaximum;
        const decltype(SettingsOperationsLinux::batteryView)& batteryView;
        const decltype(SettingsOperationsLinux::selectedCustomPreset)& selectedCustomPreset;
        const decltype(SettingsOperationsLinux::reading)& reading;
        const decltype(SettingsOperationsLinux::batteryResources)& batteryResources;
        const decltype(SettingsOperationsLinux::osBattery)& osBattery;
        const decltype(SettingsOperationsLinux::ecBattery)& ecBattery;
        const decltype(SettingsOperationsLinux::batteryPolicy)& batteryPolicy;
        const decltype(SettingsOperationsLinux::oemState)& oemState;
        const bool oem;
    };
    Presentation presentation()const{return {pageBoard,preview,brightnessMaximum,batteryView,selectedCustomPreset,reading,batteryResources,osBattery,ecBattery,batteryPolicy,oemState,static_cast<bool>(oem)};}
    const SettingsPreviewState& previewState()const noexcept{return previewState_;}
    bool previewStockPower(int value)noexcept{if(!preview)return false;previewState_.stockPower(value);return true;}
    bool previewCustomPower()noexcept{if(!preview)return false;previewState_.customPower(4);return true;}
    bool previewCustomPreset(int value)noexcept{if(!preview)return false;previewState_.customPreset(value,4);return true;}
    bool previewEffect(int value)noexcept{if(!preview)return false;previewState_.effect=value;return true;}
    bool previewColor(int value)noexcept{if(!preview)return false;previewState_.color=value;return true;}
    bool previewBrightness(int value)noexcept{if(!preview)return false;previewState_.brightness=value;return true;}
    bool previewBatteryUpper(int value)noexcept{if(!preview)return false;previewState_.batteryUpper=value;previewState_.batteryMode=-1;return true;}
    bool previewBatteryMode(int value)noexcept{if(!preview)return false;previewState_.batteryMode=value;previewState_.batteryUpper=-1;return true;}
    bool previewBatteryAging(int value)noexcept{if(!preview)return false;previewState_.batteryAging=value;return true;}
    std::string selectBatteryView(BatteryCardView);
    bool submit(SettingsWorkerRequest,std::function<void()> ready);
    std::optional<SettingsPageResult> take();
    bool transitionSleep(bool);
    bool asleep()const noexcept{return observationEpoch.asleep();}
    bool busy()const noexcept{return worker_.busy();}
    bool chargeAvailable()const noexcept;
    void shutdown(){worker_.join();}
    void bindNotice(std::function<std::uint64_t(const mc::gui::osd::Message&)> begin){beginNotice_=std::move(begin);}
    // Isolated fixtures only; no production caller exposes this seam.
private:
    friend struct SettingsOperationsTestAccess;
    int testWorkerFault{};
    struct Result {BatteryReading battery,osBattery;LaptopControlSnapshot oem;EcBatteryReading ecBattery;BatteryPowerPolicy policy;std::string message;bool success{};std::uint64_t epoch{},token{};mc::gui::osd::Message notice;};
    ResourceWorker worker_;Result pending_;std::optional<SettingsPageResult> immediate_;
    std::function<std::uint64_t(const mc::gui::osd::Message&)> beginNotice_;
};

}
