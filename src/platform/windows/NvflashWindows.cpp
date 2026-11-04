#include "../common/NvflashPlatform.h"
#include "../common/NvflashResiduePolicy.h"
#include "../common/FirmwareEvidence.h"
#include "EmbeddedRuntimeWindows.h"
#include "BinaryHashWindows.h"
#include <windows.h>
#include <wintrust.h>
#include <softpub.h>
#include <array>
#include <fstream>
#include <vector>
#include <stdexcept>
#include <cwctype>
#include <tlhelp32.h>
#include <algorithm>
#include <atomic>
namespace mc::diagnostics::nvflash {
namespace {
namespace fs=std::filesystem;
std::string q(std::string_view s){return firmwareQuote(s);}
std::string utf8(std::wstring_view s){const auto n=WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);std::string out(n,0);if(n)WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),out.data(),n,nullptr,nullptr);return out;}
LONG signature(const fs::path& p){
    WINTRUST_FILE_INFO f{sizeof(f)};f.pcwszFilePath=p.c_str();WINTRUST_DATA d{sizeof(d)};d.dwUIChoice=WTD_UI_NONE;d.fdwRevocationChecks=WTD_REVOKE_NONE;d.dwUnionChoice=WTD_CHOICE_FILE;d.pFile=&f;d.dwProvFlags=WTD_CACHE_ONLY_URL_RETRIEVAL;d.dwStateAction=WTD_STATEACTION_VERIFY;GUID id=WINTRUST_ACTION_GENERIC_VERIFY_V2;
    const auto result=WinVerifyTrust(nullptr,&id,&d);d.dwStateAction=WTD_STATEACTION_CLOSE;WinVerifyTrust(nullptr,&id,&d);return result;
}
struct ServiceHandle { SC_HANDLE value{};~ServiceHandle(){if(value)CloseServiceHandle(value);} };
std::wstring lower(std::wstring value){for(auto& c:value)c=static_cast<wchar_t>(towlower(c));return value;}
constexpr std::array driverHashes{
    "79050454d06ab7b6607bca09386f704df021c3c367cc0743de6845301bb192ac",
    "8c2d9847b70c3dc1aac5d9e5330c67deb86f369dcd3ca85845adf2a001088dde"};
std::atomic_bool pinnedReferencesTrusted{};
static_assert(SERVICE_STOPPED==residueStopped&&SERVICE_KERNEL_DRIVER==residueKernelDriver&&SERVICE_DEMAND_START==residueDemandStart);
bool driverName(std::wstring_view name){return knownResidueSys(utf8(name));}
bool serviceName(std::wstring_view name){return knownResidueService(utf8(name));}
std::array<fs::path,2> driverFolders(){
    wchar_t system[MAX_PATH]{},temp[MAX_PATH]{};
    const DWORD a=GetSystemDirectoryW(system,MAX_PATH),b=GetTempPathW(MAX_PATH,temp);
    if(!a||a>=MAX_PATH||!b||b>=MAX_PATH)throw std::runtime_error("Cannot inspect NVFlash driver paths");
    auto temporary=fs::path(temp).lexically_normal();
    if(temporary.filename().empty())temporary=temporary.parent_path();
    return {fs::path(system)/"drivers",temporary};
}
fs::path driverPath(std::wstring value){
    if(value.size()>1&&value.front()==L'"'&&value.back()==L'"')value=value.substr(1,value.size()-2);
    if(value.starts_with(L"\\??\\"))value.erase(0,4);
    if(value.starts_with(L"\\\\?\\"))value.erase(0,4);
    if(lower(value).starts_with(L"\\systemroot\\"))value=L"%SystemRoot%\\"+value.substr(12);
    std::vector<wchar_t> expanded(32768);
    const auto n=ExpandEnvironmentStringsW(value.c_str(),expanded.data(),static_cast<DWORD>(expanded.size()));
    if(!n||n>expanded.size())throw std::runtime_error("Invalid NVFlash driver image path");
    fs::path path(expanded.data());path=path.lexically_normal();
    const auto folders=driverFolders();
    if(!path.is_absolute()||!driverName(lower(path.filename().wstring()))||
       std::none_of(folders.begin(),folders.end(),[&](const auto& f){return lower(path.parent_path().wstring())==lower(f.wstring());}))
        throw std::runtime_error("NVFlash service image is outside the reviewed driver paths; unchanged");
    return path;
}
void processGuard(const Event& event){
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(snapshot==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot inspect active NVFlash processes");
    struct Close{HANDLE h;~Close(){CloseHandle(h);}} close{snapshot};
    PROCESSENTRY32W process{sizeof(process)};
    if(!Process32FirstW(snapshot,&process))throw std::runtime_error("Cannot enumerate active NVFlash processes");
    do{
        const auto name=lower(process.szExeFile);
        if(name.find(L"nvflash")!=std::wstring::npos||name.find(L"nvflsh")!=std::wstring::npos){
            event("reader_process_collision","{\"pid\":"+std::to_string(process.th32ProcessID)+",\"image\":"+q(utf8(name))+",\"cleanup_requested\":false}");
            throw std::runtime_error("Уже работает процесс NVFlash; новое чтение не начато. Процесс не завершался.");
        }
    }while(Process32NextW(snapshot,&process));
    if(GetLastError()!=ERROR_NO_MORE_FILES)throw std::runtime_error("Incomplete NVFlash process enumeration");
}
// Only inspect files here. Never delete/overwrite a SYS or take ownership of a
// running driver. Vendor commands retain their own install/start/stop lifecycle.
std::string driverFile(const fs::path& path,const Event& event){
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(file==INVALID_HANDLE_VALUE){
        if(GetLastError()==ERROR_FILE_NOT_FOUND){event("driver_image_absent","{\"image\":"+q(utf8(path.wstring()))+"}");return "missing";}
        throw std::runtime_error("Cannot verify existing NVFlash SYS; unchanged");
    }
    struct Close{HANDLE h;~Close(){CloseHandle(h);}} close{file};
    BY_HANDLE_FILE_INFORMATION info{};LARGE_INTEGER size{};
    if(!GetFileInformationByHandle(file,&info)||!GetFileSizeEx(file,&size)||
       (info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))||info.nNumberOfLinks!=1||size.QuadPart<=0||size.QuadPart>1024*1024)
        throw std::runtime_error("Existing NVFlash SYS identity is not trusted; unchanged");
    std::vector<wchar_t> final(32768);const auto n=GetFinalPathNameByHandleW(file,final.data(),static_cast<DWORD>(final.size()),FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
    if(!n||n>=final.size()||lower(driverPath(std::wstring(final.data(),n)).wstring())!=lower(path.wstring()))
        throw std::runtime_error("Existing NVFlash SYS path changed; unchanged");
    std::vector<char> data(static_cast<std::size_t>(size.QuadPart));DWORD read{};
    if(!ReadFile(file,data.data(),static_cast<DWORD>(data.size()),&read,nullptr)||read!=data.size())
        throw std::runtime_error("Cannot read existing NVFlash SYS identity; unchanged");
    const auto hash=windows::detail::binarySha256(data);
    const bool pinned=std::find(driverHashes.begin(),driverHashes.end(),hash)!=driverHashes.end();
    // The locked SYS bytes must match a reference already signature-verified
    // by this capture. Do not call trust providers again after a GPU ROM read.
    const auto trust=pinned&&pinnedReferencesTrusted.load()?ERROR_SUCCESS:signature(path);
    const auto evidence="{\"image\":"+q(utf8(path.wstring()))+",\"sha256\":"+q(hash)+",\"winverifytrust\":"+std::to_string(trust)+"}";
    event("driver_image_observed",evidence);
    if(std::find(driverHashes.begin(),driverHashes.end(),hash)==driverHashes.end()||trust!=ERROR_SUCCESS)
        throw std::runtime_error("Existing NVFlash SYS differs from the pinned signed reader driver; unchanged");
    event("driver_image_verified",evidence);
    return hash;
}
struct DriverConfig {DWORD type{},start{};std::wstring image;};
DriverConfig driverConfig(SC_HANDLE service){
    DWORD bytes{};QueryServiceConfigW(service,nullptr,0,&bytes);
    if(GetLastError()!=ERROR_INSUFFICIENT_BUFFER||!bytes)throw std::runtime_error("Cannot inspect NVFlash service configuration; unchanged");
    std::vector<BYTE> buffer(bytes);
    if(!QueryServiceConfigW(service,reinterpret_cast<QUERY_SERVICE_CONFIGW*>(buffer.data()),bytes,&bytes))
        throw std::runtime_error("Cannot read NVFlash service configuration; unchanged");
    const auto* c=reinterpret_cast<const QUERY_SERVICE_CONFIGW*>(buffer.data());
    return {c->dwServiceType,c->dwStartType,c->lpBinaryPathName};
}
void driverGuard(const Event& event){
    event("nvflash_guard_started","{}");processGuard(event);
    event("nvflash_service_scan_started","{}");
    ServiceHandle manager{OpenSCManagerW(nullptr,nullptr,SC_MANAGER_ENUMERATE_SERVICE|SC_MANAGER_CONNECT)};
    if(!manager.value)throw std::runtime_error("Cannot inspect NVFlash driver services");
    DWORD bytes{},count{},resume{};
    const auto empty=EnumServicesStatusExW(manager.value,SC_ENUM_PROCESS_INFO,SERVICE_DRIVER,SERVICE_STATE_ALL,nullptr,0,&bytes,&count,&resume,nullptr);
    if(!empty&&GetLastError()!=ERROR_MORE_DATA)throw std::runtime_error("Cannot enumerate existing driver services");
    std::vector<BYTE> buffer(bytes);resume=0;
    if(bytes&&!EnumServicesStatusExW(manager.value,SC_ENUM_PROCESS_INFO,SERVICE_DRIVER,SERVICE_STATE_ALL,buffer.data(),bytes,&bytes,&count,&resume,nullptr))
        throw std::runtime_error("Incomplete driver-service enumeration");
    const auto* services=reinterpret_cast<const ENUM_SERVICE_STATUS_PROCESSW*>(buffer.data());
    for(DWORD i=0;i<count;++i){
        const auto name=lower(services[i].lpServiceName);
        if(name.find(L"nvflash")==std::wstring::npos&&name.find(L"nvflsh")==std::wstring::npos)continue;
        const auto state=services[i].ServiceStatusProcess.dwCurrentState;
        event("driver_collision","{\"service\":"+q(utf8(name))+",\"state\":"+std::to_string(state)+",\"reader_started\":false}");
        if(state!=SERVICE_STOPPED)throw std::runtime_error("Служба NVFlash ещё активна. Новое чтение не начато; служба не останавливалась.");
        if(!serviceName(name))throw std::runtime_error("Unknown NVFlash service name; stopped registration unchanged");
        ServiceHandle service{OpenServiceW(manager.value,name.c_str(),SERVICE_QUERY_CONFIG|SERVICE_QUERY_STATUS|DELETE)};
        if(!service.value)throw std::runtime_error("Cannot inspect/remove stopped NVFlash registration; unchanged");
        const auto config=driverConfig(service.value);const auto path=driverPath(config.image);
        if(!residueConfiguration(config.type,config.start))
            throw std::runtime_error("Unexpected NVFlash driver service configuration; unchanged");
        const auto identity=driverFile(path,event);
        // Recheck both status and configuration immediately before removing an
        // inert, verified registration. Never send SERVICE_CONTROL_STOP.
        processGuard(event);SERVICE_STATUS_PROCESS status{};DWORD used{};
        const auto again=driverConfig(service.value);
        if(!QueryServiceStatusEx(service.value,SC_STATUS_PROCESS_INFO,reinterpret_cast<BYTE*>(&status),sizeof(status),&used))
            throw std::runtime_error("Cannot recheck NVFlash service status; unchanged");
        const auto label=utf8(name);
        const ResidueProof proof{label,status.dwCurrentState,again.type,again.start,true,true,
            again.type==config.type&&again.start==config.start&&again.image==config.image,
            identity=="missing"?ResidueImage::Missing:ResidueImage::PinnedSigned};
        if(!mayRemoveStoppedResidue(proof))
            throw std::runtime_error("NVFlash service changed during verification; unchanged");
        event("driver_residue_cleanup_requested","{\"service\":"+q(utf8(name))+",\"state\":1,\"image\":"+q(utf8(path.wstring()))+",\"image_identity\":"+q(identity)+",\"registration_only\":true,\"sys_deleted\":false}");
        if(!DeleteService(service.value))throw std::runtime_error("Cannot remove stopped NVFlash registration: Win32="+std::to_string(GetLastError()));
        CloseServiceHandle(service.value);service.value=nullptr;
        const auto deadline=GetTickCount64()+3000;
        for(;;){
            ServiceHandle remaining{OpenServiceW(manager.value,name.c_str(),SERVICE_QUERY_STATUS)};
            if(!remaining.value){
                const auto error=GetLastError();
                if(error==ERROR_SERVICE_DOES_NOT_EXIST)break;
                if(error!=ERROR_SERVICE_MARKED_FOR_DELETE)throw std::runtime_error("Cannot confirm NVFlash registration removal: Win32="+std::to_string(error));
            }
            if(GetTickCount64()>=deadline)throw std::runtime_error("NVFlash registration deletion is pending; retry after its handles close");
            Sleep(25);
        }
        event("driver_residue_removed","{\"service\":"+q(utf8(name))+",\"registration_only\":true,\"sys_deleted\":false}");
    }
    // A pinned, signed orphan file is inert and may be replaced by NVFlash's
    // own extraction. An unknown file still blocks the reader; no file cleanup.
    for(const auto& folder:driverFolders())for(const auto* name:{L"nvflash.sys",L"nvflash64.sys",L"nvflsh64.sys"}){
        const auto path=folder/name;const auto attr=GetFileAttributesW(path.c_str());
        if(attr!=INVALID_FILE_ATTRIBUTES)static_cast<void>(driverFile(driverPath(path.wstring()),event));
        else if(GetLastError()!=ERROR_FILE_NOT_FOUND&&GetLastError()!=ERROR_PATH_NOT_FOUND)throw std::runtime_error("Cannot inspect NVFlash orphan SYS");
    }
    event("driver_preflight","{\"active_nvflash_service\":false,\"stopped_residues_handled\":true,\"sys_deleted\":false,\"driver_install_owned_by_vendor\":true,\"display_driver_disable_requested\":false}");
}
}
void verifyNvflashIdle(const Event& event){driverGuard(event);}
std::shared_ptr<void> tryTelemetryLease(){
    HANDLE h=CreateSemaphoreW(nullptr,1,1,L"Global\\MagicControl.NvflashCapture.v1");
    if(!h)return {};
    if(WaitForSingleObject(h,0)!=WAIT_OBJECT_0){CloseHandle(h);return {};}
    return std::shared_ptr<void>(h,[](void* value){ReleaseSemaphore(value,1,nullptr);CloseHandle(value);});
}
void atomicText(const fs::path& p,const std::string& text){
    auto temp=p;temp+=L".tmp";{std::ofstream f(temp,std::ios::binary|std::ios::trunc);f.write(text.data(),static_cast<std::streamsize>(text.size()));f.flush();if(!f)throw std::runtime_error("Cannot write VBIOS metadata");}
    if(!MoveFileExW(temp.c_str(),p.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot commit VBIOS metadata: Win32="+std::to_string(GetLastError()));
}
std::shared_ptr<void> preflight(const fs::path& artifacts,const Event& event){
    HANDLE token{};TOKEN_ELEVATION elevation{};DWORD used{};
    if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token))throw std::runtime_error("Cannot inspect process elevation");
    const bool read=GetTokenInformation(token,TokenElevation,&elevation,sizeof(elevation),&used)!=FALSE;CloseHandle(token);
    event("privilege_preflight","{\"elevated\":"+std::string(read&&elevation.TokenIsElevated?"true":"false")+"}");
    if(!read||!elevation.TokenIsElevated)throw std::runtime_error("NVFlash requires an elevated Windows session; no UAC prompt is launched");
    HANDLE semaphore=CreateSemaphoreW(nullptr,1,1,L"Global\\MagicControl.NvflashCapture.v1");if(!semaphore)throw std::runtime_error("Cannot create NVFlash capture lease");
    const auto deadline=GetTickCount64()+8000;
    for(;;){const auto wait=WaitForSingleObject(semaphore,100);if(wait==WAIT_OBJECT_0)break;
        if(wait!=WAIT_TIMEOUT||GetTickCount64()>=deadline){CloseHandle(semaphore);throw std::runtime_error("NVIDIA driver access busy/cancelled; reader not started");}}
    std::shared_ptr<void> lease(semaphore,[](void* h){ReleaseSemaphore(h,1,nullptr);CloseHandle(h);});
    const auto folder=windows::embeddedRuntimeDirectory("NvFlash");
    struct Pin{const char* name;const char* sha;bool signedDriver;};
    constexpr std::array pins{
        Pin{"nvflash64.exe","c0620a7be0ffdad3782d9ddf238d1a19c31b587cbcfee7036819c8a50e2d89cd",false},
        Pin{"driver-1.reference.bin",driverHashes[0],true},
        Pin{"driver-2.reference.bin",driverHashes[1],true}};
    std::string files="[";
    for(const auto& pin:pins){
        const auto path=folder/pin.name;std::ifstream f(path,std::ios::binary);std::vector<char> data((std::istreambuf_iterator<char>(f)),{});
        if(!f||windows::detail::binarySha256(data)!=pin.sha)throw std::runtime_error("NVFlash protected runtime SHA256 mismatch");
        const auto trust=signature(path);if(pin.signedDriver&&trust!=ERROR_SUCCESS)throw std::runtime_error("NVFlash reference driver signature is not trusted; no policy bypass");
        if(files.size()>1)files+=',';files+="{\"name\":"+q(pin.name)+",\"bytes\":"+std::to_string(data.size())+",\"sha256\":"+q(pin.sha)+",\"winverifytrust\":"+std::to_string(trust)+",\"signed_driver\":"+(pin.signedDriver?"true":"false")+"}";
    }
    const auto metadata="{\"version\":\"5.867.0\",\"driver_version\":\"1.21.0\",\"exe_signed\":false,\"source_kind\":\"standard distribution mirrored by ComputerBase; x64 EXE also byte-matched to fixed GitHub reference\",\"source\":\"https://www.computerbase.de/downloads/systemtools/grafikkarten/nvflash/\",\"protected_cache\":"+q(utf8(folder.wstring()))+",\"files\":"+files+"]}";
    pinnedReferencesTrusted.store(true);
    atomicText(artifacts/"tool.json",metadata+"\n");event("tool_verified",metadata);driverGuard(event);return lease;
}
}
