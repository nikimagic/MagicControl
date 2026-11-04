#pragma once
#include "MagicControl/Performance.h"
#include "MagicControl/Monitoring.h"
#include "MagicControl/LaptopControl.h"
#include "MagicControl/KeyboardBacklight.h"
#include "MagicControl/LightingZones.h"
#include "MagicControl/DisplayContinuity.h"
#include "MagicControl/IEcBackend.h"
#include "ApuPowerWindows.h"
#include "NativeBatteryWindows.h"
#include "../../devices/mechrevo/gm6ir0c/ControlWindows.h"
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace mc::windows { class IntelTelemetryReader; class IteLightingController; }
namespace mc::gui {
// Carries measured/retained values without changing their original timestamps.
// The resource operation owner augments this snapshot with battery/SMU telemetry.
struct SettingsSnapshot {
    std::uint8_t battery{};
    std::uint8_t batteryGate{}, batteryPlatform{},batteryRom{},batteryProject{};
    std::uint8_t performance{};
    std::uint8_t keyboard{};
    std::uint8_t effect{};
    std::uint8_t breathing{};
    std::optional<std::uint8_t> keyboardPulseRaw;bool keyboardRetained{};
    std::optional<KeyboardIdleState> keyboardIdle;
    std::array<std::uint8_t,3> color{};
    std::uint8_t adapterClass{},powerInput{},typecStatus{},typecPowerA{},typecPowerB{};
    bool chargerRead{};
    std::optional<UsbPowerPriorityStatus> usbPriorityStatus;
    EcPerformanceState power;
    windows::ApuPowerDiagnostic apu;
    std::optional<double> intelWatts;
    std::string intelPowerSource,intelPowerStatus;
    BatteryReading batteryTelemetry;
    BatteryReading osBattery;
    EcBatteryReading ecBattery;
    BatteryPowerPolicy batteryPolicy;
    LaptopControlSnapshot oem;
    windows::Gm6State gm6;
    windows::NativeBatteryState nativeBattery;
    windows::NativeBatteryState voltageBattery;
    bool valid{},keyboardSupported{},keyboardWritable{},onBattery{true};
    std::string keyboardOwnerStatus;
    KeyboardCycleStatus cycle;
    LightingZonesSnapshot lighting;
    friend bool operator==(const SettingsSnapshot&,const SettingsSnapshot&)=default;
};
struct SettingsDisplayTransition {bool changed{},acceptedBattery{},accepted{};};
// Applies only already collected observations to the display snapshot.
// Failed observations remove write eligibility while keeping display caches;
// no device or preference writes occur here.
SettingsDisplayTransition transitionSettingsDisplay(SettingsSnapshot& state,SettingsSnapshot& pending,
    bool clearActual,bool superseded,bool retainSingleZone,SingleZoneModel model,bool& loaded,
    EcBatteryDisplayCache&,LaptopDisplayCache&,KeyboardDisplayCache&);
// Serialize the observed snapshot for the existing action journal; this does
// not sample hardware or infer that a requested setting has been applied.
std::string settingsSnapshotAudit(const SettingsSnapshot&);
// Display only, storage order STAPM/Slow/Fast. Exact Wujie uses its accepted
// EC cache; no implicit fallback to a request, preset table or SMU comparison.
std::array<std::optional<double>,3> observedSettingsPowerLimits(const SettingsSnapshot&,bool gm,bool wujie);
std::string wujiePowerComparisonText(const SettingsSnapshot&);
// Translate only the supplied observation; these helpers grant no admission.
void projectSettingsHidZone(SettingsSnapshot&,LightingZone);
void collectSettingsHidLighting(SettingsSnapshot&,windows::IteLightingController*,LightingZone);
void projectSettingsSingleZone(SettingsSnapshot&,const KeyboardBacklightState&,bool ownerWritable,std::string_view ownerStatus);
void collectSettingsSingleZone(SettingsSnapshot&,const KeyboardBacklightState&);

struct SettingsSnapshotContext {
    EcAccess* ec{};
    LaptopControl* laptop{};
    windows::Gm6Controller* gm{};
    KeyboardColorCycle* cycle{};
    windows::IteLightingController* hid{};
    LightingZone zone{LightingZone::Keyboard};
    // Optional injected collector for read-failure tests; production passes
    // the typed HID backend above, never a callback into a GUI page.
    std::function<void(SettingsSnapshot&)> hidObservationOverride;
};
SettingsSnapshot collectSettingsSnapshot(const SettingsSnapshotContext&);
// Read-only fast observation, executed by the same serialized SETs worker.
std::optional<KeyboardIdleState> collectSettingsKeyboardIdle(const SettingsSnapshotContext&);

// Worker-owned supplemental readers; no UI handles or writes. The priority
// callback is re-evaluated at each existing admission point, so an interactive
// command arriving during a passive poll continues to defer optional telemetry.
struct SettingsSupplementalContext {
    const SettingsSnapshot& previous;
    bool gm6NativePower{},quickBattery{},quickProfile{},saveChoice{},intelCpuUi{},gm5Ui{};
    int command{},batteryVoltageMode{-1};
    std::uint64_t started{},appliedAt{};
    std::string profile;
    EcAccess* ec{};
    LaptopControl* laptop{};
    windows::Gm6Controller* gm{};
    windows::NativeBatteryController* nativeBattery{};
    windows::NativeBatteryController* voltageBattery{};
    MonitoringReader& batteryReader;
    windows::IntelTelemetryReader& intelMonitor;
    windows::ApuPowerReader& apuPowerReader;
    std::function<bool()> priorityPending;
    std::function<void(std::string)> log;
    std::string& lastApuLog;
};
void collectSettingsSupplemental(SettingsSnapshot&,const SettingsSupplementalContext&);
// Failure recovery is read-only and preserves the existing read order. A
// false return means that no complete observation could be obtained.
bool recoverSettingsObservation(SettingsSnapshot&,const SettingsSnapshotContext&,
                                windows::NativeBatteryController*,MonitoringReader&,
                                windows::ApuPowerReader&);
bool observedGmPowerMatches(const SettingsSnapshot&,const std::array<int,3>&,
                            std::string_view profile);
}
