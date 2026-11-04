#pragma once
#include "MagicControl/BatteryProtection.h"
#include "MagicControl/BatteryEcPolicy.h"
#include <memory>
#include <string>
#include <cstdint>
#include <vector>
#include <tuple>
namespace mc::windows {
struct NativeBatteryState {
    bool connected{},writable{},voltageTransitionWritable{};
    std::optional<BatteryProtectionMode> mode;
    std::optional<int> baseVoltageMv,targetVoltageMv,cellCount;
    bool voltageOnline{},voltageRetained{},appPresent{};
    std::uint64_t voltageAtMs{};
    // GM6/HYDROC candidate configuration only; native profiles are not percentages.
    // Raw0/0 is unknown/disabled, never a selected100% policy.
    std::optional<int> configuredUpper,configuredRestart;
    std::optional<std::uint8_t> upperRaw,restartRaw;
    bool percentageWritable{};
    bool enforcementWritable{};
    std::optional<bool> enforcementOn;
    std::optional<std::uint8_t> projectRaw,platformRaw,romIdRaw;
    // Exact GM6/HYDROC read-only charger status; rating is HYDROC-only.
    std::optional<std::uint8_t> chargerRaw,typeCPowerA,typeCPowerB;
    std::string status;
    friend bool operator==(const NativeBatteryState& a,const NativeBatteryState& b){
        const auto visible=[](const NativeBatteryState& s){return std::tie(s.connected,s.writable,s.voltageTransitionWritable,s.mode,
            s.baseVoltageMv,s.targetVoltageMv,s.cellCount,s.voltageOnline,s.voltageRetained,s.appPresent,
            s.configuredUpper,s.configuredRestart,s.upperRaw,s.restartRaw,s.percentageWritable,s.enforcementWritable,
            s.enforcementOn,s.projectRaw,s.platformRaw,s.romIdRaw,s.chargerRaw,s.typeCPowerA,s.typeCPowerB,s.status);};
        return visible(a)==visible(b); // Acquisition timestamps alone never repaint the card.
    }
};
// One battery-only implementation for the two independently reviewed boards.
// The factory is gated by actual SMBIOS, never the preview/selected model.
class NativeBatteryController {
public:
    NativeBatteryController();
    ~NativeBatteryController();
    NativeBatteryState read();
    void set(BatteryProtectionMode); // Interactive caller owns DeviceOperation.
    // Exact-board trial: upper100(raw0)/95/85/60; old 10 remains for restore.
    // Native voltage mode is separate. Lower semantics remain explicitly unverified.
    void setExperimentalChargeLimit(int upper); // Caller owns DeviceOperation.
    void setChargeRestart(int percent); // Shared lower API, model guards/readback/autosave.
    void setEnforcement(bool enabled); //4/original ROM or7/owned RAM ROM04.
    void setEcPolicy(BatteryEcPolicy); // Also7/original FF on admitted exact models.
    std::uint8_t readEditorByte(std::uint16_t address);
    std::vector<std::uint8_t> readEditorBytes(std::uint16_t address,unsigned count);
    std::uint8_t writeEditorByte(std::uint16_t address,std::uint8_t expected,std::uint8_t value);
    void restore(std::string reason="resume_restore"); // Queue only; no caller-thread EC.
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
std::shared_ptr<NativeBatteryController> nativeBatteryController();
// Also supports exact WUJIE's voltage-only path. The old factory remains
// unchanged for its working percentage controller and EC ownership.
std::shared_ptr<NativeBatteryController> nativeBatteryVoltageController();
}
