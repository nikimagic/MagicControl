#include "MagicControl/HardwareInventory.h"
#include "MagicControl/OperationMessages.h"
#include "MagicControl/Logging.h"
#include "MagicControl/EcIdentityInventory.h"
#include "InventoryDetailsWindows.h"

#include <windows.h>
#include <devguid.h>
#include <setupapi.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <ctime>
#include <format>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "MagicControl/BoardIdentity.h"
#include "MagicControl/DeviceProfile.h"
#include "MagicControl/EcRegister.h"

#pragma comment(lib, "setupapi.lib")

namespace mc {
namespace {

// ---------------------------------------------------------------------------
// Registry helpers
// ---------------------------------------------------------------------------

constexpr const wchar_t* kBiosKey = L"HARDWARE\\DESCRIPTION\\System\\BIOS";
constexpr const wchar_t* kSecureBootKey =
    L"SYSTEM\\CurrentControlSet\\Control\\SecureBoot\\State";
constexpr const wchar_t* kDisplayEnumKey = L"SYSTEM\\CurrentControlSet\\Enum\\DISPLAY";
constexpr const wchar_t* kUefiEnumKey = L"SYSTEM\\CurrentControlSet\\Enum\\UEFI";
constexpr const wchar_t* kWindowsVersionKey =
    L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";

/// Opens a key under HKEY_LOCAL_MACHINE with the 64-bit view.
///
/// The 64-bit view matters: under WOW64 a 32-bit process would otherwise read a
/// redirected subtree and miss values such as the storage capacity fields.
HKEY openMachineKey(const wchar_t* path, REGSAM access = KEY_READ) {
    HKEY key = nullptr;
    const REGSAM flags = access | KEY_WOW64_64KEY;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, flags, &key) != ERROR_SUCCESS) {
        return nullptr;
    }
    return key;
}

/// Reads a REG_SZ value as UTF-8, returning nullopt when absent or mistyped.
std::optional<std::string> readString(HKEY key, const wchar_t* name) {
    if (key == nullptr) {
        return std::nullopt;
    }

    wchar_t buffer[1024] = {};
    DWORD size = sizeof(buffer);
    DWORD type = 0;
    if (RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<LPBYTE>(buffer), &size) !=
        ERROR_SUCCESS) {
        return std::nullopt;
    }
    if (type != REG_SZ && type != REG_EXPAND_SZ) {
        return std::nullopt;
    }
    buffer[(sizeof(buffer) / sizeof(buffer[0])) - 1] = L'\0';

    const int needed = WideCharToMultiByte(CP_UTF8, 0, buffer, -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 1) {
        return std::nullopt;
    }
    std::string out(static_cast<std::size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, buffer, -1, out.data(), needed, nullptr, nullptr);
    out.resize(static_cast<std::size_t>(needed - 1));
    if (out.empty()) {
        return std::nullopt;
    }
    return out;
}

/// Reads a numeric value as an unsigned integer.
std::optional<std::uint32_t> readDword(HKEY key, const wchar_t* name) {
    if (key == nullptr) {
        return std::nullopt;
    }
    DWORD value = 0;
    DWORD size = sizeof(value);
    DWORD type = 0;
    if (RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<LPBYTE>(&value), &size) !=
        ERROR_SUCCESS) {
        return std::nullopt;
    }
    if (type != REG_DWORD) {
        return std::nullopt;
    }
    return value;
}

/// Reads a REG_MULTI_SZ value as a list of wide strings.
std::vector<std::wstring> readStringArray(HKEY key, const wchar_t* name) {
    std::vector<std::wstring> values;
    if (key == nullptr) {
        return values;
    }

    DWORD size = 0;
    DWORD type = 0;
    if (RegQueryValueExW(key, name, nullptr, &type, nullptr, &size) != ERROR_SUCCESS ||
        type != REG_MULTI_SZ || size < sizeof(wchar_t) || size > 64 * 1024) {
        return values;
    }

    std::vector<wchar_t> buffer(size / sizeof(wchar_t) + 1, L'\0');
    if (RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<LPBYTE>(buffer.data()),
                         &size) != ERROR_SUCCESS) {
        return values;
    }

    const wchar_t* cursor = buffer.data();
    const wchar_t* end = buffer.data() + buffer.size();
    while (cursor < end && *cursor != L'\0') {
        values.emplace_back(cursor);
        cursor += values.back().size() + 1;
    }
    return values;
}

/// Reads a REG_BINARY value.
std::optional<std::vector<std::uint8_t>> readBinary(HKEY key, const wchar_t* name) {
    if (key == nullptr) {
        return std::nullopt;
    }
    DWORD size = 0;
    DWORD type = 0;
    if (RegQueryValueExW(key, name, nullptr, &type, nullptr, &size) != ERROR_SUCCESS ||
        type != REG_BINARY || size == 0 || size > 64 * 1024) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> data(size);
    if (RegQueryValueExW(key, name, nullptr, &type, data.data(), &size) != ERROR_SUCCESS) {
        return std::nullopt;
    }
    data.resize(size);
    return data;
}

/// A key that closes itself, so no early return can leak a handle.
class ScopedKey {
public:
    explicit ScopedKey(HKEY key) : key_(key) {}
    ~ScopedKey() {
        if (key_ != nullptr) {
            RegCloseKey(key_);
        }
    }
    ScopedKey(const ScopedKey&) = delete;
    ScopedKey& operator=(const ScopedKey&) = delete;

    [[nodiscard]] HKEY get() const noexcept { return key_; }

private:
    HKEY key_;
};

/// Reads a string from a key, or the unknown marker.
std::string textOrUnknown(HKEY key, const wchar_t* name) {
    const auto value = readString(key, name);
    return value.has_value() ? *value : kInventoryUnknown;
}

// ---------------------------------------------------------------------------
// SMBIOS access
// ---------------------------------------------------------------------------

/// Reads one of the SMBIOS values Windows caches under HARDWARE\DESCRIPTION.
///
/// Reading the cache instead of querying WMI keeps the BIOS page instant and
/// works in the same restricted contexts as the rest of the application.
std::string smbios(const wchar_t* name) {
    const ScopedKey key(openMachineKey(kBiosKey));
    return textOrUnknown(key.get(), name);
}

/// Builds "major.minor", reporting unknown when either part is absent.
std::string versionPair(const std::optional<std::uint32_t>& major,
                        const std::optional<std::uint32_t>& minor) {
    if (!major.has_value() || !minor.has_value()) {
        return kInventoryUnknown;
    }
    // SMBIOS uses 0xFF to mean "not available".
    if (*major == 0xFF || *minor == 0xFF) {
        return kInventoryUnknown;
    }
    return std::format("{}.{}", *major, *minor);
}

/// Converts the SMBIOS "MM/DD/YYYY" release date into "DD.MM.YYYY".
std::string normaliseDate(const std::string& raw) {
    int month = 0;
    int day = 0;
    int year = 0;
    if (std::sscanf(raw.c_str(), "%d/%d/%d", &month, &day, &year) != 3) {
        return raw.empty() ? kInventoryUnknown : raw;
    }
    if (month < 1 || month > 12 || day < 1 || day > 31 || year < 1980 || year > 2200) {
        return raw;
    }
    return std::format("{:02}.{:02}.{}", day, month, year);
}

/// Reports whether the machine booted through UEFI or a legacy BIOS.
std::string firmwareBootMode() {
    // GetFirmwareType arrived in Windows 8. On anything older the safe answer is
    // "not reported" rather than a guess.
    const HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    if (kernel == nullptr) {
        return kInventoryUnknown;
    }
    using GetFirmwareTypeFn = BOOL(WINAPI*)(FIRMWARE_TYPE*);
    const auto getFirmwareType =
        reinterpret_cast<GetFirmwareTypeFn>(GetProcAddress(kernel, "GetFirmwareType"));
    if (getFirmwareType == nullptr) {
        return kInventoryUnknown;
    }

    FIRMWARE_TYPE type{};
    if (getFirmwareType(&type) == FALSE) {
        return kInventoryUnknown;
    }
    switch (type) {
        case FirmwareTypeUefi:
            return "UEFI";
        case FirmwareTypeBios:
            return "Legacy BIOS";
        default:
            return kInventoryUnknown;
    }
}

/// Reads the Secure Boot state, which lives in the registry on UEFI systems.
std::string secureBootState() {
    const ScopedKey key(openMachineKey(kSecureBootKey));
    const auto value = readDword(key.get(), L"UEFISecureBootEnabled");
    if (!value.has_value()) {
        return kInventoryUnknown;
    }
    return *value == 1 ? "Включён" : "Выключен";
}

/// Finds the system firmware resource identifier.
///
/// Windows registers firmware devices under Enum\UEFI as RES_{GUID}. Only the
/// device whose compatible identifier is UEFI\CC_00010001 describes the system
/// firmware itself; other resources belong to peripheral UEFI capsules. The
/// identifier names a firmware family and is not the SMBIOS UUID of the laptop.
std::string systemFirmwareId() {
    const ScopedKey root(openMachineKey(kUefiEnumKey));
    if (root.get() == nullptr) {
        return kInventoryUnknown;
    }

    std::vector<std::wstring> matches;

    for (DWORD index = 0;; ++index) {
        wchar_t name[256] = {};
        DWORD nameLength = static_cast<DWORD>(std::size(name));
        if (RegEnumKeyExW(root.get(), index, name, &nameLength, nullptr, nullptr, nullptr,
                          nullptr) != ERROR_SUCCESS) {
            break;
        }

        const std::wstring resourceName(name, nameLength);
        if (resourceName.size() < 40 || resourceName.rfind(L"RES_{", 0) != 0) {
            continue;
        }

        const std::wstring resourcePath = std::wstring(kUefiEnumKey) + L"\\" + resourceName;
        const ScopedKey resource(openMachineKey(resourcePath.c_str()));
        if (resource.get() == nullptr) {
            continue;
        }

        for (DWORD childIndex = 0;; ++childIndex) {
            wchar_t child[256] = {};
            DWORD childLength = static_cast<DWORD>(std::size(child));
            if (RegEnumKeyExW(resource.get(), childIndex, child, &childLength, nullptr, nullptr,
                              nullptr, nullptr) != ERROR_SUCCESS) {
                break;
            }
            const std::wstring devicePath =
                resourcePath + L"\\" + std::wstring(child, childLength);
            const ScopedKey device(openMachineKey(devicePath.c_str()));
            const auto compatible = readStringArray(device.get(), L"CompatibleIDs");
            if (std::find(compatible.begin(), compatible.end(), L"UEFI\\CC_00010001") !=
                compatible.end()) {
                matches.push_back(resourceName);
                break;
            }
        }
    }

    // Exactly one match identifies the firmware family. Several matches would be
    // ambiguous, so the value stays unreported instead of picking one.
    if (matches.size() != 1) {
        return kInventoryUnknown;
    }

    // Strip the "RES_{" prefix and the trailing brace. A GUID is ASCII, so the
    // narrowing below is exact; constructing a std::string straight from wide
    // iterators would silently truncate every character instead.
    std::string guid;
    guid.reserve(matches[0].size());
    for (std::size_t index = 5; index + 1 < matches[0].size(); ++index) {
        const wchar_t character = matches[0][index];
        guid.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(character))));
    }
    return guid;
}

// ---------------------------------------------------------------------------
// EDID
// ---------------------------------------------------------------------------

/// True when a 128-byte EDID block carries a valid checksum.
bool edidBlockValid(const std::vector<std::uint8_t>& edid, std::size_t start) {
    if (edid.size() < start + 128) {
        return false;
    }
    unsigned int sum = 0;
    for (std::size_t index = start; index < start + 128; ++index) {
        sum += edid[index];
    }
    return sum % 256 == 0;
}

/// Extracts a display descriptor string, such as the panel model name.
std::optional<std::string> edidDescriptor(const std::vector<std::uint8_t>& edid, std::uint8_t type) {
    for (std::size_t offset = 54; offset <= 108; offset += 18) {
        if (edid.size() < offset + 18) {
            return std::nullopt;
        }
        if (edid[offset] == 0 && edid[offset + 1] == 0 && edid[offset + 3] == type) {
            std::string text(reinterpret_cast<const char*>(&edid[offset + 5]), 13);
            const std::size_t end = text.find_first_of("\r\n");
            if (end != std::string::npos) {
                text.resize(end);
            }
            while (!text.empty() && (text.back() == '\0' || text.back() == ' ')) {
                text.pop_back();
            }
            if (!text.empty()) {
                return text;
            }
        }
    }
    return std::nullopt;
}

/// Expands a three-letter EDID manufacturer code into its company name.
std::string edidVendorName(const std::string& code) {
    if (code == "BOE") return "BOE Technology Group";
    if (code == "AUO") return "AU Optronics";
    if (code == "LGD") return "LG Display";
    if (code == "SDC" || code == "SEC") return "Samsung Display";
    if (code == "CMN" || code == "INX") return "Innolux";
    if (code == "SHP") return "Sharp";
    if (code == "CSO") return "CSOT";
    if (code == "TMX") return "Tianma";
    return code;
}

/// Parses an EDID blob into display rows.
void appendEdidRows(std::vector<InventoryRow>& rows, const std::vector<std::uint8_t>& edid) {
    if (edid.size() < 128) {
        rows.push_back({"EDID", "Слишком короткий блок."});
        return;
    }

    constexpr std::array<std::uint8_t, 8> kHeader = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};
    if (!std::equal(kHeader.begin(), kHeader.end(), edid.begin()) || !edidBlockValid(edid, 0)) {
        rows.push_back({"EDID", "Недоступен или имеет неверную контрольную сумму."});
        return;
    }

    // Manufacturer code: three five-bit letters packed into one big-endian word.
    const int code = (edid[8] << 8) | edid[9];
    std::string manufacturer;
    manufacturer += static_cast<char>('A' - 1 + ((code >> 10) & 31));
    manufacturer += static_cast<char>('A' - 1 + ((code >> 5) & 31));
    manufacturer += static_cast<char>('A' - 1 + (code & 31));

    const std::string modelCode =
        manufacturer + std::format("{:04X}", edid[10] | (edid[11] << 8));

    rows.push_back({"Производитель матрицы", edidVendorName(manufacturer) + " · " + manufacturer});
    rows.push_back({"Модель из EDID", edidDescriptor(edid, 0xFC).value_or(kInventoryUnknown)});
    rows.push_back({"Код модели EDID", modelCode});
    rows.push_back({"Версия EDID", std::format("{}.{}", edid[18], edid[19])});

    const std::uint8_t week = edid[16];
    const int year = static_cast<int>(edid[17]) + 1990;
    rows.push_back({"Изготовление", (week == 0 || week == 255)
                                         ? std::format("{} год", year)
                                         : std::format("Неделя {}, {} год", week, year)});

    const bool digital = (edid[20] & 0x80) != 0;
    if (digital) {
        // The bit depth field only exists from EDID 1.4 onwards.
        std::string depth = kInventoryUnknown;
        if (edid[18] == 1 && edid[19] >= 4) {
            switch ((edid[20] >> 4) & 7) {
                case 1: depth = "6 бит на канал"; break;
                case 2: depth = "8 бит на канал"; break;
                case 3: depth = "10 бит на канал"; break;
                case 4: depth = "12 бит на канал"; break;
                case 5: depth = "14 бит на канал"; break;
                case 6: depth = "16 бит на канал"; break;
                default: break;
            }
        }
        rows.push_back({"Глубина цвета EDID", depth});

        const char* signal = "Цифровой";
        switch (edid[20] & 0x0F) {
            case 1: signal = "DVI"; break;
            case 2: signal = "HDMI-A"; break;
            case 3: signal = "HDMI-B"; break;
            case 4: signal = "MDDI"; break;
            case 5: signal = "DisplayPort"; break;
            default: break;
        }
        rows.push_back({"Сигнал EDID", signal});
    }

    if (edid[23] != 255) {
        rows.push_back({"Гамма EDID", std::format("{:.2f}", (edid[23] + 100) / 100.0)});
    }

    // Detailed timing descriptors give the true physical size and the modes.
    int widthMm = edid[21] * 10;
    int heightMm = edid[22] * 10;
    std::vector<std::string> modes;

    for (std::size_t offset = 54; offset <= 108; offset += 18) {
        const int pixelClock = edid[offset] | (edid[offset + 1] << 8);
        if (pixelClock == 0) {
            continue;
        }
        const int width = edid[offset + 2] | ((edid[offset + 4] & 0xF0) << 4);
        const int height = edid[offset + 5] | ((edid[offset + 7] & 0xF0) << 4);
        const int horizontal = width + edid[offset + 3] + ((edid[offset + 4] & 15) << 8);
        const int vertical = height + edid[offset + 6] + ((edid[offset + 7] & 15) << 8);
        if (horizontal > 0 && vertical > 0) {
            const double refresh = pixelClock * 10000.0 / horizontal / vertical;
            modes.push_back(std::format("{} x {} · {:.0f} Гц", width, height, refresh));
        }
        if (offset == 54) {
            const int timingWidth = edid[offset + 12] | ((edid[offset + 14] & 0xF0) << 4);
            const int timingHeight = edid[offset + 13] | ((edid[offset + 14] & 15) << 8);
            if (timingWidth > 0 && timingHeight > 0) {
                widthMm = timingWidth;
                heightMm = timingHeight;
            }
        }
    }

    if (widthMm > 0 && heightMm > 0) {
        const double diagonal =
            std::sqrt(static_cast<double>(widthMm) * widthMm + static_cast<double>(heightMm) * heightMm) /
            25.4;
        rows.push_back({"Размер / диагональ",
                        std::format("{} x {} мм · {:.1f} дюйма", widthMm, heightMm, diagonal)});
    }
    if (!modes.empty()) {
        std::sort(modes.begin(), modes.end());
        modes.erase(std::unique(modes.begin(), modes.end()), modes.end());
        std::string joined;
        for (std::size_t index = 0; index < modes.size(); ++index) {
            joined += (index == 0 ? "" : "; ") + modes[index];
        }
        rows.push_back({"Подробные режимы EDID", joined});
    }

    for (std::size_t offset = 54; offset <= 108; offset += 18) {
        if (edid[offset] == 0 && edid[offset + 1] == 0 && edid[offset + 3] == 0xFD) {
            rows.push_back({"Диапазон частот EDID",
                            std::format("{}-{} Гц", edid[offset + 5], edid[offset + 6])});
        }
    }

    // Extension blocks: the CTA-861 block carries HDR static metadata.
    std::vector<std::string> hdr;
    const int extensionCount = edid[126];
    for (int block = 1; block <= extensionCount; ++block) {
        const std::size_t start = static_cast<std::size_t>(block) * 128;
        if (edid.size() < start + 128 || edid[start] != 2 || !edidBlockValid(edid, start)) {
            continue;
        }
        const int end = edid[start + 2];
        if (end < 4 || end > 127) {
            continue;
        }
        for (std::size_t position = start + 4; position < start + static_cast<std::size_t>(end);) {
            const int length = edid[position] & 31;
            const int tag = edid[position] >> 5;
            if (position + static_cast<std::size_t>(length) >= start + static_cast<std::size_t>(end)) {
                break;
            }
            if (tag == 7 && length >= 3 && edid[position + 1] == 6) {
                if ((edid[position + 2] & 4) != 0) {
                    hdr.push_back("PQ / HDR10");
                }
                if ((edid[position + 2] & 8) != 0) {
                    hdr.push_back("HLG");
                }
            }
            position += static_cast<std::size_t>(length) + 1;
        }
    }
    if (hdr.empty()) {
        rows.push_back({"HDR из EDID", "PQ / HLG не заявлены в EDID"});
    } else {
        std::string joined;
        for (std::size_t index = 0; index < hdr.size(); ++index) {
            joined += (index == 0 ? "" : ", ") + hdr[index];
        }
        rows.push_back({"HDR из EDID", joined});
    }

    rows.push_back({"Аппаратная глубина / FRC",
                    "EDID сообщает глубину сигнала. Нативные 10 бит или 8 бит + FRC "
                    "требуют документации производителя матрицы."});
    rows.push_back({"Яркость / покрытие / тип панели",
                    "Ниты, sRGB/DCI-P3 и IPS/OLED нельзя достоверно определить из обычного EDID."});
}

/// Reads the EDID of every display Windows has enumerated.
///
/// The built-in panel is preferred: it is the one the laptop always has. An
/// external monitor appears here only while it is attached, so a stale entry
/// from a previous session is possible.
std::vector<InventoryRow> readDisplaySection() {
    std::vector<InventoryRow> rows;

    const ScopedKey displayRoot(openMachineKey(kDisplayEnumKey));
    if (displayRoot.get() == nullptr) {
        rows.push_back({"EDID", "Раздел Enum\\DISPLAY недоступен."});
        return rows;
    }

    for (DWORD index = 0;; ++index) {
        wchar_t panelName[256] = {};
        DWORD panelLength = static_cast<DWORD>(std::size(panelName));
        if (RegEnumKeyExW(displayRoot.get(), index, panelName, &panelLength, nullptr, nullptr,
                          nullptr, nullptr) != ERROR_SUCCESS) {
            break;
        }

        const std::wstring panelPath =
            std::wstring(kDisplayEnumKey) + L"\\" + std::wstring(panelName, panelLength);
        const ScopedKey panelKey(openMachineKey(panelPath.c_str()));
        if (panelKey.get() == nullptr) {
            continue;
        }

        for (DWORD instanceIndex = 0;; ++instanceIndex) {
            wchar_t instanceName[256] = {};
            DWORD instanceLength = static_cast<DWORD>(std::size(instanceName));
            if (RegEnumKeyExW(panelKey.get(), instanceIndex, instanceName, &instanceLength, nullptr,
                              nullptr, nullptr, nullptr) != ERROR_SUCCESS) {
                break;
            }

            const std::wstring parameterPath = panelPath + L"\\" +
                                               std::wstring(instanceName, instanceLength) +
                                               L"\\Device Parameters";
            const ScopedKey parameters(openMachineKey(parameterPath.c_str()));
            const auto edid = readBinary(parameters.get(), L"EDID");
            if (!edid.has_value() || edid->size() < 128) {
                continue;
            }

            const std::string panelId = [&panelName] {
                const int needed = WideCharToMultiByte(CP_UTF8, 0, panelName, -1, nullptr, 0,
                                                       nullptr, nullptr);
                if (needed <= 1) {
                    return std::string();
                }
                std::string out(static_cast<std::size_t>(needed), '\0');
                WideCharToMultiByte(CP_UTF8, 0, panelName, -1, out.data(), needed, nullptr,
                                    nullptr);
                out.resize(static_cast<std::size_t>(needed - 1));
                return out;
            }();

            const auto active = activeDisplayRows(panelId);
            if (active && active->empty()) continue;
            const std::string model =
                edidDescriptor(*edid, 0xFC).value_or(std::string(kInventoryUnknown));

            rows.push_back({"Дисплей", model + " · " + panelId});
            rows.push_back({"Идентификатор панели", panelId});
            appendEdidRows(rows, *edid);
            if (active) rows.insert(rows.end(), active->begin(), active->end());
            else rows.push_back({"Текущий режим Windows", "DisplayConfig недоступен; показаны сохранённые сведения EDID."});

            const auto friendly = readString(parameters.get(), L"FriendlyName");
            if (friendly.has_value()) {
                rows.push_back({"Понятное имя Windows", *friendly});
            }
            break;  // One instance per panel is enough; the rest repeat the EDID.
        }
    }

    if (rows.empty()) {
        rows.push_back({"EDID", "Активная матрица не найдена в реестре."});
    }
    return rows;
}

// ---------------------------------------------------------------------------
// Section helpers
// ---------------------------------------------------------------------------

/// Runs one section reader, converting any failure into a visible row.
template <typename Reader>
void appendSection(HardwareInventory& inventory, const char* title, Reader&& reader) {
    if(OperationMessages::capturing())logMessage("inventory",std::string("Раздел: ")+title);
    InventorySection section;
    section.title = title;
    try {
        section.rows = reader();
    } catch (const std::exception& error) {
        section.rows.push_back({"Сведения недоступны", error.what()});
    } catch (...) {
        section.rows.push_back({"Сведения недоступны", "неизвестная ошибка"});
    }
    if(OperationMessages::capturing())for(const auto& row:section.rows)logMessage("inventory",row.name+": "+row.value);
    inventory.sections.push_back(std::move(section));
}

std::string timestampNow() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    if (localtime_s(&local, &now) != 0) {
        return kInventoryUnknown;
    }
    return std::format("{:02}.{:02}.{:04} {:02}:{:02}:{:02}", local.tm_mday, local.tm_mon + 1,
                       local.tm_year + 1900, local.tm_hour, local.tm_min, local.tm_sec);
}

}  // namespace

// ---------------------------------------------------------------------------
// Provider
// ---------------------------------------------------------------------------

namespace {

/// Read-only Windows inventory: SMBIOS/EDID, WMI, DisplayConfig, TBS and Battery IOCTL.
/// Collection is called on a worker by the GUI; Linux has an explicit stub.
class WindowsHardwareProvider final : public IHardwareProvider {
public:
    [[nodiscard]] const char* id() const noexcept override { return "windows-wmi-native"; }

    [[nodiscard]] const char* description() const noexcept override {
        return "SMBIOS · EDID · WMI · DisplayConfig · TBS · Battery Class IOCTL";
    }

    [[nodiscard]] HardwareInventory collect(std::stop_token cancellation = {}) const override;
};

}  // namespace

HardwareInventory WindowsHardwareProvider::collect(std::stop_token cancellation) const {
    HardwareInventory inventory;
    inventory.readAt = timestampNow();

    // --- Laptop, BIOS, UEFI, EC --------------------------------------------
    appendSection(inventory, "Ноутбук · BIOS · UEFI · EC", [] {
        const ScopedKey bios(openMachineKey(kBiosKey));
        std::vector<InventoryRow> rows;

        const BoardDescriptor board = readBoardDescriptor();
        const std::string modelFromFirmware = smbios(L"SystemProductName");
        const auto& profile = deviceProfile(board);
        rows.push_back({"Ноутбук", profile.displayName.empty() ? modelFromFirmware : std::string(profile.displayName)});
        rows.push_back({"Модель из прошивки", modelFromFirmware});
        rows.push_back({"Производитель", smbios(L"SystemManufacturer")});
        rows.push_back({"Семейство", smbios(L"SystemFamily")});
        rows.push_back({"Материнская плата", smbios(L"BaseBoardProduct")});
        rows.push_back({"Производитель платы", smbios(L"BaseBoardManufacturer")});
        rows.push_back({"Производитель BIOS", smbios(L"BIOSVendor")});
        rows.push_back({"Версия BIOS", smbios(L"BIOSVersion")});
        rows.push_back({"Дата BIOS", normaliseDate(smbios(L"BIOSReleaseDate"))});

        // The EC revision comes from the SMBIOS firmware fields, which is the
        // same source the reference implementation uses: ECFirmwareMajorRelease
        // and ECFirmwareMinorRelease read 1 and 18 on this board, matching the
        // "Версия EC 1.18" the reference displays. The separate BiosMajorRelease
        // and BiosMinorRelease fields read 1.15 and are not the BIOS version, so
        // they are deliberately not shown as one.
        rows.push_back({"Версия EC (SMBIOS)",
                        versionPair(readDword(bios.get(), L"ECFirmwareMajorRelease"),
                                    readDword(bios.get(), L"ECFirmwareMinorRelease"))});

        rows.push_back({"Режим загрузки", firmwareBootMode()});
        rows.push_back({"Идентификатор системной прошивки UEFI", systemFirmwareId()});
        rows.push_back({"Secure Boot", secureBootState()});
        return rows;
    });

    // --- Display and panel --------------------------------------------------
    appendSection(inventory, "Дисплей · матрица", [] { return readDisplaySection(); });

    appendWindowsDetails(inventory, cancellation);

    appendInventoryEcIdentity(inventory,cancellation);

    return inventory;
}

const IHardwareProvider& defaultHardwareProvider() {
    // A function-local static: the provider is stateless, and constructing it on
    // first use avoids any static initialisation order question.
    static const WindowsHardwareProvider provider;
    return provider;
}

HardwareInventory collectDiagnosticInventory(std::stop_token cancellation) {
    auto inventory=defaultHardwareProvider().collect(cancellation);
    appendWindowsDiagnosticDetails(inventory,cancellation);
    return inventory;
}

std::string executableDirectory() {
    wchar_t buffer[32768] = {};
    const DWORD length = GetModuleFileNameW(nullptr, buffer, 32768);
    if (length == 0 || length >= 32768) {
        return ".";
    }

    const int needed = WideCharToMultiByte(CP_UTF8, 0, buffer, -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 1) {
        return ".";
    }
    std::string path(static_cast<std::size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, buffer, -1, path.data(), needed, nullptr, nullptr);

    path.resize(static_cast<std::size_t>(needed - 1));
    const std::size_t separator = path.find_last_of("\\/");
    if (separator == std::string::npos) {
        return ".";
    }
    return path.substr(0, separator);
}

}  // namespace mc
