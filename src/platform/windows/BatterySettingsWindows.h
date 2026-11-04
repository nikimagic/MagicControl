#pragma once
#include "MagicControl/BatteryEcPolicy.h"
#include "MagicControl/LaptopChargeResource.h"
#include <array>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace mc {
class EcAccess;
class LaptopControl;
struct DevicePreferences;
namespace gui { struct SettingsSnapshot; }
namespace windows {
class NativeBatteryController;

// Presentation of an existing observation only: it does not authorize or
// change charge control. Keep native, OEM and EC interpretations distinct.
int selectedBatteryThreshold(const gui::SettingsSnapshot&, bool native, bool oem,
                             std::string_view profile) noexcept;
bool batteryPolicyActive(const gui::SettingsSnapshot&, bool native) noexcept;
// A typed backend reporting charge values is not automatically an Honor
// implementation. Identity is descriptive; the backend still admits writes.
bool honorBatteryBackend(const LaptopControl*) noexcept;
// Render the port description solely from the supplied snapshot. The fallback
// is the existing OS battery status formatter; this function never polls EC.
std::wstring batteryPortSummary(const gui::SettingsSnapshot&,bool preview,bool native,
                                bool gm,bool oem,bool hasEc,const std::function<std::wstring()>& fallback);

// UI availability only. These observations never authorize a battery or EC
// command; the existing backend identity and operation guards remain in force.
struct BatteryEditorInput {
    bool preview{},busy{},reading{},interactiveBusy{},native{},oem{},ec{},gmAmd{};
    bool nativePercentageWritable{},oemChargeWritable{},snapshotValid{},hasObservation{};
};
struct BatteryEditorAvailability {bool address{},value{},restart{};int restartMaximum{};};
BatteryEditorAvailability batteryEditorAvailability(const BatteryEditorInput&) noexcept;
struct BatteryButtonAvailability {bool percentage{};std::array<bool,3> policy{};};
struct NativeVoltageButtonInput {bool preview{},busy{},reading{},backend{},writable{},context{},appPresent{};};
// Presentation-only: backend admission and fresh readback remain in the setter.
// The gate is queried lazily for each button, preserving the original timing.
bool nativeVoltageButtonEnabled(const NativeVoltageButtonInput&,int mode,const std::function<bool()>& interactiveBusy);
BatteryButtonAvailability gmBatteryButtonAvailability(bool idle,bool queued,bool percentageWritable,bool enforcementWritable) noexcept;
BatteryButtonAvailability honorBatteryButtonAvailability(bool idle,bool chargeWritable,bool turboWritable,const std::function<bool()>& onBattery);
struct EcBatteryButtons {bool enabled{},native{},valid{},busy{},reading{},queued{},interactiveBusy{},percentageWritable{},enforcementWritable{};};
BatteryButtonAvailability ecBatteryButtonAvailability(const EcBatteryButtons&) noexcept;

// The caller owns the DeviceOperation and every supplied object. Profile is
// the presentation profile already selected by SettingsPage, not EC identity.
// Existing exact-board backends still enforce their own identity and guards.
struct BatterySettingsContext {
    NativeBatteryController* native{};
    NativeBatteryController* voltage{};
    LaptopControl* laptop{};
    EcAccess* ec{};
    std::string profile;
    std::function<void(std::string)> log;
};
// One GUI/CLI/lifecycle entry. Presentation intent is translated here, while
// exact device transactions/readback/rollback stay with the existing adapters.
// Confirmed completion is shared; no frontend adds another settings writer.
std::string executeBatteryResource(const BatterySettingsContext&,const BatteryWriteRequest&);
// Pure Wujie saved-intent conversion. No EC reads, persistence or write admission.
std::optional<BatteryWriteRequest> savedBatteryWriteRequest(const DevicePreferences&);

}
}
