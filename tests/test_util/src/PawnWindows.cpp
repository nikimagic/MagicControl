#include "WinHelpers.h"
#include <array>
#include <cstring>
#include <intrin.h>
#include <limits>

namespace probe {
namespace {
class Module {
    using Open = HRESULT(WINAPI *)(void **);
    using Load = HRESULT(WINAPI *)(void *, const void *, std::size_t);
    using Execute = HRESULT(WINAPI *)(void *, const char *, const std::uint64_t *, std::size_t,
                                      std::uint64_t *, std::size_t, std::size_t *);
    HMODULE library{};
    void *handle{};
    Execute execute{};
    HRESULT(WINAPI *close)(void *) {};

  public:
    ~Module() {
        if (handle && close)
            close(handle);
        if (library)
            FreeLibrary(library);
    }
    void open(const fs::path &directory, const wchar_t *name) {
        library = LoadLibraryExW((directory / L"PawnIOLib.dll").c_str(), nullptr,
                                 LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!library)
            throw std::runtime_error("PawnIOLib.dll: " + winError());
        auto initialize = symbol<Open>(library, "pawnio_open");
        auto load = symbol<Load>(library, "pawnio_load");
        execute = symbol<Execute>(library, "pawnio_execute");
        close = symbol<HRESULT(WINAPI *)(void *)>(library, "pawnio_close");
        if (!initialize || !load || !execute || !close)
            throw std::runtime_error("Incomplete PawnIO library API");
        const HRESULT rc = initialize(&handle);
        if (FAILED(rc))
            throw std::runtime_error("pawnio_open HRESULT=0x" + hex(static_cast<std::uint32_t>(rc), 8));
        const auto blob = readBytes(directory / name);
        if (!blob || blob->empty())
            throw std::runtime_error("PawnIO signed module missing");
        const HRESULT loaded = load(handle, blob->data(), blob->size());
        if (FAILED(loaded))
            throw std::runtime_error("pawnio_load HRESULT=0x" + hex(static_cast<std::uint32_t>(loaded), 8));
    }
    std::array<std::uint64_t, 3> call(const char *operation, std::initializer_list<std::uint64_t> input,
                                      std::size_t count = 1) {
        std::array<std::uint64_t, 3> output{};
        std::size_t returned{};
        const HRESULT rc =
            execute(handle, operation, input.begin(), input.size(), output.data(), count, &returned);
        if (FAILED(rc) || returned < count)
            throw std::runtime_error(std::string(operation) + " HRESULT=0x" +
                                     hex(static_cast<std::uint32_t>(rc), 8) +
                                     " returned=" + std::to_string(returned));
        return output;
    }
    std::uint64_t msr(std::uint32_t address) {
        switch (address) {
        case 0x8B:
        case 0xCE:
        case 0x198:
        case 0x19C:
        case 0x1A2:
        case 0x1B1:
        case 0x606:
        case 0x611:
        case 0x619:
        case 0x639:
        case 0x641:
        case 0x64D:
        case 0xE7:
        case 0xE8:
        case 0xC0010061:
        case 0xC0010063:
        case 0xC0010299:
        case 0xC001029A:
        case 0xC001029B:
        case 0xC0010293:
        case 0xC00000E7:
        case 0xC00000E8:
        case 0xC00102B4:
            break;
        default:
            throw std::runtime_error("MSR outside collector whitelist");
        }
        return call("ioctl_read_msr", {address})[0];
    }
};
struct Core {
    WORD group{};
    KAFFINITY mask{};
    int index{};
};
std::vector<Core> cores() {
    DWORD bytes{};
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &bytes);
    if (!bytes || bytes > 4 * 1024 * 1024)
        throw std::runtime_error("CPU topology unavailable");
    std::vector<unsigned char> data(bytes);
    if (!GetLogicalProcessorInformationEx(
            RelationProcessorCore, reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(data.data()),
            &bytes))
        throw std::runtime_error(winError());
    std::vector<Core> result;
    for (std::size_t offset = 0; offset + sizeof(DWORD) * 2 <= bytes;) {
        const auto *entry =
            reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *>(data.data() + offset);
        if (!entry->Size || offset + entry->Size > bytes)
            break;
        if (entry->Relationship == RelationProcessorCore && entry->Processor.GroupCount) {
            const auto g = entry->Processor.GroupMask[0];
            if (g.Mask)
                result.push_back({g.Group, g.Mask & (~g.Mask + 1), static_cast<int>(result.size())});
        }
        offset += entry->Size;
    }
    return result;
}
struct Affinity {
    GROUP_AFFINITY previous{};
    bool changed{};
    explicit Affinity(const Core &core) {
        GROUP_AFFINITY target{};
        target.Group = core.group;
        target.Mask = core.mask;
        changed = SetThreadGroupAffinity(GetCurrentThread(), &target, &previous) != FALSE;
        if (!changed)
            throw std::runtime_error("Cannot select CPU: " + winError());
    }
    ~Affinity() {
        if (changed)
            SetThreadGroupAffinity(GetCurrentThread(), &previous, nullptr);
    }
};
struct Counter {
    std::uint32_t energy{};
    std::uint64_t aperf{};
    Clock::time_point at{};
    bool ready{};
};
class Pawn final : public Collector {
    std::unique_ptr<Module> cpu, bus;
    std::string cpuError, busError;
    std::vector<Core> topology;
    std::map<int, Counter> previous;
    Counter package;
    double energyUnit{};
    int family{}, model{};
    std::string vendor;
    fs::path directory;
    bool intelCpu{};
    std::map<unsigned, Counter> intelEnergy;
    static void raw(Log &log, const std::string &id, const char *name, std::uint64_t value,
                    const std::string &comment) {
        log.reading("PawnIO / AMD MSR", id, name, "0x" + hex(value, 16), {}, "raw", comment);
    }

  public:
    Pawn(bool enabled, const fs::path &binaryDir) : directory(binaryDir / "PawnIO") {
        if (!enabled) {
            cpuError = busError = "Disabled by command line";
            return;
        }
        int id[4]{};
        __cpuid(id, 0);
        char v[13]{};
        std::memcpy(v, &id[1], 4);
        std::memcpy(v + 4, &id[3], 4);
        std::memcpy(v + 8, &id[2], 4);
        vendor = v;
        __cpuid(id, 1);
        const unsigned eax = static_cast<unsigned>(id[0]);
        family = (eax >> 8) & 15;
        if (family == 15)
            family += (eax >> 20) & 255;
        model = ((eax >> 4) & 15) | ((eax >> 12) & 240);
        if (vendor == "AuthenticAMD" && family >= 0x17 && family <= 0x1A) {
            try {
                auto module = std::make_unique<Module>();
                module->open(directory, L"AMDFamily17.bin");
                energyUnit = std::pow(0.5, static_cast<int>((module->msr(0xC0010299) >> 8) & 31));
                topology = cores();
                cpu = std::move(module);
            } catch (const std::exception &e) {
                cpuError = e.what();
            }
        } else if (vendor == "GenuineIntel") {
            intelCpu = true;
            try {
                auto module = std::make_unique<Module>();
                module->open(directory, L"IntelMSR.bin");
                topology = cores();
                cpu = std::move(module);
            } catch (const std::exception &e) {
                cpuError = e.what();
            }
        } else
            cpuError = "No bundled CPU MSR module for this vendor/family";
        try {
            auto module = std::make_unique<Module>();
            module->open(directory, intelCpu ? L"SmbusI801.bin" : L"SmbusPIIX4.bin");
            const auto identity = module->call("ioctl_identity", {}, 3);
            if (intelCpu) {
                if ((identity[2] & 0xFFFF) != 0x8086)
                    throw std::runtime_error("SMBus identity is not Intel");
            } else {
                if ((identity[2] & 0xFFFFFFFF) != 0x790B1022)
                    throw std::runtime_error("SMBus identity is not AMD 1022:790B; no bus/address guessing");
                if (module->call("ioctl_piix4_port_sel", {std::numeric_limits<std::uint64_t>::max()})[0] != 0)
                    throw std::runtime_error(
                        "Current SMBus port is not verified port 0; selector left untouched");
            }
            bus = std::move(module);
        } catch (const std::exception &e) {
            busError = e.what();
        }
    }
    Json info() override {
        Json list = Json::array();
        for (const auto &core : topology)
            list.push({{"physical_core", core.index},
                       {"processor_group", core.group},
                       {"affinity_mask", "0x" + hex(core.mask)}});
        return {{"runtime_directory", directory.string()},
                {"cpu_vendor", vendor},
                {"cpu_family", family},
                {"cpu_model", model},
                {"cpu_available", cpu != nullptr},
                {"cpu_error", cpuError},
                {"smbus_available", bus != nullptr},
                {"smbus_error", busError},
                {"physical_cores", list},
                {"scope", "Only whitelisted MSR reads, SMN 0x59800, and SPD hub read transactions "
                          "0x50..0x57; no tuning or fan writes"}};
    }
    void sampleIntel(Log &log) {
        const std::string source = "PawnIO / Intel MSR";
        double unit{};
        bool rapl{};
        try {
            const auto value = cpu->msr(0x606);
            unit = std::pow(0.5, static_cast<int>((value >> 8) & 31));
            rapl = true;
            log.reading(source, "package/msr/606", "RAPL units", "0x" + hex(value, 16), {}, "raw",
                        "Energy unit = 0.5 ^ bits[12:8] joules");
        } catch (const std::exception &e) {
            log.unavailable(source + " RAPL", e.what());
        }
        for (unsigned address : {0x611u, 0x619u, 0x639u, 0x641u, 0x64Du})
            try {
                const auto value = static_cast<std::uint32_t>(cpu->msr(address));
                const auto at = Clock::now();
                const auto id = "energy_domain/msr/" + hex(address);
                log.reading(source, id, "Intel energy-domain counter", "0x" + hex(value, 8), {}, "raw",
                            "Package/DRAM/PP0/PP1/platform counter; some domains are unsupported");
                auto &before = intelEnergy[address];
                if (rapl && before.ready) {
                    const double seconds = std::chrono::duration<double>(at - before.at).count();
                    const bool standard = address != 0x619;
                    log.reading(source, id + "/derived_power", "Average domain power",
                                {{"energy_delta", static_cast<std::uint32_t>(value - before.energy)},
                                 {"window_seconds", seconds},
                                 {"joules_per_tick", unit}},
                                standard && seconds > 0
                                    ? Json(static_cast<std::uint32_t>(value - before.energy) * unit / seconds)
                                    : Json{},
                                "W",
                                standard ? "Counter delta over actual sample interval"
                                         : "DRAM energy unit has model-specific exceptions; raw counter "
                                           "retained without guessing",
                                standard ? "ok" : "not_decoded");
                }
                before = {value, 0, at, true};
            } catch (const std::exception &e) {
                log.reading(source, "package/msr/" + hex(address), "Intel energy-domain counter", {}, {},
                            "raw", "", "unavailable", e.what());
            }
        for (const auto &core : topology)
            try {
                Affinity affinity(core);
                const auto id = "core/" + std::to_string(core.index) + "/";
                int topologyId[4]{};
                __cpuid(topologyId, 0);
                const auto maximum = static_cast<unsigned>(topologyId[0]);
                const unsigned leaf = maximum >= 0x1F ? 0x1F : maximum >= 0xB ? 0xB : 1;
                Json levels = Json::array();
                for (unsigned level = 0; level < (leaf == 1 ? 1u : 6u); ++level) {
                    __cpuidex(topologyId, static_cast<int>(leaf), static_cast<int>(level));
                    if (leaf != 1 && !topologyId[1])
                        break;
                    levels.push({{"leaf", hex(leaf, 8)},
                                 {"subleaf", level},
                                 {"eax", hex(static_cast<unsigned>(topologyId[0]), 8)},
                                 {"ebx", hex(static_cast<unsigned>(topologyId[1]), 8)},
                                 {"ecx", hex(static_cast<unsigned>(topologyId[2]), 8)},
                                 {"edx", hex(static_cast<unsigned>(topologyId[3]), 8)}});
                }
                log.reading(
                    "CPU CPUID", id + "topology", "Selected CPU topology",
                    {{"group", core.group}, {"affinity_mask", hex(core.mask)}, {"levels", levels}}, {}, "raw",
                    "Selected logical thread; raw topology allows reconstruction of packages and cores");
                Json target;
                std::optional<double> tjmax;
                try {
                    const auto value = cpu->msr(0x1A2);
                    target = "0x" + hex(value, 16);
                    const auto temperature = (value >> 16) & 255;
                    if (temperature >= 50 && temperature <= 125)
                        tjmax = static_cast<double>(temperature);
                    log.reading(source, id + "msr/1A2", "Temperature target", target,
                                tjmax ? Json(*tjmax) : Json{}, "C", "TjMax from IA32_TEMPERATURE_TARGET");
                } catch (const std::exception &e) {
                    log.unavailable(source + " TjMax", e.what());
                }
                for (unsigned address : {0x19Cu, 0x1B1u})
                    try {
                        const auto value = cpu->msr(address);
                        const bool valid = (value & (1ull << 31)) != 0;
                        log.reading(source, id + "msr/" + hex(address),
                                    address == 0x19C ? "Core DTS temperature" : "Package DTS temperature",
                                    "0x" + hex(value, 16),
                                    valid && tjmax ? Json(*tjmax - ((value >> 16) & 127)) : Json{}, "C",
                                    "Valid bit 31; temperature = TjMax - digital readout",
                                    valid && tjmax ? "ok" : "not_decoded");
                    } catch (const std::exception &e) {
                        log.reading(source, id + "msr/" + hex(address), "DTS temperature", {}, {}, "C", "",
                                    "unavailable", e.what());
                    }
                try {
                    const auto aperf = cpu->msr(0xE8), mperf = cpu->msr(0xE7);
                    const auto at = Clock::now();
                    log.reading(source, id + "performance_counters", "APERF/MPERF",
                                {{"APERF", hex(aperf, 16)},
                                 {"MPERF", hex(mperf, 16)},
                                 {"group", core.group},
                                 {"affinity_mask", hex(core.mask)}});
                    auto &before = previous[core.index];
                    if (before.ready) {
                        const double seconds = std::chrono::duration<double>(at - before.at).count();
                        log.reading(
                            source, id + "effective_clock", "Effective clock",
                            {{"aperf_delta_hex", hex(aperf - before.aperf, 16)}, {"window_seconds", seconds}},
                            seconds > 0 ? Json((aperf - before.aperf) / seconds / 1e6) : Json{}, "MHz",
                            "Selected logical thread per physical core; includes idle interval");
                    }
                    before = {0, aperf, at, true};
                } catch (const std::exception &e) {
                    log.unavailable(source + " performance counters", e.what());
                }
                for (unsigned address : {0x198u, 0xCEu, 0x606u, 0x611u, 0x619u, 0x639u, 0x641u, 0x64Du})
                    try {
                        log.reading(
                            source, id + "msr/" + hex(address),
                            address == 0x606   ? "Energy units on selected CPU"
                            : address >= 0x611 ? "Package/domain energy on selected CPU"
                                               : "Intel frequency/platform status",
                            "0x" + hex(cpu->msr(address), 16), {}, "raw",
                            "Model-specific clock ratios are retained without assuming bus frequency");
                    } catch (const std::exception &e) {
                        log.reading(source, id + "msr/" + hex(address), "Intel status", {}, {}, "raw", "",
                                    "unavailable", e.what());
                    }
            } catch (const std::exception &e) {
                log.unavailable(source + " core " + std::to_string(core.index), e.what());
            }
    }
    void sample(Log &log) override {
        log.source("PawnIO CPU", [&] {
            if (!cpu) {
                log.unavailable("PawnIO CPU", cpuError);
                return;
            }
            if (topology.empty())
                throw std::runtime_error("No accessible physical CPU cores");
            Affinity packageAffinity(topology.front());
            if (intelCpu) {
                sampleIntel(log);
                return;
            }
            const auto units = cpu->msr(0xC0010299);
            raw(log, "package/msr/C0010299", "Energy units", units, "Energy unit = 0.5 ^ bits[12:8] joules");
            const auto energy = static_cast<std::uint32_t>(cpu->msr(0xC001029B));
            const auto at = Clock::now();
            raw(log, "package/msr/C001029B", "Package energy counter", energy,
                "32-bit wrapping counter; not instantaneous watts");
            if (package.ready) {
                const double seconds = std::chrono::duration<double>(at - package.at).count();
                log.reading("PawnIO / AMD MSR", "package/power", "CPU package power",
                            {{"energy_delta", static_cast<std::uint32_t>(energy - package.energy)},
                             {"window_seconds", seconds},
                             {"joules_per_tick", energyUnit}},
                            seconds > 0 ? Json(static_cast<std::uint32_t>(energy - package.energy) *
                                               energyUnit / seconds)
                                        : Json{},
                            "W", "Derived from two package energy readings");
            } else
                log.reading("PawnIO / AMD MSR", "package/power", "CPU package power", {}, {}, "W",
                            "First sample establishes the counter baseline", "warmup");
            package = {energy, 0, at, true};
            {
                WinHandle mutex(CreateMutexW(nullptr, FALSE, L"Global\\Access_PCI"));
                if (mutex.value == nullptr)
                    throw std::runtime_error(winError());
                const auto acquired = WaitForSingleObject(mutex.value, 250);
                if (acquired == WAIT_OBJECT_0 || acquired == WAIT_ABANDONED) {
                    try {
                        const auto value =
                            static_cast<std::uint32_t>(cpu->call("ioctl_read_smn", {0x59800})[0]);
                        double celsius = (value >> 21) * 0.125;
                        if (value & 0x80000)
                            celsius -= 49;
                        const bool valid = value && value != 0xFFFFFFFF && celsius > -20 && celsius < 150;
                        log.reading("PawnIO / AMD SMN", "package/smn/59800", "CPU Tctl/Tdie",
                                    "0x" + hex(value, 8), valid ? Json(celsius) : Json{}, "C",
                                    "SMN temperature register; model-specific Tctl offsets are not guessed",
                                    valid ? "ok" : "invalid");
                    } catch (const std::exception &e) {
                        log.unavailable("PawnIO / AMD SMN", e.what());
                    }
                    ReleaseMutex(mutex.value);
                } else
                    log.unavailable("PawnIO / AMD SMN", "Global PCI sensor mutex busy");
            }
            for (const auto &core : topology) {
                try {
                    Affinity affinity(core);
                    const auto id = "core/" + std::to_string(core.index) + "/";
                    int cpuid[4]{};
                    __cpuid(cpuid, 0x8000001E);
                    log.reading("CPU CPUID", id + "identity", "Physical core identity",
                                {{"leaf", "8000001E"},
                                 {"eax", hex(static_cast<unsigned>(cpuid[0]), 8)},
                                 {"ebx", hex(static_cast<unsigned>(cpuid[1]), 8)},
                                 {"ecx", hex(static_cast<unsigned>(cpuid[2]), 8)},
                                 {"edx", hex(static_cast<unsigned>(cpuid[3]), 8)},
                                 {"group", core.group},
                                 {"mask", hex(core.mask)}});
                    const auto pstate = cpu->msr(0xC0010293);
                    raw(log, id + "msr/C0010293", "Hardware P-state", pstate,
                        "Raw register retained; 5 MHz FID decoder only used for verified family 1Ah/model "
                        "24h");
                    if (family == 0x1A && model == 0x24)
                        log.reading("PawnIO / AMD MSR", id + "hardware_clock", "Hardware clock",
                                    "0x" + hex(pstate, 16), static_cast<double>(pstate & 0xFFF) * 5, "MHz",
                                    "Verified Strix Point decoder; not an effective clock");
                    const auto coreEnergy = static_cast<std::uint32_t>(cpu->msr(0xC001029A));
                    const auto aperf = cpu->msr(0xC00000E8), mperf = cpu->msr(0xC00000E7);
                    const auto coreAt = Clock::now();
                    raw(log, id + "msr/C001029A", "Core energy counter", coreEnergy, "32-bit energy counter");
                    raw(log, id + "msr/C00000E8", "APERF", aperf, "Actual performance counter");
                    raw(log, id + "msr/C00000E7", "MPERF", mperf, "Maximum performance counter");
                    auto &before = previous[core.index];
                    if (before.ready) {
                        const double seconds = std::chrono::duration<double>(coreAt - before.at).count();
                        log.reading(
                            "PawnIO / AMD MSR", id + "power", "Core power",
                            {{"window_seconds", seconds},
                             {"energy_delta", static_cast<std::uint32_t>(coreEnergy - before.energy)}},
                            seconds > 0 ? Json(static_cast<std::uint32_t>(coreEnergy - before.energy) *
                                               energyUnit / seconds)
                                        : Json{},
                            "W", "Delta-derived core energy");
                        log.reading(
                            "PawnIO / AMD MSR", id + "effective_clock", "Effective clock",
                            {{"window_seconds", seconds}, {"aperf_delta_hex", hex(aperf - before.aperf, 16)}},
                            seconds > 0 ? Json((aperf - before.aperf) / seconds / 1e6) : Json{}, "MHz",
                            "Includes idle time over the actual measurement window");
                    }
                    before = {coreEnergy, aperf, coreAt, true};
                    for (const auto address :
                         {0xC0010061u, 0xC0010063u, 0xC00102B4u, 0xC0010299u, 0xC001029Bu})
                        try {
                            raw(log, id + "msr/" + hex(address),
                                address == 0xC001029B   ? "Package energy on selected CPU"
                                : address == 0xC0010299 ? "Energy units on selected CPU"
                                                        : "CPU status register",
                                cpu->msr(address), "Raw read-only status; no undocumented decoding");
                        } catch (const std::exception &e) {
                            log.reading("PawnIO / AMD MSR", id + "msr/" + hex(address), "CPU status register",
                                        {}, {}, "raw", "", "unavailable", e.what());
                        }
                } catch (const std::exception &e) {
                    log.unavailable("PawnIO core " + std::to_string(core.index), e.what());
                }
            }
        });
        log.source("PawnIO SPD hubs", [&] {
            if (!bus) {
                log.unavailable("PawnIO SPD hubs", busError);
                return;
            }
            WinHandle mutex(CreateMutexW(nullptr, FALSE, L"Global\\Access_SMBUS.HTP.Method"));
            if (!mutex.value)
                throw std::runtime_error(winError());
            const auto acquired = WaitForSingleObject(mutex.value, 250);
            if (acquired != WAIT_OBJECT_0 && acquired != WAIT_ABANDONED) {
                log.unavailable("PawnIO SPD hubs", "SMBus mutex busy");
                return;
            }
            try {
                for (unsigned address = 0x50; address <= 0x57; ++address) {
                    const auto id = "smbus/port0/0x" + hex(address, 2);
                    try {
                        const auto read = [&](unsigned reg, bool word) {
                            return bus->call("ioctl_smbus_xfer", {address, 1, reg, word ? 3u : 2u})[0];
                        };
                        const auto type = read(0, true);
                        log.reading("PawnIO SMBus", id + "/type", "SPD hub type", type, {}, "raw",
                                    "DDR5 SPD5118 signature is 0x1851");
                        if (type != 0x1851) {
                            const auto memoryType = read(2, false), revision = read(1, false);
                            log.reading("PawnIO SMBus", id + "/memory_type", "SPD memory type/revision",
                                        {{"memory_type", memoryType}, {"revision", revision}}, {}, "raw",
                                        "DDR4 memory type is 0x0C; current SPD page is not changed");
                            if (memoryType == 0x0C && (revision >> 4) == 1) {
                                const auto thermal = read(14, false);
                                log.reading("PawnIO SMBus", id + "/thermal_capability",
                                            "DDR4 SPD thermal capability", thermal, {}, "raw",
                                            "Byte 14 bit 7 advertises a module temperature sensor");
                                if (thermal & 0x80) {
                                    const unsigned sensorAddress = 0x18 + address - 0x50;
                                    const auto readTsod = [&](unsigned reg) {
                                        return bus->call("ioctl_smbus_xfer", {sensorAddress, 1, reg, 3})[0];
                                    };
                                    const auto manufacturer = readTsod(6), device = readTsod(7),
                                               temperature = readTsod(5);
                                    const unsigned swapped =
                                        ((temperature & 255) << 8) | ((temperature >> 8) & 255);
                                    int signedTemperature = static_cast<int>(swapped & 0x1FFF);
                                    if (signedTemperature & 0x1000)
                                        signedTemperature -= 0x2000;
                                    const bool identified =
                                        manufacturer && manufacturer != 0xFFFF && device && device != 0xFFFF;
                                    log.reading("PawnIO SMBus JC42",
                                                "smbus/0x" + hex(sensorAddress, 2) + "/temperature",
                                                "DDR4 TSOD temperature",
                                                {{"associated_spd_address", address},
                                                 {"manufacturer_word", manufacturer},
                                                 {"device_word", device},
                                                 {"temperature_word", temperature}},
                                                identified ? Json(signedTemperature / 16.0) : Json{}, "C",
                                                "SPD-advertised TSOD; standard JC42 byte order and signed-13 "
                                                "decoder; raw IDs retained",
                                                identified ? "candidate" : "not_decoded");
                                }
                            }
                            continue;
                        }
                        const auto capability = read(5, false), config = read(0x1A, false);
                        log.reading("PawnIO SMBus", id + "/configuration",
                                    "SPD sensor capability/configuration",
                                    {{"capability", capability}, {"config", config}});
                        if (!(capability & 2) || (capability & 0xFC) || (config & 1)) {
                            log.reading("PawnIO SMBus", id + "/temperature", "DDR5 SPD hub temperature", {},
                                        {}, "C", "Temperature capability/configuration is not supported",
                                        "unsupported");
                            continue;
                        }
                        const auto value = read(0x31, true);
                        int rawTemperature = static_cast<int>((value >> 2) & 0x7FF);
                        if (rawTemperature & 0x400)
                            rawTemperature -= 0x800;
                        log.reading("PawnIO SMBus", id + "/temperature", "DDR5 SPD hub temperature",
                                    {{"register", "0x31"}, {"raw_word", value}}, rawTemperature / 4.0, "C",
                                    "Signed 11-bit temperature after shift; 0.25 C per unit");
                    } catch (const std::exception &e) {
                        log.reading("PawnIO SMBus", id, "SPD address", {}, {}, "",
                                    "Standard SPD address; absence/error retained", "unavailable", e.what());
                    }
                }
            } catch (...) {
                ReleaseMutex(mutex.value);
                throw;
            }
            ReleaseMutex(mutex.value);
        });
    }
};
} // namespace
std::unique_ptr<Collector> createPawn(bool enabled, const fs::path &binaryDir) {
    return std::make_unique<Pawn>(enabled, binaryDir);
}
} // namespace probe
