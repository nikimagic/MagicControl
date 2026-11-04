#pragma once
#include "MagicControl/KeyboardBacklight.h"
#include "MagicControl/LightingZones.h"
#include <filesystem>
#include <memory>
#include <string>
namespace mc {class EcAccess;class LaptopControl;class DeviceOperation;}
namespace mc::windows {class Gm6Controller;class IteLightingController;}
namespace mc::gui {
struct SettingsSnapshot;
struct HidLightingMenuChoice { LightingConfiguration configuration; std::string description; };
// Decode a menu choice without touching the backend; unknown menu IDs are
// rejected and unrelated configuration fields remain unchanged.
std::optional<HidLightingMenuChoice> selectHidLightingMenu(LightingConfiguration,int selection);
// Read-only presentation of an already collected snapshot. These helpers do
// not access hardware, persist settings or authorize a lighting command.
int observedLightingEffect(const SettingsSnapshot&, bool hid, LightingZone) noexcept;
int observedLightingBrightnessIndex(const SettingsSnapshot&) noexcept;
std::optional<int> observedLightingBrightness(const SettingsSnapshot&, bool preview, bool hid,
                                              bool oem, LightingZone) noexcept;
int observedLightingPalette(const SettingsSnapshot&, bool hid, LightingZone, SingleZoneModel,
                            std::string_view profile, const std::array<KeyboardColor,8>& previews);
// Captures the selected zone/configuration and the live backend; the caller
// still owns admission, scheduling and all objects for the operation lifetime.
struct LightingControlContext {
    windows::IteLightingController* ite{};
    windows::Gm6Controller* gm{};
    LaptopControl* laptop{};
    EcAccess* ec{};
    KeyboardColorCycle* cycle{};
    LightingZone zone{LightingZone::Keyboard};
    LightingConfiguration configuration{};
    SingleZoneModel model{SingleZoneModel::Wujie};
    std::string profile;
    std::filesystem::path preferences;
    KeyboardCycleSettings cycleSettings{};
    bool nativeCycleSettings{};
};
// Presentation only: this mirrors existing button conditions and grants no
// transport access. Writers still enforce their model-specific admission.
struct LightingButtonInput {
    bool ready{},supported{},writable{},hidPalette{};
    int effect{},brightnessMaximum{};
    std::array<std::uint8_t,8> pulsePresets{};
};
struct LightingButtonAvailability {
    bool effect{},native{},custom{},cycle{};
    std::array<bool,8> colors{};
    std::array<bool,5> levels{};
};
LightingButtonAvailability lightingButtonAvailability(const LightingButtonInput&) noexcept;
KeyboardBacklightState readLighting(const LightingControlContext&);
LightingZonesSnapshot readLightingZones(windows::IteLightingController&);
// One command entry for GUI/CLI/restore/cycle. The selected model retains all
// transport, ownership, mask, readback and persistence rules; context is not permission.
LightingWriteResult executeLightingResource(const LightingControlContext&,const LightingWriteRequest&);
enum class LightingCycleEditResult { Unchanged, Saved, Failed, Invalid };
using LightingCycleSaver=std::function<bool(const KeyboardCycleSettings&,std::string&)>;
// In-memory edit precedes persistence as in the original page. No hardware
// operation, scheduling or admission is performed here.
LightingCycleEditResult editLightingCycleInterval(KeyboardCycleSettings&,int,std::string&,const LightingCycleSaver&);
LightingCycleEditResult editLightingCycleColor(KeyboardCycleSettings&,int,const KeyboardColor&,std::string&,const LightingCycleSaver&);
// Called only after queued actions and native refresh have been processed.
bool shouldRestoreLightingCycle(bool passive,bool pending,bool nativePower,bool hasCycle,
    bool keyboardConfirmed,bool keyboardSuspended,bool keyboardRestorePending);
// A failed reservation leaves pending untouched; a successful one consumes it.
std::shared_ptr<DeviceOperation> reserveLightingCycleRestore(bool& pending);
// Caller already holds the reservation. Preserve the existing saved preference
// and cycle-file read order; false means the saved cycle was not enabled.
bool loadLightingCycleRestore(const std::filesystem::path&,KeyboardCycleSettings&);
enum class LightingChoice {Level,Effect,Color,CustomColor,Native};
struct LightingObservation {
    bool supported{},cycleActive{},currentCycleActive{};
    std::uint8_t keyboard{},effect{},breathing{};
    KeyboardColor color{};
};
// Called only after the worker has obtained its existing fresh observation.
// HID saves per-zone in its backend; Honor persists through its own firmware.
void rememberLightingChoice(const LightingControlContext&,LightingChoice,int,const LightingObservation&);
void rememberObservedLightingLevel(const LightingControlContext&,int);
struct ExternalLightingLevelContext {
    SettingsSnapshot& state;
    std::uint64_t& revision;
    std::optional<int>& observedLevel;
    bool preview{},oem{},busy{},reading{};
    std::function<bool()> operationBusy;
    std::function<void(int)> remember;
    std::function<void(std::string)> log;
};
// Processes a Uniwill event only; it neither samples nor writes hardware.
// Returns whether the existing screen selection needs refreshing.
bool observeExternalLightingLevel(ExternalLightingLevelContext&,int level);
}
