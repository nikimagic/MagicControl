#pragma once
#include <map>
#include "MagicControl/DiagnosticDump.h"
#include "MagicControl/EcAccess.h"
#include "MagicControl/Monitoring.h"
#include "MagicControl/TelemetryGate.h"
#include "MagicControl/PawnIoInstall.h"
#include "FirmwareEvidence.h"
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>
#include <chrono>
namespace mc::diagnostics {
struct DependencyRuntimeFile {
    std::string name,sha256,error;
    std::optional<bool> present;
    std::optional<std::uintmax_t> bytes;
};
struct DependencySnapshot {
    bool pawnIoApplicable{};
    std::optional<dependencies::PawnIoDriverStatus> pawnIo;
    std::string status,error;
    std::filesystem::path pawnIoRuntimeDirectory;
    std::string pawnIoRuntimeStorage;
    std::vector<DependencyRuntimeFile> pawnIoRuntimeFiles;
};
// Inspect service metadata only. This must not start a driver or open device IO.
DependencySnapshot platformDependencies();
// Actual current OS display evidence. No EDID preferred-mode substitution.
std::string capturePlatformDisplays(std::stop_token token);
// OS adapters add all named hwmon/WMI readings, including sensors not shown
// by the two-slot dashboard. Getter-only; optional providers are never installed.
class PlatformSensors {
public:
    PlatformSensors();~PlatformSensors();
    std::vector<SensorReading> read(bool slowProviders,std::stop_token token);
private:struct Impl;std::unique_ptr<Impl> impl_;
};
struct ReadError {std::uint32_t address{},count{};std::string message;};
struct EcReadAttempt {
    std::uint32_t address{},count{};
    std::uint64_t elapsedUs{};
    std::string outcome;
    EcBlockReadTiming transport;
};
struct EcDump {
    std::string source,status,interfaceNotes;
    std::string keyboardJson,keyboardStatus;
    std::map<std::string,std::string> metadataJson;
    std::uint32_t base{};
    std::vector<std::uint8_t> bytes,valid;
    std::vector<ReadError> errors;
    std::vector<EcReadAttempt> readAttempts;
    bool passivePollingPaused{};
    bool complete{};
    unsigned busyWaitCount{},blockRetryCount{};
    std::uint64_t elapsedMs{},maxBlockReadMs{};
    std::optional<std::uint64_t> writesBefore,writesAfter;
};
using BlockReader=std::function<std::vector<std::uint8_t>(std::uint16_t,std::size_t,EcBlockReadTiming*)>;
// Small transactions let the existing fan controller keep cooling. Firmware
// may change data between blocks; the accompanying report records this limit.
// EcObservationBusy retries the same block within a cancellable180s budget;
//16-byte reads release the gate and wait2ms for queued telemetry/control.
// Transport failures get up to3 attempts, then other blocks are still collected.
void captureEcBlocks(EcDump& dump,const BlockReader& reader,std::stop_token token,std::size_t bytes=4096);
EcDump capturePlatformEc(EcAccess* access,std::stop_token token);
// Fixed typed read-only keyboard context; parent process only for Honor I2EC.
// Empty means this OS/profile has no additional fixed context beyond inventory.
std::string capturePlatformKeyboardContext(std::stop_token token);
struct FirmwareDefaultResult {bool attempted{},confirmed{};std::string status;FanDiagnosticSession session;};
FirmwareDefaultResult preparePlatformFanDefault(EcAccess* access,const FirmwareFanDefault& restore,std::stop_token token);
std::optional<bool> platformSnapshotFanDefault(const EcDump& dump);
// Uses the initial EC bytes, never a second read after a probe. Inactive RAM
// and OEM reference curves are labelled separately from firmware-internal data.
std::string platformFanTables(const EcDump& dump,bool defaultConfirmed);
struct FirmwareFactoryResult {bool attempted{},complete{},restored{true};std::string json,status;};
// Explicit WithFanProbes only, after the initial EC snapshot and telemetry.
// The adapter owns DeviceOperation and restores only its own APP mask.
FirmwareFactoryResult capturePlatformFactoryTables(std::stop_token token);
FanMaximumResult capturePlatformMaximum(EcAccess* access,const MaximumFanProbe& probe,std::stop_token token);
FanMaximumSample readPlatformFans(EcAccess* access);
struct FastGpuIdentity{std::string name,uuid;bool available{};};
struct FastDiagnosticFrame{
    std::vector<SensorReading> readings;
    std::string gpuJson{"{}"},graphicsJson,nvidiaJson,intelJson,radeonJson,batteryJson,utc,status;
    std::vector<FastGpuIdentity> nvidia;
    std::int64_t readDurationMs{};
    bool complete{};
};
// Persistent, process-isolated read-only providers. A blocked vendor/Storage/
// PawnIO getter cannot keep the parent recorder waiting past this deadline.
class FastDiagnosticWorker{
public:virtual ~FastDiagnosticWorker()=default;
    virtual FastDiagnosticFrame read(std::stop_token,std::chrono::milliseconds timeout)=0;
    virtual FirmwareCaptureResult firmware(bool stateOnly,const FirmwareEmitter&,std::stop_token,std::chrono::milliseconds timeout,bool biosRom=false)=0;
};
std::unique_ptr<FastDiagnosticWorker> createFastDiagnosticWorker(std::shared_ptr<TelemetryGate::DiagnosticCollection> collection = {});
std::optional<int> runFastDiagnosticHelper(const std::vector<std::string>& args);
// No live controller/watchdog may be constructed here. Platform implementations
// supply only verified getter access with writes hard-denied at the transport.
class ReadOnlyDiagnosticHardware{
public:virtual ~ReadOnlyDiagnosticHardware()=default;
    virtual EcAccess* access()noexcept=0;
    virtual FanMaximumSample fans()=0;
};
std::unique_ptr<ReadOnlyDiagnosticHardware> createReadOnlyDiagnosticHardware();
class FastDiagnosticSource{
public:FastDiagnosticSource();~FastDiagnosticSource();FastDiagnosticFrame read(const std::function<void(const FastDiagnosticFrame&)>& progress={});
private:struct Impl;std::unique_ptr<Impl> impl_;
};
}
