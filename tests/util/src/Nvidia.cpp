#include "NvmlFields.h"
#include "Probe.h"
#include <algorithm>
#include <nvml.h>
#ifdef _WIN32
#include "WinHelpers.h"
#else
#include <dlfcn.h>
#endif
namespace probe {
namespace {
class Nvidia final : public Collector {
#ifdef _WIN32
    HMODULE library{};
#else
    void *library{};
#endif
    bool ready{};
    std::string error;
    unsigned count{};
    template <class T> T get(const char *name) {
#ifdef _WIN32
        return reinterpret_cast<T>(GetProcAddress(library, name));
#else
        return reinterpret_cast<T>(dlsym(library, name));
#endif
    }

    void fields(Log &log, void *device, const std::string &prefix) {
        static_assert(kNvmlFields.size() == NVML_FI_MAX);
        // Native NVML exports take an opaque pointer. The NVIDIA Go header wraps
        // that handle in a one-pointer struct for cgo; use the native C ABI here.
        using QueryFields = nvmlReturn_t (*)(void *, int, nvmlFieldValue_t *);
        const auto query = get<QueryFields>("nvmlDeviceGetFieldValues");
        if (!query) {
            log.unavailable("NVIDIA NVML field API",
                            "nvmlDeviceGetFieldValues is not exported by the installed driver");
            return;
        }
        for (unsigned first = 1; first < NVML_FI_MAX; first += 32) {
            const unsigned batchCount = std::min<unsigned>(32, NVML_FI_MAX - first);
            std::vector<nvmlFieldValue_t> values(batchCount);
            for (unsigned i = 0; i < batchCount; ++i)
                values[i].fieldId = first + i;
            const auto batch = query(device, static_cast<int>(batchCount), values.data());
            for (auto &field : values) {
                const auto rc = batch == NVML_SUCCESS ? NVML_SUCCESS : query(device, 1, &field);
                const bool valid = rc == NVML_SUCCESS && field.nvmlReturn == NVML_SUCCESS;
                Json value;
                if (valid)
                    switch (field.valueType) {
                    case NVML_VALUE_TYPE_DOUBLE:
                        value = field.value.dVal;
                        break;
                    case NVML_VALUE_TYPE_UNSIGNED_INT:
                        value = field.value.uiVal;
                        break;
                    case NVML_VALUE_TYPE_UNSIGNED_LONG:
                        value = field.value.ulVal;
                        break;
                    case NVML_VALUE_TYPE_UNSIGNED_LONG_LONG:
                        value = field.value.ullVal;
                        break;
                    case NVML_VALUE_TYPE_SIGNED_LONG_LONG:
                        value = field.value.sllVal;
                        break;
                    case NVML_VALUE_TYPE_SIGNED_INT:
                        value = field.value.siVal;
                        break;
                    case NVML_VALUE_TYPE_UNSIGNED_SHORT:
                        value = field.value.usVal;
                        break;
                    default:
                        break;
                    }
                if (field.fieldId >= kNvmlFields.size()) {
                    log.unavailable("NVIDIA NVML fields", "Driver returned an invalid field ID");
                    continue;
                }
                const auto &description = kNvmlFields[field.fieldId];
                std::string unit = "raw";
                double divisor = 1;
                bool decoded = false;
                if (field.fieldId == NVML_FI_DEV_MEMORY_TEMP) {
                    unit = "C";
                    decoded = true;
                }
                if (field.fieldId == NVML_FI_DEV_TOTAL_ENERGY_CONSUMPTION ||
                    field.fieldId == NVML_FI_DEV_ENERGY) {
                    unit = "J";
                    divisor = 1000;
                    decoded = true;
                }
                if (field.fieldId >= NVML_FI_DEV_POWER_AVERAGE &&
                    field.fieldId <= NVML_FI_DEV_POWER_CURRENT_LIMIT) {
                    unit = "W";
                    divisor = 1000;
                    decoded = true;
                }
                const auto number = value.number();
                log.reading(
                    "NVIDIA NVML fields", prefix + "field/" + std::to_string(field.fieldId),
                    std::string(description.name),
                    {{"field_id", field.fieldId},
                     {"scope_id", field.scopeId},
                     {"api_status", static_cast<unsigned>(rc)},
                     {"field_status", static_cast<unsigned>(field.nvmlReturn)},
                     {"provider_timestamp_us", field.timestamp},
                     {"latency_us", field.latencyUsec},
                     {"value_type", static_cast<unsigned>(field.valueType)},
                     {"value", value},
                     {"value_hex", valid ? Json(bytesHex(&field.value, sizeof(field.value))) : Json{}}},
                    valid && decoded && number ? Json(*number / divisor) : Json{}, unit,
                    std::string(description.comment), valid ? "ok" : "unavailable",
                    valid ? "" : "NVML status=" + std::to_string(rc == NVML_SUCCESS ? field.nvmlReturn : rc));
            }
        }
    }

  public:
    explicit Nvidia(bool enabled) {
        if (!enabled) {
            error = "Disabled by command line";
            return;
        }
#ifdef _WIN32
        library = LoadLibraryExW(L"nvml.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
#else
        library = dlopen("libnvidia-ml.so.1", RTLD_NOW | RTLD_LOCAL);
#endif
        if (!library) {
            error = "Installed NVIDIA NVML library not found";
            return;
        }
        auto initialize = get<int (*)()>("nvmlInit_v2");
        auto number = get<int (*)(unsigned *)>("nvmlDeviceGetCount_v2");
        if (!initialize || !number) {
            error = "NVML API incomplete";
            return;
        }
        const int status = initialize();
        if (status) {
            error = "nvmlInit_v2 status=" + std::to_string(status);
            return;
        }
        ready = true;
        if (number(&count) || count > 128) {
            count = 0;
            error = "NVML device enumeration failed";
        }
    }
    ~Nvidia() {
        if (ready)
            if (auto close = get<int (*)()>("nvmlShutdown"))
                close();
#ifdef _WIN32
        if (library)
            FreeLibrary(library);
#else
        if (library)
            dlclose(library);
#endif
    }
    Json info() override {
        return {{"available", ready},
                {"device_count", count},
                {"published_field_id_max", NVML_FI_MAX},
                {"field_scope", 0},
                {"error", error},
                {"library",
#ifdef _WIN32
                 library ? modulePath(library) : "nvml.dll"
#else
                 "libnvidia-ml.so.1"
#endif
                }};
    }
    void sample(Log &log) override {
        if (!ready) {
            log.unavailable("NVIDIA NVML", error);
            return;
        }
        const auto handle = get<int (*)(unsigned, void **)>("nvmlDeviceGetHandleByIndex_v2");
        if (!handle)
            throw std::runtime_error("NVML handle API missing");
        for (unsigned i = 0; i < count; ++i) {
            void *device{};
            if (handle(i, &device) || !device)
                continue;
            const std::string id = "gpu/" + std::to_string(i) + "/";
            fields(log, device, id);
            for (const char *field : {"nvmlDeviceGetName", "nvmlDeviceGetUUID"}) {
                char text[256]{};
                auto fn = get<int (*)(void *, char *, unsigned)>(field);
                if (fn) {
                    const int rc = fn(device, text, sizeof(text));
                    log.reading("NVIDIA NVML", id + field, field, rc ? Json{} : Json(text), {}, "",
                                "Device identity", rc ? "error" : "ok", rc ? std::to_string(rc) : "");
                }
            }
            const auto scalar = [&](const char *api, const char *name, const char *unit,
                                    double divisor = 1.0) {
                auto fn = get<int (*)(void *, unsigned *)>(api);
                unsigned raw{};
                const int rc = fn ? fn(device, &raw) : -1;
                log.reading("NVIDIA NVML", id + api, name, rc ? Json{} : Json(raw),
                            rc ? Json{} : Json(raw / divisor), unit, "NVML status codes preserved",
                            rc ? "unavailable" : "ok", rc ? std::to_string(rc) : "");
            };
            scalar("nvmlDeviceGetFanSpeed", "Fan speed", "%");
            scalar("nvmlDeviceGetPowerUsage", "GPU power", "W", 1000);
            scalar("nvmlDeviceGetEnforcedPowerLimit", "Enforced power limit", "W", 1000);
            scalar("nvmlDeviceGetPerformanceState", "Performance state", "P-state");
            scalar("nvmlDeviceGetCurrPcieLinkGeneration", "PCIe generation", "generation");
            scalar("nvmlDeviceGetCurrPcieLinkWidth", "PCIe width", "lanes");
            auto temperature = get<int (*)(void *, unsigned, unsigned *)>("nvmlDeviceGetTemperature");
            unsigned temp{};
            if (temperature) {
                const int rc = temperature(device, 0, &temp);
                log.reading("NVIDIA NVML", id + "temperature", "GPU temperature", rc ? Json{} : Json(temp),
                            rc ? Json{} : Json(temp), "C", "NVML GPU temperature channel",
                            rc ? "unavailable" : "ok", rc ? std::to_string(rc) : "");
            }
            auto clock = get<int (*)(void *, unsigned, unsigned *)>("nvmlDeviceGetClockInfo");
            if (clock)
                for (unsigned kind = 0; kind < 4; ++kind) {
                    unsigned raw{};
                    const int rc = clock(device, kind, &raw);
                    const char *names[]{"Graphics clock", "SM clock", "Memory clock", "Video clock"};
                    log.reading("NVIDIA NVML", id + "clock/" + std::to_string(kind), names[kind],
                                rc ? Json{} : Json(raw), rc ? Json{} : Json(raw), "MHz", "",
                                rc ? "unavailable" : "ok", rc ? std::to_string(rc) : "");
                }
            struct Util {
                unsigned gpu{}, memory{};
            } util;
            auto usage = get<int (*)(void *, Util *)>("nvmlDeviceGetUtilizationRates");
            if (usage) {
                const int rc = usage(device, &util);
                log.reading("NVIDIA NVML", id + "utilization", "GPU/memory utilization",
                            rc ? Json{} : Json{{"gpu", util.gpu}, {"memory", util.memory}}, {}, "%", "",
                            rc ? "unavailable" : "ok", rc ? std::to_string(rc) : "");
            }
            struct Memory {
                unsigned long long total{}, free{}, used{};
            } memory;
            auto ram = get<int (*)(void *, Memory *)>("nvmlDeviceGetMemoryInfo");
            if (ram) {
                const int rc = ram(device, &memory);
                log.reading("NVIDIA NVML", id + "memory", "GPU memory",
                            rc ? Json{}
                               : Json{{"total", memory.total}, {"free", memory.free}, {"used", memory.used}},
                            {}, "bytes", "", rc ? "unavailable" : "ok", rc ? std::to_string(rc) : "");
            }
        }
    }
};
} // namespace
std::unique_ptr<Collector> createNvidia(bool enabled) {
    return std::make_unique<Nvidia>(enabled);
}
} // namespace probe
