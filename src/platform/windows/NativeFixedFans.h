#pragma once
#include "MagicControl/NativeDefaultFan.h"
#include <array>
#include <memory>
#include <string>
#include "MagicControl/FanMaximum.h"
#include "MagicControl/DiagnosticDump.h"
#include "MagicControl/NativeFanCurve.h"
#include "MagicControl/Monitoring.h"
#include <stdexcept>
namespace mc::windows {
enum class NativeFanAutoResult { Applied, AlreadyAuto, OwnershipLost };
struct NativeFixedFanState {
    bool manual{},writable{};
    std::array<int,2> actualPercent{},targetPercent{};
    // Native RPM and unscaled PWM samples are independent measurements.
    // In particular, RPM/max RPM is not PWM duty and must never populate it.
    std::array<std::optional<int>,2> observedRpm{},rawPwm{};
    std::uint64_t rawPwmSampledAtMs{};
    std::string status;
    std::optional<LenovoPowerEcDiagnostic> lenovoPowerEcDiagnostic;
    bool actualPercentKnown{true},targetRpm{},needsRestore{},minimumSupported{};
    std::array<int,2> minimumPercent{};
    int mode{-1}; //0 Default,1 Manual,2 Max,3 Min; -1 uses legacy derivation.
    std::string source;
    std::optional<NativeFanCurveState> curves;
    NativeDefaultFanReading defaultFans;
    bool sharedCurve{};
    std::array<int,2> editablePointCounts{}; //0 retains the old native protected-last-row policy.
    bool targetRamped{}; // Mode/packet receipt confirms a ramped target; current RPM remains telemetry.
};
// One GUI mechanism for native fixed requests. Models retain their own
// protocols, safeguards, watchdogs and cleanup; no EC map is shared here.
class NativeFixedFans {
public:
    virtual ~NativeFixedFans()=default;
    virtual NativeFixedFanState read()=0;
    virtual void setFixed(const std::array<int,2>& percent)=0;
    // Maximum is a mode, not a Manual curve. Firmware-flag devices override
    // this command; existing fixed-PWM/RPM devices retain their exact command.
    virtual void setMaximum(){setFixed({100,100});}
    virtual NativeFanAutoResult setAutomatic()=0;
    virtual void setCurves(const NativeFanCurves&){throw std::runtime_error("Native fan curves unavailable");}
    virtual void saveCurveDraft(const NativeFanCurves&){throw std::runtime_error("Native curve draft unavailable");}
};
std::shared_ptr<NativeFixedFans> nativeFixedFanController();
}
