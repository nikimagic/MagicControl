#pragma once
#include "../../../protocols/lenovo/LegionFans.h"
#include <memory>
namespace mc::windows::legion {
// Shared ISA lease, physical5507, exact SRAM/host aliases. Only FAN normal RAM
// rows/count and the two E63C/E99A hold bits are writable; no commands/flash/SFR.
class FanRam {
public:
    FanRam();~FanRam();
    std::uint8_t read(std::uint16_t address);
    lenovo::FanPacket table();
    void write(std::uint16_t address,std::uint8_t value);
    void finish();
private:struct Impl;std::unique_ptr<Impl> impl_;
};
}
