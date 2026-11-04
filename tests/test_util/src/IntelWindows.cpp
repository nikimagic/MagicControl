#include "WinHelpers.h"
#include <array>
#include <igcl_api.h>
namespace probe {
namespace {
class IntelGpu final : public Collector {
    HMODULE library{};
    ctl_api_handle_t api{};
    std::vector<ctl_device_adapter_handle_t> devices;
    std::string error;
    template <class T> T get(const char *name) {
        return symbol<T>(library, name);
    }
    static Json itemValue(const ctl_oc_telemetry_item_t &item) {
        if (!item.bSupported)
            return {};
        switch (item.type) {
        case CTL_DATA_TYPE_INT8:
            return item.value.data8;
        case CTL_DATA_TYPE_UINT8:
            return item.value.datau8;
        case CTL_DATA_TYPE_INT16:
            return item.value.data16;
        case CTL_DATA_TYPE_UINT16:
            return item.value.datau16;
        case CTL_DATA_TYPE_INT32:
            return item.value.data32;
        case CTL_DATA_TYPE_UINT32:
            return item.value.datau32;
        case CTL_DATA_TYPE_INT64:
            return item.value.data64;
        case CTL_DATA_TYPE_UINT64:
            return item.value.datau64 <= static_cast<std::uint64_t>(INT64_MAX)
                       ? Json(item.value.datau64)
                       : Json(std::to_string(item.value.datau64));
        case CTL_DATA_TYPE_FLOAT:
            return static_cast<double>(item.value.datafloat);
        case CTL_DATA_TYPE_DOUBLE:
            return item.value.datadouble;
        default:
            return {};
        }
    }
    template <class Handle>
    std::vector<Handle> enumerate(const char *name, ctl_device_adapter_handle_t device) {
        using Fn = ctl_result_t(CTL_APICALL *)(ctl_device_adapter_handle_t, std::uint32_t *, Handle *);
        const auto function = get<Fn>(name);
        std::uint32_t count{};
        if (!function || function(device, &count, nullptr) != CTL_RESULT_SUCCESS || count > 256)
            return {};
        std::vector<Handle> handles(count);
        if (count && function(device, &count, handles.data()) != CTL_RESULT_SUCCESS)
            return {};
        handles.resize(count);
        return handles;
    }

    static std::string unitName(ctl_units_t unit) {
        static constexpr const char *names[]{"MHz",   "GT/s", "MT/s", "V", "W",    "C",  "J",   "s",
                                             "bytes", "RPM",  "mW",   "%", "GB/s", "mV", "MB/s"};
        const auto index = static_cast<unsigned>(unit);
        return index < std::size(names) ? names[index] : "raw";
    }

  public:
    explicit IntelGpu(bool enabled) {
        if (!enabled) {
            error = "Disabled";
            return;
        }
        library = LoadLibraryExW(L"ControlLib.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!library) {
            error = "Installed Intel IGCL ControlLib.dll not found";
            return;
        }
        auto initialize = get<decltype(&ctlInit)>("ctlInit");
        auto enumerateDevices = get<decltype(&ctlEnumerateDevices)>("ctlEnumerateDevices");
        if (!initialize || !enumerateDevices) {
            error = "Incomplete IGCL API";
            return;
        }
        ctl_init_args_t args{};
        args.Size = sizeof(args);
        args.AppVersion = CTL_MAKE_VERSION(1, 0);
        args.flags = CTL_INIT_FLAG_USE_LEVEL_ZERO;
        const auto rc = initialize(&args, &api);
        if (rc != CTL_RESULT_SUCCESS) {
            error = "ctlInit status=0x" + hex(rc, 8);
            return;
        }
        std::uint32_t count{};
        if (enumerateDevices(api, &count, nullptr) != CTL_RESULT_SUCCESS || count > 128) {
            error = "IGCL device enumeration failed";
            return;
        }
        devices.resize(count);
        if (count && enumerateDevices(api, &count, devices.data()) != CTL_RESULT_SUCCESS) {
            devices.clear();
            error = "IGCL device handles unavailable";
        }
    }
    ~IntelGpu() {
        if (api)
            if (auto close = get<decltype(&ctlClose)>("ctlClose"))
                close(api);
        if (library)
            FreeLibrary(library);
    }
    Json info() override {
        Json adapters = Json::array();
        auto properties =
            library ? get<decltype(&ctlGetDeviceProperties)>("ctlGetDeviceProperties") : nullptr;
        for (unsigned index = 0; index < devices.size(); ++index) {
            ctl_device_adapter_properties_t p{};
            p.Size = sizeof(p);
            std::array<unsigned char, sizeof(LUID)> deviceId{};
            p.pDeviceID = deviceId.data();
            p.device_id_size = static_cast<unsigned>(deviceId.size());
            const auto rc =
                properties ? properties(devices[index], &p) : CTL_RESULT_ERROR_UNSUPPORTED_FEATURE;
            adapters.push({{"index", index},
                           {"status", static_cast<unsigned>(rc)},
                           {"name", rc == CTL_RESULT_SUCCESS ? p.name : ""},
                           {"vendor_id", p.pci_vendor_id},
                           {"device_id", p.pci_device_id},
                           {"driver_version_hex", hex(p.driver_version, 16)},
                           {"os_device_id_hex", bytesHex(deviceId.data(), deviceId.size())}});
        }
        return {{"available", api != nullptr},
                {"library", library ? modulePath(library) : "ControlLib.dll"},
                {"error", error},
                {"adapters", adapters},
                {"SDK_source", "https://github.com/intel/drivers.gpu.control-library"}};
    }
    void sample(Log &log) override {
        if (!api || devices.empty()) {
            log.unavailable("Intel IGCL", error.empty() ? "No IGCL devices" : error);
            return;
        }
        for (unsigned index = 0; index < devices.size(); ++index) {
            const auto device = devices[index];
            const auto id = "gpu/" + std::to_string(index) + "/";
            if (auto state = get<decltype(&ctlTemperatureGetState)>("ctlTemperatureGetState")) {
                const auto sensors = enumerate<ctl_temp_handle_t>("ctlEnumTemperatureSensors", device);
                for (unsigned i = 0; i < sensors.size(); ++i) {
                    double value{};
                    const auto rc = state(sensors[i], &value);
                    log.reading("Intel IGCL", id + "temperature/" + std::to_string(i),
                                "GPU temperature sensor", rc == CTL_RESULT_SUCCESS ? Json(value) : Json{},
                                rc == CTL_RESULT_SUCCESS ? Json(value) : Json{}, "C",
                                "Enumerated IGCL sensor", rc == CTL_RESULT_SUCCESS ? "ok" : "unavailable",
                                rc == CTL_RESULT_SUCCESS ? "" : hex(rc, 8));
                }
            }
            if (auto state = get<decltype(&ctlFrequencyGetState)>("ctlFrequencyGetState")) {
                const auto sensors = enumerate<ctl_freq_handle_t>("ctlEnumFrequencyDomains", device);
                for (unsigned i = 0; i < sensors.size(); ++i) {
                    ctl_freq_state_t v{};
                    v.Size = sizeof(v);
                    const auto rc = state(sensors[i], &v);
                    log.reading("Intel IGCL", id + "frequency/" + std::to_string(i), "GPU frequency domain",
                                rc == CTL_RESULT_SUCCESS ? Json{{"current_voltage_V", v.currentVoltage},
                                                                {"requested_MHz", v.request},
                                                                {"actual_MHz", v.actual},
                                                                {"tdp_max_MHz", v.tdp},
                                                                {"efficient_MHz", v.efficient},
                                                                {"throttle_reasons", v.throttleReasons}}
                                                         : Json{},
                                v.actual >= 0 && rc == CTL_RESULT_SUCCESS ? Json(v.actual) : Json{}, "MHz",
                                "Negative IGCL fields mean unknown",
                                rc == CTL_RESULT_SUCCESS ? "ok" : "unavailable",
                                rc == CTL_RESULT_SUCCESS ? "" : hex(rc, 8));
                }
            }
            if (auto state = get<decltype(&ctlFanGetState)>("ctlFanGetState")) {
                const auto fans = enumerate<ctl_fan_handle_t>("ctlEnumFans", device);
                for (unsigned i = 0; i < fans.size(); ++i) {
                    std::int32_t rpm{};
                    const auto rc = state(fans[i], CTL_FAN_SPEED_UNITS_RPM, &rpm);
                    log.reading("Intel IGCL", id + "fan/" + std::to_string(i), "GPU fan speed",
                                rc == CTL_RESULT_SUCCESS ? Json(rpm) : Json{},
                                rc == CTL_RESULT_SUCCESS && rpm >= 0 ? Json(rpm) : Json{}, "RPM",
                                "Negative fan speed means unknown",
                                rc == CTL_RESULT_SUCCESS ? "ok" : "unavailable",
                                rc == CTL_RESULT_SUCCESS ? "" : hex(rc, 8));
                }
            }
            if (auto telemetry = get<decltype(&ctlPowerTelemetryGet)>("ctlPowerTelemetryGet")) {
                ctl_power_telemetry_t values{};
                values.Size = sizeof(values);
                values.Version = 1;
                auto rc = telemetry(device, &values);
                if (rc != CTL_RESULT_SUCCESS) {
                    values = {};
                    values.Size = sizeof(values);
                    rc = telemetry(device, &values);
                }
                if (rc != CTL_RESULT_SUCCESS) {
                    log.unavailable("Intel IGCL telemetry", hex(rc, 8));
                    continue;
                }
                log.reading("Intel IGCL", id + "telemetry_packet", "Complete IGCL telemetry packet",
                            {{"struct_size", sizeof(values)},
                             {"version", values.Version},
                             {"hex", bytesHex(&values, sizeof(values))}},
                            {}, "raw",
                            "ABI size/version retained with all documented fields; no settings changed");
                const auto emit = [&](const char *name, const ctl_oc_telemetry_item_t &v) {
                    const auto value = itemValue(v);
                    log.reading("Intel IGCL", id + name, name,
                                {{"supported", v.bSupported},
                                 {"unit_enum", static_cast<unsigned>(v.units)},
                                 {"type_enum", static_cast<unsigned>(v.type)},
                                 {"value", value}},
                                value, unitName(v.units), "Unit follows Intel ctl_units_t",
                                v.bSupported ? "ok" : "unsupported");
                };
                emit("timeStamp", values.timeStamp);
                emit("gpuEnergyCounter", values.gpuEnergyCounter);
                emit("gpuVoltage", values.gpuVoltage);
                emit("gpuCurrentClockFrequency", values.gpuCurrentClockFrequency);
                emit("gpuCurrentTemperature", values.gpuCurrentTemperature);
                emit("globalActivityCounter", values.globalActivityCounter);
                emit("renderComputeActivityCounter", values.renderComputeActivityCounter);
                emit("mediaActivityCounter", values.mediaActivityCounter);
                emit("vramEnergyCounter", values.vramEnergyCounter);
                emit("vramVoltage", values.vramVoltage);
                emit("vramCurrentClockFrequency", values.vramCurrentClockFrequency);
                emit("vramCurrentEffectiveFrequency", values.vramCurrentEffectiveFrequency);
                emit("vramReadBandwidthCounter", values.vramReadBandwidthCounter);
                emit("vramWriteBandwidthCounter", values.vramWriteBandwidthCounter);
                emit("vramCurrentTemperature", values.vramCurrentTemperature);
                emit("totalCardEnergyCounter", values.totalCardEnergyCounter);
                if (values.Version > 0) {
                    emit("gpuVrTemp", values.gpuVrTemp);
                    emit("vramVrTemp", values.vramVrTemp);
                    emit("saVrTemp", values.saVrTemp);
                    emit("gpuEffectiveClock", values.gpuEffectiveClock);
                    emit("gpuPowerPercent", values.gpuPowerPercent);
                    emit("gpuTemperaturePercent", values.gpuTemperaturePercent);
                    emit("vramReadBandwidth", values.vramReadBandwidth);
                    emit("vramWriteBandwidth", values.vramWriteBandwidth);
                }
                for (unsigned i = 0; i < CTL_FAN_COUNT; ++i)
                    emit(("fanSpeed/" + std::to_string(i)).c_str(), values.fanSpeed[i]);
            }
        }
    }
};
} // namespace
std::unique_ptr<Collector> createIntelGpu(bool enabled) {
    return std::make_unique<IntelGpu>(enabled);
}
} // namespace probe
