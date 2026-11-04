#pragma once
#include "MagicControl/KeyResourceControl.h"
#include "MagicControl/PortableSettings.h"
#include "WindowsDevices.h"
#include <array>
#include <functional>
#include <string>
#include <string_view>

namespace mc {
class EcAccess;
class LaptopControl;
namespace windows {
class Gm6Controller;

// Borrowed typed context for explicit CLI/resource entries. The GUI factory
// below owns its adapters until common operation/lifecycle workers have joined.
struct KeysHardwareContext {
    std::string_view profile;
    bool preview{};
    EcAccess* ec{};
    LaptopControl* laptop{};
    Gm6Controller* native{};
};
bool keysCameraEcAvailable(const KeysHardwareContext&);
DeviceState readKeySystemDevice(const KeysHardwareContext&,KeyAction);
KeyObservation observeKeyHardware(const KeysHardwareContext&, KeyAction);
enum class KeyApplyMode {Set,Observe};
void applyKeyHardware(const KeysHardwareContext&,KeyAction,bool on,KeyApplyMode mode=KeyApplyMode::Set);
// Reads the same quick-switch, OEM and OS backends in their previous order.
// The caller owns worker scheduling and local OSD preference display.
KeySnapshot readKeysHardware(const KeysHardwareContext&);
// Combines the existing hardware observation with application-owned OSD state.
// This is presentation data only; no preference or device operation occurs.
KeySnapshot composeKeysSnapshot(KeySnapshot,bool osdDisabled);

// Call on the GUI STA before requesting the lifecycle worker. Only Wi-Fi is
// prepared: Bluetooth remains untouched on startup and resume.
std::function<void()> prepareKeysRestoreRadio(std::string_view profile);

// Called on the GUI thread so the existing OSD apply callback keeps
// its thread affinity. On a standard exception, restore the previous choice.
void applyKeyPreferenceTransaction(KeyAction,bool,const ApplicationPreferences&,
    const std::function<void(const ApplicationPreferences&)>& apply,
    const std::function<void()>& finalize);
// Called on the existing worker. The radio callback was acquired on the STA.
// Publish the fresh observation before persistence, retaining the caller's
// other rows. GUI and CLI receive the same typed backend result.
KeyBackendResult executeKeyHardwareCommand(const KeysHardwareContext&,KeyAction,bool,
    const std::function<void()>& radioAccess={},KeySnapshot baseline={});
std::unique_ptr<KeysOperations> createKeysOperations(EcAccess*,bool preview,ApplicationPreferences&,
    std::function<void(const ApplicationPreferences&)> applyPreferences,
    std::function<std::uint64_t(KeyAction,bool)> beginNotice,std::function<void()> ready);

}
}
