#pragma once
#include <memory>
#include <optional>
#include <string>
#include <array>
#include "MagicControl/LaptopControl.h"
namespace mc {class LaptopControl;}
namespace mc::windows {
std::optional<CpuPowerChoice> savedHpbsdPowerChoice();
std::unique_ptr<LaptopControl> createHpbsdPowerControl();
std::optional<int> savedHpbsdPowerProfile();
void saveHpbsdPowerProfile(int);
void saveHpbsdCustomPower(const std::array<int,3>& values,int baseProfile);
void requestHpbsdConfigurationRestore(const std::string& reason);
void suspendHpbsdPower();
void shutdownHpbsdPower();
}
