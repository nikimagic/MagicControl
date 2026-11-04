#include "MagicControl/PawnIoInstall.h"
#include "MagicControl/BoardIdentity.h"
#include "MagicControl/DeviceProfile.h"
#include "Platform.h"
#include "../platform/windows/EmbeddedRuntimeWindows.h"
#include <windows.h>
#include <shellapi.h>
#include <wintrust.h>
#include <softpub.h>
#include <fstream>
#include <random>
#include <vector>
#include <stdexcept>
#include <thread>

namespace mc::dependencies {
namespace {
namespace fs = std::filesystem;
namespace transport = mc::update::platform;
// Original pinned installer is embedded; the explicit install action needs no download.
// Pinned compatible release, checked against GitHub asset digest and a valid
// namazso Authenticode signature on 18.10.2026. Future versions need review.
constexpr char digest[] = "1f519a22e47187f70a1379a48ca604981c4fcf694f4e65b734aaa74a9fba3032";
constexpr std::uintmax_t bytes = 3410960;
void cancel(std::stop_token stop) {
    if (stop.stop_requested()) throw std::runtime_error("Подготовка PawnIO отменена");
}
struct ReadLock {
    HANDLE handle{INVALID_HANDLE_VALUE};
    explicit ReadLock(const fs::path& path) {
        handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (handle == INVALID_HANDLE_VALUE) throw std::runtime_error("Не удалось открыть установщик драйвера");
        BY_HANDLE_FILE_INFORMATION info{};
        if (!GetFileInformationByHandle(handle, &info) || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
            CloseHandle(handle); handle = INVALID_HANDLE_VALUE;
            throw std::runtime_error("Установщик драйвера не должен быть ссылкой");
        }
    }
    ~ReadLock() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
};
void verify(const fs::path& path, HANDLE handle, bool online = true,const char* expected=digest,std::uintmax_t expectedBytes=bytes) {
    if (fs::file_size(path) != expectedBytes || transport::sha256(path) != expected)
        throw std::runtime_error("Установщик не совпадает с официальным SHA-256; запуск отменён");
    WINTRUST_FILE_INFO file{sizeof(file)}; file.pcwszFilePath = path.c_str(); file.hFile = handle;
    WINTRUST_DATA trust{sizeof(trust)};
    trust.dwUIChoice = WTD_UI_NONE;
    trust.fdwRevocationChecks = WTD_REVOKE_WHOLECHAIN;
    trust.dwUnionChoice = WTD_CHOICE_FILE; trust.pFile = &file;
    trust.dwStateAction = WTD_STATEACTION_VERIFY;
    trust.dwProvFlags = WTD_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT | (online ? 0 : WTD_CACHE_ONLY_URL_RETRIEVAL);
    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    const LONG result = WinVerifyTrust(nullptr, &action, &trust);
    trust.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(nullptr, &action, &trust);
    if (result != ERROR_SUCCESS)
        throw std::runtime_error("Windows не подтвердила цифровую подпись установщика; запуск отменён (" + std::to_string(result) + ")");
}
}
PawnIoSetupAction pawnIoSetupAction() {
    return pawnIoSetupAction(pawnIoDriverStatus(PawnIoVersionQuery::LoadedDriver));
}
std::filesystem::path preparePawnIoInstaller(std::stop_token stop) {
    cancel(stop);
    const auto installer = windows::embeddedRuntimeDirectory("Installers") / "PawnIO_setup.exe";
    cancel(stop);
    const ReadLock lock(installer); verify(installer, lock.handle);
    cancel(stop);
    return installer;
}
static void launchInstaller(const std::filesystem::path& installer,std::function<void(unsigned)> onExit,const char* expected,std::uintmax_t expectedBytes) {
    // Recheck under a read lock immediately before launch. The interactive wizard
    // owns EULA, install choices and completion; a launched process is not proof
    // that the driver was installed. Never invoke unrestricted/silent switches.
    const ReadLock lock(installer); verify(installer, lock.handle, false,expected,expectedBytes);
    SHELLEXECUTEINFOW info{sizeof(info)};
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    info.lpVerb = L"runas"; info.lpFile = installer.c_str();
    const auto directory = installer.parent_path(); info.lpDirectory = directory.c_str();
    info.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&info)) {
        if (GetLastError() == ERROR_CANCELLED) throw std::runtime_error("Запуск установщика драйвера отменён");
        throw std::runtime_error("Не удалось запустить установщик драйвера: " + std::to_string(GetLastError()));
    }
    if(info.hProcess&&onExit){std::thread([process=info.hProcess,callback=std::move(onExit)]{
        const auto wait=WaitForSingleObject(process,INFINITE);DWORD code=ERROR_GEN_FAILURE;
        if(wait==WAIT_OBJECT_0)GetExitCodeProcess(process,&code);CloseHandle(process);
        try{callback(code);}catch(...){}
    }).detach();}
    else {if(info.hProcess)CloseHandle(info.hProcess);if(onExit)onExit(ERROR_INVALID_HANDLE);}
}
void launchPawnIoInstaller(const std::filesystem::path& installer,std::function<void(unsigned)> onExit){launchInstaller(installer,std::move(onExit),digest,bytes);}
namespace {constexpr char energyDigest[]="884163897dd3a8315f1984aa14862421f59e96e9ab7e11bb088124c7aa00989f";constexpr std::uintmax_t energyBytes=798976;
void energyAdmission(){if(deviceProfile(readBoardDescriptor()).id!="lenovo-legion-kwcn")throw std::runtime_error("Lenovo Energy: фактическое устройство не поддерживается");
    if(lenovoEnergyDriverStatus().installed)throw std::runtime_error("Lenovo Energy уже установлен; замена запрещена");}}
std::filesystem::path prepareLenovoEnergyInstaller(std::stop_token stop){cancel(stop);energyAdmission();const auto installer=windows::embeddedRuntimeDirectory("LenovoEnergy")/"LenovoEnergySetup.exe";
    const ReadLock lock(installer);verify(installer,lock.handle,true,energyDigest,energyBytes);cancel(stop);return installer;}
void launchLenovoEnergyInstaller(const std::filesystem::path& installer,std::function<void(unsigned)> onExit){energyAdmission();launchInstaller(installer,std::move(onExit),energyDigest,energyBytes);}
}
