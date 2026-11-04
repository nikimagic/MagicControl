#include "ResourceDriversWindows.h"
#include "uniwill-acpi/ReviewedDriverWindows.h"
#include <setupapi.h>
#include <windows.h>
#include <iterator>
#include <stdexcept>
#include <utility>

namespace mc::windows {
ResourceDriverObservation projectPawnIoDriver(const dependencies::PawnIoDriverStatus& value){
    ResourceDriverObservation out;out.name="PawnIO";out.binding="Windows service PawnIO";
    out.installed=value.installed;out.running=value.running;
    if(!value.fileVersion.empty())out.fileVersion=value.fileVersion;
    if(!value.loadedVersion.empty())out.loadedVersion=value.loadedVersion;
    out.status=value.installed?"SCM state; installed SYS metadata and explicit loaded driver version evidence":"SCM reports service absent";
    if(!value.versionStatus.empty())out.status+="; "+value.versionStatus;
    if(!value.loadedVersionStatus.empty())out.status+="; "+value.loadedVersionStatus;
    return out;
}
std::vector<ResourceDriverObservation> projectUniwillBindings(std::span<const UniwillBindingEvidence> bindings,bool complete,std::string status){
    std::vector<ResourceDriverObservation> out;
    for(const auto& binding:bindings){ResourceDriverObservation row;row.name="ACPI INOU0000";
        row.binding=binding.instance+(binding.service.empty()?"":" · service "+binding.service);
        if(!binding.packageVersion.empty())row.packageVersion=binding.packageVersion;
        row.status="Present PnP device; registry package metadata only, installed SYS and loaded version not queried";
        if(!complete)row.status+="; enumeration incomplete";
        out.push_back(std::move(row));}
    if(!complete||bindings.empty()){
        ResourceDriverObservation row;row.name="ACPI INOU0000";
        row.status=!complete?"PnP enumeration incomplete; installation and running state unknown":"No present matching PnP device; driver installation and running state unknown";
        if(!status.empty())row.status+="; "+status;out.push_back(std::move(row));
    }
    return out;
}
std::vector<ResourceDriverObservation> observeResourceDrivers(){
    std::vector<ResourceDriverObservation> out;
    try{out.push_back(projectPawnIoDriver(dependencies::pawnIoDriverStatus(dependencies::PawnIoVersionQuery::LoadedDriver)));}
    catch(const std::exception& error){ResourceDriverObservation row;row.name="PawnIO";row.binding="Windows service PawnIO";
        row.status=std::string("SCM/installed-file metadata unavailable; state unknown: ")+error.what();out.push_back(std::move(row));}
    const auto set=SetupDiGetClassDevsW(nullptr,L"ACPI",nullptr,DIGCF_PRESENT|DIGCF_ALLCLASSES);
    if(set==INVALID_HANDLE_VALUE){auto rows=projectUniwillBindings({},false,"SetupDi error "+std::to_string(GetLastError()));
        out.insert(out.end(),std::make_move_iterator(rows.begin()),std::make_move_iterator(rows.end()));return out;}
    struct Guard{HDEVINFO value;~Guard(){SetupDiDestroyDeviceInfoList(value);}} guard{set};
    std::vector<UniwillBindingEvidence> found;bool complete=false,missingInstance=false;std::string status;
    for(DWORD i=0;i<4096;++i){SP_DEVINFO_DATA device{sizeof(device)};
        if(!SetupDiEnumDeviceInfo(set,i,&device)){const auto code=GetLastError();complete=code==ERROR_NO_MORE_ITEMS;
            if(!complete)status="SetupDi enumeration error "+std::to_string(code);break;}
        wchar_t id[512]{};if(!SetupDiGetDeviceInstanceIdW(set,&device,id,512,nullptr)){missingInstance=true;continue;}
        if(_wcsnicmp(id,L"ACPI\\INOU0000\\",14)!=0)continue;
        const auto property=[&](DWORD key){wchar_t value[2048]{};DWORD type{};
            if(!SetupDiGetDeviceRegistryPropertyW(set,&device,key,&type,reinterpret_cast<BYTE*>(value),sizeof(value),nullptr)||type!=REG_SZ)return std::wstring{};
            value[2047]=0;return std::wstring(value);};
        const auto service=property(SPDRP_SERVICE),driver=property(SPDRP_DRIVER);
        const auto version=driver.empty()?std::wstring{}:reviewed_uniwill::registry(HKEY_LOCAL_MACHINE,L"SYSTEM\\CurrentControlSet\\Control\\Class\\"+driver,L"DriverVersion");
        found.push_back({reviewed_uniwill::text(id),reviewed_uniwill::text(service),reviewed_uniwill::text(version)});
    }
    if(missingInstance){complete=false;if(!status.empty())status+="; ";status+="one or more device IDs unreadable";}
    if(!complete&&status.empty())status="device enumeration limit reached";
    auto rows=projectUniwillBindings(found,complete,std::move(status));
    out.insert(out.end(),std::make_move_iterator(rows.begin()),std::make_move_iterator(rows.end()));return out;
}
WindowsDriverResourceProvider::WindowsDriverResourceProvider(const BoardDescriptor& board,
    std::function<std::vector<ResourceDriverObservation>()> read):board_(board),read_(std::move(read)){
    if(!read_)throw std::invalid_argument("Driver metadata provider requires a reader");
}
ResourceCatalog WindowsDriverResourceProvider::observe(){auto catalog=observeMachineResources(board_,{});catalog.drivers=read_();return catalog;}
}
