#include "MagicControl/ActionLog.h"
#include "MagicControl/NvidiaGpu.h"
#include "MagicControl/GpuPresence.h"
#include "MagicControl/Monitoring.h"
#include "MagicControl/Logging.h"
#include "NvflashPlatform.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <format>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <cmath>
#include <cstddef>
#include <limits>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <setupapi.h>
#include <devguid.h>
#include "../windows/NvidiaSensorsWindows.h"
#else
#include <dlfcn.h>
#endif

namespace mc {
bool nvidiaGpuPresent() noexcept {
    return observeGpuPresence([]()->GpuPresence&{return gpuPresence();},GpuPresenceKind::Nvidia,[] {
#ifdef _WIN32
        // Also see a disabled/headless adapter absent from DXGI enumeration.
        const auto devices=SetupDiGetClassDevsW(&GUID_DEVCLASS_DISPLAY,nullptr,nullptr,DIGCF_PRESENT);
        if(devices!=INVALID_HANDLE_VALUE){bool found=false;SP_DEVINFO_DATA device{sizeof(device)};
            for(DWORD index=0;SetupDiEnumDeviceInfo(devices,index,&device);++index){wchar_t id[512]{};
                if(SetupDiGetDeviceInstanceIdW(devices,&device,id,512,nullptr)&&!_wcsnicmp(id,L"PCI\\VEN_10DE",12)){found=true;break;}}
            SetupDiDestroyDeviceInfoList(devices);if(found)return true;}
        Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
        if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))return false;
        for(unsigned i=0;i<64;++i){Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
            if(FAILED(factory->EnumAdapters1(i,&adapter)))break;DXGI_ADAPTER_DESC1 d{};
            if(SUCCEEDED(adapter->GetDesc1(&d))&&d.VendorId==0x10de&&!(d.Flags&DXGI_ADAPTER_FLAG_SOFTWARE))return true;}
#else
        std::error_code error;
        for(const auto& device:std::filesystem::directory_iterator("/sys/bus/pci/devices",error)){
            std::string vendor,klass;std::ifstream(device.path()/"vendor")>>vendor;std::ifstream(device.path()/"class")>>klass;
            if(vendor=="0x10de"&&klass.rfind("0x03",0)==0)return true;}
#endif
        return false;
    });
}
namespace {
// Minimal documented NVML C ABI; no NVIDIA binaries/headers are redistributed.
// https://docs.nvidia.com/deploy/nvml-api/latest/
using Device=void*;using Return=int;
struct Memory {unsigned long long total,free,used;};
struct Utilization {unsigned gpu,memory;};
struct PciInfo {char legacy[16];unsigned domain,bus,device,deviceId,subsystemId;char busId[32];};
static_assert(sizeof(PciInfo)==68);
// NVML field-value ABI, including the platform-sized unsigned long union arm.
// Field82 is NVML_FI_DEV_MEMORY_TEMP. Per-field nvmlReturn is authoritative.
// https://docs.nvidia.com/deploy/nvml-api/api/structnvmlFieldValue__t.html
union FieldNumber {double real;unsigned u32;unsigned long native;unsigned long long u64;long long s64;int s32;unsigned short u16;};
struct FieldValue {unsigned id,scope;long long timestamp,latency;int type,result;FieldNumber value;};
static_assert(sizeof(FieldValue)==40&&offsetof(FieldValue,value)==32);
static_assert(sizeof(Memory)==24&&sizeof(Utilization)==8);
struct Library {
#ifdef _WIN32
    HMODULE module{};
    Library(){wchar_t system[MAX_PATH]{};const auto length=GetSystemDirectoryW(system,MAX_PATH);
        if(length&&length<MAX_PATH){const auto path=std::filesystem::path(system)/L"nvml.dll";module=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);}
        // Legacy NVIDIA installation path, absolute and never the current folder.
        if(!module){wchar_t program[MAX_PATH]{};const auto n=GetEnvironmentVariableW(L"ProgramW6432",program,MAX_PATH);
            if(n&&n<MAX_PATH){const auto path=std::filesystem::path(program)/L"NVIDIA Corporation"/L"NVSMI"/L"nvml.dll";
                module=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);}}
    }
    ~Library(){if(module)FreeLibrary(module);}
    template<class T>T symbol(const char* name){return module?reinterpret_cast<T>(GetProcAddress(module,name)):nullptr;}
#else
    void* module{dlopen("libnvidia-ml.so.1",RTLD_NOW|RTLD_LOCAL)};
    ~Library(){if(module)dlclose(module);}
    template<class T>T symbol(const char* name){return module?reinterpret_cast<T>(dlsym(module,name)):nullptr;}
#endif
};
}
struct NvidiaGpu::Impl {
    std::mutex gate;std::unique_ptr<Library> library;bool initialized{};
    using Simple=Return(*)();using Count=Return(*)(unsigned*);using Handle=Return(*)(unsigned,Device*);
    using ByUuid=Return(*)(const char*,Device*);using String=Return(*)(Device,char*,unsigned);
    using Uint=Return(*)(Device,unsigned*);using Pair=Return(*)(Device,unsigned*,unsigned*);
    Simple shutdown{};Count count{};Handle handle{};ByUuid byUuid{};String name{},uuid{},vbios{};
    const char*(*errorString)(Return){};
    Return(*driver)(char*,unsigned){};
    Return(*temperature)(Device,unsigned,unsigned*){};
    Return(*clock)(Device,unsigned,unsigned*){};
    Return(*memory)(Device,Memory*){};Return(*utilization)(Device,Utilization*){};
    Return(*fieldValues)(Device,int,FieldValue*){};
    Return(*pciInfo)(Device,PciInfo*){};
#ifdef _WIN32
    windows::NvidiaSensors sensors;
#endif
    Uint power{},limit{},enforced{},defaults{},pstate{};Pair constraints{};
    Return(*setLimit)(Device,unsigned){};
    Uint numFans{};
    Return(*fanSpeed)(Device,unsigned,unsigned*){};
    Return(*fanTarget)(Device,unsigned,unsigned*){};
    Return(*fanPolicy)(Device,unsigned,unsigned*){};
    Return(*setFan)(Device,unsigned,unsigned){};
    Return(*resetFan)(Device,unsigned){};
    std::map<std::string,std::string> denied;
    ~Impl(){if(initialized&&shutdown)shutdown();}
    std::string error(Return value){const auto* s=errorString?errorString(value):nullptr;return s?s:"NVML error "+std::to_string(value);}
    void check(Return value){if(value)throw std::runtime_error(error(value));}
    void open(){
        if(initialized)return;library=std::make_unique<Library>();
        if(!library->module)throw std::runtime_error("NVML недоступна. Установите официальный драйвер NVIDIA.");
        auto init=library->symbol<Simple>("nvmlInit_v2");shutdown=library->symbol<Simple>("nvmlShutdown");
        errorString=library->symbol<decltype(errorString)>("nvmlErrorString");
        if(!init||!shutdown)throw std::runtime_error("В драйвере нет NVML ABI v2");check(init());initialized=true;
#define NVML_LOAD(field,exportName) field=library->symbol<decltype(field)>(exportName)
        NVML_LOAD(count,"nvmlDeviceGetCount_v2");NVML_LOAD(handle,"nvmlDeviceGetHandleByIndex_v2");
        NVML_LOAD(byUuid,"nvmlDeviceGetHandleByUUID");NVML_LOAD(name,"nvmlDeviceGetName");NVML_LOAD(uuid,"nvmlDeviceGetUUID");
        NVML_LOAD(vbios,"nvmlDeviceGetVbiosVersion");NVML_LOAD(driver,"nvmlSystemGetDriverVersion");
        NVML_LOAD(temperature,"nvmlDeviceGetTemperature");NVML_LOAD(clock,"nvmlDeviceGetClockInfo");
        NVML_LOAD(memory,"nvmlDeviceGetMemoryInfo");NVML_LOAD(utilization,"nvmlDeviceGetUtilizationRates");
        NVML_LOAD(fieldValues,"nvmlDeviceGetFieldValues");
        NVML_LOAD(pciInfo,"nvmlDeviceGetPciInfo_v3");
        NVML_LOAD(power,"nvmlDeviceGetPowerUsage");NVML_LOAD(limit,"nvmlDeviceGetPowerManagementLimit");
        NVML_LOAD(enforced,"nvmlDeviceGetEnforcedPowerLimit");NVML_LOAD(defaults,"nvmlDeviceGetPowerManagementDefaultLimit");
        NVML_LOAD(constraints,"nvmlDeviceGetPowerManagementLimitConstraints");NVML_LOAD(pstate,"nvmlDeviceGetPerformanceState");
        NVML_LOAD(setLimit,"nvmlDeviceSetPowerManagementLimit");
        NVML_LOAD(numFans,"nvmlDeviceGetNumFans");NVML_LOAD(fanSpeed,"nvmlDeviceGetFanSpeed_v2");
        NVML_LOAD(fanTarget,"nvmlDeviceGetTargetFanSpeed");NVML_LOAD(fanPolicy,"nvmlDeviceGetFanControlPolicy_v2");
        NVML_LOAD(setFan,"nvmlDeviceSetFanSpeed_v2");NVML_LOAD(resetFan,"nvmlDeviceSetDefaultFanSpeed_v2");
#undef NVML_LOAD
        if(!count||!handle){shutdown();initialized=false;throw std::runtime_error("В драйвере отсутствует перечисление устройств NVML");}
    }
    std::string string(String f,Device d){char value[128]{};return f&&f(d,value,sizeof(value))==0?std::string(value):std::string{};}
    std::optional<unsigned> number(Uint f,Device d){unsigned value{};return f&&f(d,&value)==0?std::optional(value):std::nullopt;}
    Device resolve(const std::string& id){if(id.empty()||!byUuid)throw std::runtime_error("Нет UUID для безопасного выбора GPU");Device d{};check(byUuid(id.c_str(),&d));
        if(string(uuid,d)!=id)throw std::runtime_error("NVIDIA UUID изменился");return d;}
    void apply(const std::string& id,unsigned requested){
        audit::Scope trace("gpu.nvml_power_limit","\"uuid\":"+audit::quote(id)+",\"requested_mw\":"+std::to_string(requested));
        auto d=resolve(id);unsigned low{},high{},before{};
        if(!setLimit||!constraints||!limit)throw std::runtime_error("Управление Power Limit не поддерживается драйвером");
        check(constraints(d,&low,&high));check(limit(d,&before));
        if(low>high||requested<low||requested>high)throw std::runtime_error("Лимит выходит за разрешённый диапазон драйвера");
        logMessage("nvidia",std::format("Power Limit request uuid={} before={} requested={} min={} max={} mW",id,before,requested,low,high));
        audit::event("gpu_limit_before","\"before_mw\":"+std::to_string(before)+",\"min_mw\":"+std::to_string(low)+",\"max_mw\":"+std::to_string(high));
        const auto code=setLimit(d,requested);audit::event("nvml_response","\"method\":\"SetPowerManagementLimit\",\"return_code\":"+std::to_string(code));
        if(code){if(code==3||code==4)denied[id]=error(code);throw std::runtime_error(error(code));}
        for(int n=0;n<5;++n){unsigned actual{};const auto readCode=limit(d,&actual);audit::event("nvml_response","\"method\":\"GetPowerManagementLimit\",\"return_code\":"+std::to_string(readCode)+",\"value_mw\":"+(readCode?"null":std::to_string(actual)));if(readCode==0&&actual==requested){trace.finish("readback_confirmed","\"actual_mw\":"+std::to_string(actual));logMessage("nvidia",std::format("Power Limit readback uuid={} value={} mW",id,actual));return;}
            std::this_thread::sleep_for(std::chrono::milliseconds(50));}
        // Never overwrite a different external/firmware limit with an old value.
        logMessage("nvidia","Power Limit readback not confirmed; external state was not overwritten");
        throw std::runtime_error("Драйвер не подтвердил лимит. Проверьте текущее состояние GPU.");
    }
};
NvidiaGpu::NvidiaGpu():impl_(std::make_unique<Impl>()){}
NvidiaGpu::~NvidiaGpu()=default;
std::vector<NvidiaGpuReading> NvidiaGpu::read(bool fanDiagnostics){
#ifdef _WIN32
    const auto firmwareLease=diagnostics::nvflash::tryTelemetryLease();
    if(!firmwareLease)return {}; // unavailable sample, never cached-as-fresh
#endif
    std::lock_guard lock(impl_->gate);std::vector<NvidiaGpuReading> rows;
#ifdef _WIN32
    // Capture before nvmlInit/query calls, which may themselves wake a hybrid GPU.
    const auto powerBefore=windows::nvidiaPowerObservations();
#endif
    try{impl_->open();unsigned count{};impl_->check(impl_->count(&count));
        for(unsigned i=0;i<std::min(count,32u);++i){NvidiaGpuReading r;Device d{};const auto code=impl_->handle(i,&d);
            if(code){r.name="NVIDIA GPU "+std::to_string(i);r.status=impl_->error(code);rows.push_back(std::move(r));continue;}
            r.source="NVIDIA NVML";
            r.name=impl_->string(impl_->name,d);r.uuid=impl_->string(impl_->uuid,d);r.vbios=impl_->string(impl_->vbios,d);
            PciInfo pci{};const bool pciKnown=impl_->pciInfo&&impl_->pciInfo(d,&pci)==0;
            if(pciKnown){r.pci=std::string(pci.busId,std::find(std::begin(pci.busId),std::end(pci.busId),'\0'));r.pciDeviceId=pci.deviceId;r.pciSubsystemId=pci.subsystemId;}
            char driver[128]{};if(impl_->driver&&impl_->driver(driver,sizeof(driver))==0)r.driver=driver;
            r.powerMw=impl_->number(impl_->power,d);r.limitMw=impl_->number(impl_->limit,d);r.enforcedMw=impl_->number(impl_->enforced,d);
            r.defaultMw=impl_->number(impl_->defaults,d);r.pstate=impl_->number(impl_->pstate,d);
            unsigned value{};if(impl_->temperature&&impl_->temperature(d,0,&value)==0&&value>0&&value<=150)r.temperature=value;
            if(r.temperature)r.temperatureSource="NVIDIA NVML";
            if(impl_->clock&&impl_->clock(d,0,&value)==0)r.graphicsMhz=value;
            if(impl_->clock&&impl_->clock(d,2,&value)==0)r.memoryMhz=value;
            Memory memory{};if(impl_->memory&&impl_->memory(d,&memory)==0){r.totalBytes=memory.total;r.usedBytes=memory.used;r.freeBytes=memory.free;}
            Utilization util{};if(impl_->utilization&&impl_->utilization(d,&util)==0){if(util.gpu<=100)r.utilization=util.gpu;if(util.memory<=100)r.memoryUtilization=util.memory;}
            unsigned low{},high{};if(impl_->constraints&&impl_->constraints(d,&low,&high)==0&&low<=high){r.minimumMw=low;r.maximumMw=high;}
            if(r.powerMw){
                const auto raw=*r.powerMw;
                const auto ceiling=r.maximumMw?r.maximumMw:r.enforcedMw?r.enforcedMw:r.defaultMw;
                // Keep the driver value for diagnosis. A broad per-device guard
                // allows transients without treating a 590W/140W answer as real.
                const auto suspect=[&](unsigned value){return ceiling&&std::uint64_t(value)>std::uint64_t(*ceiling)*2+10000;};
                if(suspect(raw)){
                    const auto retry=impl_->number(impl_->power,d);
                    r.additionalSensors.push_back({r.uuid,"power_raw","mW",r.source,"suspect: above per-device envelope; excluded from accepted power",{},double(raw),0});
                    r.apiStatus.push_back(std::format("GetPowerUsage anomaly: raw={} mW, reported envelope={} mW, repeat={}",raw,*ceiling,retry?std::to_string(*retry):"unavailable"));
                    r.powerMw=retry&&!suspect(*retry)?retry:std::nullopt;
                    if(!r.powerMw)r.additionalSensors.push_back({r.uuid,"power","mW",r.source,"unavailable: anomalous driver reading",{},{},0});
                }
            }
            // Query independent documented power fields. Keep raw values and
            // provider timestamps; never rescale a590006mW driver anomaly.
            const auto queryPower=r.powerMw;
            r.additionalSensors.push_back({r.uuid,"power_usage_query","mW","NVIDIA NVML · GetPowerUsage",queryPower?"accepted envelope":"unavailable/anomalous",queryPower?std::optional<double>(*queryPower):std::nullopt,{},0});
            r.powerSource="NVIDIA NVML · GetPowerUsage";
            std::optional<unsigned> instant,average;bool instantAnomalous=false;
            const bool queryAnomalous=std::any_of(r.apiStatus.begin(),r.apiStatus.end(),[](const auto& s){return s.starts_with("GetPowerUsage anomaly:");});
            for(const auto id:{186u,185u}){
                FieldValue f{};f.id=id;const auto code=impl_->fieldValues?impl_->fieldValues(d,1,&f):13;
                std::optional<double> raw;
                if(!code&&!f.result){switch(f.type){case 0:raw=f.value.real;break;case 1:raw=f.value.u32;break;case 2:raw=double(f.value.native);break;case 3:raw=double(f.value.u64);break;case 4:raw=double(f.value.s64);break;}}
                const auto ceiling=r.maximumMw?r.maximumMw:r.enforcedMw?r.enforcedMw:r.defaultMw;
                const bool envelope=raw&&std::isfinite(*raw)&&*raw>=0&&*raw<=std::numeric_limits<unsigned>::max()&&(!ceiling||*raw<=double(*ceiling)*2+10000);
                if(id==186&&raw&&!envelope)instantAnomalous=true;
                // A zero average conflicting with failed/590W instantaneous
                // evidence is unknown, never proof that an active GPU draws0W.
                const bool conflictingZero=raw&&*raw==0&&(queryAnomalous||instantAnomalous);
                const bool valid=envelope&&!conflictingZero&&f.timestamp>0;
                const auto source=std::format("NVIDIA NVML · field{}",id);
                r.additionalSensors.push_back({r.uuid,id==186?"power_instant":"power_average","mW",source,
                    std::format("call={} field={} type={} provider_utc_us={} latency_us={}; {}",code,f.result,f.type,f.timestamp,f.latency,valid?"accepted":conflictingZero?"unknown: zero conflicts with anomalous instantaneous/query":"unavailable/anomalous"),valid?raw:std::nullopt,raw,0});
                if(valid)(id==186?instant:average)=static_cast<unsigned>(std::lround(*raw));
            }
            if(instant){r.powerMw=instant;r.powerSource="NVIDIA NVML · field186 instantaneous";}
            else if(!r.powerMw&&average){r.powerMw=average;r.powerSource="NVIDIA NVML · field185 average1s";}
#ifdef _WIN32
            if(pciKnown)impl_->sensors.read(r,{pci.domain,pci.bus,pci.device,pci.deviceId,pci.subsystemId});
            else r.apiStatus.push_back("NVAPI: NVML PCI identity unavailable; supplement skipped");
            const auto powerAfter=windows::nvidiaPowerObservations();
            const auto addPowerState=[&](const auto& observations,const char* phase){
                unsigned matches{};const windows::NvidiaPowerObservation* matched{};
                for(const auto& p:observations)if(pciKnown&&pci.domain==0&&p.bus==pci.bus&&p.device==pci.device&&p.function==0){++matches;matched=&p;}
                if(matches==1)r.additionalSensors.push_back({r.uuid,std::string("pnp_power_state_")+phase,"D-state","Windows · PnP SPDRP_DEVICE_POWER_DATA",matched->status,
                    matched->state?std::optional<double>(*matched->state):std::nullopt,{},matched->sampledAt});
                else r.apiStatus.push_back(std::string("PnP power state ")+phase+": PCI match unavailable/ambiguous");
            };
            addPowerState(powerBefore,"before");addPowerState(powerAfter,"after");
#endif
            r.available=true;r.powerControl=impl_->setLimit&&impl_->byUuid&&!r.uuid.empty()&&r.limitMw&&r.minimumMw&&r.maximumMw&&!impl_->denied.contains(r.uuid);
            if(fanDiagnostics){
                FieldValue field{};field.id=82;
                const auto fieldCode=impl_->fieldValues?impl_->fieldValues(d,1,&field):13;
                std::optional<double> value;
                if(!fieldCode&&!field.result){switch(field.type){
                    case 0:value=field.value.real;break;case 1:value=field.value.u32;break;case 2:value=double(field.value.native);break;
                    case 3:value=double(field.value.u64);break;case 4:value=double(field.value.s64);break;case 5:value=field.value.s32;break;case 6:value=field.value.u16;break;
                }}
                if(value&&(!std::isfinite(*value)||*value<=0||*value>150))value.reset();
                if(value)r.memoryCelsius=value;
                r.additionalSensors.push_back({r.uuid,"memory_temperature","C","NVIDIA NVML · field82",
                    std::format("call={} field={} type={} provider_utc_us={} latency_us={}",fieldCode,field.result,field.type,field.timestamp,field.latency),value,{},0});
                const auto note=[&](const char* api,Return code){r.apiStatus.push_back(std::string(api)+": "+(code?impl_->error(code):"OK")+" ("+std::to_string(code)+")");};
                unsigned probe{};Memory diagnosticMemory{};Utilization diagnosticUsage{};
                note("GetTemperature",impl_->temperature?impl_->temperature(d,0,&probe):13);
                note("GetGraphicsClock",impl_->clock?impl_->clock(d,0,&probe):13);
                note("GetMemoryClock",impl_->clock?impl_->clock(d,2,&probe):13);
                note("GetMemoryInfo",impl_->memory?impl_->memory(d,&diagnosticMemory):13);
                note("GetUtilizationRates",impl_->utilization?impl_->utilization(d,&diagnosticUsage):13);
                for(auto [label,api]:{std::pair{"GetPowerUsage",impl_->power},std::pair{"GetPowerManagementLimit",impl_->limit},std::pair{"GetEnforcedPowerLimit",impl_->enforced},std::pair{"GetPowerManagementDefaultLimit",impl_->defaults}})
                    note(label,api?api(d,&probe):13);
                unsigned low{},high{};note("GetPowerManagementLimitConstraints",impl_->constraints?impl_->constraints(d,&low,&high):13);
                r.apiStatus.push_back(impl_->setLimit?"SetPowerManagementLimit: export present; setter NOT called":"SetPowerManagementLimit: export absent");
                unsigned count{};const auto code=impl_->numFans?impl_->numFans(d,&count):13;
                if(code)r.fanStatus="nvmlDeviceGetNumFans: "+impl_->error(code)+" ("+std::to_string(code)+")";
                else {r.fanCount=count;r.fanStatus="Setter доступен для проверки только при читаемых policy/target и возможности возврата";
                    for(unsigned fan=0;fan<std::min(count,16u);++fan){NvidiaFanReading f;f.index=fan;
                        const auto read=[&](auto api,const char* name,std::optional<unsigned>& output){unsigned v{};const auto c=api?api(d,fan,&v):13;
                            if(!c)output=v;else f.status+=std::string(name)+": "+impl_->error(c)+" ("+std::to_string(c)+"); ";};
                        read(impl_->fanSpeed,"GetFanSpeed_v2",f.percent);read(impl_->fanTarget,"GetTargetFanSpeed",f.target);read(impl_->fanPolicy,"GetFanControlPolicy_v2",f.policy);
                        if(f.status.empty())f.status="read ok; setter not tested";r.fans.push_back(std::move(f));}
                }
            }
            r.status=impl_->denied.contains(r.uuid)?"Power Limit: "+impl_->denied[r.uuid]:r.powerControl?"NVML · Power Limit доступен для запроса; результат проверяется чтением":"NVML · изменение Power Limit не предоставлено драйвером";
            if(std::any_of(r.apiStatus.begin(),r.apiStatus.end(),[](const auto& note){return note.starts_with("GetPowerUsage anomaly:");}))r.status+=" · Power: аномалия драйвера, см. raw/статус";
            if(r.hotspotCelsius||r.memoryJunctionCelsius)r.source+=" + NVAPI";
            r.status+=" · Power source: "+r.powerSource+(r.powerMw?"":" (unavailable)");
            audit::state("NVML."+r.uuid,"{\"power_mw\":"+(r.powerMw?std::to_string(*r.powerMw):"null")+",\"power_source\":"+audit::quote(r.powerSource)+",\"query_anomaly\":"+(queryAnomalous?"true":"false")+",\"instant_anomaly\":"+(instantAnomalous?"true":"false")+",\"pstate\":"+(r.pstate?std::to_string(*r.pstate):"null")+",\"load\":"+(r.utilization?std::to_string(*r.utilization):"null")+",\"graphics_mhz\":"+(r.graphicsMhz?std::to_string(*r.graphicsMhz):"null")+",\"memory_mhz\":"+(r.memoryMhz?std::to_string(*r.memoryMhz):"null")+"}");
            rows.push_back(std::move(r));}
        if(rows.empty()&&nvidiaGpuPresent()){NvidiaGpuReading r;r.name="NVIDIA GPU";r.status="NVML не вернула доступных устройств";rows.push_back(r);}
    }catch(const std::exception& e){NvidiaGpuReading r;r.name="NVIDIA GPU";r.status=e.what();rows.push_back(r);}
    return rows;
}
FanMaximumResult NvidiaGpu::measureMaximumFans(const std::string& id,const std::function<FanMaximumSample()>& physical,std::stop_token token){
    std::lock_guard lock(impl_->gate);FanMaximumResult skipped;skipped.source="NVIDIA NVML · "+id;
    try{
        impl_->open();const auto device=impl_->resolve(id);unsigned count{};
        if(!impl_->numFans||!impl_->fanPolicy||!impl_->fanTarget||!impl_->fanSpeed||!impl_->setFan||!impl_->resetFan)
            throw std::runtime_error("Нет полного набора fan API для пробы с возвратом");
        const auto code=impl_->numFans(device,&count);if(code)throw std::runtime_error("GetNumFans: "+impl_->error(code)+" ("+std::to_string(code)+")");
        if(count==0||count>2)throw std::runtime_error("Проба рассчитана на1–2 доступных NVML вентилятора; получено "+std::to_string(count));
        struct Saved{unsigned policy{},target{};bool attempted{};};std::vector<Saved> saved(count);
        // Read every original policy/target before the first write. A successful
        // GetNumFans alone is never treated as permission or setter support.
        for(unsigned i=0;i<count;++i){impl_->check(impl_->fanPolicy(device,i,&saved[i].policy));
            if(saved[i].policy>1)throw std::runtime_error("Неизвестная исходная fan policy");
            impl_->check(impl_->fanTarget(device,i,&saved[i].target));unsigned current{};impl_->check(impl_->fanSpeed(device,i,&current));
            if(saved[i].target>100||current>100)throw std::runtime_error("NVML уже сообщает >100%; проба100% могла бы снизить охлаждение и пропущена");}
        bool running=false;
        return mc::measureMaximumFans(skipped.source,[&]{running=true;
            for(unsigned i=0;i<count;++i){saved[i].attempted=true;impl_->check(impl_->setFan(device,i,100));}
        },[&]{
            FanMaximumSample sample;try{if(physical)sample=physical();}catch(const std::exception&){}
            sample.percent={}; // NVML indices are not assumed to map to CPU/GPU channels.
            for(unsigned i=0;i<count;++i){unsigned percent{};impl_->check(impl_->fanSpeed(device,i,&percent));sample.percent[i]=percent;
                if(running){unsigned policy{},target{};impl_->check(impl_->fanPolicy(device,i,&policy));impl_->check(impl_->fanTarget(device,i,&target));
                    if(policy!=1||target!=100)throw std::runtime_error("NVML policy/target изменён вне пробы");}}
            return sample;
        },[&]{
            std::string failures;
            for(unsigned i=0;i<count;++i)if(saved[i].attempted)try{
                if(impl_->string(impl_->uuid,device)!=id)throw std::runtime_error("UUID изменился");
                unsigned policy{},target{};impl_->check(impl_->fanPolicy(device,i,&policy));
                if(policy==saved[i].policy&&policy==0)continue;
                impl_->check(impl_->fanTarget(device,i,&target));
                if(policy==saved[i].policy&&policy==1&&target==saved[i].target)continue;
                if(policy!=1||target!=100)throw std::runtime_error("Внешнее изменение; исходные настройки не перезаписаны");
                if(saved[i].policy==0)impl_->check(impl_->resetFan(device,i));else impl_->check(impl_->setFan(device,i,saved[i].target));
                impl_->check(impl_->fanPolicy(device,i,&policy));
                if(policy!=saved[i].policy)throw std::runtime_error("Возврат policy не подтверждён");
                if(policy==1){impl_->check(impl_->fanTarget(device,i,&target));if(target!=saved[i].target)throw std::runtime_error("Возврат target не подтверждён");}
            }catch(const std::exception& e){failures+="fan"+std::to_string(i)+": "+e.what()+"; ";}
            if(!failures.empty())throw std::runtime_error(failures);
        },token);
    }catch(const std::exception& e){skipped.status=e.what();return skipped;}
}
void NvidiaGpu::setPowerLimit(const std::string& id,unsigned value){std::lock_guard lock(impl_->gate);impl_->open();impl_->apply(id,value);}
void NvidiaGpu::resetPowerLimit(const std::string& id){std::lock_guard lock(impl_->gate);impl_->open();auto d=impl_->resolve(id);
    if(!impl_->defaults)throw std::runtime_error("Нет штатного лимита NVIDIA");unsigned value{};impl_->check(impl_->defaults(d,&value));impl_->apply(id,value);}
void readNvidiaMonitoring(NvidiaGpu& reader, MonitoringSnapshot& sample) {
    readNvidiaMonitoring(reader.read(),sample);
}
void readNvidiaMonitoring(const std::vector<NvidiaGpuReading>& readings, MonitoringSnapshot& sample) {
    const NvidiaGpuReading* selected=nullptr;
    unsigned matches{};
    for(const auto& reading:readings) {
        if(reading.name==sample.processorModels[1]&&reading.available){selected=&reading;++matches;}
    }
    if(matches>1)selected=nullptr;
    if(!selected&&readings.size()==1)selected=&readings.front();
    auto& gpu=sample.processors[1];
    const auto rpm=gpu.fanRpm,pwm=gpu.fanPwm;const auto fans=gpu.fanSource;
    gpu={};gpu.fanRpm=rpm;gpu.fanPwm=pwm;gpu.fanSource=fans;
    sample.gpuMemoryUsedBytes.reset();sample.gpuMemoryTotalBytes.reset();sample.gpuMemorySource.clear();
    sample.processorVendors[1]=ProcessorVendor::Nvidia;
    if(!selected){sample.processorStatus+=" NVIDIA: выбор GPU неоднозначен";return;}
    if(!selected->name.empty())sample.processorModels[1]=selected->name;
    if(!selected->available){sample.processorStatus+=" "+selected->status;return;}
    gpu.celsius=selected->temperature;gpu.usagePercent=selected->utilization;
    gpu.clockMhz=selected->graphicsMhz;
    if(selected->powerMw)gpu.powerWatts=*selected->powerMw/1000.;
    gpu.temperatureSource=gpu.usageSource=gpu.clockSource="NVIDIA · NVML";
    if(!selected->temperatureSource.empty())gpu.temperatureSource=selected->temperatureSource;
    gpu.powerSource=selected->powerSource.empty()?selected->source:selected->powerSource;
    sample.additionalSensors.insert(sample.additionalSensors.end(),selected->additionalSensors.begin(),selected->additionalSensors.end());
    sample.gpuMemoryUsedBytes=selected->usedBytes;sample.gpuMemoryTotalBytes=selected->totalBytes;
    sample.gpuMemorySource="NVIDIA · NVML";
}

}
