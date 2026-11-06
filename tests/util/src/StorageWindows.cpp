#include "WinHelpers.h"
#include <algorithm>
#include <cstring>
#include <nvme.h>
#include <poclass.h>
#include <setupapi.h>
#include <winioctl.h>

namespace probe {
std::vector<unsigned> nativeDisks() {
    const GUID guid{0x53f56307, 0xb6bf, 0x11d0, {0x94, 0xf2, 0x00, 0xa0, 0xc9, 0x1e, 0xfb, 0x8b}};
    std::vector<unsigned> result;
    const auto devices = SetupDiGetClassDevsW(&guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devices == INVALID_HANDLE_VALUE)
        return result;
    for (DWORD index = 0;; ++index) {
        SP_DEVICE_INTERFACE_DATA entry{sizeof(entry)};
        if (!SetupDiEnumDeviceInterfaces(devices, nullptr, &guid, index, &entry))
            break;
        DWORD size{};
        SetupDiGetDeviceInterfaceDetailW(devices, &entry, nullptr, 0, &size, nullptr);
        if (size < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) || size > 65536)
            continue;
        std::vector<unsigned char> bytes(size);
        auto *detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W *>(bytes.data());
        detail->cbSize = sizeof(*detail);
        if (!SetupDiGetDeviceInterfaceDetailW(devices, &entry, detail, size, nullptr, nullptr))
            continue;
        WinHandle device(CreateFileW(detail->DevicePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                     nullptr, OPEN_EXISTING, 0, nullptr));
        if (device.value == INVALID_HANDLE_VALUE)
            continue;
        STORAGE_DEVICE_NUMBER number{};
        DWORD returned{};
        if (ioctl(device.value, IOCTL_STORAGE_GET_DEVICE_NUMBER, nullptr, 0, &number, sizeof(number),
                  returned) &&
            returned >= sizeof(number) && number.DeviceType == FILE_DEVICE_DISK &&
            number.DeviceNumber != 0xFFFFFFFF)
            if (std::find(result.begin(), result.end(), number.DeviceNumber) == result.end())
                result.push_back(number.DeviceNumber);
    }
    SetupDiDestroyDeviceInfoList(devices);
    return result;
}
void collectBattery(Log &log) {
    const GUID guid{0x72631e54, 0x78a4, 0x11d0, {0xbc, 0xf7, 0x00, 0xaa, 0x00, 0xb7, 0xb3, 0x2a}};
    const auto devices = SetupDiGetClassDevsW(&guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devices == INVALID_HANDLE_VALUE) {
        log.unavailable("Windows Battery IOCTL", winError());
        return;
    }
    unsigned found{};
    for (DWORD i = 0;; ++i) {
        SP_DEVICE_INTERFACE_DATA entry{sizeof(entry)};
        if (!SetupDiEnumDeviceInterfaces(devices, nullptr, &guid, i, &entry))
            break;
        DWORD required{};
        SetupDiGetDeviceInterfaceDetailW(devices, &entry, nullptr, 0, &required, nullptr);
        if (required < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) || required > 65536)
            continue;
        std::vector<unsigned char> buffer(required);
        auto *detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W *>(buffer.data());
        detail->cbSize = sizeof(*detail);
        if (!SetupDiGetDeviceInterfaceDetailW(devices, &entry, detail, required, nullptr, nullptr))
            continue;
        ++found;
        const std::string id = utf8(detail->DevicePath);
        WinHandle handle(CreateFileW(detail->DevicePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                     nullptr, OPEN_EXISTING, 0, nullptr));
        if (handle.value == INVALID_HANDLE_VALUE) {
            log.unavailable("Windows Battery IOCTL " + id, winError());
            continue;
        }
        DWORD wait{}, tag{}, returned{};
        if (!ioctl(handle.value, IOCTL_BATTERY_QUERY_TAG, &wait, sizeof(wait), &tag, sizeof(tag), returned) ||
            !tag) {
            log.unavailable("Windows Battery IOCTL " + id, winError());
            continue;
        }
        BATTERY_QUERY_INFORMATION query{};
        query.BatteryTag = tag;
        query.InformationLevel = BatteryInformation;
        BATTERY_INFORMATION info{};
        const bool hasInfo = ioctl(handle.value, IOCTL_BATTERY_QUERY_INFORMATION, &query, sizeof(query),
                                   &info, sizeof(info), returned);
        if (hasInfo)
            log.reading("Windows Battery IOCTL", id + "/information", "Battery information",
                        {{"Capabilities", info.Capabilities},
                         {"Technology", info.Technology},
                         {"Chemistry", std::string(reinterpret_cast<char *>(info.Chemistry), 4)},
                         {"DesignedCapacity", info.DesignedCapacity},
                         {"FullChargedCapacity", info.FullChargedCapacity},
                         {"CycleCount", info.CycleCount}});
        BATTERY_WAIT_STATUS request{};
        request.BatteryTag = tag;
        BATTERY_STATUS status{};
        if (ioctl(handle.value, IOCTL_BATTERY_QUERY_STATUS, &request, sizeof(request), &status,
                  sizeof(status), returned)) {
            log.reading("Windows Battery IOCTL", id + "/status", "Battery status",
                        {{"PowerState", status.PowerState},
                         {"Capacity", status.Capacity},
                         {"Voltage", status.Voltage},
                         {"Rate", status.Rate}});
            log.reading("Windows Battery IOCTL", id + "/voltage", "Battery voltage", status.Voltage,
                        status.Voltage == BATTERY_UNKNOWN_VOLTAGE ? Json{} : Json(status.Voltage / 1000.0),
                        "V");
            const bool relative = hasInfo && (info.Capabilities & BATTERY_CAPACITY_RELATIVE);
            log.reading("Windows Battery IOCTL", id + "/power", "Battery charging/discharging power",
                        status.Rate,
                        status.Rate == BATTERY_UNKNOWN_RATE || relative ? Json{} : Json(status.Rate / 1000.0),
                        relative ? "relative" : "W",
                        "Positive charging; negative discharging; relative-capacity batteries are not "
                        "assumed to report watts");
        }
        query.InformationLevel = BatteryTemperature;
        DWORD temp{};
        const bool got = ioctl(handle.value, IOCTL_BATTERY_QUERY_INFORMATION, &query, sizeof(query), &temp,
                               sizeof(temp), returned);
        log.reading(
            "Windows Battery IOCTL", id + "/temperature", "Battery temperature", got ? Json(temp) : Json{},
            got && temp && temp != 0xFFFFFFFF ? Json(temp / 10.0 - 273.15) : Json{}, "C",
            "BatteryTemperature uses tenths of kelvin", got ? "ok" : "unavailable", got ? "" : winError());
        for (auto level : {BatteryDeviceName, BatteryManufactureName, BatteryUniqueID, BatterySerialNumber}) {
            query.InformationLevel = level;
            wchar_t text[512]{};
            if (ioctl(handle.value, IOCTL_BATTERY_QUERY_INFORMATION, &query, sizeof(query), text,
                      sizeof(text) - sizeof(wchar_t), returned))
                log.reading("Windows Battery IOCTL", id + "/identity/" + std::to_string(level),
                            "Battery identity", utf8(text));
        }
    }
    SetupDiDestroyDeviceInfoList(devices);
    if (!found)
        log.unavailable("Windows Battery IOCTL", "No battery interfaces; normal on desktops");
}
void collectStorage(Log &log, const std::vector<unsigned> &disks) {
    if (disks.empty()) {
        log.unavailable("Windows storage", "No disks enumerated by available native interfaces or metadata");
        return;
    }
    for (unsigned index : disks) {
        const auto path = L"\\\\.\\PhysicalDrive" + std::to_wstring(index);
        WinHandle disk(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                   OPEN_EXISTING, 0, nullptr));
        if (disk.value == INVALID_HANDLE_VALUE) {
            log.unavailable("PhysicalDrive" + std::to_string(index), winError());
            continue;
        }
        const std::string id = "PhysicalDrive" + std::to_string(index);
        bool thermal{};
        for (DWORD property : {52u, 51u}) {
            DWORD query[]{property, 0, 0}, returned{};
            std::vector<unsigned char> data(65536);
            if (!ioctl(disk.value, IOCTL_STORAGE_QUERY_PROPERTY, query, sizeof(query), data.data(),
                       static_cast<DWORD>(data.size()), returned) ||
                returned < 24 || returned > data.size())
                continue;
            std::uint16_t count{};
            std::memcpy(&count, data.data() + 12, 2);
            if (24ull + 16ull * count > returned)
                continue;
            for (unsigned i = 0; i < count; ++i) {
                thermal = true;
                const auto *entry = data.data() + 24 + i * 16;
                std::uint16_t channel{};
                std::int16_t temp{}, over{}, under{};
                std::memcpy(&channel, entry, 2);
                std::memcpy(&temp, entry + 2, 2);
                std::memcpy(&over, entry + 4, 2);
                std::memcpy(&under, entry + 6, 2);
                log.reading("Windows Storage Temperature IOCTL",
                            id + "/property/" + std::to_string(property) + "/sensor/" +
                                std::to_string(channel),
                            "Storage temperature channel",
                            {{"entry_hex", bytesHex(entry, 16)},
                             {"over_threshold", over},
                             {"under_threshold", under}},
                            temp == static_cast<std::int16_t>(0x8000) ? Json{} : Json(temp), "C",
                            "Index 0 is composite; additional channels kept independently");
            }
        }
        if (!thermal)
            log.unavailable("Windows Storage Temperature IOCTL " + id,
                            "Device/adapter temperature property unsupported or unavailable");
        const std::size_t offset = offsetof(STORAGE_PROPERTY_QUERY, AdditionalParameters);
        std::vector<unsigned char> buffer(offset + sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA) + 512);
        auto *request = reinterpret_cast<STORAGE_PROPERTY_QUERY *>(buffer.data());
        request->PropertyId = StorageDeviceProtocolSpecificProperty;
        request->QueryType = PropertyStandardQuery;
        auto *protocol = reinterpret_cast<STORAGE_PROTOCOL_SPECIFIC_DATA *>(buffer.data() + offset);
        protocol->ProtocolType = ProtocolTypeNvme;
        protocol->DataType = NVMeDataTypeLogPage;
        protocol->ProtocolDataRequestValue = 2;
        protocol->ProtocolDataOffset = sizeof(*protocol);
        protocol->ProtocolDataLength = 512;
        DWORD returned{};
        if (!ioctl(disk.value, IOCTL_STORAGE_QUERY_PROPERTY, buffer.data(), static_cast<DWORD>(buffer.size()),
                   buffer.data(), static_cast<DWORD>(buffer.size()), returned) ||
            returned < sizeof(STORAGE_PROTOCOL_DATA_DESCRIPTOR) || returned > buffer.size()) {
            log.unavailable("Windows NVMe SMART " + id, "NVMe health page unavailable: " + winError());
            continue;
        }
        const auto *descriptor = reinterpret_cast<const STORAGE_PROTOCOL_DATA_DESCRIPTOR *>(buffer.data());
        const auto &p = descriptor->ProtocolSpecificData;
        const std::size_t begin =
            offsetof(STORAGE_PROTOCOL_DATA_DESCRIPTOR, ProtocolSpecificData) + p.ProtocolDataOffset;
        if (p.ProtocolDataLength < 512 || begin + 512 > returned) {
            log.unavailable("Windows NVMe SMART " + id, "Invalid health page bounds");
            continue;
        }
        const auto *health = buffer.data() + begin;
        log.reading("Windows NVMe SMART", id + "/health", "Full NVMe health log", bytesHex(health, 512), {},
                    "raw", "NVMe log page 0x02, all 512 bytes retained");
        for (unsigned channel = 0; channel < 9; ++channel) {
            std::uint16_t kelvin{};
            std::memcpy(&kelvin, health + (channel ? 200 + (channel - 1) * 2 : 1), 2);
            log.reading("Windows NVMe SMART", id + "/temperature/" + std::to_string(channel),
                        channel ? "NVMe temperature sensor" : "NVMe composite temperature", kelvin,
                        kelvin && kelvin != 0xFFFF ? Json(kelvin - 273.15) : Json{}, "C",
                        "Kelvin; zero/FFFF means unavailable",
                        kelvin && kelvin != 0xFFFF ? "ok" : "unavailable");
        }
        log.reading("Windows NVMe SMART", id + "/health_flags", "Health/spare/wear",
                    {{"critical_warning", health[0]},
                     {"available_spare_percent", health[3]},
                     {"spare_threshold_percent", health[4]},
                     {"percentage_used", health[5]}});
        for (unsigned field = 0; field < 10; ++field) {
            const char *names[]{"DataUnitsRead",     "DataUnitsWritten",   "HostReadCommands",
                                "HostWriteCommands", "ControllerBusyTime", "PowerCycles",
                                "PowerOnHours",      "UnsafeShutdowns",    "MediaErrors",
                                "ErrorLogEntries"};
            log.reading("Windows NVMe SMART", id + "/counter/" + names[field], names[field],
                        bytesHex(health + 32 + field * 16, 16), {}, "uint128_le_hex",
                        "128-bit raw counter; data-unit counters use 512000 bytes per unit");
        }
    }
}
} // namespace probe
