#pragma once
#include "UniwillControlOwnerWindows.h"
#include <array>
#include <string>
#include <stdexcept>

namespace mc::windows {
struct LightingOwnership {
    bool writable{};
    std::string status;
};

// Read-only admission for the reviewed Uniwill lighting backends. The exact
// board/protocol/EC identity remains the caller's responsibility. This snapshot
// is not an exclusive lease: OEM processes do not obey our transaction mutex.
// Never stop/start a service or treat unknown SCM/process state as permission.
inline LightingOwnership inspectLightingOwnership() {
    const auto manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!manager)
        return {false, "Подсветка: состояние OEM неизвестно (SCM Win32=" + std::to_string(GetLastError()) + ")"};
    struct ServiceHandle {
        SC_HANDLE value;
        ~ServiceHandle() { if (value) CloseServiceHandle(value); }
    } managerOwner{manager};
    struct ServiceName { const wchar_t* wide; const char* text; };
    // Same fixed names reviewed by firmware evidence. GCUBridge is the native
    // wrapper service; its GCUService/AiStone child can outlive a stop transition.
    constexpr std::array<ServiceName, 3> services{{
        {L"GCUBridge", "GCUBridge"},
        {L"GCUService", "GCUService"},
        {L"AiStoneService", "AiStoneService"}
    }};
    for (const auto& name : services) {
        const auto service = OpenServiceW(manager, name.wide, SERVICE_QUERY_STATUS);
        if (!service) {
            const auto error = GetLastError();
            if (error == ERROR_SERVICE_DOES_NOT_EXIST) continue;
            return {false, "Подсветка: состояние " + std::string(name.text) +
                " неизвестно (Win32=" + std::to_string(error) + ")"};
        }
        ServiceHandle owner{service};
        SERVICE_STATUS_PROCESS state{}; DWORD bytes{};
        if (!QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO,
                reinterpret_cast<BYTE*>(&state), sizeof(state), &bytes))
            return {false, "Подсветка: состояние " + std::string(name.text) +
                " неизвестно (Win32=" + std::to_string(GetLastError()) + ")"};
        if (state.dwCurrentState != SERVICE_STOPPED)
            return {false, "Подсветка занята OEM: " + std::string(name.text) +
                " (state=" + std::to_string(state.dwCurrentState) + ")"};
    }
    try {
        // Reuse the shared checked Toolhelp walk; an incomplete process list
        // throws instead of being mistaken for absence of the two OEM writers.
        const auto process = uniwillControlOwner();
        if (!process.empty()) return {false, "Подсветка занята OEM: " + process};
    } catch (const std::exception& error) {
        return {false, "Подсветка: состояние OEM неизвестно: " + std::string(error.what())};
    }
    return {true, "Проверенные OEM-службы и процессы подсветки не активны"};
}

inline void ensureLightingWritable() {
    const auto owner = inspectLightingOwnership();
    if (!owner.writable) throw std::runtime_error(owner.status);
}
} // namespace mc::windows
