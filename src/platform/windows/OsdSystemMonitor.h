#pragma once
#include <functional>
#include <memory>
#include <string>
#include "MagicControl/SystemEvent.h"

namespace mc::windows {
// Observes Windows state. This component has no EC backend and cannot write
// volume, brightness, touchpad state or any hardware setting.
class OsdSystemMonitor {
public:
    using Callback=std::function<void(SystemEvent)>;
    OsdSystemMonitor(Callback callback,std::function<void(std::string)> log,
                     std::function<void()> keyboardActivity = {});
    ~OsdSystemMonitor();
    OsdSystemMonitor(const OsdSystemMonitor&)=delete;
    OsdSystemMonitor& operator=(const OsdSystemMonitor&)=delete;
    void touchpadHotkey();
    void audioHotkey(bool microphone=false);
    void brightnessFallback(int percent);
    void cameraHotkey(std::uint64_t receivedAt);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
