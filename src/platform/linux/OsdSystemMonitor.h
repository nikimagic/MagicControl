#pragma once
#include <functional>
#include <memory>
#include <string>
#include "MagicControl/SystemEvent.h"

namespace mc::linuxplatform {
// Read-only desktop notifications: PulseAudio/PipeWire and GNOME/KDE/UPower.
class OsdSystemMonitor {
public:
    using Callback=std::function<void(SystemEvent)>;
    OsdSystemMonitor(Callback callback,std::function<void(std::string)> log);
    ~OsdSystemMonitor();
    OsdSystemMonitor(const OsdSystemMonitor&)=delete;
    OsdSystemMonitor& operator=(const OsdSystemMonitor&)=delete;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
