#pragma once
#include "MagicControl/BatteryAging.h"

#include "SensorReading.h"
#include "GpuPresentation.h"
#include "NativeDefaultFan.h"
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <chrono>
#include <tuple>

namespace mc {
class EcAccess;
enum class ProcessorVendor { Unknown, Amd, Intel, Nvidia };

enum class BatteryFlow { Unknown, Idle, Charging, Discharging };
struct BatteryReading {
    std::optional<bool> present;
    std::optional<double> percent, watts;
    BatteryFlow flow{BatteryFlow::Unknown};
    bool online{};
    // Unknown AC is not evidence of battery operation. Keep the compatibility
    // value separate from validity; JSON/UI must honour onlineKnown.
    bool onlineKnown{},dynamicStale{};
    std::string deviceId,model;
    std::string powerStatus; // Raw AC/IOCTL/sysfs reply or error; unknown is not false.
    // Last fresh percent / watts / temperature / voltage / current observations.
    // Copying a missed reading must never refresh these monotonic timestamps.
    std::array<std::uint64_t,5> dynamicAtMs{};
    std::string source;
    // The result of the current temperature query, even when no value is returned.
    std::string temperatureStatus, temperatureSource;
    std::optional<std::uint32_t> temperatureRaw;
    std::optional<double> celsius, volts, milliamps;
    std::optional<std::uint32_t> cycles;
    std::optional<double> healthPercent; // FullChargedCapacity / DesignedCapacity * 100, not current charge.
    bool healthUnavailable{}; // Explicitly invalidated by a coherent device sample; do not retain an old number.
    // Native OS battery metadata, never replaced by EC capacity words.
    std::string manufacturer,serial,chemistry,manufactured,uniqueId,capacityUnit;
    std::optional<double> designCapacity,fullCapacity,remainingCapacity;
    std::optional<std::uint32_t> capabilities,technology,alert1,alert2,criticalBias,powerState;
    std::optional<std::uint32_t> estimatedDischargeSeconds,estimatedChargeSeconds;
    bool capacityRelative{};
    bool healthReadUnavailable{},healthStale{}; // Transient transport/pause, not a coherent invalid sample.
    std::uint64_t healthAtMs{};std::string healthSource;
    friend bool operator==(const BatteryReading& a,const BatteryReading& b) {
        const auto visible=[](const BatteryReading& v){return std::tie(v.present,v.percent,v.watts,v.flow,v.online,v.onlineKnown,v.dynamicStale,v.deviceId,v.model,v.powerStatus,v.source,v.temperatureStatus,v.temperatureSource,v.temperatureRaw,v.celsius,v.volts,v.milliamps,v.cycles,v.healthPercent,v.healthStale,v.healthSource);};
        const auto metadata=[](const BatteryReading& v){return std::tie(v.manufacturer,v.serial,v.chemistry,v.manufactured,v.uniqueId,v.capacityUnit,v.designCapacity,v.fullCapacity,v.remainingCapacity,v.capabilities,v.technology,v.alert1,v.alert2,v.criticalBias,v.powerState,v.capacityRelative,v.estimatedDischargeSeconds,v.estimatedChargeSeconds);};
        return visible(a)==visible(b)&&metadata(a)==metadata(b); // Timestamp-only polls do not repaint cards.
    }
};
inline std::uint64_t batteryMonotonicMs(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
inline bool batteryIdentityChanged(const BatteryReading& a,const BatteryReading& b){
    const auto differs=[](const std::string& x,const std::string& y){return !x.empty()&&!y.empty()&&x!=y;};
    return differs(a.deviceId,b.deviceId)||differs(a.serial,b.serial)||differs(a.uniqueId,b.uniqueId);
}
inline void stampBatteryReading(BatteryReading& b,std::uint64_t now=batteryMonotonicMs()){
    if(b.healthPercent&&!b.healthStale&&!b.healthReadUnavailable){if(!b.healthAtMs)b.healthAtMs=now;if(b.healthSource.empty())b.healthSource=b.source;}
    const std::array values{b.percent,b.watts,b.celsius,b.volts,b.milliamps};
    for(std::size_t i=0;i<values.size();++i)if(values[i]&&!b.dynamicAtMs[i])b.dynamicAtMs[i]=now;
}
// Retain successful numeric samples through a missed poll. Confirmed battery
// removal clears the readings; fresh zeroes are real values, not missing data.
inline void retainBatteryReadings(BatteryReading& current, const BatteryReading& previous) {
    const auto now=batteryMonotonicMs();stampBatteryReading(current,now);
    if (current.present == false) {
        current.percent.reset(); current.watts.reset(); current.celsius.reset();
        current.volts.reset(); current.milliamps.reset(); current.cycles.reset(); current.healthPercent.reset();
        current.temperatureSource.clear();current.temperatureRaw.reset();current.dynamicAtMs={};
        current.healthAtMs=0;current.healthSource.clear();current.healthStale=false;
        return;
    }
    if (previous.present == false||batteryIdentityChanged(current,previous)) return;
    const auto retain = []<class T>(std::optional<T>& value, const std::optional<T>& last) {
        if (!value) value = last;
    };
    const auto dynamic=[&](std::optional<double>& value,const std::optional<double>& last,std::size_t index){
        if(value)return false;
        const auto fresh=previous.dynamicAtMs[index];
        if(last&&fresh&&now>=fresh&&now-fresh<=10000){value=last;current.dynamicAtMs[index]=fresh;current.dynamicStale=true;return true;}
        current.dynamicAtMs[index]=0;return false;
    };
    dynamic(current.percent,previous.percent,0);dynamic(current.watts,previous.watts,1);
    if(dynamic(current.celsius,previous.celsius,2)){
        current.temperatureSource=previous.temperatureSource;
        current.temperatureStatus+="; сохранено последнее успешное показание";
    }
    dynamic(current.volts,previous.volts,3);dynamic(current.milliamps,previous.milliamps,4);
    retain(current.cycles, previous.cycles);
    if(current.healthReadUnavailable||(!current.healthPercent&&!current.healthUnavailable)){
        if(previous.healthPercent){current.healthPercent=previous.healthPercent;current.healthAtMs=previous.healthAtMs;current.healthSource=previous.healthSource;current.healthStale=true;current.healthUnavailable=false;
            if(current.powerStatus.find("сохранено последнее подтверждённое здоровье батареи")==std::string::npos)current.powerStatus+="; сохранено последнее подтверждённое здоровье батареи";}
    }else if(current.healthUnavailable)current.healthPercent.reset();
    else if(current.healthPercent){if(!current.healthAtMs)current.healthAtMs=now;current.healthStale=false;}
    retain(current.present, previous.present);
    // Never infer fresh charging/discharging or AC state from an old sample.
    if (current.source.empty()) current.source = previous.source;
}
// Separate, opt-in EC-only sample. Never populated from Windows/sysfs or
// retained mixed-source dashboard values. No setter or firmware operation.
struct EcBatteryRegisterReading {
    std::uint16_t address{};
    std::uint8_t width{1};
    bool bigEndian{},confirmed{};
    std::string name,status;
    std::optional<std::uint16_t> value;
    // Measurements are recorded by the periodic/SOC snapshot, not at every fluctuation.
    bool sampledTelemetry{};
    // Raw signed interpretation for diagnostic fields only; never a flow override.
    bool signedValue{};
    friend bool operator==(const EcBatteryRegisterReading&,const EcBatteryRegisterReading&)=default;
};
struct EcBatteryDiagnosticSample {
    std::uint64_t sequence{},coreAtMs{},startedAtMs{},finishedAtMs{};
    std::string kind,reason;
    std::vector<EcBatteryRegisterReading> fields;
    friend bool operator==(const EcBatteryDiagnosticSample&,const EcBatteryDiagnosticSample&)=default;
};
struct EcBatteryReading {
    BatteryAgingStatus aging;
    bool supported{}, coherent{};
    std::string profileId;
    std::string status;
    BatteryReading battery;
    std::optional<std::uint16_t> state, designMah, fullMah, remainingMah;
    std::optional<std::uint16_t> raw520, raw522, rawBase;
    std::optional<std::uint8_t> upperRaw, restartRaw, platformRaw, voltagePolicyRaw;
    std::optional<std::uint8_t> chargerRaw, typeCRatingA, typeCRatingB, readyRaw, isdbRaw, gateRaw;
    // BMS requests are not measured battery current or pack voltage.
    std::optional<double> requestedChargeMilliamps,requestedChargeVolts;
    // KWCN EC adapter classification candidate, not measured input power.
    std::optional<unsigned> adapterWattCandidate;
    std::uint64_t adapterCandidateAtMs{}; // Original monotonic sample; zero when unavailable.
    std::optional<bool> conservationMode,rapidChargeMode;
    std::vector<EcBatteryRegisterReading> registers;
    std::optional<EcBatteryDiagnosticSample> diagnostics;
    bool retainedForDisplay{};std::uint64_t sampledAtMs{};
    friend bool operator==(const EcBatteryReading&,const EcBatteryReading&)=default;
};
// Candidate KWCN ACPI-to-I2EC alias; never used as control or confirmed mode.
struct LenovoPowerEcDiagnostic {
    std::optional<std::uint8_t> c5e0,c5e1,c5e3,c5fb,c5ff,c41d;
    std::optional<std::uint8_t> c4a3,c439,c22f,c2a7,c2de,c2df,c450,c451,c93f,c930,c931,c932,c933;
    // Individually stable candidate I2EC aliases; bytewise, not an atomic snapshot.
    std::optional<std::uint8_t> c2a5,c29a,c364,c365,c304,c305,c3b6,c3b8,c4ae,c30c,c3fe,c4be,c4ce,c200,c2c9,c4c1,c4d0,c4d1;
    std::optional<std::uint8_t> c2a2,c2a3,c452,c453,c2bb,c43e;
    std::uint64_t startedAtMs{},sampledAtMs{};
    std::string status;
    friend bool operator==(const LenovoPowerEcDiagnostic&,const LenovoPowerEcDiagnostic&)=default;
};
// Approximation from the published EC full capacity and the already-observed
// charging-voltage target. This is not measured energy or current pack voltage.
inline std::optional<double> estimatedEcFullWattHours(const EcBatteryReading& ec,std::optional<int> targetMv) noexcept {
    if(((!ec.coherent||ec.battery.dynamicStale)&&!ec.retainedForDisplay)||ec.battery.present==false||!ec.fullMah||
       !targetMv||*targetMv<5000||*targetMv>25000)return {};
    return double(*ec.fullMah)* *targetMv/1000000.;
}
// Configuration of the active Windows base power scheme, not measured CPU
// clocks, hardware EPP readback, effective overlays or firmware power limits.
struct PowerPolicySetting {
    std::optional<std::uint32_t> ac,dc;
    std::uint32_t acError{},dcError{};
    friend bool operator==(const PowerPolicySetting&,const PowerPolicySetting&)=default;
};
struct BatteryPowerPolicy {
    bool applicable{};
    std::string schemeName,schemeGuid,status;
    PowerPolicySetting frequencyMHz,epp;
    friend bool operator==(const BatteryPowerPolicy&,const BatteryPowerPolicy&)=default;
};
struct DeviceTemperature {
    std::optional<bool> present;
    std::optional<double> celsius;
    std::string model, serial, source, status;
    friend bool operator==(const DeviceTemperature&, const DeviceTemperature&) = default;
};
// Separate architectural MSRs from the independently mapped Intel MMIO
// package limits. None of these observations imply that another controller
// cannot impose a lower limit.
struct IntelPackagePowerObservation {
    std::uint64_t msrSampledAtMs{},mmioSampledAtMs{};
    std::optional<std::uint64_t> unitsRaw,msrLimitRaw,msrPerformanceRaw;
    std::optional<std::uint32_t> mmioLimit1Raw,mmioLimit2Raw;
    std::optional<double> msrLimit1Watts,msrLimit2Watts,msrWindow1Seconds,msrWindow2Seconds,mmioLimit1Watts,mmioLimit2Watts,mmioWindow1Seconds,mmioWindow2Seconds;
    std::optional<bool> msrLimit1Enabled,msrLimit2Enabled,mmioLimit1Enabled,mmioLimit2Enabled,mmioLimit1Clamp,mmioLimit2Clamp;
    std::string status;
    friend bool operator==(const IntelPackagePowerObservation&,const IntelPackagePowerObservation&)=default;
};
struct ProcessorReading {
    std::optional<double> celsius, usagePercent, clockMhz, powerWatts, fanRpm, fanPwm;
    std::string temperatureSource, usageSource, clockSource, powerSource, fanSource;
    int temperatureSensorId{-1}, adapterIndex{-1};
    // Independent readings retained for diagnostics; celsius is the UI/control source.
    std::optional<double> driverCelsius, tctlCelsius;
    friend bool operator==(const ProcessorReading&, const ProcessorReading&) = default;
};
struct MonitoringSnapshot {
    // UI copies only. Raw control/diagnostic readers never consume these retained values.
    struct RetainedField {std::string field;std::uint64_t sampledAtMs{};friend bool operator==(const RetainedField&,const RetainedField&)=default;};
    // Display-route presentation is independent of the fixed cooling channels.
    GpuPresentationSnapshot gpuCards;
    // Diagnostics retain each system battery; the existing single card keeps
    // its explicitly selected primary reading instead of mixing batteries.
    std::vector<BatteryReading> batteries;
    bool diagnostic{};
    std::vector<SensorReading> additionalSensors;
    std::optional<LenovoPowerEcDiagnostic> lenovoPowerEcDiagnostic;
    std::array<ProcessorVendor,2> processorVendors{};
    std::optional<double> cpuTemperatureTargetCelsius;
    // Intel PP0 cores is distinct from the existing processor Package reading.
    std::optional<double> cpuCorePowerWatts;
    std::string cpuCorePowerSource;
    std::optional<IntelPackagePowerObservation> intelPackagePower;
    std::optional<double> pchCelsius;
    std::string pchSource;
    std::optional<std::uint64_t> gpuMemoryUsedBytes, gpuMemoryTotalBytes;
    std::string gpuMemorySource;
    BatteryReading battery;
    std::array<DeviceTemperature, 2> memory, disks;
    // Metadata admits the ordinary-DDR5 decoder; it is never a clock reading.
    bool ordinaryDdr5{};
    bool ordinaryDdr4{};
    // Common active clock only when both UMC channels confirm the same step.
    // Channel identity is not an inferred mapping between SPD addresses/DIMMs.
    std::optional<double> memoryClockMhz;
    std::array<std::optional<unsigned>,2> memoryPstate;
    std::string memoryClockSource,memoryClockStatus;
    std::uint64_t memoryClockSampledAtMs{};
    std::array<ProcessorReading, 2> processors;
    std::optional<double> apuPackagePowerWatts;
    std::string apuPackagePowerSource;
    // Experimental EC 0x44C system-power candidate, displayed assuming 1 raw = 1 W.
    // Independent of battery charge/discharge and APU package power.
    std::optional<double> systemPowerWatts;
    // Footer estimate requested by the user: EC minus positive battery charging power.
    // Keep systemPowerWatts raw for diagnostics; never feed this estimate into control.
    std::optional<double> systemConsumptionWatts;
    std::string systemPowerSource;
    bool systemPowerStale{};
    // Read once on the first startup sample; never queried by the polling loop again.
    std::array<std::string, 2> processorModels;
    std::optional<double> socCelsius, npuUsagePercent, npuClockMhz, npuPowerWatts;
    std::string socSource, npuUsageSource, npuClockSource, npuPowerSource, processorStatus;
    std::uint64_t processorSampledAtMs{};
    std::vector<RetainedField> retainedForDisplay;
    // One collector DTO, accepted into ResourceState before graph presentation.
    // Never retained across a failed/paused poll or used for any fan control.
    std::optional<NativeDefaultFans> nativeDefaultFans;
    NativeDefaultFanObservation nativeDefaultFanObservation{NativeDefaultFanObservation::Unavailable};
    friend bool operator==(const MonitoringSnapshot&, const MonitoringSnapshot&) = default;
};

// Read-only device monitoring. Never applies fan, charge or lighting settings.
// Call read() from one background thread; platform code caches slow metadata.
class MonitoringReader {
public:
    // Expensive per-adapter display telemetry is GUI opt-in. Cooling/control
    // and existing diagnostic readers keep their original provider path.
    explicit MonitoringReader(EcAccess* access = nullptr, bool diagnostic = false,bool collectGpuPresentation=false);
    ~MonitoringReader();
    MonitoringReader(const MonitoringReader&) = delete;
    MonitoringReader& operator=(const MonitoringReader&) = delete;
    MonitoringSnapshot read();
    BatteryPowerPolicy readBatteryPowerPolicy();
    // SETs comparison card / battery journal. Existing dashboard sources are unchanged.
    // Unsupported platforms return unavailable rather than OS-derived data.
    EcBatteryReading readEcBatteryOnly();
    BatteryReading readOsBatteryOnly();
    // Same battery source as FANs (Windows Battery Class/EC; Linux power_supply).
    // Does not poll processors, disks or memory sensors.
    // Optionally return the same raw OS observation before EC/HWMI enrichment.
    // Both views then describe one OS read; the EC-only diagnostic stays separate.
    BatteryReading readBatteryOnly(BatteryReading* osObservation = nullptr);
    std::vector<BatteryReading> readBatteries();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace mc
