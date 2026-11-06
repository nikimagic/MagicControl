// Explicit vendor READ/SAVE actions only. Original Intel/AMI bytes are embedded in the
// Windows EXE and extracted into the shared protected runtime cache offline.
#include "MagicControl/BiosRomDump.h"
#include "MagicControl/AsyncReaper.h"
#include "MagicControl/Logging.h"
#include "MagicControl/OperationMessages.h"
#include "MagicControl/PortableSettings.h"
#include "MagicControl/Version.h"
#include "MagicControl/DeviceProfile.h"
#include "MagicControl/EcAccess.h"
#include "../common/FirmwareEvidence.h"
#include "EmbeddedRuntimeWindows.h"
#include "BinaryHashWindows.h"
#include "WindowsDevices.h"
#include "../common/BiosImageValidation.h"
#include "../common/InsydeBiosRead.h"
#include "../common/NvflashPlatform.h"
#include "../common/NvflashResiduePolicy.h"
#include "uniwill-acpi/ReviewedDriverWindows.h"
#include <windows.h>
#include <tlhelp32.h>
#include <wintrust.h>
#include <softpub.h>
#include <array>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <format>
#include <fstream>
#include <optional>
#include <memory>
#include <sstream>
#include <thread>

namespace mc {
namespace {
namespace fs=std::filesystem;
using Clock=std::chrono::steady_clock;
constexpr char archiveUrl[]="https://f.hubspotusercontent10.net/hubfs/9443417/Support/BIOS_Firmware_Update/Aptio_V_AMI_Firmware_Update_Utility.zip";
constexpr char archiveHash[]="b59f141c37d5f55179459f6e07f0d1ed81fa742d1998b50954f1f9d6f3955307";
constexpr char exeHash[]="535633b1c097581f34d31c3e2b9c594779c6e88dfd52cf3c02ac72c1b3e6649b";
constexpr char sysHash[]="4892914632d4a8c327e6963b8a841266d6cdc466af03603b99d486799d947b64";
struct VendorTool {
    bool intel{};
    const char *group, *exe, *driver, *version, *source, *archiveSha, *exeSha, *driverSha, *command;
    bool nvidia{},insyde{};
};
constexpr VendorTool afu{false,"AmiAfu","AFUWINx64.exe","amigendrv64.sys","5.17.00.0153",archiveUrl,archiveHash,exeHash,sysHash,"/O"};
constexpr VendorTool fpt{true,"IntelFpt","FPTW64.exe","driver-reference.bin","16.1.27.2181",
    "https://download.gigabyte.com/FileList/BIOS/nb-bios-g5_g7-vf-win11-64bit-ddr4-bios-fb10.zip",
    "a8266663675b4580b2e2d7f148c6197a65ef85efc7a8dc62462227332514b170",
    "41fa119d68f15dc567df37d9513f433507680a6cdea200ac0cdff9cc88c68aae",
    "33abb3e6d36a101c89096d8d30173c79a9efe84f7780fc701fab35e398ac58b2","-d"};
constexpr VendorTool fpt19{true,"IntelFpt19","FPTW64.exe","driver-reference.bin","19.0.5.1948",
    "https://download.colorful.cn/iGameCenter/iGameCenter-2.0.0.82-2026.09.12-Installer_Prod.exe",
    "ce7f0b697dbc64662541838aed2d914508bb085db4b8e6186749b0dd4a4f8c53",
    "88491119862830235854cd29b9857de21870ac4dc16f74544b1a665f64d62807",
    "acd5d366a73e9d60c654f9b6d76397d0439606939d6b0c7ee24c4f397452899a","-d"};
constexpr VendorTool h2offt{false,"InsydeH2offt","H2OFFT-Wx64.exe","H2OFFT64.sys","6.76",
    "https://download.lenovo.com/consumer/mobiles/kscn43ww.exe",
    "0a9df4f3eb9a6fb1ac44f1ed553dd85919147759e953954665cc08bf80b4c9d1",
    "046514122b241581a61f7c3fda9e5d444b76de7f919efc89ab3ad2eb5d3a7de5",
    "adac8f1ed5e46a8bcbdeb54e6ee08bf3695c65f2ab5e2b31cf1d438babf54463","-g",false,true};
const char* readerName(const VendorTool& spec){return spec.insyde?"OEM Insyde H2OFFT -g":spec.intel?"Official Intel FPT -d":"Official AMI AFU /O";}
bool h2oAcpi(){
    constexpr DWORD provider=0x41435049,table=0x49464555; // ACPI, UEFI
    const auto size=GetSystemFirmwareTable(provider,table,nullptr,0);
    if(size<36||size>1024*1024)return false;
    std::vector<char> data(size);if(GetSystemFirmwareTable(provider,table,data.data(),size)!=size)return false;
    return diagnostics::insyde::acpiEvidence(data);
}
bool usesFpt19(const VendorTool& spec){return std::string_view(spec.group)=="IntelFpt19";}
constexpr wchar_t biosKey[]=L"HARDWARE\\DESCRIPTION\\System\\BIOS";
std::string utf8(const std::wstring& w){const auto n=WideCharToMultiByte(CP_UTF8,0,w.data(),static_cast<int>(w.size()),nullptr,0,nullptr,nullptr);std::string s(n,0);if(n)WideCharToMultiByte(CP_UTF8,0,w.data(),static_cast<int>(w.size()),s.data(),n,nullptr,nullptr);return s;}
std::string pathText(const fs::path& p){return utf8(p.wstring());}
std::string q(std::string_view s){return diagnostics::firmwareQuote(s);}
std::string stamp(){return std::format("{:%FT%TZ}",std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));}
std::string reg(const wchar_t* key){return utf8(windows::reviewed_uniwill::registry(HKEY_LOCAL_MACHINE,biosKey,key));}
bool ami(std::string value){std::string normalized;for(unsigned char c:value)if(std::isalnum(c))normalized+=static_cast<char>(std::toupper(c));return normalized.starts_with("AMERICANMEGATRENDS")||normalized=="AMI";}
void cancel(std::stop_token token){if(token.stop_requested())throw std::runtime_error("Чтение BIOS отменено; собранные файлы сохранены");}
struct Handle{HANDLE h{};~Handle(){if(h&&h!=INVALID_HANDLE_VALUE)CloseHandle(h);}HANDLE release(){const auto v=h;h=nullptr;return v;}};
void atomicText(const fs::path& p,const std::string& value){
    auto tmp=p;tmp+=L".tmp";
    {std::ofstream f(tmp,std::ios::binary|std::ios::trunc);f.write(value.data(),static_cast<std::streamsize>(value.size()));f.flush();if(!f)throw std::runtime_error("Не удалось сохранить BIOS metadata: "+pathText(p));}
    if(!MoveFileExW(tmp.c_str(),p.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Не удалось зафиксировать BIOS metadata: Win32="+std::to_string(GetLastError()));
}
std::uint64_t declaredSize(){
    const DWORD size=GetSystemFirmwareTable(0x52534D42,0,nullptr,0);if(size<8||size>4*1024*1024)return 0;
    std::vector<unsigned char> b(size);if(GetSystemFirmwareTable(0x52534D42,0,b.data(),size)!=size)return 0;
    for(std::size_t at=8;at+4<=b.size();){const auto type=b[at],length=b[at+1];if(length<4||at+length>b.size())return 0;
        if(type==0&&length>=10){if(b[at+9]!=255)return (std::uint64_t(b[at+9])+1)*65536;
            if(length<26)return 0;const unsigned v=b[at+24]|(b[at+25]<<8),unit=v>>14;return unit<=1?std::uint64_t(v&0x3fff)*(unit?1024ull*1024*1024:1024ull*1024):0;}
        at+=length;while(at+1<b.size()&&(b[at]||b[at+1]))++at;if(at+1>=b.size())return 0;at+=2;
    }return 0;
}
std::vector<char> bytes(const fs::path& p,std::uint64_t limit=64*1024*1024){
    const auto size=fs::file_size(p);if(size>limit)throw std::runtime_error("BIOS/tool file exceeds budget");
    std::vector<char> b(static_cast<std::size_t>(size));std::ifstream f(p,std::ios::binary);f.read(b.data(),static_cast<std::streamsize>(b.size()));if(!f)throw std::runtime_error("Cannot read "+pathText(p));return b;
}
LONG signature(const fs::path& p){
    WINTRUST_FILE_INFO file{sizeof(file)};file.pcwszFilePath=p.c_str();WINTRUST_DATA data{sizeof(data)};
    data.dwUIChoice=WTD_UI_NONE;data.fdwRevocationChecks=WTD_REVOKE_NONE;data.dwUnionChoice=WTD_CHOICE_FILE;data.pFile=&file;
    data.dwProvFlags=WTD_CACHE_ONLY_URL_RETRIEVAL;data.dwStateAction=WTD_STATEACTION_VERIFY;
    GUID action=WINTRUST_ACTION_GENERIC_VERIFY_V2;const auto status=WinVerifyTrust(nullptr,&action,&data);
    data.dwStateAction=WTD_STATEACTION_CLOSE;WinVerifyTrust(nullptr,&action,&data);return status;
}
std::string serviceState(){
    SC_HANDLE manager=OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT);if(!manager)return "{\"available\":false,\"win32\":"+std::to_string(GetLastError())+"}";
    std::string rows;bool available=true;for(const auto* name:{L"GENERICDRV",L"amifldrv",L"amigendrv",L"amigendrv64",L"WNPDriver",L"H2OFFT",L"iscflash",L"iscflashx64"}){
        SC_HANDLE service=OpenServiceW(manager,name,SERVICE_QUERY_STATUS|SERVICE_QUERY_CONFIG);
        if(!service){const auto code=GetLastError();if(code!=ERROR_SERVICE_DOES_NOT_EXIST){available=false;if(!rows.empty())rows+=',';rows+="{\"name\":"+q(utf8(name))+",\"win32\":"+std::to_string(code)+"}";}continue;}
        SERVICE_STATUS_PROCESS status{};DWORD used{};const bool ok=QueryServiceStatusEx(service,SC_STATUS_PROCESS_INFO,reinterpret_cast<BYTE*>(&status),sizeof(status),&used)!=FALSE;
        if(!ok)available=false;
        DWORD size{};QueryServiceConfigW(service,nullptr,0,&size);std::vector<BYTE> data(size);std::string image;
        if(size&&QueryServiceConfigW(service,reinterpret_cast<QUERY_SERVICE_CONFIGW*>(data.data()),size,&size))image=utf8(reinterpret_cast<QUERY_SERVICE_CONFIGW*>(data.data())->lpBinaryPathName);
        if(!rows.empty())rows+=',';rows+="{\"name\":"+q(utf8(name))+",\"state\":"+(ok?std::to_string(status.dwCurrentState):"null")+",\"image\":"+q(image)+"}";CloseServiceHandle(service);
    }CloseServiceHandle(manager);return "{\"available\":"+std::string(available?"true":"false")+",\"services\":["+rows+"]}";
}
std::wstring quoted(const std::wstring& text){std::wstring out=L"\"";unsigned n{};for(auto c:text){if(c==L'\\'){++n;continue;}out.append(c==L'"'?2*n+1:n,L'\\');n=0;out+=c;}out.append(2*n,L'\\');return out+L'"';}
struct EventLog{
    fs::path dir;std::ofstream stream;
    diagnostics::nvflash::Event external;
    explicit EventLog(fs::path p):dir(std::move(p)),stream(dir/"events.jsonl",std::ios::binary){if(!stream)throw std::runtime_error("Cannot create BIOS event log");}
    EventLog(fs::path p,diagnostics::nvflash::Event sink):dir(std::move(p)),external(std::move(sink)){}
    void add(const char* event,const std::string& detail="{}"){
        if(external){external(event,detail);return;}
        const auto row="{\"schema\":1,\"app_version\":"+q(MAGICCONTROL_VERSION_STRING)+",\"utc\":"+q(stamp())+",\"event\":"+q(event)+",\"details\":"+detail+"}";
        stream<<row<<'\n';stream.flush();if(!stream)throw std::runtime_error("Cannot flush BIOS event log");
        recordLog("bios_dump","\"event\":"+q(event)+",\"details\":"+detail);
    }
};
void guardDriver(const VendorTool& spec,EventLog& log){
    if(spec.insyde){
        Handle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0)};
        if(snapshot.h==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot check existing Insyde reader");
        PROCESSENTRY32W entry{sizeof(entry)};
        if(!Process32FirstW(snapshot.h,&entry))throw std::runtime_error("Cannot enumerate Insyde reader processes");
        do{if(_wcsicmp(entry.szExeFile,L"H2OFFT-Wx64.exe")==0)
            throw std::runtime_error("Insyde reader already running: PID="+std::to_string(entry.th32ProcessID));}while(Process32NextW(snapshot.h,&entry));
    }
    const auto services=serviceState();log.add("driver_preflight",services);
    if(services.find("\"available\":false")!=std::string::npos)throw std::runtime_error("Не удалось проверить занятость BIOS driver");
    // FPT removes a pre-existing WNPDriver service itself. Never let it take
    // ownership of another program's service, even if currently stopped.
    if(spec.intel&&services.find("\"name\":\"WNPDriver\"")!=std::string::npos)
        throw std::runtime_error("Служба WNPDriver уже существует; FPT не запускался");
    if(!spec.intel&&services.find("\"state\":4")!=std::string::npos)
        throw std::runtime_error("BIOS driver уже занят другой программой");
    if(spec.intel){
        wchar_t system[MAX_PATH]{},temp[MAX_PATH]{};
        const DWORD ns=GetSystemDirectoryW(system,MAX_PATH),nt=GetTempPathW(MAX_PATH,temp);
        if(!ns||ns>=MAX_PATH||!nt||nt>=MAX_PATH)throw std::runtime_error("Cannot inspect WNPDriver paths");
        for(const auto& path:{fs::path(system)/"drivers/WNPDriver.sys",fs::path(temp)/"WNPDriver.sys"})
            if(fs::exists(path))throw std::runtime_error("Остался сторонний WNPDriver: "+pathText(path)+"; FPT не запускался");
        if(usesFpt19(spec)){
            // Reviewed FUN140018370 falls back to the SystemTemp environment
            // variable (not GetTempPath) and may replace an existing SYS there.
            const auto length=GetEnvironmentVariableW(L"SystemTemp",nullptr,0);
            if(length){
                std::wstring value(length,L'\0');const auto copied=GetEnvironmentVariableW(L"SystemTemp",value.data(),length);
                if(!copied||copied>=length)throw std::runtime_error("Cannot inspect FPT19 SystemTemp");
                value.resize(copied);const fs::path directory(value);
                if(!directory.is_absolute())throw std::runtime_error("FPT19 SystemTemp must be an absolute path");
                const auto path=directory/"WNPDriver.sys";
                log.add("fpt19_systemtemp_guard","{\"path\":"+q(pathText(path))+"}");
                if(fs::exists(path))throw std::runtime_error("Сторонний WNPDriver в SystemTemp: "+pathText(path)+"; FPT19 не запускался");
            }else if(GetLastError()!=ERROR_ENVVAR_NOT_FOUND)throw std::runtime_error("Cannot query FPT19 SystemTemp");
        }
    }
}
fs::path tool(const VendorTool& spec,std::stop_token token,EventLog& log){
    cancel(token);
    const auto directory=windows::embeddedRuntimeDirectory(spec.group);
    const auto license=directory/"LICENSE.txt";
    if(fs::exists(license))fs::copy_file(license,log.dir/"AMI-software-license-agreement.txt",fs::copy_options::none);
    if(spec.intel)fs::copy_file(directory/"NOTICE.txt",log.dir/"Intel-FPT-NOTICE.txt",fs::copy_options::none);
    if(spec.insyde)fs::copy_file(directory/"NOTICE.txt",log.dir/"Insyde-H2OFFT-NOTICE.txt",fs::copy_options::none);
    fs::copy_file(directory/"PROVENANCE.json",log.dir/"tool-provenance.json",fs::copy_options::none);
    log.add("tool_verified","{\"version\":"+q(spec.version)+",\"storage\":\"embedded Windows resources\",\"network_required\":false,\"protected_cache\":"+q(pathText(directory))+"}");
    return directory;
}
enum class ReadCommand { Information, Save, NvCheck, NvEntire, NvCompare };
// H2OFFT has pre-INI error dialogs. SW_HIDE alone cannot hide those modal
// windows. Keep only this READ-only child on a private desktop, capture error
// text and request normal error-window close; never acknowledge a flash prompt.
struct InsydeDesktop {
    HDESK handle{};std::wstring path;
    InsydeDesktop(){
        const auto name=L"MagicControl.Insyde."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(GetTickCount64());
        wchar_t station[256]{};DWORD needed{};
        if(!GetUserObjectInformationW(GetProcessWindowStation(),UOI_NAME,station,sizeof(station),&needed))throw std::runtime_error("Cannot identify reader window station");
        handle=CreateDesktopW(name.c_str(),nullptr,nullptr,0,DESKTOP_CREATEWINDOW|DESKTOP_ENUMERATE|DESKTOP_READOBJECTS|DESKTOP_WRITEOBJECTS,nullptr);
        if(!handle)throw std::runtime_error("Cannot isolate Insyde error windows: Win32="+std::to_string(GetLastError()));
        path=std::wstring(station)+L"\\"+name;
    }
    ~InsydeDesktop(){if(handle)CloseDesktop(handle);}
    struct Probe {DWORD pid{};std::wstring text;};
    static BOOL CALLBACK text(HWND w,LPARAM data){
        auto& p=*reinterpret_cast<Probe*>(data);wchar_t value[2048]{};DWORD_PTR count{};
        SendMessageTimeoutW(w,WM_GETTEXT,2048,reinterpret_cast<LPARAM>(value),SMTO_ABORTIFHUNG,100,&count);
        if(*value&&p.text.size()<16384){p.text+=value;p.text+=L"\n";}return TRUE;
    }
    static BOOL CALLBACK inspect(HWND w,LPARAM data){
        auto& p=*reinterpret_cast<Probe*>(data);DWORD pid{};GetWindowThreadProcessId(w,&pid);
        if(pid!=p.pid||!IsWindowVisible(w))return TRUE;
        wchar_t kind[64]{};GetClassNameW(w,kind,64);if(std::wstring_view(kind)!=L"#32770")return TRUE;
        text(w,data);EnumChildWindows(w,text,data);
        // Only a visible modal from our fixed-g process. Closing rejects its
        // operation; no OK/Yes/confirmation is sent and no reader is killed.
        PostMessageW(w,WM_CLOSE,0,0);return TRUE;
    }
    std::string error(DWORD pid){
        Probe p{pid};SetLastError(ERROR_SUCCESS);
        const auto ok=EnumDesktopWindows(handle,inspect,reinterpret_cast<LPARAM>(&p));const auto code=GetLastError();
        // An empty desktop is not an error. Legacy enumeration also reports
        // ERROR_INVALID_HANDLE before a desktop window exists: independently
        // verify that our HDESK remains valid before accepting that case.
        if(!ok&&code!=ERROR_SUCCESS){
            wchar_t name[256]{};DWORD needed{};
            const bool empty=code==ERROR_INVALID_HANDLE&&p.text.empty()&&GetUserObjectInformationW(handle,UOI_NAME,name,sizeof(name),&needed);
            if(!empty)throw std::runtime_error("Cannot inspect isolated Insyde dialog: Win32="+std::to_string(code));
        }
        return utf8(p.text);
    }
};
DWORD runVendor(const VendorTool& spec,const fs::path& directory,const fs::path& output,ReadCommand action,
    unsigned pass,EventLog& log,std::stop_token token,std::atomic<std::uint64_t>* progress,std::uint64_t completedBytes,
    unsigned adapterIndex=0,const std::string& outputLabel={},const std::shared_ptr<void>& lease={},
    const std::shared_ptr<BiosCaptureSession>& biosSession={}){
    const bool info=action!=ReadCommand::Save&&action!=ReadCommand::NvEntire;
    if(!spec.nvidia&&action!=ReadCommand::Information&&action!=ReadCommand::Save)throw std::runtime_error("NVFlash read command cannot be used for another vendor tool");
    if(info&&!spec.intel&&!spec.nvidia)throw std::runtime_error("Information preflight is only defined for FPT/NVFlash");
    const auto label=outputLabel.empty()?(info?std::string("info"):"pass"+std::to_string(pass)):outputLabel;
    SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};
    // Direct inherited files avoid the former per-chunk pipe flush/backpressure.
    // Preserve vendor output in files. NVFlash separately needs a hidden real
    // console HWND; stdout/stderr redirection must not remove that console.
    Handle out{CreateFileW((log.dir/(label+".stdout.log")).c_str(),GENERIC_WRITE,FILE_SHARE_READ,&sa,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr)};
    Handle err{CreateFileW((log.dir/(label+".stderr.log")).c_str(),GENERIC_WRITE,FILE_SHARE_READ,&sa,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr)};
    Handle nullIn{CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,OPEN_EXISTING,0,nullptr)};
    if(out.h==INVALID_HANDLE_VALUE||err.h==INVALID_HANDLE_VALUE||nullIn.h==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot create vendor raw output files");
    SIZE_T count{};InitializeProcThreadAttributeList(nullptr,1,0,&count);std::vector<BYTE> attributes(count);auto* list=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
    if(!InitializeProcThreadAttributeList(list,1,0,&count))throw std::runtime_error("Vendor process attributes failed");
    struct Cleanup{LPPROC_THREAD_ATTRIBUTE_LIST p;~Cleanup(){DeleteProcThreadAttributeList(p);}} cleanup{list};
    // NVFlash also retains the semaphore handle until process exit. If the
    // frontend crashes, a second capture still cannot steal its live driver.
    HANDLE allowed[]{out.h,err.h,nullIn.h,lease.get()};
    DWORD oldLeaseFlags{};
    if(lease&&(!GetHandleInformation(lease.get(),&oldLeaseFlags)||!SetHandleInformation(lease.get(),HANDLE_FLAG_INHERIT,HANDLE_FLAG_INHERIT)))throw std::runtime_error("Vendor capture lease inheritance failed");
    struct RestoreInheritance {HANDLE handle;DWORD flags;~RestoreInheritance(){if(handle)SetHandleInformation(handle,HANDLE_FLAG_INHERIT,flags&HANDLE_FLAG_INHERIT);}} restoreInheritance{lease.get(),oldLeaseFlags};
    if(!UpdateProcThreadAttribute(list,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,allowed,sizeof(HANDLE)*(lease?4:3),nullptr,nullptr))throw std::runtime_error("Vendor handle inheritance failed");
    const auto exe=directory/spec.exe;
    // No caller-supplied options. Never add -f, unlock, reset or flash scripts.
    auto command=quoted(exe.wstring());std::string operation=info?"information":"save";
    if(spec.nvidia){
        using diagnostics::nvflash::Command;
        const auto selected=action==ReadCommand::Information?Command::List:action==ReadCommand::NvCheck?Command::Check:action==ReadCommand::NvEntire?Command::SaveEntire:action==ReadCommand::NvCompare?Command::Compare:Command::Save;
        operation=diagnostics::nvflash::commandName(selected);
        for(const auto& arg:diagnostics::nvflash::arguments(selected,adapterIndex,pathText(output))){
            const auto nativeLength=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,arg.data(),static_cast<int>(arg.size()),nullptr,0);
            if(nativeLength<=0)throw std::runtime_error("Cannot encode NVFlash read argument");
            std::wstring native(static_cast<std::size_t>(nativeLength),L'\0');if(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,arg.data(),static_cast<int>(arg.size()),native.data(),nativeLength)!=nativeLength)throw std::runtime_error("Cannot encode NVFlash read argument");
            command+=L" "+quoted(static_cast<const std::wstring&>(native));
        }
    }else if(spec.insyde){
        if(action!=ReadCommand::Save)throw std::runtime_error("Insyde backend exposes whole-ROM read only");
        for(const auto& arg:diagnostics::insyde::readArguments(pathText(output))){
            const auto length=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,arg.data(),static_cast<int>(arg.size()),nullptr,0);
            if(length<=0)throw std::runtime_error("Cannot encode Insyde read argument");
            std::wstring native(length,0);MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,arg.data(),static_cast<int>(arg.size()),native.data(),length);
            command+=L" "+quoted(static_cast<const std::wstring&>(native));
        }
    }else command+=info?L" -I":spec.intel?L" -d "+quoted(output.filename().wstring()):L" "+quoted(output.wstring())+L" /O";
    // FPT has its own legacy argument parser. Never pass the user's long path
    // (132 characters in GM6 archive16) as the dump filename. Absolute EXE and
    // a fixed short filename in the unique capture directory preserve isolation.
    const auto workingDirectory=spec.intel&&!info?output.parent_path():directory;
    STARTUPINFOEXW start{};start.StartupInfo.cb=sizeof(start);start.StartupInfo.dwFlags=STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW;start.StartupInfo.wShowWindow=SW_HIDE;
    start.StartupInfo.hStdInput=nullIn.h;start.StartupInfo.hStdOutput=out.h;start.StartupInfo.hStdError=err.h;start.lpAttributeList=list;
    PROCESS_INFORMATION process{};
    std::shared_ptr<InsydeDesktop> isolated;
    if(spec.insyde){isolated=std::make_shared<InsydeDesktop>();start.StartupInfo.lpDesktop=isolated->path.data();
        log.add("vendor_desktop","{\"private\":true,\"name\":"+q(utf8(isolated->path))+",\"error_dialogs\":\"captured and rejected, never shown on user desktop\"}");}
    log.add("vendor_requested","{\"operation\":"+q(operation)+",\"pass\":"+std::to_string(pass)+",\"exe\":"+q(pathText(exe))+",\"output\":"+q(pathText(output))+",\"command_line\":"+q(utf8(command))+",\"flash_programming_requested\":false}");
    if(spec.nvidia)log.add("vendor_raw_files","{\"label\":"+q(label)+",\"stdout\":"+q(pathText(log.dir/(label+".stdout.log")))+",\"stderr\":"+q(pathText(log.dir/(label+".stderr.log")))+"}");
    log.add("vendor_working_directory","{\"path\":"+q(pathText(workingDirectory))+"}");
    // GM6 archive17: --list worked without a console, but --check printed
    // 'PROGRAMMING ERROR: Cannot find console window'. CREATE_NO_WINDOW leaves
    // no console handle. Use a private console for every NVFlash read command,
    // hidden from creation by STARTF_USESHOWWINDOW/SW_HIDE. Never attach to or
    // allocate a console in the caller (GUI/SSH), and retain inherited raw files.
    const DWORD creationFlags=EXTENDED_STARTUPINFO_PRESENT|((spec.nvidia||spec.insyde)?CREATE_NEW_CONSOLE:CREATE_NO_WINDOW);
    log.add("vendor_execution_context","{\"private_console\":"+std::string((spec.nvidia||spec.insyde)?"true":"false")+",\"show_window\":\"SW_HIDE\",\"creation_flags\":"+std::to_string(creationFlags)+",\"stdin\":\"NUL\",\"stdout_stderr\":\"raw files\"}");
    if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,TRUE,creationFlags,nullptr,workingDirectory.c_str(),&start.StartupInfo,&process))throw std::runtime_error("Vendor CreateProcess Win32="+std::to_string(GetLastError()));
    Handle child{process.hProcess},thread{process.hThread};
    struct PendingReader {
        Handle& child;std::shared_ptr<void> lease;std::shared_ptr<InsydeDesktop> desktop;std::shared_ptr<BiosCaptureSession> biosSession;
        ~PendingReader(){if((lease||desktop||biosSession)&&child.h&&WaitForSingleObject(child.h,0)!=WAIT_OBJECT_0){
            const auto h=child.release();const auto reap=[h,guard=lease,keep=desktop,session=biosSession]{while(WaitForSingleObject(h,INFINITE)!=WAIT_OBJECT_0)Sleep(1000);CloseHandle(h);};
            mc::detail::startReaper(reap);
        }}
    } pendingReader{child,lease,isolated,biosSession};
    if(lease){
        if(!SetHandleInformation(lease.get(),HANDLE_FLAG_INHERIT,oldLeaseFlags&HANDLE_FLAG_INHERIT))throw std::runtime_error("Cannot restore vendor lease handle flags");
        restoreInheritance.handle=nullptr;
    }
    log.add("vendor_started","{\"operation\":"+q(operation)+",\"pass\":"+std::to_string(pass)+",\"pid\":"+std::to_string(process.dwProcessId)+"}");
    const auto begin=Clock::now();bool pendingLogged{};std::string insydeDialog;
    std::array<std::uint64_t,2> outputOffsets{};
    std::array<std::string,2> outputProbe{};bool missingConsole{};
    const auto publishOutput=[&]{
        // NVFlash fatal-output monitoring also runs in CLI without a GUI sink.
        if(!spec.nvidia&&!OperationMessages::capturing())return;
        try{for(unsigned i=0;i<2;++i){const auto suffix=i?".stderr.log":".stdout.log";
            std::ifstream input(log.dir/(label+suffix),std::ios::binary);input.seekg(static_cast<std::streamoff>(outputOffsets[i]));
            std::array<char,16384> block{};while(input){input.read(block.data(),block.size());const auto n=static_cast<int>(input.gcount());if(n<=0)break;outputOffsets[i]+=n;
                if(spec.nvidia){auto& probe=outputProbe[i];probe.append(block.data(),n);
                    for(auto& c:probe)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    if(probe.find("cannot find console window")!=std::string::npos)missingConsole=true;
                    if(probe.size()>1024)probe.erase(0,probe.size()-1024);
                }
                auto count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,block.data(),n,nullptr,0);const auto cp=count?CP_UTF8:CP_OEMCP;
                if(!count)count=MultiByteToWideChar(cp,0,block.data(),n,nullptr,0);std::wstring text(count,L'\0');if(count)MultiByteToWideChar(cp,0,block.data(),n,text.data(),count);
                logMessage(i?"vendor_stderr":"vendor_stdout",label+": "+utf8(text));
            }
        }}catch(...){} // Raw files are authoritative; UI streaming cannot abort a read.
    };
    const auto rejectMissingConsole=[&]{
        if(!missingConsole)return;
        const bool pending=WaitForSingleObject(child.h,0)==WAIT_TIMEOUT;
        log.add("vendor_console_error","{\"pid\":"+std::to_string(process.dwProcessId)+",\"process_pending\":"+(pending?"true":"false")+",\"forcibly_terminated\":false,\"raw_label\":"+q(label)+"}");
        // PendingReader keeps the process/native lease alive on this exit path.
        // Never retry, stop a driver, or kill a GPU reader to hide this failure.
        throw std::runtime_error(pending?"NVFlash: не удалось получить консольное окно. Процесс ещё выполняется; повторный запуск заблокирован до его завершения. Сырые ответы сохранены.":"NVFlash: не удалось получить консольное окно. Чтение не выполнено; сырые ответы сохранены.");
    };
    for(;;){
        if(isolated&&insydeDialog.empty()){
            insydeDialog=isolated->error(process.dwProcessId);
            if(!insydeDialog.empty()){log.add("vendor_error_dialog","{\"text\":"+q(insydeDialog)+",\"normal_close_requested\":true}");
                DWORD written{};WriteFile(err.h,insydeDialog.data(),static_cast<DWORD>(insydeDialog.size()),&written,nullptr);}
        }
        std::error_code ec;const auto size=info?0:fs::file_size(output,ec);if(!info&&progress&&!ec)progress->store(completedBytes+std::min<std::uint64_t>(size,64*1024*1024));
        publishOutput();rejectMissingConsole();const auto wait=WaitForSingleObject(child.h,50);if(wait==WAIT_OBJECT_0)break;
        if(wait==WAIT_FAILED)throw std::runtime_error("Vendor process wait failed");
        if(token.stop_requested()&&!pendingLogged){log.add("cancel_pending","{\"reason\":\"Finish current vendor read before cancellation\"}");pendingLogged=true;}
        LARGE_INTEGER stdoutSize{},stderrSize{};GetFileSizeEx(out.h,&stdoutSize);GetFileSizeEx(err.h,&stderrSize);
        if(Clock::now()-begin>std::chrono::minutes(10)||stdoutSize.QuadPart+stderrSize.QuadPart>32*1024*1024){
            log.add("vendor_process_pending","{\"pid\":"+std::to_string(process.dwProcessId)+",\"forcibly_terminated\":false}");
            // NVFlash owns a temporary driver. Keep its cross-process lease
            // until the reader exits; do not kill it halfway through cleanup.
            throw std::runtime_error("BIOS reader ещё выполняется: PID="+std::to_string(process.dwProcessId)+"; raw/partial сохранены, процесс не завершён принудительно");}
    }
    if(!FlushFileBuffers(out.h)||!FlushFileBuffers(err.h))throw std::runtime_error("Cannot flush vendor raw logs");
    publishOutput();
    rejectMissingConsole();
    DWORD code{};if(!GetExitCodeProcess(child.h,&code))throw std::runtime_error("Cannot query vendor exit code");
    log.add("vendor_returned","{\"operation\":"+q(operation)+",\"pass\":"+std::to_string(pass)+",\"exit_code\":"+std::to_string(code)+",\"elapsed_ms\":"+std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-begin).count())+"}");
    if(spec.insyde){
        const auto source=directory/"H2OFFT.log";
        if(fs::exists(source)){
            const auto destination=log.dir/(label+".H2OFFT.log");fs::copy_file(source,destination,fs::copy_options::none);
            log.add("vendor_own_log","{\"path\":"+q(pathText(destination))+"}");
        }
        if(!insydeDialog.empty())throw std::runtime_error("Insyde reader: "+insydeDialog);
    }
    return code;
}
void verifyFpt19ReadOutput(const VendorTool& spec,unsigned pass,EventLog& log){
    if(!usesFpt19(spec))return;
    // FUN140003790 has a regional read branch which may zero-fill skipped
    // data and still save a file successfully. Never promote that to full SPI,
    // even when hashes match and Descriptor host-read permissions are present.
    for(const auto* suffix:{".stdout.log",".stderr.log"}){
        const auto path=log.dir/("pass"+std::to_string(pass)+suffix);
        const auto raw=bytes(path,32*1024*1024);const std::string text(raw.begin(),raw.end());
        for(const auto* marker:{"Skipping overlapping data from region:","Read failed with error ","Do you want to truncate the data and continue?"})
            if(text.find(marker)!=std::string::npos){
                log.add("fpt19_read_incomplete","{\"pass\":"+std::to_string(pass)+",\"raw_log\":"+q(pathText(path))+",\"marker\":"+q(marker)+"}");
                throw std::runtime_error("FPT19 сообщил пропуск/ошибку/усечение региона; полный SPI не подтверждён, raw/partial сохранены");
            }
    }
}
std::string shapeJson(const diagnostics::bios::Shape& shape,std::span<const char> image){
    std::string rows;
    for(const auto& region:shape.regions){
        if(!rows.empty())rows+=',';
        rows+="{\"index\":"+std::to_string(region.index)+",\"enabled\":"+(region.enabled?"true":"false")+",\"host_read\":"+(region.hostRead?"true":"false")+",\"base\":"+std::to_string(region.base)+",\"limit\":"+std::to_string(region.end)+",\"nonblank\":"+(region.nonblank?"true":"false");
        if(region.enabled){
            rows+=",\"sha256\":"+q(windows::detail::binarySha256(image.subspan(static_cast<std::size_t>(region.base),static_cast<std::size_t>(region.end-region.base+1))));
            if(region.index==3&&region.end-region.base+1>=128){
                unsigned sum{};for(unsigned at=0;at<128;at+=2)sum+=static_cast<unsigned char>(image[region.base+at])|(unsigned(static_cast<unsigned char>(image[region.base+at+1]))<<8);
                rows+=",\"gbe_bank0_checksum16\":"+std::to_string(sum&0xffff)+",\"gbe_bank0_baba\":"+((sum&0xffff)==0xbaba?"true":"false");
            }
        }
        rows+='}';
    }
    return "{\"capture_kind\":"+q(shape.kind)+",\"descriptor_flash_bytes\":"+std::to_string(shape.descriptorBytes)+",\"components\":"+std::to_string(shape.components)+",\"flmstr1\":"+std::to_string(shape.hostMaster)+",\"firmware_volumes\":"+std::to_string(shape.firmwareVolumes)+",\"all_enabled_regions_host_readable\":"+(shape.descriptorBytes?(diagnostics::bios::allRegionsReadable(shape)?"true":"false"):"null")+",\"regions\":["+rows+"]}";
}
}
namespace diagnostics::nvflash {
void verifyNvflashIdle(const Event&);
unsigned run(Command command,unsigned index,const fs::path& output,const fs::path& artifacts,const std::string& label,
    const Event& event,std::stop_token token,std::atomic<std::uint64_t>* progress,std::uint64_t completedBytes,const std::shared_ptr<void>& lease){
    if(index>255)throw std::runtime_error("Invalid NVFlash index");
    if(token.stop_requested())throw std::runtime_error("VBIOS cancelled before command");
    verifyNvflashIdle(event);
    const VendorTool spec{false,"NvFlash","nvflash64.exe","","5.867.0","","","","","--save",true};
    EventLog log(artifacts,event);
    ReadCommand action;
    switch(command){case Command::List:action=ReadCommand::Information;break;case Command::Check:action=ReadCommand::NvCheck;break;case Command::Save:action=ReadCommand::Save;break;case Command::SaveEntire:action=ReadCommand::NvEntire;break;case Command::Compare:action=ReadCommand::NvCompare;break;default:throw std::runtime_error("Invalid NVFlash read command");}
    const auto cleanup=[&]{
        try{
            event("post_reader_guard_started","{\"label\":"+q(label)+"}");
            verifyNvflashIdle(event);
            event("driver_cleanup_checked","{\"label\":"+q(label)+",\"registration_checked\":true,\"active_driver_stopped\":false,\"sys_deleted\":false}");
        }catch(const std::exception& error){
            // The same error text is deduplicated by the shared transcript.
            // A pending/live reader retains its lease; never stop it to clean up.
            event("failed","{\"phase\":\"post_reader_cleanup\",\"label\":"+q(label)+",\"error\":"+q(error.what())+"}");
            throw;
        }
    };
    return readWithResidueCleanup([&]{
        const auto code=runVendor(spec,windows::embeddedRuntimeDirectory("NvFlash"),output,action,
            label.ends_with("pass2")?2:1,log,token,progress,completedBytes,index,label,lease);
        if(saves(command)&&fs::exists(output)){
            const auto size=fs::file_size(output);if(size>64*1024*1024)throw std::runtime_error("Reader output exceeds64MiB budget");
            const auto proof="{\"file\":"+q(pathText(output))+",\"bytes\":"+std::to_string(size)+",\"sha256\":"+q(windows::detail::binarySha256(bytes(output)))+",\"vendor_exit\":"+std::to_string(code)+",\"pair_verified\":false}";
            atomicText(artifacts/(label+".output.json"),proof+"\n");event("pass_saved_before_cleanup",proof);
        }
        return code;
    },cleanup);
}
}
bool biosRomDumpAvailable(){return true;} // Every model may request the logged precheck.
std::uint64_t biosRomExpectedBytes(){
    return declaredSize(); // Unknown until read/validated map; never a model-sized fallback.
}
DiagnosticDumpResult createPlatformBiosRomDump(std::stop_token token,std::atomic<std::uint64_t>* progress,const std::shared_ptr<BiosCaptureSession>& biosSession){
    DiagnosticDumpResult result;std::string error,hash;std::uint64_t expected{};std::array<std::uint64_t,2> counts{};std::array<std::string,2> hashes{};
    diagnostics::bios::Shape shape;std::optional<diagnostics::insyde::Validation> insydeProof;
    bool passesEqual{};std::string phase="precheck";const VendorTool* selected{};
    try{
        auto root=currentLogPath().parent_path();if(root.empty())root=defaultLogDirectory();root/="bios-dumps";fs::create_directories(root);
        const auto prefix=std::format("bios-{:%Y%m%d-%H%M%S}",std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));
        for(unsigned n=0;n<1000;++n){const auto p=root/(prefix+"-"+std::to_string(n));if(fs::create_directory(p)){result.directory=p;break;}}
        if(result.directory.empty())throw std::runtime_error("Cannot create unique BIOS log directory");
        EventLog log(result.directory);const auto manufacturer=reg(L"BIOSVendor"),board=reg(L"BaseBoardProduct");expected=declaredSize();const auto declaredExpected=expected;
        const auto vendor=utf8(windows::reviewed_uniwill::registry(HKEY_LOCAL_MACHINE,L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",L"VendorIdentifier"));
        const auto cpuName=utf8(windows::reviewed_uniwill::registry(HKEY_LOCAL_MACHINE,L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",L"ProcessorNameString"));
        // Pure registry/SMBIOS classification: deviceProfile does not probe EC
        // and ignores whichever laptop the user is previewing in the menu.
        const BoardDescriptor actualBoard{reg(L"SystemManufacturer"),board,{},reg(L"SystemProductName")};
        const auto& actualProfile=deviceProfile(actualBoard);
        const auto precheck="{\"bios_vendor\":"+q(manufacturer)+",\"bios_version\":"+q(reg(L"BIOSVersion"))+",\"baseboard\":"+q(board)+",\"system_manufacturer\":"+q(actualBoard.manufacturer)+",\"system_product\":"+q(actualBoard.productName)+",\"actual_profile\":"+q(actualProfile.id)+",\"cpu_vendor\":"+q(vendor)+",\"cpu_name\":"+q(cpuName)+",\"ami\":"+(ami(manufacturer)?"true":"false")+",\"smbios_rom_bytes\":"+std::to_string(expected)+"}";
        atomicText(result.directory/"precheck.json",precheck+"\n");log.add("precheck",precheck);
        const auto checkpoint=[&](const std::string& state){atomicText(result.directory/"manifest.json","{\"schema\":5,\"app_version\":"+q(MAGICCONTROL_VERSION_STRING)+",\"utc\":"+q(stamp())+",\"transport\":"+q(selected?readerName(*selected):"not selected")+",\"state\":"+q(state)+",\"expected_bytes\":"+std::to_string(expected)+",\"descriptor_flash_bytes\":"+std::to_string(shape.descriptorBytes)+",\"pass1_bytes\":"+std::to_string(counts[0])+",\"pass2_bytes\":"+std::to_string(counts[1])+",\"pass1_sha256\":"+(hashes[0].empty()?"null":q(hashes[0]))+",\"pass2_sha256\":"+(hashes[1].empty()?"null":q(hashes[1]))+",\"capture_kind\":"+(shape.kind.empty()?"null":q(shape.kind))+",\"completeness_scope\":"+q(insydeProof&&shape.kind!="insyde_intel_spi"?"rom_map":selected&&selected->intel||shape.kind=="insyde_intel_spi"?"full_spi":"declared_rom_size")+",\"rom_map_verified\":"+(insydeProof?"true":"false")+",\"region_only\":false,\"firmware_volumes\":"+std::to_string(shape.firmwareVolumes)+",\"two_passes_equal\":"+(passesEqual?"true":"false")+",\"capture_saved\":"+(result.saved?"true":"false")+",\"capture_complete\":"+(result.saved?"true":"false")+",\"physical_spi_complete\":"+(selected&&(selected->intel||shape.kind=="insyde_intel_spi")?(result.saved?"true":"false"):"null")+",\"sha256\":"+(hash.empty()?"null":q(hash))+",\"live_ec_flash_captured\":false,\"ec_ram_captured\":false,\"embedded_ec_update_image_possible\":true,\"flash_programming_requested\":false,\"error\":"+q(error)+"}\n");};
        checkpoint(phase);
        try{
            if(wcsstr(GetCommandLineW(),L"--no-ec"))throw std::runtime_error("Аппаратный BIOS dump отключён параметром --no-ec");
            if(vendor=="GenuineIntel"){
                const auto ids=windows::presentIntelLpcIds();std::string rows;
                for(const auto id:ids){if(!rows.empty())rows+=',';rows+=q(std::format("8086:{:04X}",id));}
                // Standard readers follow actual PCI compatibility, not a model
                // catalogue or a CPU marketing name. Unknown matching boards
                // still require the reviewed LPC table and FPT -I admission.
                const bool target19=ids.size()==1&&diagnostics::bios::reviewedFpt19Lpc(ids[0]);
                const auto& candidate=target19?fpt19:fpt;
                const bool admitted=ids.size()==1&&(target19?diagnostics::bios::reviewedFpt19Lpc(ids[0]):diagnostics::bios::reviewedFpt16Lpc(ids[0]));
                const auto policy="{\"present_lpc_ids\":["+rows+"],\"actual_profile\":"+q(actualProfile.id)+",\"hardware_selected_fpt19\":"+(target19?"true":"false")+",\"candidate_group\":"+q(candidate.group)+",\"candidate_version\":"+q(candidate.version)+",\"source_policy\":"+q(target19?"Actual Intel LPC in reviewed FPT19 table, then FPT -I":"FPT16.1 internal LPC table, then FPT -I")+",\"admitted\":"+(admitted?"true":"false")+",\"hardware_tested\":false}";
                atomicText(result.directory/"compatibility.json",policy+"\n");log.add("tool_selection",policy);
                // A driver conflict is a STOP, never admission for another
                // firmware utility. 0.1.437 added that fallback; a subsequent
                // KWCN user report describes a power-off during main BIOS read.
                // Keep WNPDriver/SYS intact and let the common guard reject it.
                if(admitted)selected=&candidate;
                else if(actualProfile.id=="lenovo-legion-kwcn"){
                    log.add("reader_refused","{\"reason\":\"KWCN H2OFFT compatibility is not verified; main-BIOS power-off reported\",\"vendor_started\":false}");
                    throw std::runtime_error("Lenovo KWCN: совместимый FPT не подтверждён; H2OFFT не запускается после сообщения о выключении при чтении BIOS");
                }
                else if(diagnostics::insyde::candidate(vendor,manufacturer,h2oAcpi()))selected=&h2offt;
                else throw std::runtime_error("Intel LPC не подтверждён для FPT; интерфейс Insyde также не обнаружен");
            }else if(diagnostics::insyde::candidate(vendor,manufacturer,h2oAcpi())){
                selected=&h2offt;
            }else if(vendor=="AuthenticAMD"&&ami(manufacturer))selected=&afu;
            else throw std::runtime_error("Нет допущенного BIOS reader для CPU/BIOS: "+vendor+" / "+manufacturer);
            if(selected->insyde)log.add("insyde_admission","{\"actual_profile\":"+q(actualProfile.id)+",\"bios_version\":"+q(reg(L"BIOSVersion"))+",\"live_h2o_acpi\":"+(h2oAcpi()?"true":"false")+",\"model_gate\":false,\"native_IHISI_acceptance_required\":true,\"declared_bytes\":"+std::to_string(declaredExpected)+",\"size_source\":\"SMBIOS or validated dynamic HFDM ROM window\",\"read_command\":\"-g\",\"region_overrides\":false}");
            log.add("reader_selected","{\"runtime_group\":"+q(selected->group)+",\"tool_version\":"+q(selected->version)+",\"actual_profile\":"+q(actualProfile.id)+"}");
            checkpoint(phase);
            if(!selected->intel&&!selected->insyde&&(expected<1024*1024||expected>64*1024*1024))throw std::runtime_error("SMBIOS не сообщил поддерживаемый размер BIOS1–64МиБ");
            cancel(token);Handle gate{CreateMutexW(nullptr,FALSE,L"Global\\MagicControl.BiosCapture.v1")};if(!gate.h)throw std::runtime_error("BIOS capture mutex unavailable");
            const auto lock=WaitForSingleObject(gate.h,0);if(lock!=WAIT_OBJECT_0&&lock!=WAIT_ABANDONED)throw std::runtime_error("Другой BIOS dump уже выполняется");
            struct Unlock{HANDLE h;~Unlock(){ReleaseMutex(h);}} unlock{gate.h};
            SYSTEM_POWER_STATUS power{};if(!GetSystemPowerStatus(&power)||power.ACLineStatus!=1)throw std::runtime_error("Для чтения BIOS подключите внешнее питание");
            guardDriver(*selected,log);
            atomicText(result.directory/"tool.json","{\"version\":"+q(selected->version)+",\"source_url\":"+q(selected->source)+",\"source_sha256\":"+q(selected->archiveSha)+",\"exe_sha256\":"+q(selected->exeSha)+",\"driver_sha256\":"+q(selected->driverSha)+",\"command\":"+q(selected->command)+",\"storage\":\"embedded Windows resources\",\"network_required\":false,\"security_policy_changes\":false}\n");
            phase="prepare_tool";checkpoint(phase);const auto directory=tool(*selected,token,log);
            if(selected->insyde){
                // No cross-model image.6.76 needs only its signed FlsHook
                // dependency; fixed-g returns before the hook launch branch.
                for(const auto& entry:fs::directory_iterator(directory)){
                    const auto ext=entry.path().extension().string();
                    if(ext==".fd"||ext==".bin"||ext==".rom"||ext==".cap"||entry.path().filename()=="FWUpdLcl.exe")
                        throw std::runtime_error("Unexpected firmware/update payload in Insyde READ runtime");
                }
                for(const auto* name:{"FlsHook.exe"}){
                    const auto status=signature(directory/name);
                    log.add("dependency_signature","{\"name\":"+q(name)+",\"status\":"+std::to_string(status)+",\"application_launch_allowed\":false}");
                    if(status!=ERROR_SUCCESS)throw std::runtime_error("Windows не подтвердила обязательную Insyde dependency: "+std::string(name));
                }
            }
            const bool checkExe=selected->intel||selected->insyde;
            const auto driverTrust=signature(directory/selected->driver),exeTrust=checkExe?signature(directory/selected->exe):ERROR_SUCCESS;
            log.add("signature","{\"kind\":\"Authenticode cache-only, not kernel admission proof\",\"exe_checked\":"+(checkExe?std::string("true"):std::string("false"))+",\"exe_status\":"+std::to_string(exeTrust)+",\"driver_status\":"+std::to_string(driverTrust)+",\"exe_sha256\":"+q(selected->exeSha)+",\"driver_sha256\":"+q(selected->driverSha)+"}");
            if(driverTrust!=ERROR_SUCCESS||exeTrust!=ERROR_SUCCESS)throw std::runtime_error("Windows не подтвердила подпись BIOS reader/driver");
            if(selected->insyde&&signature(directory/"H2OFFT.cat")!=ERROR_SUCCESS)throw std::runtime_error("Windows не подтвердила каталог Insyde driver");
            const auto awake=SetThreadExecutionState(ES_CONTINUOUS|ES_SYSTEM_REQUIRED);struct Awake{bool set;~Awake(){if(set)SetThreadExecutionState(ES_CONTINUOUS);}} restoreAwake{awake!=0};
            if(selected->intel){
                phase="fpt_information";checkpoint(phase);cancel(token);
                if(runVendor(*selected,directory,{},ReadCommand::Information,0,log,token,nullptr,0,0,{}, {},biosSession))throw std::runtime_error("FPT -I отклонил систему; информация и ошибка сохранены, чтение SPI не запускалось");
                guardDriver(*selected,log);cancel(token);
            }
            for(unsigned pass=1;pass<=2;++pass){
                cancel(token);guardDriver(*selected,log);phase="read_pass"+std::to_string(pass);checkpoint(phase);const auto output=result.directory/("pass"+std::to_string(pass)+".partial.bin");
                ULONGLONG before{},after{};const bool clockOk=QueryUnbiasedInterruptTime(&before)!=FALSE;const auto began=Clock::now();
                const auto code=runVendor(*selected,directory,output,ReadCommand::Save,pass,log,token,progress,pass==1?0:counts[0],0,{}, {},biosSession);
                std::error_code ec;const auto count=fs::file_size(output,ec);counts[pass-1]=ec?0:count;checkpoint(phase);
                std::vector<char> image;if(!ec){image=bytes(output);hashes[pass-1]=windows::detail::binarySha256(image);}
                log.add("pass_file_observed","{\"pass\":"+std::to_string(pass)+",\"bytes\":"+std::to_string(counts[pass-1])+",\"sha256\":"+(hashes[pass-1].empty()?"null":q(hashes[pass-1]))+"}");checkpoint(phase);
                const bool clockEnd=QueryUnbiasedInterruptTime(&after)!=FALSE;
                if(clockOk&&clockEnd&&std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-began).count()-static_cast<long long>((after-before)/10000)>5000)throw std::runtime_error("Во время чтения был сон; partial сохранён, повторите dump");
                if(selected->insyde?!diagnostics::insyde::readExitAccepted(code):code!=0)throw std::runtime_error("BIOS reader вернул ошибку "+std::to_string(code)+"; полный stdout/stderr сохранён");
                if(selected->insyde&&code==3010)log.add("read_exit_mapping","{\"exit_code\":3010,\"meaning\":\"OEM configured silent success; file validation still required\",\"reboot_requested\":false}");
                verifyFpt19ReadOutput(*selected,pass,log);
                diagnostics::bios::Shape actual;
                if(selected->insyde){
                    auto proof=diagnostics::insyde::validate(image,declaredExpected,vendor,[](auto data){return windows::detail::binarySha256(data);});
                    expected=proof.romBytes;actual=proof.shape;
                    std::string regions;
                    for(const auto& region:proof.map.regions){if(!regions.empty())regions+=',';
                        const auto ignored=(region.attributes&2)!=0;
                        regions+="{\"offset\":"+std::to_string(region.offset)+",\"bytes\":"+std::to_string(region.bytes)+",\"attributes\":"+std::to_string(region.attributes)+",\"ignored\":"+(ignored?"true":"false")+",\"stored_sha256\":"+q(region.hash)+",\"observed_sha256\":"+(ignored?"null":q(windows::detail::binarySha256(std::span<const char>(image).subspan(static_cast<std::size_t>(region.offset),static_cast<std::size_t>(region.bytes)))))+"}";
                    }
                    atomicText(result.directory/("pass"+std::to_string(pass)+".insyde-map.json"),"{\"map_offset\":"+std::to_string(proof.map.offset)+",\"rom_base\":"+std::to_string(proof.map.base)+",\"rom_bytes\":"+std::to_string(proof.romBytes)+",\"smbios_size_used\":"+(proof.smbiosSize?"true":"false")+",\"header_checksum_verified\":true,\"protected_region_hashes_verified\":"+std::to_string(proof.protectedHashes)+",\"psp_roots_verified\":"+std::to_string(proof.pspRoots)+",\"regions\":["+regions+"]}\n");
                    insydeProof=std::move(proof);
                }else actual=diagnostics::bios::validate(image,expected,selected->intel);
                const auto evidence=shapeJson(actual,image);atomicText(result.directory/("pass"+std::to_string(pass)+".regions.json"),evidence+"\n");log.add("pass_validated","{\"pass\":"+std::to_string(pass)+",\"shape\":"+evidence+"}");
                // Reviewed FPT can FF-fill region9 when FLMSTR1 read permission
                // is absent. Equal hashes/length alone do NOT prove a full read.
                if(selected->intel&&!diagnostics::bios::allRegionsReadable(actual))throw std::runtime_error("Descriptor запрещает чтение одного из регионов; FPT мог оставить FF, полный SPI не подтверждён");
                if(pass==1){hash=hashes[0];shape=actual;}
                else {if(actual.kind!=shape.kind||counts[0]!=counts[1]||hashes[1]!=hash)throw std::runtime_error("Проходы BIOS различаются; оба файла и их хэши сохранены для разбора");passesEqual=true;}
                checkpoint(phase);
            }
            cancel(token);fs::rename(result.directory/"pass1.partial.bin",result.directory/"bios.bin");fs::rename(result.directory/"pass2.partial.bin",result.directory/"bios-verify.bin");
            result.saved=true;phase="complete";log.add("driver_after",serviceState());checkpoint(phase);
        }catch(const std::exception& e){error=e.what();result.cancelled=token.stop_requested();log.add("failed","{\"phase\":"+q(phase)+",\"error\":"+q(error)+",\"win32_last_error\":"+std::to_string(GetLastError())+"}");log.add("driver_after",serviceState());checkpoint(result.cancelled?"cancelled":"failed");}
    }catch(const std::exception& e){result.saved=false;error=e.what();}
    result.message=result.saved?"BIOS: "+std::string(selected&&selected->insyde?"Insyde H2O ROM":selected&&selected->intel?"SPI Intel":"образ ROM AFU")+" "+std::to_string(counts[0]/(1024*1024))+" МиБ, два одинаковых прохода. "+(selected&&selected->insyde&&shape.kind!="insyde_intel_spi"?"Карта ROM проверена; физическая полнота SPI не установлена. ":"")+pathText(result.directory):"BIOS: "+error+(result.directory.empty()?"":". Логи и partial: "+pathText(result.directory));return result;
}
}
