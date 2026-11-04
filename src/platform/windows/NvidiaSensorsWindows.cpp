#include "NvidiaSensorsWindows.h"
#include <windows.h>
#include <setupapi.h>
#include <devguid.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <format>
#include <map>
#include <mutex>
namespace mc::windows {
std::vector<NvidiaPowerObservation> nvidiaPowerObservations(){
    std::vector<NvidiaPowerObservation> rows;
    const auto devices=SetupDiGetClassDevsW(&GUID_DEVCLASS_DISPLAY,nullptr,nullptr,DIGCF_PRESENT);
    if(devices==INVALID_HANDLE_VALUE)return rows;
    SP_DEVINFO_DATA dev{sizeof(dev)};
    for(DWORD i=0;i<64&&SetupDiEnumDeviceInfo(devices,i,&dev);++i){wchar_t id[512]{};
        if(!SetupDiGetDeviceInstanceIdW(devices,&dev,id,512,nullptr)||_wcsnicmp(id,L"PCI\\VEN_10DE",12))continue;
        DWORD bus{},address{};const auto property=[&](DWORD key,void* out,DWORD size){return SetupDiGetDeviceRegistryPropertyW(devices,&dev,key,nullptr,static_cast<BYTE*>(out),size,nullptr)!=FALSE;};
        if(!property(SPDRP_BUSNUMBER,&bus,sizeof(bus))||!property(SPDRP_ADDRESS,&address,sizeof(address)))continue;
        NvidiaPowerObservation r;r.bus=bus;r.device=address>>16;r.function=address&65535;r.sampledAt=GetTickCount64();
        const int n=WideCharToMultiByte(CP_UTF8,0,id,-1,nullptr,0,nullptr,nullptr);
        if(n>1){r.instance.resize(n);WideCharToMultiByte(CP_UTF8,0,id,-1,r.instance.data(),n,nullptr,nullptr);r.instance.pop_back();}
        CM_POWER_DATA data{};data.PD_Size=sizeof(data);
        if(property(SPDRP_DEVICE_POWER_DATA,&data,sizeof(data))&&data.PD_MostRecentPowerState>=PowerDeviceD0&&data.PD_MostRecentPowerState<=PowerDeviceD3){
            r.state=static_cast<unsigned>(data.PD_MostRecentPowerState)-static_cast<unsigned>(PowerDeviceD0);
            r.status=std::format("PnP most recent D{}; caps=0x{:X}; not proof of runtime D3/D3cold or MUX mode",*r.state,data.PD_Capabilities);
        }else r.status="PnP power state unavailable/unspecified; Win32="+std::to_string(GetLastError());
        rows.push_back(std::move(r));
    }
    SetupDiDestroyDeviceInfoList(devices);return rows;
}
namespace {
using Gpu=void*;using Status=int;
// Public NVAPI thermal ABI: NVIDIA/nvapi nvapi.h and nvapi_interface.h.
struct ThermalSensor {int controller,min,max,current,target;};
struct ThermalSettings {unsigned version,count;ThermalSensor sensor[3];};
// Optional, undocumented read-only GetThermalSensors ABI (0x65FE3AAD),
// independently declared from the LibreHardwareMonitor interoperability contract.
// It can disappear/change in a driver. Never use a successful call as a setter capability.
struct ThermalChannels {unsigned version,mask;int reserved[8],temperature[32];};
static_assert(sizeof(ThermalSettings)==68&&sizeof(ThermalChannels)==168);
struct Runtime {
    HMODULE dll{};bool initialized{};std::mutex gate;
    void*(__cdecl* query)(unsigned){};
    Status(__cdecl* init)(){};Status(__cdecl* unload)(){};
    Status(__cdecl* enumerate)(Gpu*,unsigned*){};
    Status(__cdecl* bus)(Gpu,unsigned*){};Status(__cdecl* slot)(Gpu,unsigned*){};
    Status(__cdecl* pci)(Gpu,unsigned*,unsigned*,unsigned*,unsigned*){};
    Status(__cdecl* thermal)(Gpu,int,ThermalSettings*){};
    Status(__cdecl* channels)(Gpu,ThermalChannels*){};
    template<class T>T api(unsigned id){return query?reinterpret_cast<T>(query(id)):nullptr;}
    Runtime(){
        dll=LoadLibraryExW(L"nvapi64.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(!dll)return;
        query=reinterpret_cast<decltype(query)>(GetProcAddress(dll,"nvapi_QueryInterface"));
        init=api<decltype(init)>(0x0150E828);unload=api<decltype(unload)>(0xD22BDD7E);
        if(!init||!unload||init()!=0)return;
        initialized=true;
        enumerate=api<decltype(enumerate)>(0xE5AC921F);bus=api<decltype(bus)>(0x1BE0B8E5);
        slot=api<decltype(slot)>(0x2A0A350F);pci=api<decltype(pci)>(0x2DDFB66E);
        thermal=api<decltype(thermal)>(0xE3640A56);channels=api<decltype(channels)>(0x65FE3AAD);
    }
    ~Runtime(){if(initialized&&unload)unload();if(dll)FreeLibrary(dll);}
};
std::shared_ptr<Runtime> runtime(){static std::mutex gate;static std::weak_ptr<Runtime> cached;
    std::lock_guard lock(gate);auto p=cached.lock();if(!p){p=std::make_shared<Runtime>();cached=p;}return p;}
std::optional<double> temperature(double value){return std::isfinite(value)&&value>0&&value<=150?std::optional<double>(value):std::nullopt;}
}
struct NvidiaSensors::Impl {
    std::shared_ptr<Runtime> lib;
    struct Mask {unsigned value{};ULONGLONG retryAt{};};std::map<std::string,Mask> masks;
    void read(GpuReading& r,const NvidiaPciIdentity& id){
        if(!lib)lib=runtime();std::lock_guard lock(lib->gate);
        const auto note=[&](const char* name,int code){r.apiStatus.push_back(std::format("{}: NVAPI status={}",name,code));};
        if(!lib->initialized||!lib->enumerate||!lib->bus||!lib->slot||!lib->pci){r.apiStatus.push_back("NVAPI: runtime/PCI identity APIs unavailable");return;}
        // NVAPI's bus/slot API does not identify PCI domains. Do not silently
        // attach domain>0 devices or an ambiguous duplicate to domain0.
        if(id.domain){r.apiStatus.push_back("NVAPI: nonzero PCI domain; identity not provable");return;}
        std::array<Gpu,64> handles{};unsigned count{};const auto code=lib->enumerate(handles.data(),&count);
        note("EnumPhysicalGPUs",code);if(code||count>handles.size())return;
        Gpu selected{};unsigned matches{};
        for(unsigned i=0;i<count;++i){unsigned bus{},slot{},device{},sub{},revision{},ext{};
            if(lib->bus(handles[i],&bus)||lib->slot(handles[i],&slot)||lib->pci(handles[i],&device,&sub,&revision,&ext))continue;
            if(bus==id.bus&&slot==id.device&&device==id.deviceId&&sub==id.subsystemId){selected=handles[i];++matches;}}
        if(matches!=1){r.apiStatus.push_back(std::format("NVAPI: PCI matches={}, supplement skipped",matches));return;}
        const auto add=[&](std::string metric,std::optional<double> value,std::string status,std::optional<double> raw={}){
            r.additionalSensors.push_back({r.uuid,std::move(metric),"C","NVIDIA NVAPI",std::move(status),value,raw,GetTickCount64()});};
        if(lib->thermal){ThermalSettings t{};t.version=sizeof(t)|(2u<<16);
            const auto result=lib->thermal(selected,15,&t);note("GetThermalSettings v2",result);
            if(!result&&t.count<=3)for(unsigned i=0;i<t.count;++i){const auto& s=t.sensor[i];const auto v=temperature(s.current);
                add("thermal/target"+std::to_string(s.target)+"/sensor"+std::to_string(i),v,std::format("controller={} target={} min={} max={}",s.controller,s.target,s.min,s.max),s.current);
                if(s.target==1&&v&&!r.temperature){r.temperature=static_cast<unsigned>(*v);r.temperatureSource="NVIDIA NVAPI · thermal target GPU";}
                if(s.target==2&&v&&!r.memoryCelsius)r.memoryCelsius=v;
            }
        }else r.apiStatus.push_back("GetThermalSettings: NVAPI export absent");
        if(!lib->channels){r.apiStatus.push_back("GetThermalSensors: optional NVAPI export absent");return;}
        auto& mask=masks[r.uuid];const auto now=GetTickCount64();
        if(!mask.value&&now>=mask.retryAt){
            mask.retryAt=now+30000;
            // Probe only this read-only API's accepted channel mask, at most32
            // calls on discovery. No register-address guessing and no workloads.
            for(unsigned bit=0;bit<32;++bit){ThermalChannels probe{};probe.version=sizeof(probe)|(2u<<16);probe.mask=1u<<bit;
                const auto result=lib->channels(selected,&probe);
                if(result){note("GetThermalSensors discovery",result);break;}
                mask.value|=1u<<bit;}
        }
        if(!mask.value){add("thermal_channels",{},"optional NVAPI channel mask unavailable");return;}
        ThermalChannels t{};t.version=sizeof(t)|(2u<<16);t.mask=mask.value;
        const auto result=lib->channels(selected,&t);note("GetThermalSensors v2",result);
        if(result){mask.value=0;mask.retryAt=now+30000;add("thermal_channels",{},"NVAPI status="+std::to_string(result));return;}
        for(unsigned i=0;i<32;++i)if(mask.value&(1u<<i)){
            add("thermal_channel"+std::to_string(i),temperature(t.temperature[i]/256.),std::format("optional ABI v2; mask=0x{:08X}; raw/256",mask.value),t.temperature[i]);}
        // Observed channel semantics are generation-specific. RTX40(Ada) is
        // corroborated by LHM; retain raw channels on all other GPUs instead of
        // guessing a hotspot/memory mapping (notably RTX50 has a different map).
        if(r.name.starts_with("NVIDIA GeForce RTX 40")){
            if(mask.value&2)r.hotspotCelsius=temperature(t.temperature[1]/256.);
            if(mask.value&128)r.memoryJunctionCelsius=temperature(t.temperature[7]/256.);
            add("hotspot_temperature",r.hotspotCelsius,"Ada channel1; optional NVAPI ABI v2",t.temperature[1]);
            add("memory_junction_temperature",r.memoryJunctionCelsius,"Ada channel7; optional NVAPI ABI v2",t.temperature[7]);
        }
    }
};
NvidiaSensors::NvidiaSensors():impl_(std::make_unique<Impl>()){}
NvidiaSensors::~NvidiaSensors()=default;
void NvidiaSensors::read(GpuReading& r,const NvidiaPciIdentity& id){impl_->read(r,id);}
}
