#pragma once
// Shared installed-driver evidence for explicitly admitted Uniwill readers.
// Does not start/install a driver, open the EC device or authorize any write.
#include "../BinaryHashWindows.h"
#include "MagicControl/BoardIdentity.h"
#include "MagicControl/DeviceProfile.h"
#include <windows.h>
#include <setupapi.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>
#include <stdexcept>
namespace mc::windows::reviewed_uniwill {
struct Reg {HKEY key{};~Reg(){if(key)RegCloseKey(key);}};
inline std::wstring registry(HKEY root,const std::wstring& path,const wchar_t* name){
    Reg r;if(RegOpenKeyExW(root,path.c_str(),0,KEY_READ,&r.key)!=ERROR_SUCCESS)return {};
    wchar_t value[32768]{};DWORD size=sizeof(value),type{};
    if(RegQueryValueExW(r.key,name,nullptr,&type,reinterpret_cast<BYTE*>(value),&size)!=ERROR_SUCCESS||(type!=REG_SZ&&type!=REG_EXPAND_SZ))return {};
    value[32767]=0;return value;
}
inline std::string text(const std::wstring& s){if(s.empty())return {};const int n=WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);
    std::string value(n,0);WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),value.data(),n,nullptr,nullptr);return value;}
inline std::string fileHash(const std::filesystem::path& path){
    const auto size=std::filesystem::file_size(path);
    if(!size||size>1024*1024)throw std::runtime_error("Uniwill: unexpected SYS size");
    std::ifstream input(path,std::ios::binary);std::vector<char> bytes{std::istreambuf_iterator<char>(input),{}};
    if(bytes.size()!=size)throw std::runtime_error("Uniwill: installed SYS not readable");
    return detail::binarySha256(bytes);
}
inline std::filesystem::path driverImage(const std::wstring& raw){
    wchar_t expanded[32768]{},windows[32768]{};const auto wn=GetWindowsDirectoryW(windows,32768);
    if(!wn||wn>=32768)throw std::runtime_error("Uniwill: Windows directory unavailable");
    const auto n=ExpandEnvironmentStringsW(raw.c_str(),expanded,32768);if(!n||n>32768)throw std::runtime_error("Uniwill: invalid SYS ImagePath");
    std::wstring path=expanded;
    if(path.starts_with(L"\\SystemRoot\\"))path=std::wstring(windows)+path.substr(11);
    else if(path.starts_with(L"\\??\\"))path.erase(0,4);
    else if(path.starts_with(L"System32\\")||path.starts_with(L"system32\\"))path=std::wstring(windows)+L"\\"+path;
    if(path.empty()||path.starts_with(L"\\\\"))throw std::runtime_error("Uniwill: local SYS ImagePath required");
    auto p=std::filesystem::weakly_canonical(path);const auto base=std::filesystem::weakly_canonical(windows).wstring()+L"\\";
    if(_wcsnicmp(p.c_str(),base.c_str(),base.size()))throw std::runtime_error("Uniwill: SYS ImagePath outside Windows directory");
    return p;
}
inline void verifyReviewedUniwillDriver(std::string& notes,std::string_view expectedHash){
    const auto devices=SetupDiGetClassDevsW(nullptr,L"ACPI",nullptr,DIGCF_PRESENT|DIGCF_ALLCLASSES);
    if(devices==INVALID_HANDLE_VALUE)throw std::runtime_error("Uniwill: ACPI PnP enumeration failed");
    struct Devices {HDEVINFO h;~Devices(){SetupDiDestroyDeviceInfoList(h);}} list{devices};
    SP_DEVINFO_DATA d{sizeof(d)};std::wstring service,driver,instance;unsigned matches{};
    for(DWORD i=0;i<4096&&SetupDiEnumDeviceInfo(devices,i,&d);++i){wchar_t id[512]{};
        if(!SetupDiGetDeviceInstanceIdW(devices,&d,id,512,nullptr)||_wcsnicmp(id,L"ACPI\\INOU0000\\",14))continue;
        ++matches;instance=id;
        const auto property=[&](DWORD key){wchar_t value[2048]{};DWORD type{};
            if(!SetupDiGetDeviceRegistryPropertyW(devices,&d,key,&type,reinterpret_cast<BYTE*>(value),sizeof(value),nullptr)||type!=REG_SZ)return std::wstring{};
            value[2047]=0;return std::wstring(value);};
        service=property(SPDRP_SERVICE);driver=property(SPDRP_DRIVER);
    }
    notes+="pnp_matches="+std::to_string(matches)+"\npnp_instance="+text(instance)+"\nservice="+text(service)+"\n";
    if(matches!=1||_wcsicmp(service.c_str(),L"UWACPIDriver"))throw std::runtime_error("Uniwill: expected a single ACPI\\INOU0000 using UWACPIDriver; native UWACPIDriver is required");
    const auto driverKey=L"SYSTEM\\CurrentControlSet\\Control\\Class\\"+driver;
    notes+="driver_version="+text(registry(HKEY_LOCAL_MACHINE,driverKey,L"DriverVersion"))+"\ndriver_inf="+text(registry(HKEY_LOCAL_MACHINE,driverKey,L"InfPath"))+"\n";
    const auto raw=registry(HKEY_LOCAL_MACHINE,L"SYSTEM\\CurrentControlSet\\Services\\"+service,L"ImagePath");
    notes+="driver_image_registry="+text(raw)+"\n";
    const auto path=driverImage(raw);const auto hash=fileHash(path);
    notes+="driver_image="+text(path.wstring())+"\ndriver_sha256="+hash+"\nexpected_sha256="+std::string(expectedHash)+"\n";
    if(hash!=expectedHash)throw std::runtime_error("Uniwill: installed SYS differs from reviewed driver; evidence saved, EC not opened");
}
inline void verifyGm5Driver(std::string& notes){
    const auto board=readBoardDescriptor();
    if(board.manufacturer!="MECHREVO"||board.baseBoard!="GM5HG0A"||board.productName!="Yilong15Pro Series GM5HG0A"||
       !gm5Profile(deviceProfile(board).id)||registry(HKEY_LOCAL_MACHINE,L"HARDWARE\\DESCRIPTION\\System\\BIOS",L"BIOSVersion")!=L"N.1.09MRO19")
        throw std::runtime_error("GM5HG0A: exact reviewed board/BIOS required");
    verifyReviewedUniwillDriver(notes,"e9840521975e9073881a488f9e978a18c93aed44c57fe2382b1d513fd27126e2");
}
inline void verifyL16Identity(){
    const auto board=readBoardDescriptor();
    if(board.manufacturer!="MACHENIKE"||board.baseBoard!="L16"||board.productName!="L16"||
       !l16Profile(deviceProfile(board).id)||registry(HKEY_LOCAL_MACHINE,L"HARDWARE\\DESCRIPTION\\System\\BIOS",L"BIOSVersion")!=L"N.1.13MAC10")
        throw std::runtime_error("MACHENIKE L16: exact reviewed board/BIOS required");
}
inline void verifyL16Driver(std::string& notes){
    verifyL16Identity();
    verifyReviewedUniwillDriver(notes,"6e360ee4a0c42b2a4eaf46208c1592c87f8cd71aca2a6c52831a2ad356e61235");
}
template<class Read> void verifyL16EcIdentity(Read read){
    // DefaultTool GetBiosProjctID reads74C, using low6 only if7CA.0 is1.
    // EC ProjectID740 is a separate identity, not that BIOS enumeration.
    if(read(0x740)!=26||!(read(0x78E)&0x40)||!(read(0x7CA)&1)||(read(0x74C)&0x3F)!=29)
        throw std::runtime_error("L16: EC Project1A / BIOS project ID6 / RAM capability mismatch");
}

}
