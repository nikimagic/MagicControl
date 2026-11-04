#pragma once
#include "../../../platform/windows/NativeFixedFans.h"
namespace mc::windows {
class ApuPowerReader;
struct ApuPowerDiagnostic;
class HpbsdFanController final:public NativeFixedFans {
public:
    HpbsdFanController();~HpbsdFanController() override;
    NativeFixedFanState read() override;
    void setFixed(const std::array<int,2>& percent) override;
    void setCurves(const NativeFanCurves& curves) override;
    void saveCurveDraft(const NativeFanCurves& curves) override;
    NativeFanAutoResult setAutomatic() override;
    // Device backend transaction: Default follows native factory B1/B2;
    // active user Manual/Min/Max remains byte-identical. GUI never calls it.
    ApuPowerDiagnostic applyStockPowerProfile(int profile,ApuPowerReader&);
    ApuPowerDiagnostic applyCustomPower(const std::array<int,3>& values,ApuPowerReader&);
private:struct Impl;std::unique_ptr<Impl> impl_;
};
std::shared_ptr<HpbsdFanController> hpbsdFanController();
}
