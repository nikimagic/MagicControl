#pragma once
#include <cstdint>
#include <memory>
#include <vector>
namespace mc::windows {
// Exact HPBSD/BIOS1.06/CPUID and physical5571/live signature admission.
// Holds the shared ISA mutex. No PNPCFG mode, flash, PMC or fan override writes.
class HpbsdI2EcSession {
public:
    HpbsdI2EcSession();
    ~HpbsdI2EcSession();
    std::uint8_t read(std::uint16_t address);
    std::vector<std::uint8_t> readRamBlock(std::uint16_t address,std::size_t count);
    // Only normal CPU bases/slopes and normal system bases; critical rows,
    // thresholds, overrides088A/088B, PWM/SFR and BIOS selectors are excluded.
    void writeFanByte(std::uint16_t address,std::uint8_t value);
    // Backend factory transition only, after a journalled complete canonical
    // table plan. No arbitrary selector, EC command, CMOS or SMI operation.
    void selectFanFactory(std::uint8_t expected,std::uint8_t desired);
    void finish();
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
