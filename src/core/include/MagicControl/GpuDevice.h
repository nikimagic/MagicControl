#pragma once
#include "SensorReading.h"
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <utility>
namespace mc {
enum class GpuVendor {Nvidia,Radeon};
enum class GpuType {Unknown,Integrated,Discrete};
enum class GpuPowerUnit {Watts,Percent};
enum class GpuPowerDomain {Unknown,Board,Asic,Gpu,Gfx};
inline const char* gpuPowerDomainName(GpuPowerDomain domain){switch(domain){case GpuPowerDomain::Board:return "board";case GpuPowerDomain::Asic:return "asic";case GpuPowerDomain::Gpu:return "gpu";case GpuPowerDomain::Gfx:return "gfx";default:return "unknown";}}
struct GpuPciLocation {std::optional<unsigned> domain,bus,device,function;std::string source;};
struct GpuProviderIdentity {std::string handle,source,status;};
struct GpuFanReading {unsigned index{};std::optional<unsigned> percent,target,policy;std::string status;};
struct GpuReading {
    // Provider PCI hardware IDs used to corroborate an OS devnode for capture.
    std::optional<unsigned> pciDeviceId,pciSubsystemId;
    std::string name,uuid,pci,driver,vbios,status,source,memoryType;
    GpuType type{GpuType::Unknown};
    bool available{},powerControl{};
    GpuPowerUnit powerUnit{GpuPowerUnit::Watts};
    std::optional<int> powerValue,powerMinimum,powerMaximum,powerDefault,powerStep;
    std::optional<unsigned> temperature,utilization,memoryUtilization,graphicsMhz,memoryMhz,pstate;
    std::optional<unsigned> powerMw,limitMw,enforcedMw,defaultMw,minimumMw,maximumMw;
    std::optional<unsigned> fanRpm;std::optional<double> hotspotCelsius,memoryCelsius,memoryJunctionCelsius;
    std::string powerSource,temperatureSource;
    // A proven current OS identity, never a name/VEN:DEV-only grouping key.
    // uuid remains the original provider control handle; physicalId is display
    // grouping only and MUST NOT be passed to a vendor setter.
    std::string physicalId,osAdapterId,identitySource,identityStatus;
    GpuPciLocation location;
    GpuPowerDomain powerDomain{GpuPowerDomain::Unknown};
    std::vector<GpuProviderIdentity> providerIdentities;
    std::optional<std::uint64_t> totalBytes,usedBytes,freeBytes;
    std::optional<unsigned> fanCount;std::string fanStatus;
    std::vector<SensorReading> additionalSensors;
    std::vector<GpuFanReading> fans;std::vector<std::string> apiStatus;
    bool deviceAbsent{},retainedForDisplay{};
    std::vector<std::pair<std::string,std::uint64_t>> retainedFieldTimes;
};
struct GraphicsAdapter {
    std::string name,id,pci,driver,status,primarySource;
    unsigned vendorId{},deviceId{},subsystemId{},revision{};
    GpuType type{GpuType::Unknown};
    std::string physicalId,identitySource,identityStatus;
    GpuPciLocation location;
    bool primary{},software{};
    std::optional<std::uint64_t> dedicatedBytes,sharedBytes,processLocalUsageBytes,processLocalBudgetBytes;
    bool primaryKnown{};std::optional<bool> bootVga;
};
// Enumerate every adapter, independent of the display's primary GPU. No render
// device/workload is created just to wake an idle hybrid dGPU.
std::vector<GraphicsAdapter> graphicsAdapters();
// Presentation only. Raw diagnostic provider rows are never discarded by this
// function's callers. Unknown/ambiguous identities remain separate observations.
std::vector<GpuReading> physicalGpuReadings(std::vector<GpuReading> readings);
bool radeonDiscretePresent() noexcept;
class RadeonGpu {
public:
    RadeonGpu();~RadeonGpu();
    std::vector<GpuReading> read(bool includeIntegrated=false);
    void setPowerLimit(const std::string& id,int value);
    void resetPowerLimit(const std::string& id);
private:struct Impl;std::unique_ptr<Impl> impl_;
};
class DiscreteGpu {
public:
    explicit DiscreteGpu(GpuVendor vendor);~DiscreteGpu();
    GpuVendor vendor() const;
    std::vector<GpuReading> read();
    void setPowerLimit(const std::string& id,int value);
    void resetPowerLimit(const std::string& id);
private:struct Impl;std::unique_ptr<Impl> impl_;
};
inline const char* gpuVendorName(GpuVendor v){return v==GpuVendor::Nvidia?"NVIDIA":"Radeon";}
inline int gpuPowerValue(const GpuReading& r){return r.powerValue.value_or(static_cast<int>(r.limitMw.value_or(0)/1000));}
inline bool gpuPowerKnown(const GpuReading& r){return r.powerValue.has_value()||r.limitMw.has_value();}
inline int gpuPowerMinimum(const GpuReading& r){return r.powerMinimum.value_or(static_cast<int>((r.minimumMw.value_or(0)+999)/1000));}
inline int gpuPowerMaximum(const GpuReading& r){return r.powerMaximum.value_or(static_cast<int>(r.maximumMw.value_or(0)/1000));}
inline bool gpuPowerRangeKnown(const GpuReading& r){return (r.powerMinimum&&r.powerMaximum)||(r.minimumMw&&r.maximumMw);}
inline bool gpuPowerDefaultKnown(const GpuReading& r){return r.powerDefault.has_value()||r.defaultMw.has_value();}
}
