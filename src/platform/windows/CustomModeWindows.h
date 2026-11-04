#pragma once
#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include "MagicControl/CustomPresets.h"
#include "ApuPowerWindows.h"
namespace mc { class EcAccess; class DeviceOperation; class LaptopControl; }
namespace mc::windows {
bool honorCustomPowerBackend(const LaptopControl*) noexcept;
bool nativeCustomPowerBackend(const LaptopControl*) noexcept;
class Gm6Controller;
class FanController;
class ApuPowerReader;
class NativeFixedFans;
struct CustomPresetSelection {int selected{};bool active{};};
struct CustomCompletionState {
    int& chosenMode;bool& savedIntent;bool& editing;int& selectedPreset;
    std::array<int,3>& values;std::optional<int>& pendingMode;std::optional<int>& pendingPreset;
    std::optional<std::array<int,3>>& pendingValues;bool pendingPresetApplied{};
};
// UI state transition after the existing worker has completed. It does not
// persist settings, perform a device operation or grant write admission.
bool completeCustomSelection(CustomCompletionState,bool passive,bool superseded,bool success,int customIndex) noexcept;
// Read/create only; callers still decide whether initialization is allowed.
CustomPresetSelection loadCustomSelection(std::string_view profile);
// Injected actions permit checking ordering and failures without filesystem or hardware.
CustomPresetSelection initializeCustomSelection(const std::function<void()>& ensure,
    const std::function<CustomPresetLibrary()>& load);
// Caller performs UI admission and owns the lifetime of every supplied object.
// Each branch retains its existing model-specific writer and readback guards.
struct CustomModeContext {
    std::string profileId;
    std::shared_ptr<Gm6Controller> gm6;
    bool honor{};
    bool wujie{};
    EcAccess* ec{};
    std::shared_ptr<FanController> fans;
    std::shared_ptr<DeviceOperation> operation;
    ApuPowerReader* apu{};
    bool unsupportedTyped{};
    LaptopControl* laptop{}; // Typed CPU policy; never a raw EC fallback.
    std::function<bool()> onBattery,fanDefaultLocked;
    std::function<void(std::string)> log;
    std::function<void(std::string_view,bool)> persistStockChoice{setCustomPresetActive};
    std::shared_ptr<NativeFixedFans> nativeFans;
};
enum class PowerWrite { Custom, Preset, Automatic, Economy, Allowed, Stock, CustomEditor, CustomBase };
enum class PowerWriteOrigin { Explicit, Restore };
// Stock profile and preset slot are zero-based inside the application.
struct PowerWriteRequest {PowerWrite kind;std::array<int,3> values{};int slot{};PowerWriteOrigin origin{PowerWriteOrigin::Explicit};PowerOptions options{};};
struct PowerWriteResult {
    std::optional<std::array<int,3>> limits;
    std::optional<ApuPowerDiagnostic> diagnostic;
};
// One CPU/APU write entry for GUI, CLI and restore. The model owner retains
// identity, supply, ownership, codec, readback and rollback. Restore never
// turns an old selection into a new explicit user choice.
PowerWriteResult executePowerResource(const CustomModeContext&,const PowerWriteRequest&);
// Manual editing is deliberately a two-click operation. Callbacks are invoked
// only along the same short-circuit paths as the former card implementation.
enum class CustomClickDecision { Reject, OpenGm, OpenHonor, OpenEc, ApplyGm, ApplyHonor, ApplyEc, OpenNative, ApplyNative };
struct CustomClickContext {
    bool gm{},honor{},editing{},writable{},pl4Double{};
    std::function<bool()> doubleCodecSupported,onBattery,fanDefaultLocked;
    bool unsupportedTyped{};
    bool nativeCustom{};
};
CustomClickDecision decideCustomClick(const CustomClickContext&);
enum class CustomPrepareError { None, Fields, HonorRange };
struct CustomEditorPlan {
    CustomClickDecision decision{CustomClickDecision::Reject};
    CustomPrepareError error{CustomPrepareError::None};
    std::array<int,3> values{};
};
// Prepare the editor or the validated application request only. The caller
// retains the UI and worker lifetime; this function never writes hardware.
struct CustomEditorActions {
    std::function<std::optional<std::array<int,3>>()> readFields;
    std::function<void(std::array<int,3>&)> loadDraft;
    std::function<std::array<int,3>(std::array<int,3>)> prepareHonor;
    std::function<bool(const std::array<int,3>&)> acceptHonor;
    std::function<std::array<int,3>(std::array<int,3>)> nativeDraft;
};
CustomEditorPlan prepareCustomEditor(const CustomClickContext&,std::array<int,3>,const CustomEditorActions&);
struct CustomApplyActions {
    std::function<std::array<int,3>(const std::array<int,3>&)> apply;
    std::function<void(const std::array<int,3>&)> confirmGm;
    std::function<void(std::string)> log;
};
// Worker-thread dispatch for a previously prepared request. No admission is
// granted here: the supplied writer retains all device-specific checks.
void executeCustomEditorApply(const CustomEditorPlan&,const CustomApplyActions&);
std::array<int,3> prepareHonorCustomDraft(std::array<int,3>);
bool honorCustomManualAccepted(const std::array<int,3>&);
// The manual editor and the preset buttons share the same device policies.
// Reading a draft never applies it or changes the fan policy.
std::array<int,3> honorCustomMaximum();
std::array<int,3> honorCustomAllowed(std::array<int,3> values);
std::array<int,3> honorCustomRecipe(int preset);
int gmCustomFieldMaximum(std::string_view profile,int index);
int customFieldMaximum(const CustomModeContext&, int index);
int customFieldMinimum(const CustomModeContext&,int index=-1);
// Pure editor parsing only; draft clamping never applies or saves a setting.
// Hardware writers still validate their own exact-device policies.
std::optional<int> parseCustomField(const wchar_t* text,int minimum,int maximum);
std::optional<int> clampCustomDraftField(const wchar_t* text,int minimum,int maximum);
// Inputs are acquired lazily in field order; a bad field stops before the
// next read or range lookup. This only parses a draft and cannot apply it.
std::optional<std::array<int,3>> parseCustomFields(
    const std::function<std::wstring(int)>& read,
    const std::function<int(int)>& maximum,const std::function<int()>& minimum);
const wchar_t* customFieldsError(std::string_view profile,bool gm,bool honor) noexcept;
bool customLockedByFanDefault(const CustomModeContext&);
void loadCustomManualDraft(std::array<int,3>& values);
}
