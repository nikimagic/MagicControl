#pragma once
#include "Probe.h"
#include <utility>
#include <windows.h>
namespace probe {
inline std::string utf8(const wchar_t *value, int length = -1) {
    if (!value)
        return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value, length, nullptr, 0, nullptr, nullptr);
    if (size <= 0)
        return {};
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, length, result.data(), size, nullptr, nullptr);
    if (length < 0 && !result.empty())
        result.pop_back();
    return result;
}
inline std::wstring wide(const std::string &value) {
    const int size =
        MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    std::wstring result(size, L'\0');
    if (size)
        MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size);
    return result;
}
inline std::string winError(DWORD code = GetLastError()) {
    wchar_t *message{};
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                       FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, code, 0, reinterpret_cast<wchar_t *>(&message), 0, nullptr);
    auto text = utf8(message);
    if (message)
        LocalFree(message);
    return "Win32 " + std::to_string(code) + ": " + text;
}
struct WinHandle {
    HANDLE value{INVALID_HANDLE_VALUE};
    WinHandle() = default;
    explicit WinHandle(HANDLE v) : value(v) {};
    ~WinHandle() {
        if (value != INVALID_HANDLE_VALUE && value)
            CloseHandle(value);
    }
    WinHandle(const WinHandle &) = delete;
    WinHandle &operator=(const WinHandle &) = delete;
};
template <class T> T symbol(HMODULE library, const char *name) {
    return reinterpret_cast<T>(GetProcAddress(library, name));
}
inline bool ioctl(HANDLE handle, DWORD code, const void *input, DWORD inputSize, void *output,
                  DWORD outputSize, DWORD &returned) {
    return DeviceIoControl(handle, code, const_cast<void *>(input), inputSize, output, outputSize, &returned,
                           nullptr) != FALSE;
}
inline std::string modulePath(HMODULE module) {
    wchar_t path[32768]{};
    const auto count = GetModuleFileNameW(module, path, 32768);
    return count ? utf8(path, static_cast<int>(count)) : std::string{};
}
} // namespace probe
