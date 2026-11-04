#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
namespace mc::linux_platform {
// Standard portal global shortcut. No evdev capture, root access or key logging.
// Compositor/user grants the binding; absence/denial never becomes KEY_UNKNOWN.
class DisplayRotationHotkey {
public:
    DisplayRotationHotkey(std::function<void(std::uint64_t)>,std::function<void(std::string)>);
    ~DisplayRotationHotkey();
    bool bound()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
}
