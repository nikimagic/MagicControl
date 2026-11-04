#pragma once
#include <cstdint>
#include <memory>
#include <vector>
#include <stdexcept>
namespace mc::windows {
class HlylI2EcFinishError:public std::runtime_error {
public:using std::runtime_error::runtime_error;
};
// Internal HLYL BIOS1.09 transport. Identification happens before port access.
// Separate whitelists prevent a lighting setter from writing fan fields.
class HlylI2EcSession {
public:
    HlylI2EcSession();
    ~HlylI2EcSession();
    HlylI2EcSession(const HlylI2EcSession&)=delete;
    HlylI2EcSession& operator=(const HlylI2EcSession&)=delete;
    bool writable{};
    std::uint8_t read(std::uint16_t address);
    // Required before publishing a successful read/write result. Destruction
    // remains best-effort fallback; it cannot report a failed PNPCFG exit.
    void finish();
    // RAM diagnostic window only, not peripheral/SFR/flash space. Retains the
    // pinned PawnIO module, releases PNPCFG/ISA mutex after every small block.
    std::vector<std::uint8_t> readDiagnosticBlock(std::uint16_t address,std::size_t count);
    // Only the guarded lighting controller may defer exact duty equality until
    // its native-idle classifier readback. Level/shadow keep strict readback.
    void writeBacklight(std::uint16_t address,std::uint8_t value,bool nativePwmReadback=false);
    void writeFanRuntime(std::uint16_t address,std::uint8_t value);
private:
    void verifyAccess();
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
