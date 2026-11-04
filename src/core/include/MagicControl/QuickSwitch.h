#pragma once
#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace mc {
class EcAccess;
enum class QuickSwitch { Windows, Fn, Usb, Ac };
inline constexpr std::array quickSwitches{QuickSwitch::Windows,QuickSwitch::Fn,QuickSwitch::Usb,QuickSwitch::Ac};
struct QuickSwitchState {
    std::optional<bool> enabled;
    bool writable{};
    std::string detail;
};
using QuickSwitchStates=std::array<QuickSwitchState,4>;
// Admission is exact-board, not a vendor/CPU/backlight capability inference.
[[nodiscard]] constexpr bool uniwillQuickSwitchProfile(std::string_view id) noexcept {
    return id=="wu-jie-15-xpro"||id=="mechrevo-gm6ir0c"||id=="mechrevo-gm5hg0a"||
        id=="machenike-l16"||id=="eluktronics-hydroc16g2";
}
[[nodiscard]] bool readQuickSwitch(EcAccess&,QuickSwitch);
// Only the selected field is changed. Windows Lock is a single toggle command
// with status readback, never a retry of the self-clearing command.
void setQuickSwitch(EcAccess&,QuickSwitch,bool);
// Native Copilot lock ABI: OEM HYDROC5.23/L16 5.60 SetCopilotKey and the
// keyboard handler in the owner's Wujie BIOS EC payload. This is protocol
// capability metadata, not permission to copy the byte to another firmware.
[[nodiscard]] constexpr bool uniwillCopilotEcProfile(std::string_view id) noexcept {
    return id=="wu-jie-15-xpro"||id=="machenike-l16"||id=="eluktronics-hydroc16g2";
}
[[nodiscard]] bool readCopilotKey(EcAccess&);
void setCopilotKey(EcAccess&,bool);
// Exact Wujie OEM MicMuteControl.SetLED:7A6.2 mirrors capture mute. This
// controls the EC indicator request, not audio capture or guaranteed LED fitment.
constexpr bool uniwillMicrophoneLedProfile(std::string_view id) noexcept{return id=="wu-jie-15-xpro";}
bool readMicrophoneMuteLed(EcAccess&);
void setMicrophoneMuteLed(EcAccess&,bool muted);
}
