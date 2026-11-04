#pragma once
#include <string>
namespace mc::windows {
// Fixed read-only fan evidence from BIOS1.09 EC code (file offset13A50).
// Candidate manual fields are diagnostic only until a physical RPM A/B check.
struct HlylFanDiagnostic {int flags,period,duty0,duty1,target0,target1;};
HlylFanDiagnostic readHlylFanDiagnostic();
struct HlylBacklightState {
    int level{};bool writable{};
    int actualDuty{},period{};
    // Native selected brightness and effective PWM differ during idle fade.
    // Phase is model-specific evidence, not permission for more EC addresses.
    std::string phase,status;
};
// Only HLYL-WXX9-PCB + BIOS1.09 + IT8987. No flash, no fan registers.
HlylBacklightState readHlylBacklight();
void setHlylBacklight(int level);
// Parent-process, fixed read-only context. Never call from a killable helper:
// even reads use a checked configuration-port session. No sweep/target writes.
std::string readHlylKeyboardDiagnosticJson();
}
