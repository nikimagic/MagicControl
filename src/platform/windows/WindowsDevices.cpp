#include "MagicControl/ActionLog.h"
#include "WindowsDevices.h"
#include "RadioPermission.h"
#include "MagicControl/MicrophoneControl.h"
#include "MagicControl/QuickSwitch.h"

#include <windows.h>
#include <initguid.h>
#include <cfgmgr32.h>
#include <devguid.h>
#include <devpkey.h>
#include <setupapi.h>
#include <winrt/Windows.Devices.Radios.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/base.h>

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <stdexcept>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include "MagicControl/EcAccess.h"
#include "MagicControl/EcRegister.h"

namespace mc::windows {
namespace {
RadioPermission radioPermission;
std::wstring lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return value;
}
bool matches(const std::wstring& name, DeviceKind kind) {
    const auto value = lower(name);
    switch (kind) {
    case DeviceKind::Camera: return value.find(L"cam") != std::wstring::npos || value.find(L"камер") != std::wstring::npos || value.find(L"веб") != std::wstring::npos;
    case DeviceKind::Touchpad: return value.find(L"touchpad") != std::wstring::npos || value.find(L"сенсорная панель") != std::wstring::npos;
    default: return false;
    }
}
struct DeviceSet {
    HDEVINFO handle{INVALID_HANDLE_VALUE};
    ~DeviceSet() { if (handle != INVALID_HANDLE_VALUE) SetupDiDestroyDeviceInfoList(handle); }
};
std::wstring friendly(HDEVINFO set, SP_DEVINFO_DATA& data) {
    wchar_t buffer[512]{}; DEVPROPTYPE type{}; DWORD size = sizeof(buffer);
    if (SetupDiGetDevicePropertyW(set, &data, &DEVPKEY_Device_FriendlyName, &type,
                                  reinterpret_cast<PBYTE>(buffer), size, &size, 0)) return buffer;
    size = sizeof(buffer);
    if (SetupDiGetDeviceRegistryPropertyW(set, &data, SPDRP_DEVICEDESC, &type,
                                          reinterpret_cast<PBYTE>(buffer), size, &size)) return buffer;
    return {};
}
std::vector<std::pair<HDEVINFO, SP_DEVINFO_DATA>> devices(DeviceKind kind, DeviceSet& holder) {
    holder.handle = SetupDiGetClassDevsW(nullptr, nullptr, nullptr,
        DIGCF_ALLCLASSES | (kind == DeviceKind::Camera ? 0 : DIGCF_PRESENT));
    if (holder.handle == INVALID_HANDLE_VALUE) throw std::runtime_error("SetupAPI: устройство не перечислено");
    std::vector<std::pair<HDEVINFO, SP_DEVINFO_DATA>> result;
    for (DWORD index = 0; index < 4096; ++index) {
        SP_DEVINFO_DATA data{sizeof(data)};
        if (!SetupDiEnumDeviceInfo(holder.handle, index, &data)) {
            if (GetLastError() == ERROR_NO_MORE_ITEMS) break;
            continue;
        }
        constexpr GUID cameraClass{0xca3e7ab9, 0xb4c3, 0x4ae6, {0x82, 0x51, 0x57, 0x9e, 0xf9, 0x33, 0x89, 0x0f}};
        bool match = false;
        if (kind == DeviceKind::Camera) {
            match = IsEqualGUID(data.ClassGuid, cameraClass) ||
                (IsEqualGUID(data.ClassGuid, GUID_DEVCLASS_IMAGE) && matches(friendly(holder.handle, data), kind));
        } else if (kind == DeviceKind::Touchpad) {
            if (IsEqualGUID(data.ClassGuid, GUID_DEVCLASS_HIDCLASS)) {
                for (const DWORD property : {SPDRP_HARDWAREID, SPDRP_COMPATIBLEIDS}) {
                    DWORD size{}, type{};
                    SetupDiGetDeviceRegistryPropertyW(holder.handle, &data, property, &type, nullptr, 0, &size);
                    std::vector<wchar_t> ids(size / sizeof(wchar_t) + 2);
                    if (size && SetupDiGetDeviceRegistryPropertyW(holder.handle, &data, property, &type,
                            reinterpret_cast<PBYTE>(ids.data()), size, &size)) {
                        const std::wstring value(ids.data(), size / sizeof(wchar_t));
                        match = match || lower(value).find(L"up:000d_u:0005") != std::wstring::npos;
                    }
                }
            } else if (IsEqualGUID(data.ClassGuid, GUID_DEVCLASS_MOUSE)) match = matches(friendly(holder.handle, data), kind);
        }
        if (match) result.emplace_back(holder.handle, data);
    }
    if (kind == DeviceKind::Camera) {
        const auto liveOrDisabled = [](const auto& entry) {
            ULONG status{}, problem{};
            return CM_Get_DevNode_Status(&status, &problem, entry.second.DevInst, 0) == CR_SUCCESS &&
                ((status & DN_STARTED) != 0 || problem == CM_PROB_DISABLED);
        };
        if (std::any_of(result.begin(), result.end(), liveOrDisabled))
            std::erase_if(result, [&](const auto& entry) { return !liveOrDisabled(entry); });
    }
    return result;
}
DeviceState readSetupApi(DeviceKind kind, bool includeWindowsState = true) {
    DeviceSet holder; auto found = devices(kind, holder);
    if (found.empty()) return {std::nullopt, false, "Устройство не найдено."};
    std::optional<bool> state;
    std::string names;
    for (auto [set, data] : found) {
        ULONG status{}, problem{};
        const CONFIGRET statusResult = CM_Get_DevNode_Status(&status, &problem, data.DevInst, 0);
        const bool started = statusResult == CR_SUCCESS && problem == 0 &&
            (status & DN_HAS_PROBLEM) == 0 && (status & DN_STARTED) != 0;
        const auto name = friendly(set, data);
        if (!name.empty()) { if (!names.empty()) names += " · "; names += winrt::to_string(name); }
        if (!state) state = started; else if (*state != started) return {std::nullopt, true, "Несколько устройств имеют разные состояния."};
    }
    if (kind == DeviceKind::Touchpad && includeWindowsState && state && *state) {
        if (const auto enabled = readTouchpadEnabled()) state = enabled;
    }
    return {state, true, names.empty() ? "Состояние устройства Windows." : names};
}
void setSetupApi(DeviceKind kind, bool enabled) {
    DeviceSet holder; auto found = devices(kind, holder);
    if (found.empty()) throw std::runtime_error("Устройство Windows не найдено.");
    for (auto [set, data] : found) {
        SP_PROPCHANGE_PARAMS change{};
        change.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
        change.ClassInstallHeader.InstallFunction = DIF_PROPERTYCHANGE;
        change.StateChange = enabled ? DICS_ENABLE : DICS_DISABLE;
        change.Scope = DICS_FLAG_GLOBAL;
        change.HwProfile = 0;
        if (!SetupDiSetClassInstallParamsW(set, &data, &change.ClassInstallHeader, sizeof(change)) ||
            !SetupDiCallClassInstaller(DIF_PROPERTYCHANGE, set, &data))
            throw std::runtime_error("Windows не подтвердил изменение состояния устройства.");
    }
}
winrt::Windows::Devices::Radios::RadioKind radioKind(DeviceKind kind) {
    return kind == DeviceKind::Wifi ? winrt::Windows::Devices::Radios::RadioKind::WiFi : winrt::Windows::Devices::Radios::RadioKind::Bluetooth;
}
DeviceState readRadio(DeviceKind kind) {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    struct Apartment { ~Apartment() { winrt::uninit_apartment(); } } apartment;
    auto operation = winrt::Windows::Devices::Radios::Radio::GetRadiosAsync();
    if (operation.wait_for(std::chrono::seconds(2)) != winrt::Windows::Foundation::AsyncStatus::Completed) {
        operation.Cancel(); throw std::runtime_error("Windows не ответила при чтении радиоустройств.");
    }
    auto radios = operation.get();
    std::optional<bool> state; int count = 0;
    for (auto const& radio : radios) if (radio.Kind() == radioKind(kind)) {
        ++count; const bool on = radio.State() == winrt::Windows::Devices::Radios::RadioState::On;
        if (!state) state = on; else if (*state != on) return {std::nullopt, true, "Радиоустройства имеют разные состояния."};
    }
    return {state, count != 0, count == 0 ? "Радиоустройство не найдено." : "Текущее состояние Windows."};
}
void setRadio(DeviceKind kind, bool enabled) {
    radioPermission.ensureAllowed();
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    struct Apartment { ~Apartment() { winrt::uninit_apartment(); } } apartment;
    auto operation = winrt::Windows::Devices::Radios::Radio::GetRadiosAsync();
    if (operation.wait_for(std::chrono::seconds(2)) != winrt::Windows::Foundation::AsyncStatus::Completed) {
        operation.Cancel(); throw std::runtime_error("Windows не ответила при чтении радиоустройств.");
    }
    auto radios = operation.get(); int count = 0;
    const auto wanted = enabled ? winrt::Windows::Devices::Radios::RadioState::On : winrt::Windows::Devices::Radios::RadioState::Off;
    for (auto const& radio : radios) if (radio.Kind() == radioKind(kind)) {
        ++count;
        auto change = radio.SetStateAsync(wanted);
        if (change.wait_for(std::chrono::seconds(2)) != winrt::Windows::Foundation::AsyncStatus::Completed) {
            change.Cancel(); throw std::runtime_error("Windows не подтвердила переключение радиоустройства вовремя.");
        }
        if (change.get() != winrt::Windows::Devices::Radios::RadioAccessStatus::Allowed) {
            radioPermission.revoke();
            throw std::runtime_error("Windows не разрешил изменение радиоустройства.");
        }
    }
    if (!count) throw std::runtime_error("Радиоустройство не найдено.");
}
}

std::optional<bool> readTouchpadEnabled() {
    DWORD value{}, size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\PrecisionTouchPad\\Status", L"Enabled",
        RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS) return value != 0;
    struct Parameters { UINT version, maxContacts, legacyFeatures, status, settings, sensitivity,
        cursorSpeed, feedback, clickForce, rightWidth, rightHeight; } parameters{};
    parameters.version = 1;
    if (SystemParametersInfoW(0xAE, sizeof(parameters), &parameters, 0) && (parameters.status & 1))
        return (parameters.status & 8) != 0;
    return std::nullopt;
}

// Only used by an explicit KEYs command. The notification observer never
// injects this shortcut or changes a device; it only reads the Windows flag.
void setTouchpadWindowsState(bool enabled) {
    const auto previous = readTouchpadEnabled();
    if (!previous || *previous == enabled) return;
    const bool controlHeld = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool windowsHeld = ((GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) & 0x8000) != 0;
    std::vector<INPUT> inputs;
    const auto key = [&](WORD code, bool up) {
        INPUT input{}; input.type=INPUT_KEYBOARD; input.ki.wVk=code; input.ki.dwFlags=up?KEYEVENTF_KEYUP:0;
        inputs.push_back(input);
    };
    if (!controlHeld) key(VK_CONTROL,false);
    if (!windowsHeld) key(VK_LWIN,false);
    key(VK_F24,false); key(VK_F24,true);
    if (!windowsHeld) key(VK_LWIN,true);
    if (!controlHeld) key(VK_CONTROL,true);
    const UINT sent = SendInput(static_cast<UINT>(inputs.size()),inputs.data(),sizeof(INPUT));
    if (sent != inputs.size()) {
        inputs.clear();
        if (!windowsHeld) key(VK_LWIN,true);
        if (!controlHeld) key(VK_CONTROL,true);
        if (!inputs.empty()) SendInput(static_cast<UINT>(inputs.size()),inputs.data(),sizeof(INPUT));
        throw std::runtime_error("Windows не приняла команду сенсорной панели.");
    }
    for (int attempt=0; attempt<50; ++attempt) {
        if (const auto actual=readTouchpadEnabled(); actual && *actual==enabled) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    throw std::runtime_error("Windows не подтвердила состояние сенсорной панели.");
}

std::function<void()> requestRadioAccessForUser() {
    try {
        return radioPermission.request([] {
            // Consent must start in the GUI's valid user-interaction context.
            auto request = winrt::Windows::Devices::Radios::Radio::RequestAccessAsync();
            return [request] {
                try {
                winrt::init_apartment(winrt::apartment_type::multi_threaded);
                struct Apartment { ~Apartment() { winrt::uninit_apartment(); } } apartment;
                if (request.wait_for(std::chrono::seconds(30)) != winrt::Windows::Foundation::AsyncStatus::Completed) {
                    request.Cancel(); throw std::runtime_error("Истекло время ожидания разрешения Wi-Fi/Bluetooth.");
                }
                if (request.get() != winrt::Windows::Devices::Radios::RadioAccessStatus::Allowed)
                    throw std::runtime_error("Windows не разрешила приложению управлять Wi-Fi/Bluetooth. Проверьте разрешения и системную политику.");
                } catch (const winrt::hresult_error& error) { throw std::runtime_error(winrt::to_string(error.message())); }
            };
        });
    } catch (const winrt::hresult_error& error) { throw std::runtime_error(winrt::to_string(error.message())); }
}

DeviceState readDevice(DeviceKind kind) {
    try {
        if(kind==DeviceKind::Microphone){const auto state=readDefaultMicrophone();return {state.enabled,state.available,state.detail};}
        if (kind == DeviceKind::Wifi || kind == DeviceKind::Bluetooth) return readRadio(kind);
        return readSetupApi(kind);
    } catch (const winrt::hresult_error& error) { throw std::runtime_error(winrt::to_string(error.message())); }
}
void setDevice(DeviceKind kind, bool enabled) {
    audit::Scope trace("system.device_switch","\"kind\":"+std::to_string(static_cast<int>(kind))+",\"requested_enabled\":"+(enabled?"true":"false"));
    try {
    // Microphone is a coordinated KEYs operation (capture state + optional EC
    // indication), never a second generic device setter that skips that policy.
    if(kind==DeviceKind::Microphone)throw std::invalid_argument("Microphone commands must use the KEYs resource handler");
    if (kind == DeviceKind::Wifi || kind == DeviceKind::Bluetooth) setRadio(kind, enabled);
    else if (kind == DeviceKind::Touchpad && readTouchpadEnabled()) {
        if (enabled) {
            const auto device=readSetupApi(kind,false);
            if (device.enabled && !*device.enabled) setSetupApi(kind,true);
        }
        setTouchpadWindowsState(enabled);
    } else setSetupApi(kind, enabled);
    } catch (const winrt::hresult_error& error) {
        if (kind == DeviceKind::Wifi || kind == DeviceKind::Bluetooth) radioPermission.revoke();
        throw std::runtime_error(winrt::to_string(error.message()));
    }
    for (int attempt = 0; attempt < 10; ++attempt) {
        const auto actual = readDevice(kind);
        if (actual.enabled && *actual.enabled == enabled){trace.finish("readback_confirmed","\"enabled\":"+std::string(enabled?"true":"false")+",\"detail\":"+audit::quote(actual.detail));return;}
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    throw std::runtime_error("Windows не подтвердила итоговое состояние устройства.");
}

void setWindowsKeyLocked(EcAccess& access, bool locked) {
    setQuickSwitch(access,QuickSwitch::Windows,locked);
}

std::vector<std::uint16_t> presentIntelLpcIds() {
    DeviceSet holder;
    holder.handle = SetupDiGetClassDevsW(nullptr, L"PCI", nullptr, DIGCF_PRESENT | DIGCF_ALLCLASSES);
    if (holder.handle == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot enumerate present LPC devices");
    std::vector<std::uint16_t> result;
    const auto property = [&](SP_DEVINFO_DATA& data, DWORD key) {
        DWORD size{}, type{};
        SetupDiGetDeviceRegistryPropertyW(holder.handle, &data, key, &type, nullptr, 0, &size);
        if (!size || size > 65536) return std::wstring{};
        std::vector<wchar_t> value(size / sizeof(wchar_t) + 2);
        if (!SetupDiGetDeviceRegistryPropertyW(holder.handle, &data, key, &type,
                reinterpret_cast<PBYTE>(value.data()), size, &size)) return std::wstring{};
        return lower(std::wstring(value.data(), size / sizeof(wchar_t)));
    };
    for (DWORD index = 0; ; ++index) {
        SP_DEVINFO_DATA data{sizeof(data)};
        if (!SetupDiEnumDeviceInfo(holder.handle, index, &data)) {
            if (GetLastError() == ERROR_NO_MORE_ITEMS) break;
            throw std::runtime_error("Incomplete LPC enumeration");
        }
        const auto compatible = property(data, SPDRP_COMPATIBLEIDS);
        if (compatible.find(L"&cc_0601") == std::wstring::npos) continue;
        const auto ids = property(data, SPDRP_HARDWAREID);
        const auto at = ids.find(L"ven_8086&dev_");
        if (at == std::wstring::npos || at + 17 > ids.size()) continue;
        const auto hex = ids.substr(at + 13, 4);
        if (!std::all_of(hex.begin(), hex.end(), [](wchar_t c) { return std::iswxdigit(c) != 0; })) continue;
        result.push_back(static_cast<std::uint16_t>(std::stoul(hex, nullptr, 16)));
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

DeviceState readCamera(EcAccess& access) {
    access.ensureWritable();
    const bool open = (access.readStable(ec::kControl727) & ec::kCameraPresentBit) == 0;
    auto state = readSetupApi(DeviceKind::Camera);
    if (!open) return {false, true, state.detail + " · Электронно отключена · подтверждено EC"};
    return state;
}

void setCamera(EcAccess& access, bool enabled) {
    {
        const EcAccess::Transaction transaction(access);
        access.ensureWritable();
        if (((access.readStable(ec::kControl727) & ec::kCameraPresentBit) == 0) != enabled) {
            access.updateBitsCommand(ec::kControl727, ec::kControl727CamMask,
                enabled ? ec::kControl727CamOn : ec::kControl727CamOff);
            bool confirmed = false;
            for (int attempt = 0; attempt < 30; ++attempt) {
                const auto actual = access.readStable(ec::kControl727);
                if ((actual & 0x10) == 0 && ((actual & ec::kCameraPresentBit) == 0) == enabled) { confirmed = true; break; }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            if (!confirmed) throw std::runtime_error("EC не подтвердил изменение состояния камеры.");
        }
    }
    if (!enabled) return;
    // Explicit On also repairs a Windows disable left by an older application.
    for (int attempt = 0; attempt < 40; ++attempt) {
        const auto state = readSetupApi(DeviceKind::Camera);
        if (state.enabled && *state.enabled) return;
        if (state.available) { try { setSetupApi(DeviceKind::Camera, true); } catch (...) {} }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    throw std::runtime_error("EC включил камеру, но Windows ещё не подтвердила её появление.");
}
}
