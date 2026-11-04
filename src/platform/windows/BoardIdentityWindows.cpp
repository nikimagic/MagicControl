#include "MagicControl/BoardIdentity.h"
#include "MagicControl/DeviceProfile.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <windows.h>

namespace mc {
namespace {
std::string trim(std::string value) {
    const auto space = [](unsigned char c) { return std::isspace(c) != 0; };
    value.erase(value.begin(), std::find_if_not(value.begin(), value.end(), space));
    value.erase(std::find_if_not(value.rbegin(), value.rend(), space).base(), value.end());
    return value;
}

std::string readSmbiosValue(const wchar_t* name) {
    HKEY key{};
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\BIOS", 0,
                      KEY_READ, &key) != ERROR_SUCCESS) return {};
    wchar_t value[256]{};
    DWORD bytes = sizeof(value), type{};
    const LSTATUS status = RegQueryValueExW(key, name, nullptr, &type,
                                            reinterpret_cast<LPBYTE>(value), &bytes);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS || type != REG_SZ) return {};
    value[(std::size(value) - 1)] = L'\0';
    const int needed = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 1) return {};
    std::string result(static_cast<std::size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), needed, nullptr, nullptr);
    result.pop_back();
    return result;
}
}

BoardDescriptor readBoardDescriptor() {
    BoardDescriptor descriptor;
    descriptor.manufacturer = trim(readSmbiosValue(L"SystemManufacturer"));
    descriptor.baseBoard = trim(readSmbiosValue(L"BaseBoardProduct"));
    descriptor.productName = trim(readSmbiosValue(L"SystemProductName"));
    identifyDevice(descriptor);
    return descriptor;
}
}
