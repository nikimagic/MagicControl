#pragma once
#include "MagicControl/QuickSwitch.h"
#include "../../../protocols/uniwill/gm6ir0c/Registers.h"
#include "../../../protocols/uniwill/gm6ir0c/NativeProfile.h"
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "MagicControl/FanMaximum.h"
#include "MagicControl/DiagnosticDump.h"
#include "MagicControl/KeyboardBacklight.h"
#include "../../../protocols/uniwill/RamFanFactory.h"
namespace mc {struct BatteryReading;struct EcBatteryReading;}
namespace mc::windows {
// Read-only supplement for one identified physical battery on exact GM6.
// No live controller/restore worker, threshold setter, or charger-port guessing.
void readGm6BatteryTelemetry(BatteryReading& result,bool singlePhysicalBattery);
EcBatteryReading readGm6EcBatteryOnly();
struct Gm6State {
    bool connected{},writable{},boost{},custom{},ramActive{},respective{},pl4Double{},overrideActive{};
    bool commandReadbackConfirmed{},fanResponseConfirmed{};
    bool online{},onlineLinked{},needsRestore{},tablesRead{};
    bool economyActive{}; // Our transient Economy intent AND matching EC Custom/PL readback.
    std::uint64_t customPresetRevision{};
    std::optional<gm6::Curves> selectedCustomCurves;
    bool autoPowerActive{}; // Explicit Auto intent AND freshly confirmed zero overrides.
    std::array<bool,2> onlineControlled{};
    std::array<std::optional<int>,2> onlineTarget{};
    int mode{-1}; // Read-only whole-system751 mode:0 Office,1 Gaming,2 Turbo.
    std::optional<gm6::NativeProfile> nativeProfile;
    std::string nativeProfileStatus;
    std::array<int,3> limits{}; // EC override, zero means OEM policy
    // CPU-only application preset inferred from actual PL override readback.
    // Never inferred from the whole-system 0x751 mode. -1=auto/manual/ambiguous.
    int cpuPreset{-1};
    bool stockCpuActive{}; // Explicit CPU-only intent AND matching fresh limits.
    bool cpuPresetWritable{};
    std::array<std::optional<std::array<int,3>>,3> cpuPresetLimits;
    std::string cpuPresetStatus;
    std::array<std::optional<int>,2> rpm,pwm,temperature;
    std::optional<gm6::Curves> curves,onlineCurves;
    std::optional<KeyboardBacklightState> keyboard;
    std::string keyboardStatus;
    bool keyboardRestorePending{},keyboardSuspended{};
    bool keyboardRestoreConfirmed{};
    std::string keyboardRestoreStatus;
    std::string status;
    friend bool operator==(const Gm6State&,const Gm6State&)=default;
};
struct Gm6NativeProfileResult {
    std::uint64_t receivedAt{};
    std::optional<gm6::NativeProfile> requested,observed;
    bool readbackConfirmed{}; // Never a claim about physical LED/fan/PL response.
    std::string status;
};
class Gm6Controller : public std::enable_shared_from_this<Gm6Controller> {
public:
    // SSH constructs the same controller with startupQuickRestore=false:
    // one explicit command must not replay unrelated saved KEYs settings.
    explicit Gm6Controller(bool startupQuickRestore=true);
    ~Gm6Controller();
    Gm6State read(bool tables=false);
    // Read-only diagnostic window, through the same model guards/gate.
    std::vector<std::uint8_t> readDiagnosticBlock(std::uint16_t address,std::size_t count);
    // Active factory mailbox, explicit diagnostic only and only after Default
    // plus the initial EC snapshot. Does not replay or activate RAM payloads.
    uniwill::RamFanFactoryResult exportFactoryTables(bool explicitWithFanProbes,std::stop_token={});
    FanMaximumResult measureMaximum(std::stop_token cancellation={});
    FanDiagnosticSession prepareDiagnostic();
    // 0 Office,1 Gaming,2 Turbo: fresh own firmware limits and model codec.
    // No FAN/mode751 writes; exact GM6 requires its already-active override gate.
    std::array<int,3> setCpuPreset(int preset);
    void resetCpuPower(); // Explicit CPU override reset, no FANs/mode/draft writes.
    // GM6: temporary native Custom/FAN Manual with10/15/20; preserve Custom draft.
    // GM5/L16: unchanged CPU-only command. Never adds Economy to the physical cycle.
    void setEconomyPower();
    void setBoost(bool on);
    void setAutomatic();
    void setCurves(const gm6::Curves& curves,std::optional<std::array<int,3>> power={},bool respective=true);
    // Host curve targets use this board's native RAM tables, never feedback
    // registers. Monitoring is independent of which GUI tab is visible.
    void setOnlineCurves(const gm6::Curves& curves,std::array<bool,2> controlled,bool linked);
    // GM6 Custom: shared native power/FAN apply and checked draft persistence.
    // GM5/L16: existing CPU-only Manual, no FAN writes.
    // Failure rolls back only our unchanged PL fields and reports rollback errors.
    void setPower(const std::array<int,3>& limits);
    void setCustomBase(int base);
    // Read the existing three-integer manual draft without hardware access.
    // Missing file => nullopt/empty error; malformed/unreadable => explicit error.
    std::optional<std::array<int,3>> loadPowerDraft(std::string& error);
    // Shared model-bound library. Ready slots resolve fresh own firmware PL
    // defaults; existing explicit slots retain their limits/curves/split.
    // GM6 uses native apply; GM5/L16 use their existing Manual RAM writer.
    bool customPresetFilled(int slot);
    void saveCustomPresetDraft(int slot,const std::array<int,3>& power);
    std::optional<std::array<int,3>> useCustomPreset(int slot);
    KeyboardBacklightState readKeyboard();
    KeyboardBacklightState setKeyboard(const KeyboardBacklightChange&);
    QuickSwitchStates readQuickSwitches();
    void setQuickSwitch(QuickSwitch,bool);
    std::optional<bool> readCopilotKey();
    void setCopilotKey(bool);
    // Independent of keyboard effect, FAN intent, native power and battery.
    // HYDROC only: [7C7 presence,7E7 pump target,7E6 external fan target].
    // No FAN-table, PL, battery or lighting writes. LPP backend owns policy lease.
    std::array<std::uint8_t,3> readLiquidCoolingPolicy();
    void verifyLiquidCoolingPolicyAdmission();
    void setLiquidCoolingPresence(bool enabled);
    bool liquidCoolingPresenceOwned()const noexcept;
    // Lifecycle request only; exact GM6 also restores persisted power/FAN intent.
    // Other RAM1.5 models retain their existing static-keyboard-only contract.
    void restoreKeyboard(const std::string& reason,std::function<void()> radioAccess={});
    // Cancel queued restoration before sleep; no hardware command/reset.
    void suspendKeyboard();
    // Physical OEM176 cycles whole Office/Gaming/Turbo/Custom, independently
    // of CPU-only setCpuPreset. WMI/UI/OSD callbacks never perform IO.
    // Existing controller worker serializes presses and reports checked result.
    void enqueueNativeProfileCycle(std::uint64_t receivedAt,std::function<void(Gm6NativeProfileResult)> complete);
    void nativeHotkeySubscribed(); // Queue OEM APP presence only after provider subscription succeeds.
    void cancelNativeProfileCommands(); // Shutdown only, no EC reset.
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
// Exact-board only, --no-ec respected. Shared gate across FANs and SETs.
std::shared_ptr<Gm6Controller> gm6Controller(bool startupQuickRestore=true);
}
