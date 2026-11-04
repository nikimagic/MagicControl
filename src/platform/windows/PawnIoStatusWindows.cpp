#include "MagicControl/PawnIoInstall.h"
#include "FileVersionWindows.h"
#include <windows.h>
#include <winioctl.h>
#include <vector>
#include <stdexcept>

// One version inspector for startup/INFO/catalogue/dumps. File-only callers
// remain passive. Explicit live queries use only the driver's version command.
namespace mc::dependencies {
namespace {
void loadedVersion(PawnIoDriverStatus& result){
    if(!result.running){result.loadedVersionStatus="Not queried: service is not running";return;}
    // Official PawnIO 2.1/2.2 pawnio_um.h and driver.cpp: FILE_ANY_ACCESS,
    // no input, exactly one ULONG output, major<<16 | minor<<8 | patch.
    // Do NOT use pawnio_version() from PawnIOLib (hardcoded library ABI 2.0.0).
    constexpr DWORD ioctlVersion=CTL_CODE(41394,0x861,METHOD_BUFFERED,FILE_ANY_ACCESS);
    const auto device=CreateFileW(L"\\\\?\\GLOBALROOT\\Device\\PawnIO",GENERIC_READ,
        FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(device==INVALID_HANDLE_VALUE){result.loadedVersionStatus="Version device open Win32="+std::to_string(GetLastError());return;}
    struct Close{HANDLE value;~Close(){CloseHandle(value);}} close{device};
    DWORD packed{},returned{};
    if(!DeviceIoControl(device,ioctlVersion,nullptr,0,&packed,sizeof(packed),&returned,nullptr)){
        result.loadedVersionStatus="IOCTL_PIO_VERSION Win32="+std::to_string(GetLastError());return;
    }
    if(returned!=sizeof(packed)||!packed||packed==0xffffffffu){
        result.loadedVersionStatus="IOCTL_PIO_VERSION returned invalid size/value";return;
    }
    result.loadedVersionRaw=packed;
    result.loadedVersion=std::to_string(packed>>16)+"."+std::to_string((packed>>8)&0xff)+"."+std::to_string(packed&0xff);
    result.loadedVersionStatus="Loaded kernel driver replied; no Pawn module or hardware IO";
}
std::wstring imagePath(std::wstring path){
    if(path.size()>1&&path.front()==L'"'&&path.back()==L'"')path=path.substr(1,path.size()-2);
    wchar_t windows[32768]{};const auto n=GetWindowsDirectoryW(windows,32768);
    if(!n||n>=32768)throw std::runtime_error("Windows directory unavailable");
    if(path.starts_with(L"\\??\\"))path.erase(0,4);
    if(!_wcsnicmp(path.c_str(),L"\\SystemRoot\\",12))path=std::wstring(windows)+L"\\"+path.substr(12);
    else if(!_wcsnicmp(path.c_str(),L"System32\\",9))path=std::wstring(windows)+L"\\"+path;
    const auto bytes=ExpandEnvironmentStringsW(path.c_str(),nullptr,0);
    if(!bytes||bytes>32768)throw std::runtime_error("Invalid driver image path");
    std::wstring expanded(bytes,L'\0');if(!ExpandEnvironmentStringsW(path.c_str(),expanded.data(),bytes))throw std::runtime_error("Cannot expand driver image path");
    expanded.resize(bytes-1);if(!std::filesystem::path(expanded).is_absolute())throw std::runtime_error("Driver image path is not absolute");return expanded;
}
}
static PawnIoDriverStatus driverStatus(const wchar_t* serviceName,PawnIoVersionQuery query,bool pawn) {
    struct ServiceHandle {
        SC_HANDLE value{};
        ~ServiceHandle() { if (value) CloseServiceHandle(value); }
    };
    const ServiceHandle manager{OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT)};
    if (!manager.value) throw std::runtime_error("Не удалось открыть диспетчер драйверов: " + std::to_string(GetLastError()));
    const ServiceHandle service{OpenServiceW(manager.value, serviceName, SERVICE_QUERY_CONFIG | SERVICE_QUERY_STATUS)};
    if (!service.value) {
        const DWORD error = GetLastError();
        if (error == ERROR_SERVICE_DOES_NOT_EXIST) return {};
        throw std::runtime_error("Не удалось проверить службу драйвера: " + std::to_string(error));
    }
    DWORD required{};
    QueryServiceConfigW(service.value, nullptr, 0, &required);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || required > 64 * 1024)
        throw std::runtime_error("Не удалось прочитать конфигурацию драйвера");
    std::vector<BYTE> storage(required);
    const auto config = reinterpret_cast<QUERY_SERVICE_CONFIGW*>(storage.data());
    if (!QueryServiceConfigW(service.value, config, required, &required))
        throw std::runtime_error("Не удалось прочитать конфигурацию драйвера: " + std::to_string(GetLastError()));
    if (!(config->dwServiceType & SERVICE_KERNEL_DRIVER))
        throw std::runtime_error("Служба не является драйвером; установка отменена");
    SERVICE_STATUS_PROCESS status{};
    if (!QueryServiceStatusEx(service.value, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&status), sizeof(status), &required))
        throw std::runtime_error("Не удалось проверить состояние службы драйвера: " + std::to_string(GetLastError()));
    PawnIoDriverStatus result{true, status.dwCurrentState == SERVICE_RUNNING, status.dwCurrentState,
            status.dwWin32ExitCode, status.dwServiceSpecificExitCode,
            config->dwServiceType, config->dwStartType, config->lpBinaryPathName ? config->lpBinaryPathName : L""};
    try{
        result.resolvedBinaryPath=imagePath(result.binaryPath);
        const auto version=windows::readFileVersionResource(result.resolvedBinaryPath);
        result.fileVersion=version.text();result.fileVersionNumbers=version.numbers;
        result.versionStatus="Installed SYS VERSIONINFO; not an in-memory driver version query";
    }catch(const std::exception& error){result.versionStatus=error.what();}
    if(pawn&&query==PawnIoVersionQuery::LoadedDriver)loadedVersion(result);
    else result.loadedVersionStatus="Not queried: file metadata only";
    return result;
}
PawnIoDriverStatus pawnIoDriverStatus(PawnIoVersionQuery query){return driverStatus(L"PawnIO",query,true);}
PawnIoDriverStatus lenovoEnergyDriverStatus(){return driverStatus(L"ACPIVPC",PawnIoVersionQuery::FileMetadataOnly,false);}
bool pawnIoInstalled() { return pawnIoDriverStatus().installed; }
}
