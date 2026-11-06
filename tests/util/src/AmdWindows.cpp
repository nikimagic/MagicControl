#include "WinHelpers.h"
#include <ADLX.h>
#include <IPerformanceMonitoring2.h>
#include <adl_sdk.h>
#include <cstdlib>

namespace probe {
namespace {
using namespace adlx;

// Use the unmodified AMD SDK types for all layouts and interface calls.
static_assert(sizeof(ADLPMLogDataOutput) == 2052);
static_assert(sizeof(AdapterInfo) == 1572);
static_assert(ADL_PMLOG_MAX_SENSORS == 256);
static_assert(ADLX_VER_MAJOR == 1 && ADLX_VER_MINOR == 5);
void *__stdcall allocate(int bytes) {
    return bytes > 0 ? std::malloc(static_cast<std::size_t>(bytes)) : nullptr;
}
const char *pmName(unsigned id) {
    static constexpr const char *names[]{"UNKNOWN_0",
                                         "CLK_GFXCLK",
                                         "CLK_MEMCLK",
                                         "CLK_SOCCLK",
                                         "CLK_UVDCLK1",
                                         "CLK_UVDCLK2",
                                         "CLK_VCECLK",
                                         "CLK_VCNCLK",
                                         "TEMPERATURE_EDGE",
                                         "TEMPERATURE_MEM",
                                         "TEMPERATURE_VRVDDC",
                                         "TEMPERATURE_VRMVDD",
                                         "TEMPERATURE_LIQUID",
                                         "TEMPERATURE_PLX",
                                         "FAN_RPM",
                                         "FAN_PERCENTAGE",
                                         "SOC_VOLTAGE",
                                         "SOC_POWER",
                                         "SOC_CURRENT",
                                         "INFO_ACTIVITY_GFX",
                                         "INFO_ACTIVITY_MEM",
                                         "GFX_VOLTAGE",
                                         "MEM_VOLTAGE",
                                         "ASIC_POWER",
                                         "TEMPERATURE_VRSOC",
                                         "TEMPERATURE_VRMVDD0",
                                         "TEMPERATURE_VRMVDD1",
                                         "TEMPERATURE_HOTSPOT",
                                         "TEMPERATURE_GFX",
                                         "TEMPERATURE_SOC",
                                         "GFX_POWER",
                                         "GFX_CURRENT",
                                         "TEMPERATURE_CPU",
                                         "CPU_POWER",
                                         "CLK_CPUCLK",
                                         "THROTTLER_STATUS",
                                         "CLK_VCN1CLK1",
                                         "CLK_VCN1CLK2",
                                         "SMART_POWERSHIFT_CPU",
                                         "SMART_POWERSHIFT_DGPU",
                                         "BUS_SPEED",
                                         "BUS_LANES",
                                         "TEMPERATURE_LIQUID0",
                                         "TEMPERATURE_LIQUID1",
                                         "CLK_FCLK",
                                         "THROTTLER_STATUS_CPU",
                                         "SSPAIRED_ASICPOWER",
                                         "SSTOTAL_POWERLIMIT",
                                         "SSAPU_POWERLIMIT",
                                         "SSDGPU_POWERLIMIT",
                                         "TEMPERATURE_HOTSPOT_GCD",
                                         "TEMPERATURE_HOTSPOT_MCD",
                                         "THROTTLE_PERCENTAGE_TEMP_GFX",
                                         "THROTTLE_PERCENTAGE_TEMP_MEM",
                                         "THROTTLE_PERCENTAGE_TEMP_VR",
                                         "THROTTLE_PERCENTAGE_POWER",
                                         "THROTTLE_PERCENTAGE_TDC",
                                         "THROTTLE_PERCENTAGE_VMAX",
                                         "BUS_CURR_MAX_SPEED",
                                         "RESERVED_1",
                                         "RESERVED_2",
                                         "RESERVED_3",
                                         "RESERVED_4",
                                         "RESERVED_5",
                                         "RESERVED_6",
                                         "RESERVED_7",
                                         "RESERVED_8",
                                         "RESERVED_9",
                                         "RESERVED_10",
                                         "RESERVED_11",
                                         "RESERVED_12",
                                         "CLK_NPUCLK",
                                         "NPU_BUSY_AVG",
                                         "BOARD_POWER",
                                         "TEMPERATURE_INTAKE"};
    static_assert(std::size(names) == ADL_PMLOG_MAX_SENSORS_REAL);
    return id < std::size(names) ? names[id] : "UNKNOWN_PMLOG_SENSOR";
}

template <class Value, class Support, class Metrics, class Check, class Read>
void adlxField(Log &log, const std::string &prefix, Support *support, Metrics *metrics, Check check,
               Read read, const char *name, const char *unit, const char *comment) {
    adlx_bool supported{};
    auto status = (support->*check)(&supported);
    Json value;
    if (status == ADLX_OK && supported) {
        Value raw{};
        status = (metrics->*read)(&raw);
        if (status == ADLX_OK)
            value = raw;
    }
    const int rc = static_cast<int>(status);
    log.reading("AMD ADLX", prefix + name, name,
                {{"supported", supported}, {"api_status", rc}, {"value", value}}, value, unit, comment,
                rc          ? "error"
                : supported ? "ok"
                            : "unsupported",
                rc ? std::to_string(rc) : "");
}
template <class Metrics>
void providerTimestamp(Log &log, const std::string &id, const char *name, Metrics *metrics) {
    adlx_int64 stamp{};
    const auto status = metrics->TimeStamp(&stamp);
    const int rc = static_cast<int>(status);
    log.reading("AMD ADLX", id, name, rc ? Json{} : Json(stamp), {}, "ms",
                "ADLX provider clock; UTC origin is not assumed", rc ? "unavailable" : "ok",
                rc ? std::to_string(rc) : "");
}
class Amd final : public Collector {
    HMODULE adl{}, adlx{};
    void *context{};
    IADLXSystem *system{}; // Owned by ADLXInitialize/ADLXTerminate, not ref-counted.
    IADLXPerformanceMonitoringServicesPtr performance;
    adlx_uint64 version{};
    using Create = int(__cdecl *)(ADL_MAIN_MALLOC_CALLBACK, int, void **);
    using Destroy = int(__cdecl *)(void *);
    using Read = int(__cdecl *)(void *, int, ADLPMLogDataOutput *);
    using Count = int(__cdecl *)(void *, int *);
    using Adapters = int(__cdecl *)(void *, AdapterInfo *, int);
    Destroy destroy{};
    Read read{};
    ADLXTerminate_Fn terminate{};
    Json adapters = Json::array();
    std::vector<int> indices;
    std::string adlError, adlxError;
    static std::string entryString(const char *start, std::size_t limit) {
        std::size_t length{};
        while (length < limit && start[length])
            ++length;
        return std::string(start, length);
    }

  public:
    explicit Amd(bool enabled) {
        if (!enabled) {
            adlError = adlxError = "Disabled by command line";
            return;
        }
        adl = LoadLibraryExW(L"atiadlxx.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (adl) {
            auto create = symbol<Create>(adl, "ADL2_Main_Control_Create");
            destroy = symbol<Destroy>(adl, "ADL2_Main_Control_Destroy");
            read = symbol<Read>(adl, "ADL2_New_QueryPMLogData_Get");
            auto count = symbol<Count>(adl, "ADL2_Adapter_NumberOfAdapters_Get");
            auto info = symbol<Adapters>(adl, "ADL2_Adapter_AdapterInfo_Get");
            if (create && destroy && read && count && info && create(allocate, 1, &context) == ADL_OK &&
                context) {
                int n{};
                if (count(context, &n) == ADL_OK && n > 0 && n <= 128) {
                    std::vector<AdapterInfo> entries(n);
                    for (auto &entry : entries)
                        entry.iSize = sizeof(AdapterInfo);
                    if (info(context, entries.data(),
                             static_cast<int>(entries.size() * sizeof(AdapterInfo))) == ADL_OK)
                        for (const auto &entry : entries) {
                            adapters.push(
                                {{"index", entry.iAdapterIndex},
                                 {"udid", entryString(entry.strUDID, sizeof(entry.strUDID))},
                                 {"name", entryString(entry.strAdapterName, sizeof(entry.strAdapterName))},
                                 {"bus", entry.iBusNumber},
                                 {"device", entry.iDeviceNumber},
                                 {"function", entry.iFunctionNumber},
                                 {"vendor_id", entry.iVendorID},
                                 {"present", entry.iPresent}});
                            if (entry.iPresent && (entry.iVendorID == 1002 || entry.iVendorID == 0x1002))
                                indices.push_back(entry.iAdapterIndex);
                        }
                }
            }
            if (indices.empty())
                adlError = "No present AMD PMLog adapters or ADL initialization failed";
        } else
            adlError = "Installed atiadlxx.dll not found";
        adlx = LoadLibraryExW(ADLX_DLL_NAMEW, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (adlx) {
            auto query = symbol<ADLXQueryFullVersion_Fn>(adlx, ADLX_QUERY_FULL_VERSION_FUNCTION_NAME);
            auto initialize = symbol<ADLXInitialize_Fn>(adlx, ADLX_INIT_FUNCTION_NAME);
            terminate = symbol<ADLXTerminate_Fn>(adlx, ADLX_TERMINATE_FUNCTION_NAME);
            if (!query || !initialize || !terminate || query(&version) != ADLX_OK)
                adlxError = "ADLX API/version query failed";
            else if ((version >> 48) != 1 || ((version >> 32) & 0xFFFF) < 5)
                adlxError =
                    "This ADLX SDK binding supports 1.5+ in major 1; ADL and system sources remain available";
            else if (initialize(ADLX_FULL_VERSION, &system) != ADLX_OK || !system ||
                     system->GetPerformanceMonitoringServices(&performance) != ADLX_OK ||
                     !performance.GetPtr())
                adlxError = "ADLX performance initialization failed";
        } else
            adlxError = "Installed amdadlx64.dll not found";
    }
    ~Amd() {
        performance.Release();
        if (system && terminate)
            terminate();
        if (context && destroy)
            destroy(context);
        if (adlx)
            FreeLibrary(adlx);
        if (adl)
            FreeLibrary(adl);
    }
    Json info() override {
        return {{"ADL",
                 {{"library", adl ? modulePath(adl) : "atiadlxx.dll"},
                  {"adapters", adapters},
                  {"error", adlError},
                  {"pm_log_slots", ADL_PMLOG_MAX_SENSORS},
                  {"headers_commit", "84c881cb2136f04287eb3af12de00a9e2867aa9e"}}},
                {"ADLX",
                 {{"library", adlx ? modulePath(adlx) : "amdadlx64.dll"},
                  {"full_version", hex(version, 16)},
                  {"sdk_full_version", hex(ADLX_FULL_VERSION, 16)},
                  {"headers_commit", "d9f04a9bba022d6cf6333f005dd540b4ad19fb63"},
                  {"error", adlxError}}},
                {"API_source", "https://github.com/GPUOpen-LibrariesAndSDKs/ADLX"},
                {"ADL_API_source", "https://github.com/GPUOpen-LibrariesAndSDKs/display-library"}};
    }
    void sample(Log &log) override {
        log.source("AMD ADL PMLog", [&] {
            if (!adlError.empty()) {
                log.unavailable("AMD ADL PMLog", adlError);
                return;
            }
            for (int adapter : indices) {
                ADLPMLogDataOutput values{};
                values.size = sizeof(values);
                const auto started = utcNow();
                const int rc = read(context, adapter, &values);
                if (rc != ADL_OK) {
                    log.unavailable("AMD ADL PMLog adapter " + std::to_string(adapter),
                                    "API status=" + std::to_string(rc));
                    continue;
                }
                for (unsigned id = 0; id < ADL_PMLOG_MAX_SENSORS; ++id) {
                    const auto v = values.sensors[id];
                    const std::string name = pmName(id);
                    std::string unit;
                    double divisor = 1;
                    bool known = false;
                    if (name.starts_with("CLK_")) {
                        unit = "MHz";
                        known = true;
                    } else if (name.starts_with("TEMPERATURE_")) {
                        unit = "C";
                        known = true;
                    } else if (id == ADL_PMLOG_FAN_RPM) {
                        unit = "RPM";
                        known = true;
                    } else if (id == ADL_PMLOG_FAN_PERCENTAGE || id == ADL_PMLOG_INFO_ACTIVITY_GFX ||
                               id == ADL_PMLOG_INFO_ACTIVITY_MEM || id == ADL_PMLOG_NPU_BUSY_AVG ||
                               (id >= ADL_PMLOG_THROTTLE_PERCENTAGE_TEMP_GFX &&
                                id <= ADL_PMLOG_THROTTLE_PERCENTAGE_VMAX)) {
                        unit = "%";
                        known = true;
                    } else if (id == ADL_PMLOG_SOC_VOLTAGE || id == ADL_PMLOG_GFX_VOLTAGE ||
                               id == ADL_PMLOG_MEM_VOLTAGE) {
                        unit = "V";
                        divisor = 1000;
                        known = true;
                    } else if (id == ADL_PMLOG_ASIC_POWER) {
                        unit = "W";
                        known = true;
                    } else if (id == ADL_PMLOG_GFX_POWER) {
                        // On the verified WUJIE APU, this domain tracked the
                        // GPU-only phase and reconciled with PawnIO core/package energy.
                        unit = "W";
                        known = true;
                    }
                    log.reading("AMD ADL PMLog",
                                "adapter/" + std::to_string(adapter) + "/sensor/" + std::to_string(id), name,
                                {{"supported", v.supported},
                                 {"value", v.value},
                                 {"api_snapshot_started_utc", started}},
                                v.supported && known ? Json(v.value / divisor) : Json{}, unit,
                                known ? "Public ADL sensor meaning; raw value also retained"
                                      : "Raw PMLog slot; unit/meaning not confirmed",
                                v.supported ? "ok" : "unsupported");
                }
            }
        });
        log.source("AMD ADLX", [&] {
            if (!adlxError.empty() || !performance.GetPtr()) {
                log.unavailable("AMD ADLX", adlxError);
                return;
            }
            IADLXSystemMetricsSupportPtr cpuSupport;
            IADLXSystemMetricsPtr cpuMetrics;
            if (performance->GetSupportedSystemMetrics(&cpuSupport) == ADLX_OK && cpuSupport.GetPtr() &&
                performance->GetCurrentSystemMetrics(&cpuMetrics) == ADLX_OK && cpuMetrics.GetPtr()) {
                providerTimestamp(log, "system/timestamp", "System metrics provider timestamp",
                                  cpuMetrics.GetPtr());
                adlxField<adlx_double>(log, "system/", cpuSupport.GetPtr(), cpuMetrics.GetPtr(),
                                       &IADLXSystemMetricsSupport::IsSupportedCPUUsage,
                                       &IADLXSystemMetrics::CPUUsage, "CPUUsage", "%",
                                       "ADLX system metrics CPU utilization");
                adlxField<adlx_int>(log, "system/", cpuSupport.GetPtr(), cpuMetrics.GetPtr(),
                                    &IADLXSystemMetricsSupport::IsSupportedSystemRAM,
                                    &IADLXSystemMetrics::SystemRAM, "SystemRAM", "MB",
                                    "ADLX system RAM metric");
                adlxField<adlx_int>(log, "system/", cpuSupport.GetPtr(), cpuMetrics.GetPtr(),
                                    &IADLXSystemMetricsSupport::IsSupportedSmartShift,
                                    &IADLXSystemMetrics::SmartShift, "SmartShift", "shift_index",
                                    "Range -100..100; negative shifts power to CPU, positive to GPU");
            }
            IADLXGPUListPtr gpus;
            if (system->GetGPUs(&gpus) != ADLX_OK || !gpus.GetPtr())
                throw std::runtime_error("ADLX GPU list unavailable");
            const unsigned count = gpus->Size();
            if (count > 128)
                throw std::runtime_error("Invalid ADLX GPU count");
            for (unsigned index = 0; index < count; ++index) {
                IADLXGPUPtr gpu;
                IADLXGPUMetricsSupportPtr support;
                IADLXGPUMetricsPtr metrics;
                if (gpus->At(index, &gpu) != ADLX_OK || !gpu.GetPtr())
                    continue;
                if (performance->GetSupportedGPUMetrics(gpu.GetPtr(), &support) != ADLX_OK ||
                    !support.GetPtr() ||
                    performance->GetCurrentGPUMetrics(gpu.GetPtr(), &metrics) != ADLX_OK || !metrics.GetPtr())
                    continue;
                const std::string prefix = "gpu/" + std::to_string(index) + "/";
                providerTimestamp(log, prefix + "timestamp", "GPU metrics provider timestamp",
                                  metrics.GetPtr());
                const auto field = [&](auto valueType, auto check, auto readValue, const char *name,
                                       const char *unit) {
                    adlxField<decltype(valueType)>(log, prefix, support.GetPtr(), metrics.GetPtr(), check,
                                                   readValue, name, unit,
                                                   "ADLX GPU index is scoped to this API and run");
                };
                field(adlx_double{}, &IADLXGPUMetricsSupport::IsSupportedGPUUsage, &IADLXGPUMetrics::GPUUsage,
                      "GPUUsage", "%");
                field(adlx_int{}, &IADLXGPUMetricsSupport::IsSupportedGPUClockSpeed,
                      &IADLXGPUMetrics::GPUClockSpeed, "GPUClockSpeed", "MHz");
                field(adlx_int{}, &IADLXGPUMetricsSupport::IsSupportedGPUVRAMClockSpeed,
                      &IADLXGPUMetrics::GPUVRAMClockSpeed, "GPUVRAMClockSpeed", "MHz");
                field(adlx_double{}, &IADLXGPUMetricsSupport::IsSupportedGPUTemperature,
                      &IADLXGPUMetrics::GPUTemperature, "GPUTemperature", "C");
                field(adlx_double{}, &IADLXGPUMetricsSupport::IsSupportedGPUHotspotTemperature,
                      &IADLXGPUMetrics::GPUHotspotTemperature, "GPUHotspotTemperature", "C");
                field(adlx_double{}, &IADLXGPUMetricsSupport::IsSupportedGPUPower, &IADLXGPUMetrics::GPUPower,
                      "GPUPower", "W");
                field(adlx_double{}, &IADLXGPUMetricsSupport::IsSupportedGPUTotalBoardPower,
                      &IADLXGPUMetrics::GPUTotalBoardPower, "GPUTotalBoardPower", "W");
                field(adlx_int{}, &IADLXGPUMetricsSupport::IsSupportedGPUFanSpeed,
                      &IADLXGPUMetrics::GPUFanSpeed, "GPUFanSpeed", "RPM");
                field(adlx_int{}, &IADLXGPUMetricsSupport::IsSupportedGPUVRAM, &IADLXGPUMetrics::GPUVRAM,
                      "GPUVRAM", "MB");
                field(adlx_int{}, &IADLXGPUMetricsSupport::IsSupportedGPUVoltage,
                      &IADLXGPUMetrics::GPUVoltage, "GPUVoltage", "mV");
                field(adlx_double{}, &IADLXGPUMetricsSupport::IsSupportedGPUIntakeTemperature,
                      &IADLXGPUMetrics::GPUIntakeTemperature, "GPUIntakeTemperature", "C");
                IADLXGPUMetricsSupport1Ptr extendedSupport(support.GetPtr());
                IADLXGPUMetrics1Ptr extendedMetrics(metrics.GetPtr());
                if (extendedSupport.GetPtr() && extendedMetrics.GetPtr()) {
                    adlxField<adlx_double>(log, prefix, extendedSupport.GetPtr(), extendedMetrics.GetPtr(),
                                           &IADLXGPUMetricsSupport1::IsSupportedGPUMemoryTemperature,
                                           &IADLXGPUMetrics1::GPUMemoryTemperature, "GPUMemoryTemperature",
                                           "C", "ADLX GPU index is scoped to this API and run");
                    adlxField<adlx_int>(log, prefix, extendedSupport.GetPtr(), extendedMetrics.GetPtr(),
                                        &IADLXGPUMetricsSupport1::IsSupportedNPUFrequency,
                                        &IADLXGPUMetrics1::NPUFrequency, "NPUFrequency", "MHz",
                                        "ADLX GPU index is scoped to this API and run");
                    adlxField<adlx_int>(log, prefix, extendedSupport.GetPtr(), extendedMetrics.GetPtr(),
                                        &IADLXGPUMetricsSupport1::IsSupportedNPUActivityLevel,
                                        &IADLXGPUMetrics1::NPUActivityLevel, "NPUActivityLevel", "%",
                                        "ADLX GPU index is scoped to this API and run");
                }
            }
        });
    }
};
} // namespace
std::unique_ptr<Collector> createAmd(bool enabled) {
    return std::make_unique<Amd>(enabled);
}
} // namespace probe
