#include "MagicControl/IntelGpu.h"
#include <windows.h>
#define CTL_APIEXPORT
#define CTL_DLLEXPORT
#include "../../third_party/igcl/include/igcl_api.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <map>
#include <mutex>
#include <stdexcept>
namespace mc {
namespace {
std::string result(ctl_result_t code){return std::format("IGCL result=0x{:08X}",static_cast<std::uint32_t>(code));}
template<class T>T structure(std::uint8_t version=0){T value{};value.Size=sizeof(value);value.Version=version;return value;}
bool note(GpuReading& r,const char* api,ctl_result_t code){r.apiStatus.push_back(std::string(api)+": "+result(code));return code==CTL_RESULT_SUCCESS;}
struct Runtime {
    HMODULE library{};ctl_api_handle_t api{};std::mutex gate;
    std::string description;
    decltype(&ctlInit) init{};decltype(&ctlClose) close{};
    decltype(&ctlEnumerateDevices) devices{};decltype(&ctlGetDeviceProperties) properties{};
    decltype(&ctlPowerTelemetryGet) telemetry{};
    decltype(&ctlPowerTelemetryGetV2) telemetryV2{};
    decltype(&ctlEnumTemperatureSensors) temperatures{};decltype(&ctlTemperatureGetProperties) temperatureProperties{};decltype(&ctlTemperatureGetState) temperatureState{};
    decltype(&ctlEnumFrequencyDomains) frequencies{};decltype(&ctlFrequencyGetProperties) frequencyProperties{};decltype(&ctlFrequencyGetState) frequencyState{};
    decltype(&ctlEnumMemoryModules) memories{};decltype(&ctlMemoryGetProperties) memoryProperties{};decltype(&ctlMemoryGetState) memoryState{};decltype(&ctlMemoryGetBandwidth) memoryBandwidth{};
    Runtime(){
        // Same production search policy as Intel's official wrapper. Never
        // load an arbitrary ControlLib.dll beside a downloaded portable EXE.
        library=LoadLibraryExW(L"ControlLib.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(!library)throw std::runtime_error("Intel IGCL: ControlLib.dll драйвера недоступна, Win32="+std::to_string(GetLastError()));
        try{
#define LOAD(field,name) field=reinterpret_cast<decltype(field)>(GetProcAddress(library,name))
            LOAD(init,"ctlInit");LOAD(close,"ctlClose");LOAD(devices,"ctlEnumerateDevices");LOAD(properties,"ctlGetDeviceProperties");LOAD(telemetry,"ctlPowerTelemetryGet");
            LOAD(telemetryV2,"ctlPowerTelemetryGetV2");
            LOAD(temperatures,"ctlEnumTemperatureSensors");LOAD(temperatureProperties,"ctlTemperatureGetProperties");LOAD(temperatureState,"ctlTemperatureGetState");
            LOAD(frequencies,"ctlEnumFrequencyDomains");LOAD(frequencyProperties,"ctlFrequencyGetProperties");LOAD(frequencyState,"ctlFrequencyGetState");
            LOAD(memories,"ctlEnumMemoryModules");LOAD(memoryProperties,"ctlMemoryGetProperties");LOAD(memoryState,"ctlMemoryGetState");LOAD(memoryBandwidth,"ctlMemoryGetBandwidth");
#undef LOAD
            if(!init||!close||!devices||!properties)throw std::runtime_error("Intel IGCL: обязательные exports отсутствуют");
            auto args=structure<ctl_init_args_t>();args.AppVersion=CTL_IMPL_VERSION;args.flags=CTL_INIT_FLAG_USE_LEVEL_ZERO;
            const auto code=init(&args,&api);if(code!=CTL_RESULT_SUCCESS||!api)throw std::runtime_error("Intel IGCL ctlInit: "+result(code));
            wchar_t path[32768]{};GetModuleFileNameW(library,path,32768);
            DWORD unused{};const auto bytes=GetFileVersionInfoSizeW(path,&unused);std::string fileVersion="unavailable";
            if(bytes&&bytes<1024*1024){std::vector<unsigned char> info(bytes);
                if(GetFileVersionInfoW(path,0,bytes,info.data())){VS_FIXEDFILEINFO* fixed{};UINT length{};
                    if(VerQueryValueW(info.data(),L"\\",reinterpret_cast<void**>(&fixed),&length)&&length>=sizeof(*fixed))
                        fileVersion=std::format("{}.{}.{}.{}",HIWORD(fixed->dwFileVersionMS),LOWORD(fixed->dwFileVersionMS),HIWORD(fixed->dwFileVersionLS),LOWORD(fixed->dwFileVersionLS));}}
            description=std::format("ControlLib.dll version={}; ctlInit requested_API=0x{:08X} negotiated_API=0x{:08X} Size={} Version={} flags=0x{:X}; loaded from System32",fileVersion,CTL_IMPL_VERSION,args.AppVersion,args.Size,args.Version,args.flags);
        }catch(...){if(api&&close)close(api);FreeLibrary(library);library=nullptr;throw;}
    }
    ~Runtime(){if(api&&close)close(api);if(library)FreeLibrary(library);}
};
std::shared_ptr<Runtime> runtime(){static std::mutex gate;static std::weak_ptr<Runtime> cached;std::lock_guard lock(gate);auto value=cached.lock();if(!value){value=std::make_shared<Runtime>();cached=value;}return value;}
template<class Handle,class Fn,class Parent>std::vector<Handle> enumerate(Fn api,Parent parent,GpuReading& r,const char* name){
    if(!api){r.apiStatus.push_back(std::string(name)+": export absent");return {};}
    std::uint32_t count{};if(!note(r,name,api(parent,&count,nullptr)))return {};
    if(count>256){r.apiStatus.push_back(std::string(name)+": invalid count");return {};}
    if(!count)return {};
    std::vector<Handle> values(count);const auto capacity=count;
    if(!note(r,name,api(parent,&count,values.data()))||count>capacity)return {};
    values.resize(count);return values;
}
std::optional<double> value(const ctl_oc_telemetry_item_t& item){
    if(!item.bSupported)return {};double v{};
    switch(item.type){
    case CTL_DATA_TYPE_INT8:v=item.value.data8;break;case CTL_DATA_TYPE_UINT8:v=item.value.datau8;break;
    case CTL_DATA_TYPE_INT16:v=item.value.data16;break;case CTL_DATA_TYPE_UINT16:v=item.value.datau16;break;
    case CTL_DATA_TYPE_INT32:v=item.value.data32;break;case CTL_DATA_TYPE_UINT32:v=item.value.datau32;break;
    case CTL_DATA_TYPE_INT64:v=static_cast<double>(item.value.data64);break;case CTL_DATA_TYPE_UINT64:v=static_cast<double>(item.value.datau64);break;
    case CTL_DATA_TYPE_FLOAT:v=item.value.datafloat;break;case CTL_DATA_TYPE_DOUBLE:v=item.value.datadouble;break;default:return {};
    }return std::isfinite(v)?std::optional<double>(v):std::nullopt;
}
std::string unit(ctl_units_t v){switch(v){
    case CTL_UNITS_FREQUENCY_MHZ:return "MHz";case CTL_UNITS_OPERATIONS_GTS:return "GT/s";case CTL_UNITS_OPERATIONS_MTS:return "MT/s";
    case CTL_UNITS_VOLTAGE_VOLTS:return "V";case CTL_UNITS_POWER_WATTS:return "W";case CTL_UNITS_TEMPERATURE_CELSIUS:return "C";
    case CTL_UNITS_ENERGY_JOULES:return "J";case CTL_UNITS_TIME_SECONDS:return "s";case CTL_UNITS_MEMORY_BYTES:return "bytes";
    case CTL_UNITS_ANGULAR_SPEED_RPM:return "RPM";case CTL_UNITS_POWER_MILLIWATTS:return "mW";case CTL_UNITS_PERCENT:return "percent";
    case CTL_UNITS_MEM_SPEED_GBPS:return "GB/s";case CTL_UNITS_VOLTAGE_MILLIVOLTS:return "mV";case CTL_UNITS_BANDWIDTH_MBPS:return "MB/s";
    default:return std::format("IGCL units {}",static_cast<unsigned>(v));}}
void add(GpuReading& r,std::string metric,std::string units,std::optional<double> v,std::string status="ok"){
    r.additionalSensors.push_back({r.uuid+" / "+r.name,std::move(metric),std::move(units),r.source,v?std::move(status):status=="ok"?"not reported":std::move(status),v,{},GetTickCount64()});
}
std::optional<unsigned> bounded(std::optional<double> v,double maximum){return v&&*v>=0&&*v<=maximum?std::optional<unsigned>(static_cast<unsigned>(std::lround(*v))):std::nullopt;}
}
struct IntelGpu::Impl {
    std::mutex gate;std::shared_ptr<Runtime> shared;ULONGLONG retryAt{};std::string lastError;
    struct Previous {double timestamp{},value{};};std::map<std::string,Previous> previous;
    std::optional<double> rate(const std::string& id,std::optional<double> time,std::optional<double> current){
        if(!time||!current||*current<0)return {};const auto it=previous.find(id);std::optional<double> v;
        if(it!=previous.end()){const auto dt=*time-it->second.timestamp;
            if(dt>.05&&dt<30&&*current>=it->second.value)v=(*current-it->second.value)/dt;
            if(dt>=0&&dt<=.05)return {}; // Keep the baseline until enough time has elapsed.
        }
        previous[id]={*time,*current};return v;
    }
    template<class T>void consumePower(const T& t,GpuReading& r,bool stableIdentity,const std::string& schema){
        const auto item=[&](const char* name,const ctl_oc_telemetry_item_t& i){auto v=value(i);add(r,name,i.bSupported?unit(i.units):"not reported",v,std::format("supported={} data_type={}",i.bSupported,static_cast<unsigned>(i.type)));return v;};
        item("telemetry_timestamp",t.timeStamp);item("gpu_energy",t.gpuEnergyCounter);item("gpu_voltage",t.gpuVoltage);
        item("gpu_frequency",t.gpuCurrentClockFrequency);item("gpu_temperature",t.gpuCurrentTemperature);
        item("global_activity_counter",t.globalActivityCounter);item("render_compute_activity_counter",t.renderComputeActivityCounter);item("media_activity_counter",t.mediaActivityCounter);
        item("vram_energy",t.vramEnergyCounter);item("vram_voltage",t.vramVoltage);item("vram_frequency",t.vramCurrentClockFrequency);item("vram_effective_frequency",t.vramCurrentEffectiveFrequency);
        item("vram_read_counter",t.vramReadBandwidthCounter);item("vram_write_counter",t.vramWriteBandwidthCounter);item("vram_temperature",t.vramCurrentTemperature);item("total_card_energy",t.totalCardEnergyCounter);
        for(unsigned i=0;i<CTL_FAN_COUNT;++i)item(("fan"+std::to_string(i)+"_speed").c_str(),t.fanSpeed[i]);
        for(unsigned i=0;i<CTL_PSU_COUNT;++i)if(t.psu[i].bSupported){item(("psu"+std::to_string(i)+"_energy").c_str(),t.psu[i].energyCounter);item(("psu"+std::to_string(i)+"_voltage").c_str(),t.psu[i].voltage);}
        if(t.Version>0){item("gpu_vr_temperature",t.gpuVrTemp);item("vram_vr_temperature",t.vramVrTemp);item("sa_vr_temperature",t.saVrTemp);item("gpu_effective_frequency",t.gpuEffectiveClock);item("gpu_power_percent",t.gpuPowerPercent);item("gpu_temperature_percent",t.gpuTemperaturePercent);item("gpu_over_voltage_percent",t.gpuOverVoltagePercent);item("vram_read_bandwidth",t.vramReadBandwidth);item("vram_write_bandwidth",t.vramWriteBandwidth);}
        for(const auto& [name,flag]:{std::pair{"gpu_power_limited",t.gpuPowerLimited},std::pair{"gpu_temperature_limited",t.gpuTemperatureLimited},std::pair{"gpu_current_limited",t.gpuCurrentLimited},std::pair{"gpu_voltage_limited",t.gpuVoltageLimited},std::pair{"gpu_utilization_limited",t.gpuUtilizationLimited}})add(r,name,"boolean",flag?1.:0.);
        if(t.gpuCurrentTemperature.units==CTL_UNITS_TEMPERATURE_CELSIUS)r.temperature=bounded(value(t.gpuCurrentTemperature),200);
        if(t.gpuCurrentClockFrequency.units==CTL_UNITS_FREQUENCY_MHZ)r.graphicsMhz=bounded(value(t.gpuCurrentClockFrequency),30000);
        if(t.vramCurrentClockFrequency.units==CTL_UNITS_FREQUENCY_MHZ)r.memoryMhz=bounded(value(t.vramCurrentClockFrequency),50000);
        const auto time=stableIdentity&&t.timeStamp.units==CTL_UNITS_TIME_SECONDS?value(t.timeStamp):std::nullopt;
        const auto energyRate=[&](const char* key,const ctl_oc_telemetry_item_t& energy){return rate(r.uuid+schema+key,time,energy.units==CTL_UNITS_ENERGY_JOULES?value(energy):std::nullopt);};
        const auto gpuPower=energyRate("/gpu",t.gpuEnergyCounter),vramPower=energyRate("/vram",t.vramEnergyCounter),cardPower=energyRate("/card",t.totalCardEnergyCounter);
        add(r,"gpu_power_average","W",gpuPower,"derived from energy/time; two valid samples required");add(r,"vram_power_average","W",vramPower,"derived from energy/time; two valid samples required");add(r,"card_power_average","W",cardPower,"derived from energy/time; two valid samples required");
        r.apiStatus.push_back(cardPower?"power_mw source=total_card_energy/time":gpuPower?"power_mw source=gpu_energy/time":"power_mw unavailable; two valid samples required");
        const auto watts=cardPower?cardPower:gpuPower;if(watts)r.powerMw=bounded(*watts*1000,5000000);
        const auto activity=rate(r.uuid+schema+"/activity",time,t.globalActivityCounter.units==CTL_UNITS_TIME_SECONDS?value(t.globalActivityCounter):std::nullopt);
        if(activity&&*activity>=0&&*activity<=1.001)r.utilization=bounded(std::min(100.,*activity*100),100);
        add(r,"gpu_usage_derived","percent",r.utilization?std::optional<double>(*r.utilization):std::nullopt,"global activity/time; two valid samples required");
    }
    void power(Runtime& lib,ctl_device_adapter_handle_t device,GpuReading& r,bool stableIdentity){
        r.apiStatus.push_back(lib.description);
        // The0.1.64 dump returns INVALID_ARGUMENT for legacyVersion1, not
        // UNSUPPORTED_FEATURE. Intel's current sample uses GetV2; older drivers
        // need the legacy layout. Negotiate read-only ABI shapes and log EACH
        // exact export, Size, Version and result instead of hiding the failure.
        const auto compatibility=[](ctl_result_t code){return code==CTL_RESULT_ERROR_UNSUPPORTED_VERSION||code==CTL_RESULT_ERROR_INVALID_ARGUMENT||code==CTL_RESULT_ERROR_INVALID_SIZE||code==CTL_RESULT_ERROR_UNSUPPORTED_SIZE;};
        const auto attempt=[&](const char* api,auto& t,auto fn){const auto code=fn(device,&t);
            r.apiStatus.push_back(std::format("{} Size={} Version={}: {}",api,t.Size,t.Version,result(code)));return code;};
        if(lib.telemetryV2){
            for(const auto version:{1,0}){auto t=structure<ctl_power_telemetry_v2_t>(static_cast<std::uint8_t>(version));
                const auto code=attempt("ctlPowerTelemetryGetV2",t,lib.telemetryV2);
                if(code==CTL_RESULT_SUCCESS){consumePower(t,r,stableIdentity,"/v2/"+std::to_string(version));return;}
                if(!compatibility(code))break;
            }
        }else r.apiStatus.push_back("ctlPowerTelemetryGetV2: export absent");
        if(lib.telemetry){
            for(int shape=0;shape<3;++shape){auto t=structure<ctl_power_telemetry_t>(shape==0?1:0);
                // Version0 has no fields from gpuVrTemp onward. Some older
                // implementations validate the original structure's exact size.
                if(shape==2)t.Size=static_cast<std::uint32_t>(offsetof(ctl_power_telemetry_t,gpuVrTemp));
                const auto code=attempt("ctlPowerTelemetryGet",t,lib.telemetry);
                if(code==CTL_RESULT_SUCCESS){consumePower(t,r,stableIdentity,"/legacy/"+std::to_string(shape));return;}
                if(!compatibility(code))break;
            }
        }else r.apiStatus.push_back("ctlPowerTelemetryGet: export absent");
        std::erase_if(previous,[&](const auto& entry){return entry.first.starts_with(r.uuid+"/");});
        add(r,"power_telemetry","",{},"all attempted ABI shapes failed; see api_status");
    }
    std::vector<GpuReading> read(){
        const auto os=graphicsAdapters();std::vector<GpuReading> rows;
        if(std::none_of(os.begin(),os.end(),[](const auto& a){return a.vendorId==0x8086&&!a.software;}))return rows;
        try{
            if(!shared){if(GetTickCount64()<retryAt)throw std::runtime_error(lastError);shared=runtime();}
            std::lock_guard lock(shared->gate);lastError.clear();GpuReading enumeration;auto handles=enumerate<ctl_device_adapter_handle_t>(shared->devices,shared->api,enumeration,"ctlEnumerateDevices");
            for(std::size_t index=0;index<handles.size();++index){
                LUID luid{};auto p=structure<ctl_device_adapter_properties_t>();p.pDeviceID=&luid;p.device_id_size=sizeof(luid);
                GpuReading r;r.source="Intel IGCL";r.apiStatus=enumeration.apiStatus;
                if(!note(r,"ctlGetDeviceProperties",shared->properties(handles[index],&p))){lastError=r.apiStatus.back();continue;}
                if(p.pci_vendor_id!=0x8086||p.device_type!=CTL_DEVICE_TYPE_GRAPHICS)continue;
                r.name=std::string(p.name,strnlen_s(p.name,sizeof(p.name)));
                r.uuid=std::format("luid:{:08x}:{:08x}",static_cast<unsigned>(luid.HighPart),luid.LowPart);
                const bool stableIdentity=std::any_of(os.begin(),os.end(),[&](const auto& a){return a.vendorId==0x8086&&!a.software&&a.id==r.uuid;});
                if(!stableIdentity){r.uuid="igcl-session-index:"+std::to_string(index);r.apiStatus.push_back("OS Device ID not mapped; per-session identity, derived rates disabled");}
                r.type=(p.graphics_adapter_properties&CTL_ADAPTER_PROPERTIES_FLAG_INTEGRATED)?GpuType::Integrated:GpuType::Discrete;
                r.driver=std::format("IGCL driver_version=0x{:016X}",p.driver_version);
                for(const auto& a:os)if(a.vendorId==0x8086&&!a.software&&a.id==r.uuid){r.pci=a.pci;r.driver=a.driver;break;}
                add(r,"pci_device_id","id",p.pci_device_id);add(r,"slices","count",p.num_slices);add(r,"subslices_per_slice","count",p.num_sub_slices_per_slice);add(r,"EUs_per_subslice","count",p.num_eus_per_sub_slice);
                power(*shared,handles[index],r,stableIdentity);
                auto temps=enumerate<ctl_temp_handle_t>(shared->temperatures,handles[index],r,"ctlEnumTemperatureSensors");
                for(std::size_t i=0;i<temps.size();++i){auto props=structure<ctl_temp_properties_t>();const bool known=shared->temperatureProperties&&note(r,"ctlTemperatureGetProperties",shared->temperatureProperties(temps[i],&props));
                    const auto prefix="temperature"+std::to_string(i)+"/type"+(known?std::to_string(props.type):"unknown");double c{};
                    const bool ok=shared->temperatureState&&note(r,"ctlTemperatureGetState",shared->temperatureState(temps[i],&c));
                    const auto value=ok&&std::isfinite(c)&&c>=-40&&c<=200?std::optional<double>(c):std::nullopt;add(r,prefix,"C",value);
                    if(known){add(r,prefix+"/maximum","C",std::isfinite(props.maxTemperature)?std::optional<double>(props.maxTemperature):std::nullopt);if(props.type==CTL_TEMP_SENSORS_GPU&&!r.temperature)r.temperature=bounded(value,200);}
                }
                auto freqs=enumerate<ctl_freq_handle_t>(shared->frequencies,handles[index],r,"ctlEnumFrequencyDomains");
                for(std::size_t i=0;i<freqs.size();++i){auto props=structure<ctl_freq_properties_t>();const bool known=shared->frequencyProperties&&note(r,"ctlFrequencyGetProperties",shared->frequencyProperties(freqs[i],&props));auto state=structure<ctl_freq_state_t>();
                    if(!shared->frequencyState||!note(r,"ctlFrequencyGetState",shared->frequencyState(freqs[i],&state)))continue;
                    const auto prefix="frequency"+std::to_string(i)+"/type"+(known?std::to_string(props.type):"unknown");
                    for(const auto& [name,n]:{std::pair{"actual",state.actual},std::pair{"requested",state.request},std::pair{"tdp",state.tdp},std::pair{"efficient",state.efficient}})add(r,prefix+"/"+name,"MHz",std::isfinite(n)&&n>=0?std::optional<double>(n):std::nullopt);
                    add(r,prefix+"/voltage","V",std::isfinite(state.currentVoltage)&&state.currentVoltage>=0?std::optional<double>(state.currentVoltage):std::nullopt);add(r,prefix+"/throttle_reasons","bitmask",state.throttleReasons);
                    if(known){add(r,prefix+"/minimum","MHz",props.min>=0?std::optional<double>(props.min):std::nullopt);add(r,prefix+"/maximum","MHz",props.max>=0?std::optional<double>(props.max):std::nullopt);if(props.type==CTL_FREQ_DOMAIN_GPU&&!r.graphicsMhz)r.graphicsMhz=bounded(state.actual,30000);if(props.type==CTL_FREQ_DOMAIN_MEMORY&&!r.memoryMhz)r.memoryMhz=bounded(state.actual,50000);}
                }
                auto memories=enumerate<ctl_mem_handle_t>(shared->memories,handles[index],r,"ctlEnumMemoryModules");
                for(std::size_t i=0;i<memories.size();++i){const auto prefix="memory"+std::to_string(i);auto props=structure<ctl_mem_properties_t>();
                    if(shared->memoryProperties&&note(r,"ctlMemoryGetProperties",shared->memoryProperties(memories[i],&props))){add(r,prefix+"/type","enum",props.type);add(r,prefix+"/location","enum",props.location);add(r,prefix+"/physical_size","bytes",props.physicalSize?std::optional<double>(static_cast<double>(props.physicalSize)):std::nullopt);add(r,prefix+"/bus_width","bits",props.busWidth>=0?std::optional<double>(props.busWidth):std::nullopt);add(r,prefix+"/channels","count",props.numChannels>=0?std::optional<double>(props.numChannels):std::nullopt);}
                    auto state=structure<ctl_mem_state_t>();if(shared->memoryState&&note(r,"ctlMemoryGetState",shared->memoryState(memories[i],&state))){add(r,prefix+"/allocatable","bytes",static_cast<double>(state.size));add(r,prefix+"/free","bytes",state.free<=state.size?std::optional<double>(static_cast<double>(state.free)):std::nullopt);}
                    auto bw=structure<ctl_mem_bandwidth_t>();if(shared->memoryBandwidth&&note(r,"ctlMemoryGetBandwidth",shared->memoryBandwidth(memories[i],&bw))){add(r,prefix+"/read_counter","bytes",static_cast<double>(bw.readCounter));add(r,prefix+"/write_counter","bytes",static_cast<double>(bw.writeCounter));add(r,prefix+"/maximum_bandwidth","bytes/s",static_cast<double>(bw.maxBandwidth));add(r,prefix+"/timestamp","us",static_cast<double>(bw.timestamp));}
                }
                r.available=true;r.status="Intel IGCL · только чтение; поддержка каждого датчика и API указана отдельно";rows.push_back(std::move(r));
            }
            if(rows.empty()&&lastError.empty())lastError="Intel IGCL не вернул графические устройства: "+(enumeration.apiStatus.empty()?std::string("нет ответа"):enumeration.apiStatus.back());
        }catch(const std::exception& e){lastError=e.what();retryAt=GetTickCount64()+30000;shared.reset();}
        // An inactive/unhandled adapter stays identifiable. Never substitute
        // another Intel device's readings by matching only a friendly name.
        for(const auto& a:os)if(a.vendorId==0x8086&&!a.software&&std::none_of(rows.begin(),rows.end(),[&](const auto& r){return r.uuid==a.id;})){
            GpuReading r;r.name=a.name;r.uuid=a.id;r.pci=a.pci;r.driver=a.driver;r.type=a.type;r.source="Windows · DXGI/DXCore";r.status=lastError.empty()?"ОС видит Intel GPU; IGCL не сопоставил его OS Device ID":lastError;rows.push_back(std::move(r));
        }
        return rows;
    }
};
IntelGpu::IntelGpu():impl_(std::make_unique<Impl>()){}IntelGpu::~IntelGpu()=default;
std::vector<GpuReading> IntelGpu::read(){std::lock_guard lock(impl_->gate);return impl_->read();}
}
