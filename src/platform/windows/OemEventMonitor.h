#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include "MagicControl/ResourceControl.h"

namespace mc::windows {

class OemEventMonitor {
public:
    using Callback = std::function<void(const std::string& provider, std::uint64_t code)>;
    OemEventMonitor(Callback callback, std::function<void(std::string)> log,std::function<void(std::string)> subscribed={});
    ~OemEventMonitor();
    OemEventMonitor(const OemEventMonitor&) = delete;
    OemEventMonitor& operator=(const OemEventMonitor&) = delete;

private:
    void run();
    Callback callback_;
    std::function<void(std::string)> log_;
    std::function<void(std::string)> subscribed_;
    std::atomic<bool> stop_{false};
    ResourceActor worker_;
};

}  // namespace mc::windows
