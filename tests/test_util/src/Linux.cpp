#include "Probe.h"
#include "Smbios.h"
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <gnu/libc-version.h>
#include <regex>
#include <set>
#include <sys/utsname.h>
#include <unistd.h>
#if defined(__x86_64__) || defined(__i386__)
#include <cpuid.h>
#endif

namespace probe {
Json runLinuxCommand(const fs::path &, const std::vector<std::string> &, int timeoutMs);
fs::path executablePath() {
    std::vector<char> buffer(32768);
    const auto size = readlink("/proc/self/exe", buffer.data(), buffer.size());
    if (size < 0)
        throw std::runtime_error("Cannot locate executable");
    return fs::path(std::string(buffer.data(), size));
}
std::string hostName() {
    char name[256]{};
    return gethostname(name, sizeof(name)) == 0 ? name : "unknown";
}
namespace {
std::vector<fs::path> children(const fs::path &root) {
    std::vector<fs::path> result;
    std::error_code error;
    for (fs::directory_iterator it(root, fs::directory_options::skip_permission_denied, error), end;
         !error && it != end; it.increment(error))
        result.push_back(it->path());
    std::sort(result.begin(), result.end());
    return result;
}
std::string sensorPath(const fs::path &path) {
    std::error_code error;
    auto resolved = fs::weakly_canonical(path, error);
    return error ? path.string() : resolved.string();
}
Json attributes(const fs::path &directory) {
    Json result;
    for (const auto &path : children(directory)) {
        std::error_code error;
        if (fs::is_regular_file(path, error)) {
            if (auto value = readText(path, 65536)) {
                if (value->find('\0') != std::string::npos)
                    result[path.filename().string()] = {{"encoding", "hex"},
                                                        {"data", bytesHex(value->data(), value->size())}};
                else
                    result[path.filename().string()] = *value;
            } else
                result[path.filename().string()] = {{"status", "unavailable"},
                                                    {"error", std::strerror(errno)}};
        }
    }
    return result;
}
std::optional<double> number(const std::string &text) {
    try {
        std::size_t end{};
        const auto value = std::stod(text, &end);
        return end == text.size() && std::isfinite(value) ? std::optional<double>(value) : std::nullopt;
    } catch (...) {
        return {};
    }
}
void attribute(Log &log, const std::string &source, const fs::path &path, const std::string &name,
               double divisor = 1, const std::string &unit = "raw", const std::string &comment = "") {
    const auto raw = readText(path, 65536);
    if (!raw) {
        log.reading(source, sensorPath(path), name, {}, {}, unit, comment, "unavailable",
                    std::strerror(errno));
        return;
    }
    const auto value = number(*raw);
    log.reading(source, sensorPath(path), name, *raw, value ? Json(*value / divisor) : Json{}, unit, comment);
}
void intelDrmAttributes(Log &log, const fs::path &root, std::set<std::string> &visited, int depth = 0) {
    if (depth > 5 || !visited.insert(sensorPath(root)).second)
        return;
    static const std::regex directory("^(device|gt[0-9]*|tile[0-9]+|freq[0-9]+|memory[0-9]*|mem[0-9]*|rps|"
                                      "engine|rcs[0-9]+|bcs[0-9]+|vcs[0-9]+|vecs[0-9]+|ccs[0-9]+)$");
    for (const auto &path : children(root)) {
        const auto name = path.filename().string();
        std::error_code error;
        if (fs::is_directory(path, error)) {
            if (std::regex_match(name, directory))
                intelDrmAttributes(log, path, visited, depth + 1);
            continue;
        }
        if (name.find("freq") == std::string::npos && name.find("clock") == std::string::npos &&
            name.find("busy") == std::string::npos && name.find("throttle") == std::string::npos &&
            name.find("energy") == std::string::npos && name.find("power") == std::string::npos &&
            !name.ends_with("_bytes"))
            continue;
        attribute(log, "Linux Intel DRM", path, name, 1, name.ends_with("_mhz") ? "MHz" : "raw",
                  "Intel i915/xe native attribute; names distinguish live readings, counters and limits");
    }
}
class Linux final : public Platform {
    Options options;
    std::unique_ptr<Collector> nvidia;
    std::map<std::string, std::pair<unsigned long long, unsigned long long>> cpuBefore;
    struct Energy {
        double value{}, range{};
        Clock::time_point at;
    };
    std::map<std::string, Energy> energyBefore;
    std::vector<fs::path> ecFiles;
    fs::path smartctl, xrt;
    bool xrtAll = true;

  public:
    explicit Linux(const Options &opts) : options(opts), nvidia(createNvidia(opts.nvidia)) {
        if (options.ec)
            for (const auto &folder : children("/sys/kernel/debug/ec")) {
                const auto io = folder / "io";
                if (fs::exists(io))
                    ecFiles.push_back(io);
            }
        for (const char *path : {"/usr/sbin/smartctl", "/usr/bin/smartctl", "/usr/local/sbin/smartctl"})
            if (smartctl.empty() && access(path, X_OK) == 0)
                smartctl = path;
        for (const char *path : {"/usr/bin/xrt-smi", "/usr/local/bin/xrt-smi", "/opt/xilinx/xrt/bin/xrt-smi"})
            if (xrt.empty() && access(path, X_OK) == 0)
                xrt = path;
    }
    Json inventory() override {
        utsname u{};
        uname(&u);
        Json result{{"platform", "Linux"},
                    {"hostname", hostName()},
                    {"uid", getuid()},
                    {"euid", geteuid()},
                    {"uname",
                     {{"sysname", u.sysname},
                      {"release", u.release},
                      {"version", u.version},
                      {"machine", u.machine}}},
                    {"DMI", attributes("/sys/class/dmi/id")},
                    {"os_release", readText("/etc/os-release").value_or("")},
                    {"cpuinfo", readText("/proc/cpuinfo").value_or("")},
                    {"kernel_modules", readText("/proc/modules").value_or("")},
                    {"memory", readText("/proc/meminfo").value_or("")}};
        const std::string release = u.release;
        result["WSL"] =
            release.find("microsoft") != std::string::npos || release.find("Microsoft") != std::string::npos;
        result["virtualization_note"] = "Only the running Linux environment is queried. WSL is a VM; no "
                                        "Windows interop or host-sensor substitution.";
        result["glibc_runtime"] = gnu_get_libc_version();
        Json smbios;
        result["memory_modules"] = Json::array();
        for (const char *file :
             {"/sys/firmware/dmi/tables/DMI", "/sys/firmware/dmi/tables/smbios_entry_point"}) {
            const auto bytes = readBytes(file, 4 * 1024 * 1024);
            if (bytes) {
                smbios[file] = {{"status", "ok"},
                                {"bytes", bytes->size()},
                                {"raw_hex", bytesHex(bytes->data(), bytes->size())}};
                if (std::string(file).ends_with("/DMI"))
                    result["memory_modules"] = memoryModulesFromDmi(*bytes);
            } else
                smbios[file] = {{"status", "unavailable"}, {"error", std::strerror(errno)}};
        }
        result["SMBIOS_raw"] = std::move(smbios);
        Json modules = Json::array();
        for (const auto &module : children("/sys/module"))
            modules.push({{"name", module.filename().string()},
                          {"version", readText(module / "version").value_or("")},
                          {"srcversion", readText(module / "srcversion").value_or("")},
                          {"taint", readText(module / "taint").value_or("")},
                          {"initstate", readText(module / "initstate").value_or("")}});
        result["loaded_kernel_module_versions"] = std::move(modules);
        result["optional_readonly_tools"] = {{"smartctl", smartctl.string()}, {"xrt_smi", xrt.string()}};
        Json pci = Json::array();
        for (const auto &device : children("/sys/bus/pci/devices")) {
            Json entry{{"path", device.string()}, {"driver", sensorPath(device / "driver")}};
            for (const char *key : {"vendor", "device", "subsystem_vendor", "subsystem_device", "class",
                                    "revision", "numa_node", "uevent"})
                if (auto value = readText(device / key))
                    entry[key] = *value;
            pci.push(std::move(entry));
        }
        result["PCI"] = std::move(pci);
        Json monitors = Json::array();
        for (const auto &hwmon : children("/sys/class/hwmon")) {
            Json files = Json::array();
            for (const auto &file : children(hwmon))
                files.push(file.filename().string());
            monitors.push({{"path", hwmon.string()},
                           {"device_path", sensorPath(hwmon / "device")},
                           {"driver", sensorPath(hwmon / "device/driver")},
                           {"name", readText(hwmon / "name").value_or("")},
                           {"attributes", files}});
        }
        result["hwmon_catalog"] = std::move(monitors);
        Json blocks = Json::array();
        for (const auto &path : children("/sys/block"))
            blocks.push({{"name", path.filename().string()},
                         {"device", attributes(path / "device")},
                         {"queue", attributes(path / "queue")},
                         {"sectors", readText(path / "size").value_or("")}});
        result["block_devices"] = std::move(blocks);
        Json batteries = Json::array();
        for (const auto &path : children("/sys/class/power_supply"))
            batteries.push({{"path", path.string()}, {"attributes", attributes(path)}});
        result["power_supplies"] = std::move(batteries);
        Json ec = Json::array();
        for (const auto &path : ecFiles)
            ec.push({{"path", path.string()}, {"bytes", 256}, {"transport", "kernel ec_sys debugfs"}});
        result["EC"] = ec;
        result["EC_note"] =
            "ec_sys exposes its standard 256-byte window. Extended OEM EC banks require a separate verified "
            "kernel interface. No /dev/mem access, module loading, or guessed bank scanning.";
        result["NVIDIA_NVML"] = nvidia->info();
        result["PawnIO"] = "Windows-only driver; Linux uses kernel-exported native sensor interfaces";
#if defined(__x86_64__) || defined(__i386__)
        Json cpuid = Json::array();
        for (unsigned leaf : {0u, 1u, 7u, 0x80000000u, 0x80000001u, 0x80000007u, 0x80000008u}) {
            unsigned a{}, b{}, c{}, d{};
            if (__get_cpuid_count(leaf, 0, &a, &b, &c, &d))
                cpuid.push({{"leaf", hex(leaf, 8)},
                            {"eax", hex(a, 8)},
                            {"ebx", hex(b, 8)},
                            {"ecx", hex(c, 8)},
                            {"edx", hex(d, 8)}});
        }
        result["CPUID"] = std::move(cpuid);
#endif
        return result;
    }
    void sample(Log &log) override {
        log.source("Linux hwmon", [&] {
            const std::regex sensor(
                "^(temp|in|curr|power|energy|humidity|fan|pwm|freq)[0-9]+(_[a-zA-Z0-9_]+)?$");
            for (const auto &monitor : children("/sys/class/hwmon")) {
                const auto chip = readText(monitor / "name").value_or("unknown");
                for (const auto &file : children(monitor)) {
                    const auto name = file.filename().string();
                    if (!std::regex_match(name, sensor))
                        continue;
                    std::string unit = "raw", comment = "Kernel hwmon " + chip + "; " + name;
                    double divisor = 1;
                    const auto split = name.find('_');
                    const auto prefix = name.substr(0, split);
                    const auto item = split == std::string::npos ? "" : name.substr(split + 1);
                    const auto label = readText(monitor / (prefix + "_label")).value_or("");
                    const bool measured = item == "input" || item == "average" || item == "min" ||
                                          item == "max" || item == "crit" || item == "lcrit" ||
                                          item == "cap" || item == "lowest" || item == "highest" ||
                                          item == "target" || item == "offset";
                    if (measured) {
                        if (name.starts_with("temp")) {
                            unit = "C";
                            divisor = 1000;
                        } else if (name.starts_with("in")) {
                            unit = "V";
                            divisor = 1000;
                        } else if (name.starts_with("curr")) {
                            unit = "A";
                            divisor = 1000;
                        } else if (name.starts_with("power")) {
                            unit = "W";
                            divisor = 1e6;
                        } else if (name.starts_with("energy")) {
                            unit = "J";
                            divisor = 1e6;
                        } else if (name.starts_with("humidity")) {
                            unit = "%";
                            divisor = 1000;
                        } else if (name.starts_with("fan"))
                            unit = "RPM";
                        else if (name.starts_with("freq"))
                            unit = "Hz";
                    }
                    if (prefix.starts_with("pwm") && item.empty()) {
                        unit = "%";
                        divisor = 255.0 / 100;
                    }
                    if (item.ends_with("alarm") || item == "alarm" || item == "fault")
                        unit = "boolean";
                    if (item == "label")
                        unit = "text";
                    attribute(log, "Linux hwmon / " + chip, file, label.empty() ? name : label + " / " + name,
                              divisor, unit, comment);
                }
            }
        });
        log.source("Linux thermal", [&] {
            for (const auto &zone : children("/sys/class/thermal")) {
                if (!zone.filename().string().starts_with("thermal_zone"))
                    continue;
                const auto name = readText(zone / "type").value_or(zone.filename().string());
                for (const auto &path : children(zone)) {
                    const auto field = path.filename().string();
                    if (field == "temp" || (field.starts_with("trip_point_") &&
                                            (field.ends_with("_temp") || field.ends_with("_hyst"))))
                        attribute(log, "Linux thermal", path, name + " / " + field, 1000, "C",
                                  "Kernel thermal zone; a zone is not automatically a CPU core");
                }
            }
        });
        log.source("Linux CPU / proc", [&] {
            const auto stat = readText("/proc/stat");
            if (!stat)
                throw std::runtime_error("/proc/stat unavailable");
            log.reading("Linux proc", "/proc/stat", "CPU scheduler counters", *stat);
            std::istringstream lines(*stat);
            std::string row;
            while (std::getline(lines, row)) {
                std::istringstream fields(row);
                std::string name;
                fields >> name;
                if (!name.starts_with("cpu"))
                    continue;
                std::array<unsigned long long, 8> times{};
                bool valid = true;
                for (auto &value : times)
                    if (!(fields >> value))
                        valid = false;
                if (!valid)
                    continue;
                unsigned long long total{};
                for (auto value : times)
                    total += value;
                const auto idle = times[3] + times[4];
                const auto before = cpuBefore.find(name);
                Json usage;
                if (before != cpuBefore.end() && total > before->second.first &&
                    idle >= before->second.second)
                    usage = 100.0 * (1.0 - static_cast<double>(idle - before->second.second) /
                                               (total - before->second.first));
                log.reading("Linux proc", name + "/usage", name + " usage",
                            {{"total", total}, {"idle", idle}}, usage, "%",
                            "Delta of /proc/stat; guest time not double-counted",
                            before == cpuBefore.end() ? "warmup" : "ok");
                cpuBefore[name] = {total, idle};
            }
            for (const char *path : {"/proc/meminfo", "/proc/loadavg", "/proc/diskstats", "/proc/uptime",
                                     "/proc/pressure/cpu", "/proc/pressure/memory", "/proc/pressure/io"})
                if (auto value = readText(path))
                    log.reading("Linux proc", path, path, *value);
            if (const auto info = readText("/proc/cpuinfo")) {
                log.reading("Linux proc", "/proc/cpuinfo", "Kernel CPU description and reported frequencies",
                            *info, {}, "text", "Guest/virtual CPU information remains identified as such");
                std::istringstream input(*info);
                std::string line, cpu;
                while (std::getline(input, line)) {
                    const auto colon = line.find(':');
                    if (colon == std::string::npos)
                        continue;
                    auto key = line.substr(0, colon);
                    const auto last = key.find_last_not_of(" \t");
                    if (last != std::string::npos)
                        key.resize(last + 1);
                    auto value = line.substr(colon + 1);
                    const auto first = value.find_first_not_of(" \t");
                    if (first != std::string::npos)
                        value.erase(0, first);
                    if (key == "processor")
                        cpu = value;
                    else if (key == "cpu MHz") {
                        const auto mhz = number(value);
                        log.reading("Linux proc/cpuinfo", "cpu/" + cpu + "/reported_clock",
                                    "Kernel-reported CPU clock", value, mhz ? Json(*mhz) : Json{}, "MHz",
                                    "May be virtual/nominal in a VM; not substituted with host-driver data");
                    }
                }
            }
            for (const auto &policy : children("/sys/devices/system/cpu/cpufreq"))
                for (const auto &file : children(policy)) {
                    const auto name = file.filename().string();
                    if (name.find("freq") != std::string::npos)
                        attribute(log, "Linux cpufreq", file, policy.filename().string() + " / " + name, 1000,
                                  "MHz", "Kernel cpufreq values in kHz");
                    else if (name == "scaling_governor" || name == "scaling_driver" ||
                             name == "affected_cpus" || name == "related_cpus")
                        attribute(log, "Linux cpufreq", file, name, 1, "text");
                }
        });
        log.source("Linux power_supply", [&] {
            for (const auto &supply : children("/sys/class/power_supply")) {
                if (auto text = readText(supply / "uevent"))
                    log.reading("Linux power_supply", sensorPath(supply) + "/uevent",
                                "All reported power-supply properties", *text);
                for (const auto &path : children(supply)) {
                    const auto name = path.filename().string();
                    double divisor = 1;
                    std::string unit = "raw";
                    if (name.starts_with("voltage_")) {
                        divisor = 1e6;
                        unit = "V";
                    } else if (name.starts_with("current_")) {
                        divisor = 1e6;
                        unit = "A";
                    } else if (name.starts_with("power_")) {
                        divisor = 1e6;
                        unit = "W";
                    } else if (name.starts_with("energy_")) {
                        divisor = 1e6;
                        unit = "Wh";
                    } else if (name.starts_with("charge_")) {
                        divisor = 1e6;
                        unit = "Ah";
                    } else if (name == "capacity")
                        unit = "%";
                    else if (name == "temp" || name == "temp_ambient") {
                        divisor = 10;
                        unit = "C";
                    } else
                        continue;
                    attribute(log, "Linux power_supply", path, supply.filename().string() + " / " + name,
                              divisor, unit, "power_supply ABI; raw retained");
                }
            }
        });
        log.source("Linux RAPL / powercap", [&] {
            std::vector<fs::path> queue = children("/sys/class/powercap");
            std::set<std::string> visited;
            bool exposed{};
            for (std::size_t i = 0; i < queue.size() && i < 256; ++i) {
                const auto zone = queue[i];
                const auto id = sensorPath(zone);
                if (!visited.insert(id).second)
                    continue;
                const auto raw = readText(zone / "energy_uj");
                const auto readError = std::string(std::strerror(errno));
                if (fs::exists(zone / "energy_uj")) {
                    exposed = true;
                    if (!raw)
                        log.unavailable("Linux RAPL " + id, readError);
                }
                if (raw) {
                    const auto value = number(*raw);
                    const auto maximum = number(readText(zone / "max_energy_range_uj").value_or(""));
                    const auto name = readText(zone / "name").value_or(zone.filename().string());
                    log.reading("Linux RAPL", id + "/energy_uj", name + " energy", *raw,
                                value ? Json(*value / 1e6) : Json{}, "J", "Counter, not instantaneous power");
                    if (value) {
                        const auto at = Clock::now();
                        auto before = energyBefore.find(id);
                        if (before != energyBefore.end()) {
                            double delta = *value - before->second.value;
                            if (delta < 0 && maximum)
                                delta += *maximum;
                            const double seconds =
                                std::chrono::duration<double>(at - before->second.at).count();
                            log.reading("Linux RAPL", id + "/derived_power", name + " average power",
                                        {{"delta_uj", delta}, {"window_seconds", seconds}},
                                        delta >= 0 && seconds > 0 ? Json(delta / 1e6 / seconds) : Json{}, "W",
                                        "RAPL counter delta, with reported wrap range");
                        }
                        energyBefore[id] = {*value, maximum.value_or(0), at};
                    }
                }
                for (const auto &child : children(zone)) {
                    std::error_code error;
                    if (fs::is_directory(child, error) &&
                        child.filename().string().find(':') != std::string::npos)
                        queue.push_back(child);
                }
            }
            if (!exposed)
                log.unavailable("Linux RAPL", "Kernel exposes no powercap energy counters");
        });
        log.source("Linux DRM / accelerator", [&] {
            for (const auto &card : children("/sys/class/drm")) {
                const auto name = card.filename().string();
                if (name.find('-') != std::string::npos || !name.starts_with("card"))
                    continue;
                const auto device = card / "device";
                if (readText(device / "vendor").value_or("") == "0x8086") {
                    std::set<std::string> visited;
                    intelDrmAttributes(log, card, visited);
                    intelDrmAttributes(log, device, visited);
                }
                for (const char *field :
                     {"gpu_busy_percent", "mem_busy_percent", "pp_dpm_sclk", "pp_dpm_mclk", "pp_dpm_socclk",
                      "pp_dpm_fclk", "pp_dpm_dcefclk", "pp_dpm_vclk", "pp_dpm_dclk", "mem_info_vram_total",
                      "mem_info_vram_used", "mem_info_vis_vram_total", "mem_info_vis_vram_used",
                      "mem_info_gtt_total", "mem_info_gtt_used", "current_link_speed", "current_link_width",
                      "max_link_speed", "max_link_width"})
                    if (fs::exists(device / field))
                        attribute(log, "Linux DRM", device / field, name + " / " + field, 1,
                                  std::string(field).ends_with("percent") ? "%" : "raw",
                                  "Native driver attribute; no vendor substitution");
                if (auto blob = readBytes(device / "gpu_metrics", 16384))
                    log.reading(
                        "Linux amdgpu gpu_metrics", sensorPath(device / "gpu_metrics"),
                        "Full GPU metrics packet",
                        {{"bytes", blob->size()},
                         {"hex", bytesHex(blob->data(), blob->size())},
                         {"format_revision", blob->size() > 2 ? static_cast<unsigned char>((*blob)[2]) : 0},
                         {"content_revision", blob->size() > 3 ? static_cast<unsigned char>((*blob)[3]) : 0}},
                        {}, "raw", "Packet retained without guessing version-dependent struct offsets");
            }
            for (const auto &accel : children("/sys/class/accel"))
                for (const char *field :
                     {"npu_busy_percent", "npu_clock_freq_mhz", "aie_clock_freq_mhz", "status", "device_type",
                      "instance", "memory_usage", "fw_version", "vbnv"})
                    if (fs::exists(accel / "device" / field))
                        attribute(log, "Linux accelerator", accel / "device" / field, field);
        });
        log.source("NVIDIA NVML", [&] { nvidia->sample(log); });
        log.source("Linux SMART", [&] {
            if (smartctl.empty()) {
                log.unavailable(
                    "Linux SMART",
                    "smartctl not installed; available hwmon and block attributes are still collected");
                return;
            }
            for (const auto &block : children("/sys/block")) {
                const auto name = block.filename().string();
                if (!(name.starts_with("nvme") || name.starts_with("sd") || name.starts_with("hd")))
                    continue;
                const auto device = fs::path("/dev") / name;
                auto result =
                    runLinuxCommand(smartctl, {"-a", "-j", "-n", "standby,0", device.string()}, 2000);
                log.reading("Linux smartctl", device.string(), "Complete disk health report",
                            std::move(result), {}, "json_text",
                            "Read-only SMART; sleeping disks are not woken");
            }
        });
        log.source("Linux AMD XRT", [&] {
            if (!options.amd) {
                log.unavailable("Linux AMD XRT", "Disabled by command line");
                return;
            }
            if (xrt.empty()) {
                log.unavailable("Linux AMD XRT", "Native xrt-smi not installed");
                return;
            }
            auto result =
                runLinuxCommand(xrt, {"examine", "--report", xrtAll ? "all" : "platform", "--batch"}, 2500);
            const auto *code = result.find("exit_code");
            if (xrtAll && code && code->number().value_or(1) != 0) {
                log.reading("Linux AMD XRT", "all_report", "XRT all-report attempt", result, {}, "text",
                            "Unsupported report request retained", "unavailable");
                xrtAll = false;
                result = runLinuxCommand(xrt, {"examine", "--report", "platform", "--batch"}, 2500);
            }
            log.reading("Linux AMD XRT", xrt.string(), "Complete accelerator report", std::move(result), {},
                        "text", "Installed native XRT driver tool; no Windows interop");
        });
        log.source("Linux EC", [&] {
            if (!options.ec) {
                log.unavailable("Linux EC", "Disabled by command line");
                return;
            }
            if (ecFiles.empty()) {
                log.unavailable(
                    "Linux EC",
                    "No existing readable ec_sys debugfs interface; no modules loaded or host EC emulated");
                return;
            }
            for (const auto &path : ecFiles) {
                const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
                if (fd < 0) {
                    log.unavailable(path.string(), std::strerror(errno));
                    continue;
                }
                Json bytes = Json::array();
                const auto started = utcNow();
                int valid{};
                for (unsigned address = 0; address < 256; ++address) {
                    unsigned char value{};
                    const auto at = utcNow();
                    const auto count = pread(fd, &value, 1, address);
                    if (count == 1)
                        ++valid;
                    bytes.push({{"address", address},
                                {"address_hex", "0x" + hex(address, 2)},
                                {"utc", at},
                                {"raw", count == 1 ? Json(value) : Json{}},
                                {"error", count == 1 ? "" : std::strerror(errno)},
                                {"comment", "Unknown meaning; generic EC byte"}});
                }
                close(fd);
                log.event("ec_dump", {{"source", "Linux ec_sys"},
                                      {"path", path.string()},
                                      {"started_utc", started},
                                      {"finished_utc", utcNow()},
                                      {"atomic", false},
                                      {"range_start", 0},
                                      {"range_end", 255},
                                      {"read_bytes", valid},
                                      {"bytes", std::move(bytes)}});
            }
        });
    }
};
} // namespace
std::unique_ptr<Platform> createPlatform(const Options &options, const fs::path &) {
    return std::make_unique<Linux>(options);
}
} // namespace probe
