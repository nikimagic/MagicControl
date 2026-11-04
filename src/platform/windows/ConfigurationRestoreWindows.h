#pragma once
#include <functional>
#include <memory>
#include <string>
#include "MagicControl/LaptopControl.h"
#include "NativeFixedFans.h"

namespace mc { class EcAccess; }
namespace mc::windows {
class FanController;
// Compare saved choices with available readback; restore only mismatches.
class ConfigurationRestore {
public:
    ConfigurationRestore(EcAccess&, std::shared_ptr<FanController>);
    explicit ConfigurationRestore(std::string profileId,
        std::function<std::unique_ptr<LaptopControl>()> laptop=createLaptopControl,
        std::function<std::shared_ptr<NativeFixedFans>()> native=nativeFixedFanController);
    ~ConfigurationRestore();
    void powerSourceChanged(bool ac);
    void request(std::string reason, std::function<void()> radioAccess = {});
    void suspend();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
