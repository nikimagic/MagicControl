#pragma once
#include <cstdint>

namespace mc {
enum class SystemEventKind { Volume, Microphone, Brightness, KeyboardBacklight, CapsLock, NumLock, ScrollLock, Touchpad, Camera };
struct SystemEvent {
    SystemEventKind kind{};
    int value{-1};
    bool muted{};
    std::uint64_t receivedAt{};
    int maximum{100};
};
}
