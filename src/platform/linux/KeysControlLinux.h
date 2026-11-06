#pragma once
#include "MagicControl/KeyResourceControl.h"
#include "MagicControl/PortableSettings.h"
#include "MagicControl/MicrophoneControl.h"
#include <mutex>
#include <optional>
#include <string_view>
#include <utility>

namespace mc {
class LaptopControl;
struct LaptopControlSnapshot;
namespace linux_platform {
KeyObservation observeKeyForRestore(std::string_view profile, bool preview,
                                    LaptopControl* control, std::mutex& adapterGate,
                                    KeyAction key);
void applyKey(std::string_view profile, bool preview, LaptopControl* control,
              std::mutex& adapterGate, KeyAction key, bool on);
void readKeyState(LaptopControl* control, std::mutex& adapterGate,
                  LaptopControlSnapshot& snapshot, MicrophoneState& microphone);
void confirmKey(KeyAction key, bool on, const LaptopControlSnapshot& snapshot,
                const MicrophoneState& microphone, std::string& error);
// KeysOperations (or explicit CLI caller) retains DeviceOperation admission,
// worker lifetime and OSD. A failed
// setter is followed by the same read-only observation as an ordinary poll.
void executeKeyOperation(std::string_view profile, bool preview, LaptopControl* control,
                         std::mutex& adapterGate,
                         std::optional<std::pair<KeyAction,bool>> request,
                         LaptopControlSnapshot& snapshot, MicrophoneState& microphone,
                         std::string& error);
// OSD preference lives solely in keys-settings.ini; preserve the
// caller's values when the file is missing or cannot be read.
bool loadApplicationKeyPreferences(bool& osdDisabled,std::string& error);
bool saveOsdPreference(bool disabled, std::string& error);
std::unique_ptr<KeysOperations> createKeysOperations(bool preview,ApplicationPreferences&,
    std::function<void(const ApplicationPreferences&)> applyPreferences,
    std::function<std::uint64_t(KeyAction,bool)> beginNotice,std::function<void()> ready,
    std::string presentationProfile={});

}
}
