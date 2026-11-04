#pragma once
#include "MagicControl/HardwareInventory.h"
#include "WmiQueryWindows.h"
#include "BinaryHashWindows.h"
#include "../../protocols/uniwill/hydroc16g2/KeyboardHid.h"
#include <setupapi.h>
#include <cfgmgr32.h>
#include <devpkey.h>
#include <objbase.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <wintrust.h>
#include <softpub.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <array>

namespace mc::windows::inventory {
// Read-only onboarding evidence. This is not an OEM transport selector: no
// CreateService/StartService, EC IOCTL or HID feature/output report is allowed.
struct Rows:std::vector<InventoryRow>{
    std::function<void(const InventoryRow&)> emit;
    bool complete{true},capped{};std::size_t textBytes{};
    void push_back(InventoryRow row){
        if(capped)return;
        if(row.name.size()>4096||row.value.size()>65536){complete=false;const auto bytes=row.value.size();if(row.name.size()>4096)row.name="Metadata field";row.value="metadata field exceeds bounded64KiB representation; original_bytes="+std::to_string(bytes);}
        if(size()>=60000||textBytes+row.name.size()+row.value.size()>16*1024*1024){complete=false;capped=true;row={"Metadata completeness","record/text budget exhausted; previous rows retained"};}
        textBytes+=row.name.size()+row.value.size();if(emit)emit(row);std::vector<InventoryRow>::push_back(std::move(row));
    }
};
inline std::string text(const std::wstring& value){return wmi::utf8(value);}
inline std::string error(DWORD code){return "Win32="+std::to_string(code);}
struct Devices {HDEVINFO value{INVALID_HANDLE_VALUE};~Devices(){if(value!=INVALID_HANDLE_VALUE)SetupDiDestroyDeviceInfoList(value);}};
struct Key {HKEY value{};~Key(){if(value)RegCloseKey(value);}};
struct Handle {HANDLE value{INVALID_HANDLE_VALUE};~Handle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}};
struct Service {SC_HANDLE value{};~Service(){if(value)CloseServiceHandle(value);}};
struct Library {HMODULE value{};explicit Library(const wchar_t* name):value(LoadLibraryExW(name,nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32)){}~Library(){if(value)FreeLibrary(value);}};
inline std::wstring property(HDEVINFO devices,SP_DEVINFO_DATA& device,DWORD key,std::string& failure){
    DWORD size{},type{};
    SetupDiGetDeviceRegistryPropertyW(devices,&device,key,&type,nullptr,0,&size);
    if(!size){failure=error(GetLastError());return {};}
    if(size>1024*1024){failure="property exceeds1MiB metadata limit";return {};}
    std::vector<wchar_t> data((size+sizeof(wchar_t)-1)/sizeof(wchar_t)+2,0);
    if(!SetupDiGetDeviceRegistryPropertyW(devices,&device,key,&type,reinterpret_cast<BYTE*>(data.data()),size,nullptr)){
        failure=error(GetLastError());return {};
    }
    if(type!=REG_SZ&&type!=REG_EXPAND_SZ&&type!=REG_MULTI_SZ){failure="unexpected property type="+std::to_string(type);return {};}
    std::wstring result;
    for(std::size_t at=0;at<data.size()&&data[at];){
        const auto length=wcsnlen(data.data()+at,data.size()-at);
        if(!result.empty())result+=L" | ";result.append(data.data()+at,length);at+=length+1;
        if(type!=REG_MULTI_SZ)break;
    }
    return result;
}
inline std::wstring registry(HKEY key,const wchar_t* name,std::string& failure){
    DWORD bytes{},type{};auto code=RegQueryValueExW(key,name,nullptr,&type,nullptr,&bytes);
    if(code!=ERROR_SUCCESS){failure=error(code);return {};}
    if((type!=REG_SZ&&type!=REG_EXPAND_SZ)||bytes>1024*1024){failure="invalid registry metadata size/type";return {};}
    std::vector<wchar_t> data((bytes+sizeof(wchar_t)-1)/sizeof(wchar_t)+1,0);
    code=RegQueryValueExW(key,name,nullptr,&type,reinterpret_cast<BYTE*>(data.data()),&bytes);
    if(code!=ERROR_SUCCESS){failure=error(code);return {};}return data.data();
}
inline std::filesystem::path localDriverImage(std::wstring raw){
    if(raw.size()>1&&raw.front()==L'"'&&raw.back()==L'"')raw=raw.substr(1,raw.size()-2);
    wchar_t expanded[32768]{},windows[32768]{};
    const auto n=ExpandEnvironmentStringsW(raw.c_str(),expanded,32768);
    const auto w=GetWindowsDirectoryW(windows,32768);
    if(!n||n>32768||!w||w>=32768)throw std::runtime_error("cannot expand local driver ImagePath");
    std::wstring path=expanded;
    if(!_wcsnicmp(path.c_str(),L"\\SystemRoot\\",12))path=std::wstring(windows)+path.substr(11);
    else if(path.starts_with(L"\\??\\"))path.erase(0,4);
    else if(!_wcsnicmp(path.c_str(),L"System32\\",9))path=std::wstring(windows)+L"\\"+path;
    const std::filesystem::path result(path);
    // Do not trigger network access or guess a relative working directory.
    if(path.starts_with(L"\\\\")||!result.is_absolute())throw std::runtime_error("ImagePath is not an absolute local file");
    return result;
}
inline std::string imageHash(const std::filesystem::path& path,std::stop_token stop,std::chrono::steady_clock::time_point deadline){
    std::error_code ec;const auto bytes=std::filesystem::file_size(path,ec);
    if(ec)return "unavailable: "+ec.message();
    if(!bytes||bytes>128*1024*1024)return "unavailable: image outside128MiB evidence limit";
    std::ifstream file(path,std::ios::binary);if(!file)return "unavailable: local image cannot be read";
    std::vector<char> data;data.reserve(static_cast<std::size_t>(bytes));char block[65536];
    while(file&&!stop.stop_requested()&&std::chrono::steady_clock::now()<deadline){
        file.read(block,sizeof(block));data.insert(data.end(),block,block+file.gcount());
        if(data.size()>128*1024*1024)return "unavailable: image grew beyond evidence limit";
    }
    if(stop.stop_requested())return "cancelled";
    if(std::chrono::steady_clock::now()>=deadline)return "unavailable: image hash phase deadline";
    if(file.bad()||data.size()!=bytes)return "unavailable: read failed/image changed";
    return detail::binarySha256(data);
}
inline std::string embeddedSignature(const std::filesystem::path& path){
    Library library(L"wintrust.dll");
    const auto verify=library.value?reinterpret_cast<decltype(&WinVerifyTrust)>(GetProcAddress(library.value,"WinVerifyTrust")):nullptr;
    if(!verify)return "unavailable: system WinVerifyTrust export absent";
    WINTRUST_FILE_INFO file{sizeof(file)};file.pcwszFilePath=path.c_str();
    WINTRUST_DATA data{sizeof(data)};data.dwUIChoice=WTD_UI_NONE;data.fdwRevocationChecks=WTD_REVOKE_NONE;
    data.dwUnionChoice=WTD_CHOICE_FILE;data.pFile=&file;data.dwStateAction=WTD_STATEACTION_VERIFY;
    data.dwProvFlags=WTD_CACHE_ONLY_URL_RETRIEVAL;
    GUID action=WINTRUST_ACTION_GENERIC_VERIFY_V2;const auto result=verify(nullptr,&action,&data);
    data.dwStateAction=WTD_STATEACTION_CLOSE;verify(nullptr,&action,&data);
    // NO_SIGNATURE does not mean unsigned: a kernel image may be catalog signed.
    return std::format("embedded WinVerifyTrust=0x{:08X}; cache-only/offline, revocation not checked; catalog membership not evaluated",static_cast<unsigned long>(result));
}
inline void serviceEvidence(Rows& rows,SC_HANDLE manager,const std::wstring& name,std::stop_token stop,std::chrono::steady_clock::time_point deadline){
    if(stop.stop_requested()||std::chrono::steady_clock::now()>=deadline){rows.complete=false;rows.push_back({"Служба "+text(name),"metadata phase cancelled/deadline"});return;}
    if(!manager){rows.complete=false;rows.push_back({"Служба "+text(name),"SCM недоступен; состояние неизвестно"});return;}
    Service service{OpenServiceW(manager,name.c_str(),SERVICE_QUERY_CONFIG|SERVICE_QUERY_STATUS)};
    if(!service.value){rows.complete=false;rows.push_back({"Служба "+text(name),error(GetLastError())});return;}
    SERVICE_STATUS_PROCESS status{};DWORD size{};
    if(QueryServiceStatusEx(service.value,SC_STATUS_PROCESS_INFO,reinterpret_cast<BYTE*>(&status),sizeof(status),&size))
        rows.push_back({"Состояние службы "+text(name),std::format("state={} win32_exit={} service_exit={}; query only",status.dwCurrentState,status.dwWin32ExitCode,status.dwServiceSpecificExitCode)});
    else {rows.complete=false;rows.push_back({"Состояние службы "+text(name),error(GetLastError())});}
    QueryServiceConfigW(service.value,nullptr,0,&size);
    if(GetLastError()!=ERROR_INSUFFICIENT_BUFFER||size>1024*1024){rows.complete=false;rows.push_back({"Конфигурация службы "+text(name),error(GetLastError())});return;}
    std::vector<BYTE> buffer(size);auto* config=reinterpret_cast<QUERY_SERVICE_CONFIGW*>(buffer.data());
    if(!QueryServiceConfigW(service.value,config,size,&size)){rows.complete=false;rows.push_back({"Конфигурация службы "+text(name),error(GetLastError())});return;}
    rows.push_back({"Тип/запуск службы "+text(name),std::format("type=0x{:X} start={}; installation/running/sensor success are independent",config->dwServiceType,config->dwStartType)});
    const std::wstring image=config->lpBinaryPathName?config->lpBinaryPathName:L"";
    rows.push_back({"ImagePath "+text(name),image.empty()?kInventoryUnknown:text(image)});
    if(!(config->dwServiceType&(SERVICE_KERNEL_DRIVER|SERVICE_FILE_SYSTEM_DRIVER))||image.empty()||stop.stop_requested())return;
    try{const auto path=localDriverImage(image);rows.push_back({"Локальный образ "+text(name),text(path.wstring())});
        const auto hash=imageHash(path,stop,deadline);rows.push_back({"SHA256 "+text(name),hash});if(hash.size()!=64||hash.find_first_not_of("0123456789abcdef")!=std::string::npos)rows.complete=false;
        if(!stop.stop_requested()&&std::chrono::steady_clock::now()<deadline){const auto signature=embeddedSignature(path);rows.push_back({"Подпись "+text(name),signature});if(signature.starts_with("unavailable:"))rows.complete=false;}
        else {rows.complete=false;rows.push_back({"Подпись "+text(name),"skipped: service evidence phase cancelled/deadline"});}
    }catch(const std::exception& e){rows.complete=false;rows.push_back({"Образ драйвера "+text(name),e.what()});}
}
using Clock=std::chrono::steady_clock;
struct Phase {
    Rows& rows;std::stop_token stop;Clock::time_point deadline;const char* label;bool reported{};
    bool ended(){
        if(!stop.stop_requested()&&Clock::now()<deadline&&!rows.capped)return false;
        rows.complete=false;if(!reported){reported=true;rows.push_back({std::string(label)+" completeness",stop.stop_requested()?"cancelled; partial rows retained":rows.capped?"metadata text/record budget exhausted":"phase deadline; partial rows retained"});}return true;
    }
    void failed(std::string label,std::string detail){rows.complete=false;rows.push_back({std::move(label),std::move(detail)});}
};
inline void driverPackage(Rows& rows,const std::wstring& driver,const std::string& prefix){
    if(driver.empty())return;
    Key key;const auto path=L"SYSTEM\\CurrentControlSet\\Control\\Class\\"+driver;
    const auto code=RegOpenKeyExW(HKEY_LOCAL_MACHINE,path.c_str(),0,KEY_QUERY_VALUE,&key.value);
    if(code!=ERROR_SUCCESS){rows.complete=false;rows.push_back({prefix+"Driver package metadata",error(code)});return;}
    for(const auto name:{L"InfPath",L"InfSection",L"DriverVersion",L"DriverDate",L"ProviderName",L"DriverDesc"}){
        std::string failed;const auto value=registry(key.value,name,failed);rows.push_back({prefix+text(name),value.empty()?"not reported; "+failed:text(value)});
    }
}
inline std::wstring cmProperty(DEVINST instance,ULONG property,std::string& failure){
    ULONG type{},size{};auto code=CM_Get_DevNode_Registry_PropertyW(instance,property,&type,nullptr,&size,0);
    if(code!=CR_BUFFER_SMALL||!size||size>65536){failure="CONFIGRET="+std::to_string(code)+"; absent/invalid property size";return {};}
    std::vector<wchar_t> data((size+sizeof(wchar_t)-1)/sizeof(wchar_t)+1,0);
    code=CM_Get_DevNode_Registry_PropertyW(instance,property,&type,data.data(),&size,0);
    if(code!=CR_SUCCESS||type!=REG_SZ){failure="CONFIGRET="+std::to_string(code)+" type="+std::to_string(type);return {};}
    return data.data();
}
inline std::wstring instanceId(DEVINST device,std::string& failure){
    ULONG count{};auto code=CM_Get_Device_ID_Size(&count,device,0);
    if(code!=CR_SUCCESS||count>4095){failure="CM_Get_Device_ID_Size CONFIGRET="+std::to_string(code);return {};}
    std::vector<wchar_t> id(count+1,0);code=CM_Get_Device_IDW(device,id.data(),static_cast<ULONG>(id.size()),0);
    if(code!=CR_SUCCESS){failure="CM_Get_Device_ID CONFIGRET="+std::to_string(code);return {};}return id.data();
}
inline void parentBinding(Phase& phase,DEVINST child,std::set<std::wstring>& services){
    std::set<DEVINST> visited;DEVINST current=child;
    for(unsigned depth=1;depth<=8&&!phase.ended();++depth){
        DEVINST parent{};const auto code=CM_Get_Parent(&parent,current,0);
        if(code==CR_NO_SUCH_DEVNODE)return;
        if(code!=CR_SUCCESS){phase.failed("PnP parent","CM_Get_Parent CONFIGRET="+std::to_string(code));return;}
        if(!visited.insert(parent).second){phase.failed("PnP parent","cycle in ConfigMgr parent chain; stopped");return;}
        const auto prefix="Parent["+std::to_string(depth)+"] ";std::string failure;const auto id=instanceId(parent,failure);
        if(id.empty())phase.failed(prefix+"instance",failure);else phase.rows.push_back({prefix+"instance",text(id)});
        std::wstring driver;
        for(auto [name,key]:{std::pair{"Class",CM_DRP_CLASS},std::pair{"Description",CM_DRP_DEVICEDESC},std::pair{"Service",CM_DRP_SERVICE},std::pair{"Driver key",CM_DRP_DRIVER}}){
            failure.clear();const auto value=cmProperty(parent,key,failure);phase.rows.push_back({prefix+name,value.empty()?"not reported; "+failure:text(value)});
            if(key==CM_DRP_SERVICE&&!value.empty())services.insert(value);else if(key==CM_DRP_DRIVER)driver=value;
        }
        driverPackage(phase.rows,driver,prefix);current=parent;
        if(depth==8){DEVINST extra{};if(CM_Get_Parent(&extra,current,0)==CR_SUCCESS)phase.failed("PnP parent completeness","8 ancestor limit reached; remaining parents not inferred");}
    }
}
inline void deviceBinding(Phase& phase,HDEVINFO devices,SP_DEVINFO_DATA& device,std::set<std::wstring>& services,std::set<std::wstring>& seen){
    if(phase.ended())return;
    std::string failed;const auto id=instanceId(device.DevInst,failed);
    if(id.empty()){phase.failed("PnP instance",failed);return;}
    // Explicit binding for EVERY HID interface, even if another interface has
    // already emitted this same devnode's driver properties.
    phase.rows.push_back({"PnP instance binding",text(id)});
    // Documented DEVPKEY_Device_ContainerId value, local to avoid an external
    // GUID symbol dependency in every translation unit including this helper.
    constexpr DEVPROPKEY containerKey{{0x8c7ed206,0x3f8a,0x4827,{0xb3,0xab,0xae,0x9e,0x1f,0xae,0xfc,0x6c}},2};
    GUID container{};DEVPROPTYPE type{};DWORD bytes{};
    const bool containerOk=SetupDiGetDevicePropertyW(devices,&device,&containerKey,&type,reinterpret_cast<BYTE*>(&container),sizeof(container),&bytes,0)!=FALSE;
    const auto containerError=containerOk?ERROR_SUCCESS:GetLastError();
    if(containerOk&&type==DEVPROP_TYPE_GUID&&bytes==sizeof(container)){
        wchar_t value[40]{};if(StringFromGUID2(container,value,40))phase.rows.push_back({"PnP container",text(value)});
        else phase.failed("PnP container","GUID formatting failed");
    }else {phase.rows.push_back({"PnP container",containerOk?"invalid native property type/size":"not reported; "+error(containerError)});if(containerOk||(containerError!=ERROR_NOT_FOUND&&containerError!=ERROR_INVALID_DATA))phase.rows.complete=false;}
    if(!seen.insert(id).second){phase.rows.push_back({"PnP metadata","same exact instance emitted above; no VID/PID/name inference"});return;}
    std::wstring service,driver;
    for(auto [caption,key]:{std::pair{"Class",SPDRP_CLASS},std::pair{"Description",SPDRP_DEVICEDESC},std::pair{"Hardware IDs",SPDRP_HARDWAREID},std::pair{"Compatible IDs",SPDRP_COMPATIBLEIDS},std::pair{"Service",SPDRP_SERVICE},std::pair{"Driver key",SPDRP_DRIVER},std::pair{"Enumerator",SPDRP_ENUMERATOR_NAME},std::pair{"Location paths",SPDRP_LOCATION_PATHS},std::pair{"Location information",SPDRP_LOCATION_INFORMATION}}){
        if(phase.ended())return;failed.clear();const auto value=property(devices,device,key,failed);
        phase.rows.push_back({caption,value.empty()?"not reported; "+failed:text(value)});
        if(key==SPDRP_SERVICE)service=value;else if(key==SPDRP_DRIVER)driver=value;
    }
    if(!service.empty())services.insert(service);driverPackage(phase.rows,driver,"");parentBinding(phase,device.DevInst,services);
}
inline void hidSchema(Phase& phase,PHIDP_PREPARSED_DATA parsed,const HIDP_CAPS& caps,
                      decltype(&HidP_GetValueCaps) getValues,decltype(&HidP_GetButtonCaps) getButtons){
    constexpr unsigned limit=2048;
    const std::array types{HidP_Input,HidP_Output,HidP_Feature};
    const std::array<const char*,3> names{"input","output","feature"};
    const std::array<USHORT,3> valueCounts{caps.NumberInputValueCaps,caps.NumberOutputValueCaps,caps.NumberFeatureValueCaps};
    const std::array<USHORT,3> buttonCounts{caps.NumberInputButtonCaps,caps.NumberOutputButtonCaps,caps.NumberFeatureButtonCaps};
    for(unsigned i=0;i<types.size()&&!phase.ended();++i){
        std::set<unsigned> ids;
        if(valueCounts[i]>limit)phase.failed(std::string("HID ")+names[i]+" value caps","descriptor metadata exceeds2048 cap records; schema partial");
        else if(valueCounts[i]){
            std::vector<HIDP_VALUE_CAPS> values(valueCounts[i]);USHORT count=static_cast<USHORT>(values.size());
            const auto code=getValues(types[i],values.data(),&count,parsed);
            if(code!=HIDP_STATUS_SUCCESS||count>values.size())phase.failed(std::string("HID ")+names[i]+" value caps",std::format("HidP_GetValueCaps=0x{:08X} count={}",static_cast<unsigned long>(code),count));
            else for(unsigned n=0;n<count&&!phase.ended();++n){const auto& cap=values[n];ids.insert(cap.ReportID);
                const auto low=cap.IsRange?cap.Range.UsageMin:cap.NotRange.Usage;
                const auto high=cap.IsRange?cap.Range.UsageMax:cap.NotRange.Usage;
                phase.rows.push_back({std::string("HID ")+names[i]+" value cap",std::format("report_id={} usage_page=0x{:04X} usage=0x{:04X}..0x{:04X} link={} bit_size={} report_count={} logical={}..{} physical={}..{} units=0x{:X} units_exp=0x{:X}; parsed descriptor only",cap.ReportID,cap.UsagePage,low,high,cap.LinkCollection,cap.BitSize,cap.ReportCount,cap.LogicalMin,cap.LogicalMax,cap.PhysicalMin,cap.PhysicalMax,cap.Units,cap.UnitsExp)});
            }
        }
        if(buttonCounts[i]>limit)phase.failed(std::string("HID ")+names[i]+" button caps","descriptor metadata exceeds2048 cap records; schema partial");
        else if(buttonCounts[i]){
            std::vector<HIDP_BUTTON_CAPS> buttons(buttonCounts[i]);USHORT count=static_cast<USHORT>(buttons.size());
            const auto code=getButtons(types[i],buttons.data(),&count,parsed);
            if(code!=HIDP_STATUS_SUCCESS||count>buttons.size())phase.failed(std::string("HID ")+names[i]+" button caps",std::format("HidP_GetButtonCaps=0x{:08X} count={}",static_cast<unsigned long>(code),count));
            else for(unsigned n=0;n<count&&!phase.ended();++n){const auto& cap=buttons[n];ids.insert(cap.ReportID);
                phase.rows.push_back({std::string("HID ")+names[i]+" button cap",std::format("report_id={} usage_page=0x{:04X} usage=0x{:04X}..0x{:04X} link={}; parsed descriptor only",cap.ReportID,cap.UsagePage,cap.IsRange?cap.Range.UsageMin:cap.NotRange.Usage,cap.IsRange?cap.Range.UsageMax:cap.NotRange.Usage,cap.LinkCollection)});
            }
        }
        std::string list;for(const auto id:ids){if(!list.empty())list+=',';list+=std::to_string(id);}
        phase.rows.push_back({std::string("HID ")+names[i]+" report IDs",list.empty()?"none in returned descriptor capabilities":list});
    }
}
inline void hidInventory(Phase& phase,std::set<std::wstring>& services,std::set<std::wstring>& seen){
    Library hid(L"hid.dll");
    const auto guid=hid.value?reinterpret_cast<decltype(&HidD_GetHidGuid)>(GetProcAddress(hid.value,"HidD_GetHidGuid")):nullptr;
    const auto attributes=hid.value?reinterpret_cast<decltype(&HidD_GetAttributes)>(GetProcAddress(hid.value,"HidD_GetAttributes")):nullptr;
    const auto preparsed=hid.value?reinterpret_cast<decltype(&HidD_GetPreparsedData)>(GetProcAddress(hid.value,"HidD_GetPreparsedData")):nullptr;
    const auto freeData=hid.value?reinterpret_cast<decltype(&HidD_FreePreparsedData)>(GetProcAddress(hid.value,"HidD_FreePreparsedData")):nullptr;
    const auto capabilities=hid.value?reinterpret_cast<decltype(&HidP_GetCaps)>(GetProcAddress(hid.value,"HidP_GetCaps")):nullptr;
    const auto values=hid.value?reinterpret_cast<decltype(&HidP_GetValueCaps)>(GetProcAddress(hid.value,"HidP_GetValueCaps")):nullptr;
    const auto buttons=hid.value?reinterpret_cast<decltype(&HidP_GetButtonCaps)>(GetProcAddress(hid.value,"HidP_GetButtonCaps")):nullptr;
    if(!guid||!attributes||!preparsed||!freeData||!capabilities){phase.failed("HID metadata","system hid.dll descriptor exports unavailable");return;}
    if(!values||!buttons)phase.failed("HID schema metadata","HidP capability-array exports unavailable; base descriptor metadata retained");
    GUID hidClass{};guid(&hidClass);Devices hids{SetupDiGetClassDevsW(&hidClass,nullptr,nullptr,DIGCF_PRESENT|DIGCF_DEVICEINTERFACE)};
    if(hids.value==INVALID_HANDLE_VALUE){phase.failed("HID interfaces",error(GetLastError()));return;}
    for(DWORD i=0;i<65536&&!phase.ended();++i){
        SP_DEVICE_INTERFACE_DATA entry{sizeof(entry)};
        if(!SetupDiEnumDeviceInterfaces(hids.value,nullptr,&hidClass,i,&entry)){
            const auto code=GetLastError();if(code!=ERROR_NO_MORE_ITEMS)phase.failed("HID enumeration",error(code));break;
        }
        DWORD size{};SetupDiGetDeviceInterfaceDetailW(hids.value,&entry,nullptr,0,&size,nullptr);
        if(size<sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)||size>1024*1024){phase.failed("HID interface detail","invalid descriptor size; "+error(GetLastError()));continue;}
        std::vector<BYTE> buffer(size);auto* detail=reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buffer.data());detail->cbSize=sizeof(*detail);
        SP_DEVINFO_DATA device{sizeof(device)};
        if(!SetupDiGetDeviceInterfaceDetailW(hids.value,&entry,detail,size,nullptr,&device)){phase.failed("HID interface detail",error(GetLastError()));continue;}
        phase.rows.push_back({"HID interface",text(detail->DevicePath)});deviceBinding(phase,hids.value,device,services,seen);
        if(phase.ended())break;
        Handle handle{CreateFileW(detail->DevicePath,0,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr)};
        if(handle.value==INVALID_HANDLE_VALUE){phase.failed("HID metadata open",error(GetLastError()));continue;}
        HIDD_ATTRIBUTES info{sizeof(info)};const bool attributesOk=attributes(handle.value,&info)!=FALSE;
        if(attributesOk)phase.rows.push_back({"HID attributes",std::format("VID={:04X} PID={:04X} revision={:04X}; revision is not OEM firmware/protocol version",info.VendorID,info.ProductID,info.VersionNumber)});
        else phase.failed("HID attributes",error(GetLastError()));
        PHIDP_PREPARSED_DATA parsed{};
        if(!preparsed(handle.value,&parsed)){phase.failed("HID preparsed metadata",error(GetLastError()));continue;}
        struct FreeParsed{PHIDP_PREPARSED_DATA data;decltype(&HidD_FreePreparsedData) release;~FreeParsed(){release(data);}} release{parsed,freeData};
        HIDP_CAPS caps{};const auto code=capabilities(parsed,&caps);
        if(code!=HIDP_STATUS_SUCCESS){phase.failed("HID capabilities",std::format("HidP_GetCaps=0x{:08X}",static_cast<unsigned long>(code)));continue;}
        phase.rows.push_back({"HID report descriptors",std::format("usage_page=0x{:04X} usage=0x{:04X} input_bytes={} output_bytes={} feature_bytes={}; descriptor lengths include ReportID where used; reports not requested",caps.UsagePage,caps.Usage,caps.InputReportByteLength,caps.OutputReportByteLength,caps.FeatureReportByteLength)});
        if(attributesOk){
            const hydroc16g2::KeyboardHidDescriptor descriptor{info.VendorID,info.ProductID,info.VersionNumber,caps.UsagePage,caps.Usage,caps.FeatureReportByteLength};
            const auto candidate=hydroc16g2::keyboardHidCandidateText(hydroc16g2::keyboardHidCandidate(descriptor));
            if(!candidate.empty())phase.rows.push_back({"HYDROC OEM ITE descriptor candidate",candidate+"; discovery only, actual board/firmware/type not inferred; writer permission=false"});
        }
        if(values&&buttons)hidSchema(phase,parsed,caps,values,buttons);
        if(i==65535)phase.failed("HID completeness","65536 interface limit reached; further devices not inferred");
    }
}
inline Rows nativeInterfaces(std::stop_token stop,const std::function<void(const InventoryRow&)>& emit={}){
    Rows rows;rows.emit=emit;rows.push_back({"Метод","Read-only HID/SetupAPI/ConfigMgr/SCM metadata; no feature/output reports, EC commands, LampArray acquisition, driver install or service start"});
    const auto begin=Clock::now(),deadline=begin+std::chrono::seconds(19);std::set<std::wstring> services,seen;
    // HID comes first. Slow disk hashing/offline signature verification must
    // never consume the entire helper deadline before descriptors are attempted.
    Phase hid{rows,stop,std::min(deadline,Clock::now()+std::chrono::seconds(10)),"HID"};hidInventory(hid,services,seen);
    rows.push_back({"HID phase","ended; per-interface status above, no lighting permission inferred"});
    Phase setup{rows,stop,std::min(deadline,Clock::now()+std::chrono::seconds(5)),"SetupAPI"};
    if(!setup.ended()){
        Devices devices{SetupDiGetClassDevsW(nullptr,nullptr,nullptr,DIGCF_PRESENT|DIGCF_ALLCLASSES)};
        if(devices.value==INVALID_HANDLE_VALUE)setup.failed("SetupAPI native inventory",error(GetLastError()));
        else for(DWORD i=0;i<65536&&!setup.ended();++i){
            SP_DEVINFO_DATA device{sizeof(device)};
            if(!SetupDiEnumDeviceInfo(devices.value,i,&device)){const auto code=GetLastError();if(code!=ERROR_NO_MORE_ITEMS)setup.failed("SetupAPI enumeration",error(code));break;}
            std::string failed;const auto id=instanceId(device.DevInst,failed);
            if(id.empty()){setup.failed("Device instance",failed);continue;}
            const auto klass=property(devices.value,device,SPDRP_CLASS,failed);
            if(klass.empty()&&!failed.empty())setup.failed("SetupAPI class "+text(id),failed);
            if(_wcsicmp(klass.c_str(),L"System")&&_wcsicmp(klass.c_str(),L"HIDClass")&&_wcsicmp(klass.c_str(),L"Keyboard")&&_wcsnicmp(id.c_str(),L"ACPI\\",5))continue;
            if(!seen.contains(id)){rows.push_back({"Device instance",text(id)});deviceBinding(setup,devices.value,device,services,seen);}
            if(i==65535)setup.failed("SetupAPI completeness","65536 device limit reached; further devices not inferred");
        }
    }
    Phase evidence{rows,stop,std::min(deadline,Clock::now()+std::chrono::seconds(4)),"SCM/image evidence"};
    if(!evidence.ended()){
        Service manager{OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT)};
        if(!manager.value)evidence.failed("SCM metadata",error(GetLastError()));
        for(const auto& service:services){if(evidence.ended())break;serviceEvidence(rows,manager.value,service,stop,evidence.deadline);}
    }
    rows.push_back({"Native metadata completeness",std::format("complete={} elapsed_ms={}; phase loops bounded10/5/4s, parent helper total deadline20s; unsupported/denied endpoints remain errors",rows.complete,std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-begin).count())});
    return rows;
}
} // namespace mc::windows::inventory
