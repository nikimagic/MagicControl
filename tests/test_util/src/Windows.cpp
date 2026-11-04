#include "WmiWindows.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <intrin.h>
#include <atomic>
#include <powrprof.h>
#include <powersetting.h>
#include <future>
#include <thread>

namespace probe {
void collectBattery(Log &log);
void collectStorage(Log &log, const std::vector<unsigned> &disks);
std::vector<unsigned> nativeDisks();
Json runWindowsCommand(const fs::path &, const std::wstring &, DWORD timeout = 2500);
std::unique_ptr<Collector> createIntelGpu(bool enabled);
fs::path executablePath() {
    wchar_t path[32768]{};
    const auto length = GetModuleFileNameW(nullptr, path, 32768);
    if (!length || length >= 32768)
        throw std::runtime_error("Cannot locate executable");
    return fs::path(std::wstring(path, length));
}
std::string hostName() {
    wchar_t name[256]{};
    DWORD size = 256;
    return GetComputerNameW(name, &size) ? utf8(name, static_cast<int>(size)) : "unknown";
}
namespace {
// Receives only RAWINPUTHEADER, never key codes, characters or scan codes.
class KeyboardActivity {
    std::promise<void> started;
    std::thread worker;
    std::atomic<DWORD> threadId{},lastTick{},error{};
    std::atomic<std::uint64_t> events{};
    std::atomic<bool> registered{};
    static LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM wParam,LPARAM lParam) {
        auto* self=reinterpret_cast<KeyboardActivity*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if(message==WM_NCCREATE) {
            self=static_cast<KeyboardActivity*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
            SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));
        }
        if(self && message==WM_INPUT) {
            RAWINPUTHEADER header{}; UINT bytes=sizeof(header);
            const auto read=GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam),RID_HEADER,&header,&bytes,sizeof(header));
            if(read==sizeof(header) && header.dwType==RIM_TYPEKEYBOARD) {
                self->lastTick.store(static_cast<DWORD>(GetMessageTime()));
                self->events.fetch_add(1);
            }
        }
        return DefWindowProcW(window,message,wParam,lParam);
    }
    void run() {
        threadId.store(GetCurrentThreadId());
        WNDCLASSW type{}; type.lpfnWndProc=procedure; type.hInstance=GetModuleHandleW(nullptr);
        type.lpszClassName=L"SensorProbe.KeyboardActivity";
        if(!RegisterClassW(&type)) { error.store(GetLastError()); started.set_value(); return; }
        HWND window=CreateWindowExW(0,type.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,type.hInstance,this);
        if(!window) { error.store(GetLastError()); started.set_value(); UnregisterClassW(type.lpszClassName,type.hInstance); return; }
        RAWINPUTDEVICE input{1,6,RIDEV_INPUTSINK,window};
        registered.store(RegisterRawInputDevices(&input,1,sizeof(input))!=FALSE);
        if(!registered.load()) error.store(GetLastError());
        started.set_value();
        MSG message{};
        while(GetMessageW(&message,nullptr,0,0)>0) { TranslateMessage(&message); DispatchMessageW(&message); }
        if(registered.load()) {
            RAWINPUTDEVICE remove{1,6,RIDEV_REMOVE,nullptr};
            RegisterRawInputDevices(&remove,1,sizeof(remove));
        }
        DestroyWindow(window); UnregisterClassW(type.lpszClassName,type.hInstance);
    }
public:
    KeyboardActivity() { auto ready=started.get_future(); worker=std::thread([this]{run();}); ready.get(); }
    ~KeyboardActivity() { PostThreadMessageW(threadId.load(),WM_QUIT,0,0); if(worker.joinable()) worker.join(); }
    Json state(DWORD now) const {
        const auto count=events.load(); const auto tick=lastTick.load();
        return {{"registered",registered.load()},{"error",error.load()},{"header_events",count},
                {"last_tick32",count?Json(tick):Json{}},{"idle_ms",count?Json(now-tick):Json{}},
                {"scope","Raw Input keyboard headers; keyboard press/release only, no key data"}};
    }
};
std::string registry(const wchar_t *key, const wchar_t *name) {
    wchar_t value[4096]{};
    DWORD size = sizeof(value);
    return RegGetValueW(HKEY_LOCAL_MACHINE, key, name, RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, nullptr, value,
                        &size) == ERROR_SUCCESS
               ? utf8(value)
               : "";
}
struct AcpiTable {
    std::string path;
    std::vector<unsigned char> bytes;
};
void readAcpiRegistry(const std::wstring &path, std::vector<AcpiTable> &result, int depth = 0) {
    if (depth > 8 || result.size() > 512)
        return;
    HKEY key{};
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, path.c_str(), 0, KEY_READ | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS)
        return;
    for (DWORD index = 0;; ++index) {
        wchar_t name[512]{};
        DWORD length = 512, type{}, bytes{};
        const auto rc = RegEnumValueW(key, index, name, &length, nullptr, &type, nullptr, &bytes);
        if (rc == ERROR_NO_MORE_ITEMS)
            break;
        if ((rc != ERROR_SUCCESS && rc != ERROR_MORE_DATA) || type != REG_BINARY || bytes < 36 ||
            bytes > 4 * 1024 * 1024)
            continue;
        std::vector<unsigned char> table(bytes);
        if (RegQueryValueExW(key, name, nullptr, &type, table.data(), &bytes) == ERROR_SUCCESS) {
            table.resize(bytes);
            result.push_back({utf8((path + L"\\" + name).c_str()), std::move(table)});
        }
    }
    for (DWORD index = 0;; ++index) {
        wchar_t name[512]{};
        DWORD length = 512;
        const auto rc = RegEnumKeyExW(key, index, name, &length, nullptr, nullptr, nullptr, nullptr);
        if (rc == ERROR_NO_MORE_ITEMS)
            break;
        if (rc == ERROR_SUCCESS)
            readAcpiRegistry(path + L"\\" + name, result, depth + 1);
    }
    RegCloseKey(key);
}
std::vector<AcpiTable> acpiRegistry() {
    std::vector<AcpiTable> result;
    for (const auto *name : {L"DSDT", L"SSDT", L"FACP", L"ECDT"})
        readAcpiRegistry(std::wstring(L"HARDWARE\\ACPI\\") + name, result);
    return result;
}
bool declaredEcWindow(const std::vector<AcpiTable> &tables) {
    const std::array<unsigned char, 7> prefix{0x5B, 0x80, 'E', 'C', 'M', 'G', 0};
    for (const auto &table : tables) {
        if (table.bytes.size() < 36 || std::memcmp(table.bytes.data(), "DSDT", 4) != 0)
            continue;
        auto found = std::search(table.bytes.begin(), table.bytes.end(), prefix.begin(), prefix.end());
        if (found == table.bytes.end())
            continue;
        std::size_t offset = static_cast<std::size_t>(found - table.bytes.begin()) + prefix.size();
        const auto integer = [&]() -> std::optional<std::uint64_t> {
            if (offset >= table.bytes.size())
                return {};
            const auto code = table.bytes[offset++];
            if (code == 0 || code == 1)
                return code;
            const unsigned count = code == 0x0A   ? 1
                                   : code == 0x0B ? 2
                                   : code == 0x0C ? 4
                                   : code == 0x0E ? 8
                                                  : 0;
            if (!count || offset + count > table.bytes.size())
                return {};
            std::uint64_t value{};
            for (unsigned i = 0; i < count; ++i)
                value |= static_cast<std::uint64_t>(table.bytes[offset++]) << (i * 8);
            return value;
        };
        const auto base = integer(), length = integer();
        if (base && length && *base == 0xFED50000 && *length == 4096)
            return true;
    }
    return false;
}
class Windows final : public Platform {
    Options options;
    fs::path binaryDir;
    std::unique_ptr<Wmi> wmi;
    std::string wmiError;
    std::unique_ptr<Collector> amd, pawn, nvidia, intel;
    std::vector<std::wstring> counters;
    std::vector<unsigned> disks;
    WinHandle ec;
    bool ecVerified{};
    std::string ecError;
    Json ecIdentity;
    std::vector<AcpiTable> firmwareTables;
    fs::path xrt;
    bool xrtAll = true;
    bool com{};
    std::atomic<int> effectiveMode{-1};
    void* effectiveHandle{};
    HRESULT effectiveRegistration{E_NOTIMPL};
    std::unique_ptr<KeyboardActivity> keyboard;
    static void CALLBACK effectiveChanged(EFFECTIVE_POWER_MODE mode,void* context) {
        static_cast<Windows*>(context)->effectiveMode.store(static_cast<int>(mode));
    }
    Json ecInfo() const {
        return {{"transport","UWACPIDriver MMRB read IOCTL 0x9C40A490, 8-byte input"},
                {"physical_base","0xFED50000"},{"verified",ecVerified},{"identity",ecIdentity},
                {"range_start",0},{"range_end",4095},{"error",ecError},
                {"scope","Primary 4 KiB window on verified X5SP4NAG only; not firmware flash"}};
    }
    void observeInputAndPower(Log& log) {
        LASTINPUTINFO input{sizeof(input)};
        const auto now=GetTickCount64();
        const bool inputOk=GetLastInputInfo(&input)!=FALSE;
        SYSTEM_POWER_STATUS power{};
        const bool powerOk=GetSystemPowerStatus(&power)!=FALSE;
        Json policy{{"effective_mode",effectiveMode.load()},
                    {"effective_registration_hresult",static_cast<long>(effectiveRegistration)}};
        GUID* scheme{};
        const DWORD schemeError=PowerGetActiveScheme(nullptr,&scheme);
        policy["scheme_error"]=schemeError;
        if(schemeError==ERROR_SUCCESS && scheme) {
            wchar_t name[40]{}; StringFromGUID2(*scheme,name,40);
            policy["scheme_guid"]=utf8(name);
            struct Setting {const char* name;const GUID* id;};
            for(const auto& setting:std::array<Setting,3>{{
                {"energy_performance_preference",&GUID_PROCESSOR_PERF_ENERGY_PERFORMANCE_PREFERENCE},
                {"boost_mode",&GUID_PROCESSOR_PERF_BOOST_MODE},
                {"frequency_limit_mhz",&GUID_PROCESSOR_FREQUENCY_LIMIT}}}) {
                DWORD ac{},dc{};
                const auto ae=PowerReadACValueIndex(nullptr,scheme,&GUID_PROCESSOR_SETTINGS_SUBGROUP,setting.id,&ac);
                const auto de=PowerReadDCValueIndex(nullptr,scheme,&GUID_PROCESSOR_SETTINGS_SUBGROUP,setting.id,&dc);
                policy[setting.name]={{"ac",ae==ERROR_SUCCESS?Json(ac):Json{}},
                                      {"dc",de==ERROR_SUCCESS?Json(dc):Json{}},
                                      {"ac_error",ae},{"dc_error",de}};
            }
            LocalFree(scheme);
        }
        log.event("observation_context",{{"uptime_ms",now},{"input_available",inputOk},
            {"last_input_tick32",inputOk?Json(input.dwTime):Json{}},
            {"idle_ms",inputOk?Json(static_cast<DWORD>(now)-input.dwTime):Json{}},
            {"input_scope","Windows last-input timestamp includes keyboard and mouse; no key data recorded"},
            {"power_available",powerOk},{"ac_line_status",powerOk?Json(power.ACLineStatus):Json{}},
            {"battery_percent",powerOk?Json(power.BatteryLifePercent):Json{}},{"windows_policy",std::move(policy)},
            {"keyboard",keyboard?keyboard->state(static_cast<DWORD>(now)):Json{}}});
    }
    std::uint8_t ecByte(unsigned address, bool mapped = false) {
        if (address > 0xFFF)
            throw std::runtime_error("EC address exceeds the verified primary window");
        struct Packet {
            DWORD address, value;
        } packet{mapped ? 0xFED50000u + address : address, 0};
        std::array<unsigned char, 16> result{};
        DWORD returned{};
        if (!ioctl(ec.value, mapped ? 0x9C40A490 : 0x9C40A488, &packet, sizeof(packet), result.data(),
                   static_cast<DWORD>(result.size()), returned))
            throw std::runtime_error(winError());
        if (!returned || returned > result.size())
            throw std::runtime_error("Invalid EC reply length=" + std::to_string(returned));
        return result[0];
    }
    void queryLog(Log &log, const wchar_t *space, const std::wstring &name) {
        const std::string source = "Windows WMI / " + utf8(space) + " / " + utf8(name.c_str());
        if (!wmi) {
            log.unavailable(source, wmiError);
            return;
        }
        auto result = wmi->query(space, L"SELECT * FROM " + name);
        if (!result.error.empty())
            log.unavailable(source, result.error);
        if (result.partial)
            log.event("source_partial", {{"source", source}, {"comment", "WMI time/row limit reached"}});
        unsigned index{};
        for (const auto &row : result.rows) {
            auto id = jsonField(row, "__RELPATH");
            if (id.empty())
                id = std::to_string(index);
            log.reading(source, id, utf8(name.c_str()), row, {}, "raw",
                        "All provider properties/timestamps retained");
            if (name == L"MSAcpi_ThermalZoneTemperature") {
                for (const char *key : {"CurrentTemperature", "PassiveTripPoint", "CriticalTripPoint"}) {
                    if (const auto *raw = row.find(key)) {
                        const auto value = raw->number();
                        log.reading(source, id + "/" + key, key, *raw,
                                    value && *value > 0 ? Json(*value / 10.0 - 273.15) : Json{}, "C",
                                    "ACPI thermal zone value in tenths of kelvin; zone identity retained",
                                    value && *value > 0 ? "ok" : "unavailable");
                    }
                }
            } else if (name == L"Sensor") {
                const auto type = jsonField(row, "SensorType");
                const std::map<std::string, std::string> units{
                    {"Temperature", "C"}, {"Load", "%"},    {"Clock", "MHz"}, {"Fan", "RPM"},
                    {"Power", "W"},       {"Voltage", "V"}, {"Current", "A"}};
                if (const auto *raw = row.find("Value")) {
                    const auto value = raw->number();
                    const auto unit = units.find(type);
                    log.reading(source, id + "/Value", jsonField(row, "Name"), *raw,
                                value ? Json(*value) : Json{}, unit == units.end() ? "raw" : unit->second,
                                "Optional installed hardware-monitor WMI provider; raw SensorType retained",
                                value ? "ok" : "unavailable");
                }
            }
            ++index;
        }
    }

  public:
    Windows(const Options &opts, const fs::path &directory) : options(opts), binaryDir(directory) {
        const auto rc = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        com = SUCCEEDED(rc);
        CoInitializeSecurity(nullptr, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_DEFAULT,
                             RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE, nullptr);
        if(!options.ecOnly) try {
            wmi = std::make_unique<Wmi>();
        } catch (const std::exception &e) {
            wmiError = e.what();
        }
        amd = createAmd(options.amd);
        pawn = createPawn(options.pawn, binaryDir);
        nvidia = createNvidia(options.nvidia);
        intel = createIntelGpu(!options.ecOnly);
        if(!options.ecOnly) disks = nativeDisks();
        firmwareTables = acpiRegistry();
        if(options.ecOnly)
            effectiveRegistration=PowerRegisterForEffectivePowerModeNotifications(EFFECTIVE_POWER_MODE_V2,effectiveChanged,this,&effectiveHandle);
        if(options.ecOnly) keyboard=std::make_unique<KeyboardActivity>();
        wchar_t system[32768]{};
        if (GetSystemDirectoryW(system, 32768))
            xrt = fs::path(system) / L"AMD" / L"xrt-smi.exe";
        if (!options.ec)
            ecError = "Disabled by command line";
        else if (registry(L"HARDWARE\\DESCRIPTION\\System\\BIOS", L"BaseBoardProduct") !=
                 "WUJIE Series-X5SP4NAG")
            ecError = "No verified OEM EC window profile for this motherboard; no guessed addresses read";
        else {
            ec.value = CreateFileW(L"\\\\.\\ACPIDriver", GENERIC_READ | GENERIC_WRITE,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
            if (ec.value == INVALID_HANDLE_VALUE)
                ecError = winError();
            else
                try {
                    const auto a = ecByte(0x454), b = ecByte(0x456), c = ecByte(0x740);
                    ecIdentity = {{"0x454", a}, {"0x456", b}, {"0x740", c}};
                    ecVerified = a == 82 && b == 72 && c == 26;
                    if (!ecVerified)
                        ecError = "OEM EC identity differs from the verified profile";
                    else if (!declaredEcWindow(firmwareTables)) {
                        ecVerified = false;
                        ecError = "Expected primary EC window is not confirmed by current DSDT";
                    } else if (ecByte(0x454, true) != 82 || ecByte(0x456, true) != 72 ||
                               ecByte(0x740, true) != 26) {
                        ecVerified = false;
                        ecError = "Mapped EC identity differs from the ECRR identity";
                    }
                } catch (const std::exception &e) {
                    ecError = e.what();
                }
        }
    }
    ~Windows() {
        if(effectiveHandle) PowerUnregisterFromEffectivePowerModeNotifications(effectiveHandle);
        intel.reset();
        nvidia.reset();
        pawn.reset();
        amd.reset();
        wmi.reset();
        if (com)
            CoUninitialize();
    }
    Json inventory() override {
        Json result{
            {"platform", "Windows"},
            {"hostname", hostName()},
            {"process_architecture", "x64"},
            {"os_build", registry(L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"CurrentBuildNumber")},
            {"os_product", registry(L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"ProductName")}};
        if(options.ecOnly) {
            result["EC"]=ecInfo();
            result["capture_scope"]="EC RAM, Windows input idle timestamp and power policy. No setting writes or key logging.";
            return result;
        }
        WinHandle token;
        HANDLE rawToken{};
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &rawToken)) {
            token.value = rawToken;
            TOKEN_ELEVATION elevation{};
            DWORD bytes{};
            if (GetTokenInformation(token.value, TokenElevation, &elevation, sizeof(elevation), &bytes))
                result["elevated"] = elevation.TokenIsElevated != 0;
        }
        Json cpuid = Json::array();
        int id[4]{};
        __cpuid(id, 0);
        const auto maximum = static_cast<unsigned>(id[0]);
        __cpuid(id, static_cast<int>(0x80000000u));
        const auto extended = static_cast<unsigned>(id[0]);
        for (unsigned leaf : {0u, 1u, 7u, 0x80000000u, 0x80000001u, 0x80000007u, 0x80000008u}) {
            if ((leaf < 0x80000000 && leaf > maximum) || (leaf >= 0x80000000 && leaf > extended))
                continue;
            __cpuidex(id, static_cast<int>(leaf), 0);
            cpuid.push({{"leaf", hex(leaf, 8)},
                        {"eax", hex(static_cast<unsigned>(id[0]), 8)},
                        {"ebx", hex(static_cast<unsigned>(id[1]), 8)},
                        {"ecx", hex(static_cast<unsigned>(id[2]), 8)},
                        {"edx", hex(static_cast<unsigned>(id[3]), 8)}});
        }
        result["CPUID"] = std::move(cpuid);
        Json classes;
        for (const wchar_t *name :
             {L"Win32_ComputerSystem", L"Win32_ComputerSystemProduct", L"Win32_BaseBoard", L"Win32_BIOS",
              L"Win32_OperatingSystem", L"Win32_Processor", L"Win32_PhysicalMemory",
              L"Win32_PhysicalMemoryArray", L"Win32_VideoController", L"Win32_DiskDrive", L"Win32_Battery",
              L"Win32_Fan", L"Win32_TemperatureProbe", L"Win32_PnPSignedDriver", L"Win32_SystemDriver",
              L"Win32_PnPEntity", L"Win32_PortResource", L"Win32_DeviceMemoryAddress",
              L"Win32_IRQResource"}) {
            if (!wmi)
                break;
            auto query = wmi->query(L"ROOT\\CIMV2", L"SELECT * FROM " + std::wstring(name), 15000);
            if (std::wstring(name) == L"Win32_DiskDrive")
                for (const auto &row : query.rows)
                    if (const auto *value = row.find("Index"))
                        if (auto index = value->number())
                            if (std::find(disks.begin(), disks.end(), static_cast<unsigned>(*index)) ==
                                disks.end())
                                disks.push_back(static_cast<unsigned>(*index));
            classes[utf8(name)] = query.json();
        }
        result["WMI_inventory"] = std::move(classes);
        result["WMI_error"] = wmiError;
        if (wmi) {
            auto list =
                wmi->query(L"ROOT\\CIMV2",
                           L"SELECT * FROM meta_class WHERE __CLASS LIKE 'Win32_PerfFormattedData_%'", 5000);
            result["performance_counter_discovery"] = list.json();
            for (const auto &row : list.rows) {
                const auto name = jsonField(row, "__CLASS");
                if (name.find("PerfOS_Processor") != std::string::npos ||
                    name.find("Counters_ProcessorInformation") != std::string::npos ||
                    name.find("GPU") != std::string::npos || name.find("NPU") != std::string::npos ||
                    name.find("Thermal") != std::string::npos ||
                    name.find("PerfOS_Memory") != std::string::npos ||
                    name.find("PerfOS_NUMANodeMemory") != std::string::npos ||
                    name.find("PhysicalDisk") != std::string::npos ||
                    name.find("PowerMeter") != std::string::npos ||
                    name.find("EnergyMeter") != std::string::npos ||
                    name.find("Temperature") != std::string::npos || name.find("Fan") != std::string::npos)
                    counters.push_back(wide(name));
            }
        }
        if (counters.empty())
            counters = {L"Win32_PerfFormattedData_PerfOS_Processor",
                        L"Win32_PerfFormattedData_Counters_ProcessorInformation",
                        L"Win32_PerfFormattedData_GPUPerformanceCounters_GPUEngine",
                        L"Win32_PerfFormattedData_GPUPerformanceCounters_GPUAdapterMemory"};
        Json counterList = Json::array();
        for (const auto &name : counters)
            counterList.push(utf8(name.c_str()));
        result["performance_counter_classes"] = std::move(counterList);
        const DWORD smbiosSize = GetSystemFirmwareTable(0x52534D42, 0, nullptr, 0);
        if (smbiosSize && smbiosSize < 16 * 1024 * 1024) {
            std::vector<unsigned char> bytes(smbiosSize);
            if (GetSystemFirmwareTable(0x52534D42, 0, bytes.data(), smbiosSize) == smbiosSize)
                result["SMBIOS_raw"] = {{"source", "GetSystemFirmwareTable RSMB"},
                                        {"hex", bytesHex(bytes.data(), bytes.size())}};
        }
        const DWORD acpiSize = EnumSystemFirmwareTables(0x41435049, nullptr, 0);
        Json tables = Json::array();
        if (acpiSize && acpiSize < 65536) {
            std::vector<DWORD> ids((acpiSize + 3) / 4);
            if (EnumSystemFirmwareTables(0x41435049, ids.data(), acpiSize) == acpiSize)
                for (DWORD table : ids) {
                    char signature[5]{};
                    std::memcpy(signature, &table, 4);
                    const DWORD size = GetSystemFirmwareTable(0x41435049, table, nullptr, 0);
                    Json entry{{"signature", signature}, {"bytes", size}};
                    if ((std::string(signature) == "DSDT" || std::string(signature) == "ECDT" ||
                         std::string(signature) == "FACP") &&
                        size && size <= 4 * 1024 * 1024) {
                        std::vector<unsigned char> bytes(size);
                        if (GetSystemFirmwareTable(0x41435049, table, bytes.data(), size) == size)
                            entry["raw_hex"] = bytesHex(bytes.data(), bytes.size());
                    }
                    tables.push(std::move(entry));
                }
        }
        result["ACPI_tables"] = std::move(tables);
        Json registryTables = Json::array();
        for (const auto &table : firmwareTables)
            registryTables.push({{"source", "HKLM registry ACPI cache"},
                                 {"path", table.path},
                                 {"bytes", table.bytes.size()},
                                 {"raw_hex", bytesHex(table.bytes.data(), table.bytes.size())}});
        result["ACPI_registry_tables"] = std::move(registryTables);
        result["AMD"] = amd->info();
        result["PawnIO"] = pawn->info();
        result["NVIDIA"] = nvidia->info();
        result["Intel_GPU"] = intel->info();
        result["EC"] = {{"transport", "UWACPIDriver MMRB read IOCTL 0x9C40A490, 8-byte input"},
                        {"physical_base", "0xFED50000"},
                        {"verified", ecVerified},
                        {"identity", ecIdentity},
                        {"range_start", 0},
                        {"range_end", 4095},
                        {"error", ecError},
                        {"scope", "Primary 4 KiB window on verified X5SP4NAG only; not firmware flash"}};
        result["AMD_XRT"] = {{"path", xrt.string()}, {"installed", !xrt.empty() && fs::exists(xrt)}};
        return result;
    }
    void sample(Log &log) override {
        if(options.ecOnly) observeInputAndPower(log);
        else {
        amd->sample(log);
        pawn->sample(log);
        log.source("NVIDIA NVML", [&] { nvidia->sample(log); });
        log.source("Intel IGCL", [&] { intel->sample(log); });
        log.source("Windows system", [&] {
            MEMORYSTATUSEX memory{sizeof(memory)};
            if (GlobalMemoryStatusEx(&memory))
                log.reading("Windows GlobalMemoryStatusEx", "memory", "System memory",
                            {{"load_percent", memory.dwMemoryLoad},
                             {"physical_total", memory.ullTotalPhys},
                             {"physical_available", memory.ullAvailPhys},
                             {"pagefile_total", memory.ullTotalPageFile},
                             {"pagefile_available", memory.ullAvailPageFile}});
            SYSTEM_POWER_STATUS power{};
            if (GetSystemPowerStatus(&power))
                log.reading("Windows GetSystemPowerStatus", "power", "System power",
                            {{"ACLineStatus", power.ACLineStatus},
                             {"BatteryFlag", power.BatteryFlag},
                             {"BatteryLifePercent", power.BatteryLifePercent},
                             {"BatteryLifeTime", power.BatteryLifeTime},
                             {"BatteryFullLifeTime", power.BatteryFullLifeTime},
                             {"SystemStatusFlag", power.SystemStatusFlag}});
        });
        log.source("Windows WMI sensors", [&] {
            for (const auto &name : counters)
                queryLog(log, L"ROOT\\CIMV2", name);
            for (const wchar_t *name :
                 {L"MSAcpi_ThermalZoneTemperature", L"BatteryStatus", L"BatteryFullChargedCapacity",
                  L"BatteryCycleCount", L"BatteryStaticData", L"MSStorageDriver_ATAPISmartData",
                  L"MSStorageDriver_FailurePredictData", L"MSStorageDriver_FailurePredictStatus",
                  L"MSStorageDriver_FailurePredictThresholds"})
                queryLog(log, L"ROOT\\WMI", name);
            queryLog(log, L"ROOT\\LibreHardwareMonitor", L"Sensor");
            queryLog(log, L"ROOT\\OpenHardwareMonitor", L"Sensor");
        });
        log.source("Windows Battery IOCTL", [&] { collectBattery(log); });
        log.source("Windows storage", [&] { collectStorage(log, disks); });
        log.source("AMD XRT", [&] {
            if (!options.amd) {
                log.unavailable("AMD XRT", "Disabled by command line");
                return;
            }
            if (xrt.empty() || !fs::exists(xrt)) {
                log.unavailable("AMD XRT", "Installed xrt-smi.exe not found");
                return;
            }
            auto result = runWindowsCommand(xrt, xrtAll ? L"examine --report all --batch"
                                                        : L"examine --report platform --batch");
            const auto *exit = result.find("exit_code");
            if (xrtAll && exit && exit->number().value_or(1) != 0) {
                log.reading("AMD XRT", "all_report", "XRT all-report attempt", result, {}, "text",
                            "Unsupported report request retained", "unavailable");
                xrtAll = false;
                result = runWindowsCommand(xrt, L"examine --report platform --batch");
            }
            log.reading("AMD XRT", "report", "Complete XRT report", std::move(result), {}, "text",
                        "Vendor report retained without guessed units");
        });
        }
        log.source("Windows EC", [&] {
            if (!ecVerified) {
                log.unavailable("Windows EC", ecError);
                return;
            }
            std::optional<unsigned> typecPowerA, typecPowerB;
            for (unsigned address : {0x43Eu, 0x464u, 0x465u, 0x46Cu, 0x46Du, 0x49Fu, 0x74Cu, 0x75Bu, 0x75Cu,
                                     0x751u, 0x726u, 0x727u, 0x741u, 0x7C5u, 0x7C6u, 0x7CCu, 0x7CDu, 0x7CEu}) {
                try {
                    const auto raw = ecByte(address);
                    Json value;
                    std::string unit = "raw", comment;
                    if (address == 0x43E) {
                        value = static_cast<std::int8_t>(raw);
                        unit = "C";
                        comment = "EC thermal-control temperature";
                    }
                    if (address == 0x75B || address == 0x75C) {
                        value = raw / 2.0;
                        unit = "%";
                        comment = "Physical fan PWM feedback";
                    }
                    if (address == 0x49F) {
                        const auto field = (raw & 0x78u) >> 3;
                        value = {{"raw_byte", raw}, {"mask_0x78", raw & 0x78u}, {"adapter_class", field}};
                        comment = "ADPT field at bits 3..6; OEM GetAdapterWattFromEC maps generic classes, not a live watt meter";
                    }
                    if (address == 0x74C) {
                        value = {{"raw_byte", raw}, {"pdin_field", raw & 0x0Fu}};
                        comment = "PDIN field at bits 0..3; meaning is platform-specific and not independently decoded";
                    }
                    if (address == 0x7CC) {
                        value = {{"raw_byte", raw},
                                 {"dc0c", (raw & 0x01u) != 0},
                                 {"tc1c", (raw & 0x02u) != 0},
                                 {"tc2c", (raw & 0x04u) != 0},
                                 {"tc1p", (raw & 0x10u) != 0},
                                 {"tc2p", (raw & 0x20u) != 0},
                                 {"usb_c_power_priority", (raw & 0x80u) != 0 ? "performance" : "charging"}};
                        comment = "USB-C status/priority byte; bit 7 follows the Uniwill interpretation, applicability to this board remains to be verified";
                    }
                    if (address == 0x7CD) {
                        typecPowerA = raw;
                        comment = "OEM GetPowerFromTypeC reads this byte and compares it with 0x7CE; unit is not independently confirmed";
                    }
                    if (address == 0x7CE) {
                        typecPowerB = raw;
                        comment = "OEM GetPowerFromTypeC reads this byte and compares it with 0x7CD; unit is not independently confirmed";
                    }
                    log.reading("UWACPIDriver / ECRR", "ec/0x" + hex(address, 3), "Known EC register", raw,
                                value, unit, comment);
                } catch (const std::exception &e) {
                    log.reading("UWACPIDriver / ECRR", "ec/0x" + hex(address, 3), "Known EC register", {}, {},
                                "raw", "", "error", e.what());
                }
            }
            if (typecPowerA && typecPowerB) {
                const auto negotiated = std::max(*typecPowerA, *typecPowerB);
                log.reading("UWACPIDriver / ECRR", "typec/negotiated_power", "Type-C power candidate",
                            {{"0x7CD", *typecPowerA}, {"0x7CE", *typecPowerB}}, negotiated, "raw",
                            "OEM GetPowerFromTypeC returns max(0x7CD, 0x7CE); a watt interpretation still requires live validation");
            }
            for (unsigned base : {0x464u, 0x46Cu}) {
                try {
                    const auto high = ecByte(base), low = ecByte(base + 1), check = ecByte(base);
                    log.reading("UWACPIDriver / ECRR", "fan/0x" + hex(base, 3) + "/rpm",
                                "Physical fan tachometer",
                                {{"high", high}, {"low", low}, {"high_after", check}},
                                high == check ? Json(static_cast<unsigned>(high) * 256 + low) : Json{}, "RPM",
                                "High/low/high consistency check", high == check ? "ok" : "inconsistent");
                } catch (const std::exception &e) {
                    log.unavailable("UWACPIDriver fan tachometer", e.what());
                }
            }
            Json bytes = Json::array();
            const auto started = utcNow();
            int valid{};
            for (unsigned address = 0; address < 4096; ++address) {
                const auto at = utcNow();
                Json value;
                std::string error;
                try {
                    value = ecByte(address, true);
                    ++valid;
                } catch (const std::exception &e) {
                    error = e.what();
                }
                std::string comment;
                if (address == 0x43E)
                    comment = "EC thermal-control temperature; may differ from ADL die temperature";
                else if (address == 0x464 || address == 0x465)
                    comment = "CPU fan RPM high/low byte";
                else if (address == 0x46C || address == 0x46D)
                    comment = "Second fan RPM high/low byte";
                else if (address == 0x75B || address == 0x75C)
                    comment = "Fan PWM: raw/2 percent";
                else if (address >= 0xF00 && address < 0xF60)
                    comment = "Fan table/status area; thresholds are not live temperatures";
                bytes.push({{"address", address},
                            {"address_hex", "0x" + hex(address, 3)},
                            {"utc", at},
                            {"raw", value},
                            {"error", error},
                            {"comment", comment}});
            }
            log.event("ec_dump", {{"source", "UWACPIDriver / MMRB / IOCTL 0x9C40A490"},
                                  {"physical_base", "0xFED50000"},
                                  {"started_utc", started},
                                  {"finished_utc", utcNow()},
                                  {"atomic", false},
                                  {"range_start", 0},
                                  {"range_end", 4095},
                                  {"read_bytes", valid},
                                  {"failed_bytes", 4096 - valid},
                                  {"bytes", std::move(bytes)}});
        });
    }
};
} // namespace
std::unique_ptr<Platform> createPlatform(const Options &options, const fs::path &binaryDir) {
    return std::make_unique<Windows>(options, binaryDir);
}
} // namespace probe
