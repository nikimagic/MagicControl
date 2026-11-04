#pragma once
#include "WmiQueryWindows.h"
#include <winioctl.h>
#include <setupapi.h>
#include <poclass.h>
#include <utility>
#include <climits>

namespace mc::windows::battery {
struct Device {
    HANDLE handle{INVALID_HANDLE_VALUE};ULONG tag{};BATTERY_INFORMATION information{};
    std::wstring path;std::string id;
    Device()=default;Device(const Device&)=delete;Device& operator=(const Device&)=delete;
    Device(Device&& other) noexcept:handle(std::exchange(other.handle,INVALID_HANDLE_VALUE)),tag(other.tag),information(other.information),path(std::move(other.path)),id(std::move(other.id)){}
    Device& operator=(Device&& other) noexcept{if(this!=&other){if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);handle=std::exchange(other.handle,INVALID_HANDLE_VALUE);tag=other.tag;information=other.information;path=std::move(other.path);id=std::move(other.id);}return *this;}
    ~Device(){if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);}
    bool query(BATTERY_QUERY_INFORMATION_LEVEL level,void* out,DWORD size,DWORD& returned)const{
        BATTERY_QUERY_INFORMATION request{tag,level,0};
        return DeviceIoControl(handle,IOCTL_BATTERY_QUERY_INFORMATION,&request,sizeof(request),out,size,&returned,nullptr)&&returned<=size;
    }
    std::string string(BATTERY_QUERY_INFORMATION_LEVEL level)const{
        wchar_t data[1024]{};DWORD returned{};
        if(!query(level,data,sizeof(data)-sizeof(wchar_t),returned)||returned<sizeof(wchar_t))return "not reported; Win32="+std::to_string(GetLastError());
        return wmi::utf8(data);
    }
};
struct Enumeration {std::vector<Device> devices;std::vector<std::string> errors;bool complete{true};};
// Shared by inventory and monitoring. Never aggregate the tag/capacity/temperature
// of one system battery with another, and never treat an UPS as laptop battery.
inline Enumeration enumerate(std::stop_token cancellation={},const std::function<void(const Device&)>& onDevice={}){
    Enumeration result;
    constexpr GUID guid{0x72631e54,0x78a4,0x11d0,{0xbc,0xf7,0x00,0xaa,0x00,0xb7,0xb3,0x2a}};
    const auto list=SetupDiGetClassDevsW(&guid,nullptr,nullptr,DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);
    if(list==INVALID_HANDLE_VALUE){result.complete=false;result.errors.push_back("SetupAPI battery enumeration: Win32="+std::to_string(GetLastError()));return result;}
    struct Close{HDEVINFO h;~Close(){SetupDiDestroyDeviceInfoList(h);}} close{list};
    for(DWORD i=0;i<65536;++i){
        if(cancellation.stop_requested()){result.complete=false;result.errors.push_back("battery enumeration cancelled; partial devices retained");break;}
        SP_DEVICE_INTERFACE_DATA entry{sizeof(entry)};
        if(!SetupDiEnumDeviceInterfaces(list,nullptr,&guid,i,&entry)){
            const auto code=GetLastError();if(code!=ERROR_NO_MORE_ITEMS){result.complete=false;result.errors.push_back("battery interface enumeration: Win32="+std::to_string(code));}break;
        }
        DWORD bytes{};SetupDiGetDeviceInterfaceDetailW(list,&entry,nullptr,0,&bytes,nullptr);
        if(bytes<sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)||bytes>1024*1024){result.errors.push_back("battery interface metadata: invalid size");result.complete=false;continue;}
        std::vector<BYTE> data(bytes);auto* detail=reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(data.data());detail->cbSize=sizeof(*detail);
        SP_DEVINFO_DATA device{sizeof(device)};
        if(!SetupDiGetDeviceInterfaceDetailW(list,&entry,detail,bytes,nullptr,&device)){result.errors.push_back("battery interface detail: Win32="+std::to_string(GetLastError()));result.complete=false;continue;}
        Device battery;battery.path=detail->DevicePath;
        wchar_t instance[4096]{};
        battery.id=SetupDiGetDeviceInstanceIdW(list,&device,instance,4096,nullptr)?wmi::utf8(instance):wmi::utf8(battery.path);
        battery.handle=CreateFileW(battery.path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
        if(battery.handle==INVALID_HANDLE_VALUE){result.errors.push_back(battery.id+": open Win32="+std::to_string(GetLastError()));result.complete=false;continue;}
        DWORD wait{},returned{};
        if(!DeviceIoControl(battery.handle,IOCTL_BATTERY_QUERY_TAG,&wait,sizeof(wait),&battery.tag,sizeof(battery.tag),&returned,nullptr)||returned!=sizeof(battery.tag)||!battery.tag){result.errors.push_back(battery.id+": query tag Win32="+std::to_string(GetLastError()));result.complete=false;continue;}
        if(!battery.query(BatteryInformation,&battery.information,sizeof(battery.information),returned)||returned<sizeof(battery.information)){result.errors.push_back(battery.id+": BatteryInformation Win32="+std::to_string(GetLastError()));result.complete=false;continue;}
        if(!(battery.information.Capabilities&BATTERY_SYSTEM_BATTERY)||(battery.information.Capabilities&BATTERY_IS_SHORT_TERM))continue;
        result.devices.push_back(std::move(battery));
        if(onDevice)onDevice(result.devices.back());
        if(i==65535){result.complete=false;result.errors.push_back("battery interface safety limit reached");}
    }
    std::stable_sort(result.devices.begin(),result.devices.end(),[](const auto& a,const auto& b){return a.id<b.id;});
    return result;
}
inline bool metadata(const std::function<void(const InventoryRow&)>& emit,std::stop_token stop={}){
    unsigned count{};
    const auto devices=enumerate(stop,[&](const Device& battery){
        const auto& info=battery.information;emit({"Батарея "+std::to_string(++count),battery.id});
        emit({"Идентификатор Windows",battery.id});emit({"Интерфейс Windows",wmi::utf8(battery.path)});
        emit({"Модель",battery.string(BatteryDeviceName)});emit({"Производитель",battery.string(BatteryManufactureName)});
        emit({"Серийный номер",battery.string(BatterySerialNumber)});emit({"Уникальный идентификатор батареи",battery.string(BatteryUniqueID)});
        emit({"Химия",std::string(reinterpret_cast<const char*>(info.Chemistry),strnlen(reinterpret_cast<const char*>(info.Chemistry),4))});
        const bool relative=(info.Capabilities&BATTERY_CAPACITY_RELATIVE)!=0;
        const auto energy=[&](ULONG value){return relative||value==BATTERY_UNKNOWN_CAPACITY?std::string(kInventoryUnknown):std::format("{:.2f} Вт·ч",value/1000.);};
        emit({"Проектная / полная ёмкость",energy(info.DesignedCapacity)+" / "+energy(info.FullChargedCapacity)});
        emit({"Износ",info.DesignedCapacity&&info.DesignedCapacity!=BATTERY_UNKNOWN_CAPACITY&&info.FullChargedCapacity!=BATTERY_UNKNOWN_CAPACITY?
            std::format("{:.1f} %",std::max(0.,100.*(1.-double(info.FullChargedCapacity)/info.DesignedCapacity))):kInventoryUnknown});
        emit({"Циклы",info.CycleCount&&info.CycleCount!=ULONG_MAX?std::to_string(info.CycleCount):kInventoryUnknown});
        BATTERY_MANUFACTURE_DATE date{};DWORD returned{};
        emit({"Изготовление",battery.query(BatteryManufactureDate,&date,sizeof(date),returned)&&returned>=sizeof(date)&&date.Year>=1980&&date.Month>=1&&date.Month<=12&&date.Day>=1&&date.Day<=31?
            std::format("{:02}.{:02}.{:04}",date.Day,date.Month,date.Year):kInventoryUnknown});
        emit({"Источник","Windows · Battery Class IOCTL (только чтение); independent system battery"});
    });
    for(const auto& error:devices.errors)emit({"Ошибка интерфейса батареи",error});
    emit({"Полнота списка батарей",std::format("system_batteries={} complete={}",devices.devices.size(),devices.complete)});
    if(!count)emit({"Подробности батареи","Драйвер не вернул сведения о системных батареях."});
    return devices.complete;
}
} // namespace mc::windows::battery
