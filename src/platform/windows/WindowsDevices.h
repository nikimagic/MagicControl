#pragma once

#include <optional>
#include <string>
#include <functional>
#include <vector>
#include <cstdint>

namespace mc { class EcAccess; }

namespace mc::windows {

enum class DeviceKind { Wifi, Bluetooth, Camera, Touchpad, Microphone };

struct DeviceState {
    std::optional<bool> enabled;
    bool available{};
    std::string detail;
};

[[nodiscard]] DeviceState readDevice(DeviceKind kind);
[[nodiscard]] std::optional<bool> readTouchpadEnabled();
void setDevice(DeviceKind kind, bool enabled);
// Start on the GUI STA; invoke the returned completion on a worker before setDevice.
[[nodiscard]] std::function<void()> requestRadioAccessForUser();
[[nodiscard]] DeviceState readCamera(EcAccess& access);
void setCamera(EcAccess& access, bool enabled);
void setWindowsKeyLocked(EcAccess& access, bool locked);

// Present PCI LPC/eSPI bridges only (class0601), from Windows PnP metadata.
// Does not open a driver, access PCI config space or change device state.
[[nodiscard]] std::vector<std::uint16_t> presentIntelLpcIds();

}  // namespace mc::windows
