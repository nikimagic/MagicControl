#include "MagicControl/GpuDevice.h"
#include "MagicControl/Logging.h"
#include "AdlxRuntime.h"
#include "AmdTelemetryWindows.h"
#include "../../third_party/adlx/include/IPerformanceMonitoring.h"
#include "../../third_party/adlx/include/IGPUTuning.h"
#include "../../third_party/adlx/include/IGPUManualPowerTuning.h"
#include <cmath>
#include <algorithm>
#include <format>
#include <mutex>
#include <thread>
#include <utility>
#include <charconv>
#include <cctype>
namespace mc {
namespace {
template<class T>using Ref=windows::AdlxRef<T>;
void checked(ADLX_RESULT code,const char* api){if(code!=ADLX_OK)throw std::runtime_error(std::format("AMD ADLX {}: result={}",api,static_cast<int>(code)));}
std::string key(adlx::IADLXGPU* gpu){adlx_int id{};const char* device{};const char* sub{};
    checked(gpu->UniqueId(&id),"UniqueId");gpu->DeviceId(&device);gpu->SubSystemId(&sub);
    return std::format("adlx:{}:{}:{}",id,device?device:"",sub?sub:"");}
std::optional<unsigned> pciNumber(std::string_view value){
    if(value.starts_with("0x")||value.starts_with("0X"))value.remove_prefix(2);
    unsigned number{};const auto parsed=std::from_chars(value.data(),value.data()+value.size(),number,16);
    return !value.empty()&&parsed.ec==std::errc{}&&parsed.ptr==value.data()+value.size()?std::optional<unsigned>(number):std::nullopt;
}
// Keep provider handles independent. A unique PCI VEN/DEV/SUBSYS tuple can bind
// OS metadata, whereas a name (or successful enumeration of another GPU) cannot.
void bindOsIdentity(GpuReading& row,const std::vector<GraphicsAdapter>& adapters,
                    std::optional<unsigned> device,std::optional<unsigned> subsystem,std::optional<unsigned> revision={}){
    if(!device||!subsystem){row.identityStatus="Required provider PCI device/subsystem IDs not reported";return;}
    const GraphicsAdapter* match=nullptr;unsigned count{};
    std::vector<const GraphicsAdapter*> candidates;
    for(const auto& adapter:adapters)if(!adapter.software&&adapter.vendorId==0x1002&&adapter.deviceId==*device&&adapter.subsystemId==*subsystem&&(!revision||adapter.revision==*revision))candidates.push_back(&adapter);
    for(const auto* adapter:candidates){
        const bool locationKnown=row.location.bus&&row.location.device&&row.location.function&&adapter->location.bus&&adapter->location.device&&adapter->location.function;
        const bool locationMatches=locationKnown&&row.location.bus==adapter->location.bus&&row.location.device==adapter->location.device&&row.location.function==adapter->location.function;
        // With duplicate hardware tuples an unknown location cannot win merely
        // because another candidate had a known different address.
        if(candidates.size()>1&&!locationMatches)continue;
        if(locationKnown&&!locationMatches)continue;match=adapter;++count;}
    if(count!=1&&!candidates.empty()){
        // RDP LUID aliases can bind to the same independently verified present
        // PCI devnode. This is one physical device, not two ambiguous boards.
        const auto& identity=candidates.front()->physicalId;
        const bool samePhysical=!identity.empty()&&std::all_of(candidates.begin(),candidates.end(),[&](const auto* a){return a->physicalId==identity;});
        if(samePhysical){
            match=*std::min_element(candidates.begin(),candidates.end(),[](const auto* a,const auto* b){
                if((a->type!=GpuType::Unknown)!=(b->type!=GpuType::Unknown))return a->type!=GpuType::Unknown;
                return a->id<b->id;});
            const bool known=row.location.bus&&row.location.device&&row.location.function;
            if(!known||(row.location.bus==match->location.bus&&row.location.device==match->location.device&&row.location.function==match->location.function))count=1;
        }
    }
    if(count!=1){row.identityStatus="OS PCI identity unavailable/ambiguous; name matching is not used";row.apiStatus.push_back(row.identityStatus);return;}
    row.driver=match->driver;
    if(row.pci.empty())row.pci=match->pci;
    row.osAdapterId=match->id;row.physicalId=match->physicalId;row.identitySource=match->identitySource;
    row.identityStatus=row.physicalId.empty()?"OS adapter matched; physical devnode identity unavailable; provider rows remain separate":match->identityStatus;
    if(!row.location.bus)row.location=match->location;
    row.apiStatus.push_back("os_adapter_id="+match->id);
}
std::optional<unsigned> pnpNumber(std::string text,std::string_view token,std::size_t digits){
    std::transform(text.begin(),text.end(),text.begin(),[](unsigned char c){return static_cast<char>(std::toupper(c));});
    const auto at=text.find(token);if(at==std::string::npos||at+token.size()+digits>text.size())return {};
    return pciNumber(std::string_view(text).substr(at+token.size(),digits));
}
}
struct RadeonGpu::Impl {
    std::mutex gate;std::shared_ptr<windows::AdlxRuntime> runtime;
    windows::AmdTelemetryReader adl;
    void open(){if(!runtime)runtime=windows::acquireAdlxRuntime();}
    Ref<adlx::IADLXGPU> resolve(const std::string& id){
        Ref<adlx::IADLXGPUList> list;checked(runtime->system->GetGPUs(list.out()),"GetGPUs");
        for(adlx_uint i=0;i<list->Size();++i){Ref<adlx::IADLXGPU> gpu;if(list->At(i,gpu.out())!=ADLX_OK||!gpu)continue;
            if(key(gpu.p)==id){ADLX_GPU_TYPE type{};checked(gpu->Type(&type),"Type");if(type!=GPUTYPE_DISCRETE)throw std::runtime_error("Управление разрешено только для дискретного Radeon");return gpu;}}
        throw std::runtime_error("Radeon с указанным идентификатором больше не найден");
    }
    Ref<adlx::IADLXManualPowerTuning> power(adlx::IADLXGPU* gpu){
        Ref<adlx::IADLXGPUTuningServices> tuning;checked(runtime->system->GetGPUTuningServices(tuning.out()),"GetGPUTuningServices");
        adlx_bool supported{};checked(tuning->IsSupportedManualPowerTuning(gpu,&supported),"IsSupportedManualPowerTuning");
        if(!supported)throw std::runtime_error("AMD ADLX: Manual Power Tuning не поддерживается драйвером");
        Ref<adlx::IADLXInterface> unknown;checked(tuning->GetManualPowerTuning(gpu,unknown.out()),"GetManualPowerTuning");
        Ref<adlx::IADLXManualPowerTuning> power;checked(unknown->QueryInterface(adlx::IADLXManualPowerTuning::IID(),reinterpret_cast<void**>(power.out())),"ManualPowerTuning interface");return power;
    }
    void apply(const std::string& id,std::optional<int> desired){
        open();std::lock_guard runtimeLock(runtime->gate);auto gpu=resolve(id);auto p=power(gpu.p);
        ADLX_IntRange range{};checked(p->GetPowerLimitRange(&range),"GetPowerLimitRange");
        int value{};
        if(desired)value=*desired;else{Ref<adlx::IADLXManualPowerTuning1> extended;
            checked(p->QueryInterface(adlx::IADLXManualPowerTuning1::IID(),reinterpret_cast<void**>(extended.out())),"ManualPowerTuning1");checked(extended->GetPowerLimitDefault(&value),"GetPowerLimitDefault");}
        if(range.minValue>range.maxValue||range.step<=0||value<range.minValue||value>range.maxValue||(value-range.minValue)%range.step)
            throw std::runtime_error("Radeon: значение вне диапазона/шага ADLX");
        checked(p->SetPowerLimit(value),"SetPowerLimit"); // Never ResetToFactory: it would reset other GPU tuning.
        for(int n=0;n<5;++n){int actual{};if(p->GetPowerLimit(&actual)==ADLX_OK&&actual==value){logMessage("radeon",std::format("ADLX Power Limit id={} percent={} readback confirmed",id,value));return;}std::this_thread::sleep_for(std::chrono::milliseconds(50));}
        throw std::runtime_error("AMD ADLX не подтвердил Power Limit; чужое состояние не перезаписано");
    }
};
RadeonGpu::RadeonGpu():impl_(std::make_unique<Impl>()){}RadeonGpu::~RadeonGpu()=default;
std::vector<GpuReading> RadeonGpu::read(bool includeIntegrated){
    std::lock_guard lock(impl_->gate);std::vector<GpuReading> rows;std::string adlxError;
    const auto adapters=graphicsAdapters();
    try{
        impl_->open();std::lock_guard runtimeLock(impl_->runtime->gate);
        Ref<adlx::IADLXGPUList> list;checked(impl_->runtime->system->GetGPUs(list.out()),"GetGPUs");
        Ref<adlx::IADLXPerformanceMonitoringServices> perf;impl_->runtime->system->GetPerformanceMonitoringServices(perf.out());
        for(adlx_uint i=0;i<list->Size();++i){Ref<adlx::IADLXGPU> gpu;if(list->At(i,gpu.out())!=ADLX_OK||!gpu)continue;
            ADLX_GPU_TYPE type{};const auto typeCode=gpu->Type(&type);
            if(!includeIntegrated&&(typeCode!=ADLX_OK||type!=GPUTYPE_DISCRETE))continue;
            GpuReading r;r.source="AMD ADLX";r.powerUnit=GpuPowerUnit::Percent;r.type=typeCode!=ADLX_OK?GpuType::Unknown:type==GPUTYPE_INTEGRATED?GpuType::Integrated:type==GPUTYPE_DISCRETE?GpuType::Discrete:GpuType::Unknown;
            const char* name{};if(gpu->Name(&name)==ADLX_OK&&name)r.name=name;r.uuid=key(gpu.p);
            {const char *device{},*subsystem{},*subvendor{},*revision{};
                gpu->DeviceId(&device);gpu->SubSystemId(&subsystem);gpu->SubSystemVendorId(&subvendor);gpu->RevisionId(&revision);
                const auto sub=subsystem?pciNumber(subsystem):std::nullopt,vendor=subvendor?pciNumber(subvendor):std::nullopt;
                // ADLX reports separate 16-bit subsystem device/vendor IDs;
                // DXGI and PCI SUBSYS encode device<<16 | vendor. Comparing
                // 5006 directly with50061D05 caused the Wujie ADLX mismatch.
                const auto full=sub&&vendor&&*sub<=65535&&*vendor<=65535?std::optional<unsigned>((*sub<<16)|*vendor):std::nullopt;
                bindOsIdentity(r,adapters,device?pciNumber(device):std::nullopt,full,revision?pciNumber(revision):std::nullopt);}
            const char* part{},*version{},*date{};if(gpu->BIOSInfo(&part,&version,&date)==ADLX_OK&&version)r.vbios=version;
            const char* memoryType{};if(gpu->VRAMType(&memoryType)==ADLX_OK&&memoryType)r.memoryType=memoryType;
            adlx_uint total{};if(gpu->TotalVRAM(&total)==ADLX_OK)r.totalBytes=std::uint64_t(total)*1024*1024;
            Ref<adlx::IADLXGPUMetrics> metrics;
            const auto metricsCode=perf?perf->GetCurrentGPUMetrics(gpu.p,metrics.out()):ADLX_NOT_SUPPORTED;
            if(metricsCode==ADLX_OK&&metrics){
                const auto d=[&](auto getter,const char* api,double low,double high)->std::optional<double>{double v{};const auto code=(metrics.p->*getter)(&v);r.apiStatus.push_back(std::format("{}: result={}",api,static_cast<int>(code)));return code==ADLX_OK&&std::isfinite(v)&&v>=low&&v<=high?std::optional<double>(v):std::nullopt;};
                const auto integer=[&](auto getter,const char* api,int high)->std::optional<unsigned>{int v{};const auto code=(metrics.p->*getter)(&v);r.apiStatus.push_back(std::format("{}: result={}",api,static_cast<int>(code)));return code==ADLX_OK&&v>=0&&v<=high?std::optional<unsigned>(v):std::nullopt;};
                if(auto t=d(&adlx::IADLXGPUMetrics::GPUTemperature,"GPUTemperature",0,150))r.temperature=static_cast<unsigned>(std::lround(*t));
                if(r.temperature)r.temperatureSource="AMD ADLX · GPUTemperature";
                r.hotspotCelsius=d(&adlx::IADLXGPUMetrics::GPUHotspotTemperature,"GPUHotspotTemperature",0,200);
                if(auto u=d(&adlx::IADLXGPUMetrics::GPUUsage,"GPUUsage",0,100))r.utilization=static_cast<unsigned>(std::lround(*u));
                Ref<adlx::IADLXGPUMetricsSupport> support;
                if(perf)perf->GetSupportedGPUMetrics(gpu.p,support.out());
                const auto powerReading=[&](auto getter,auto supportedGetter,const char* api){
                    adlx_bool supported{};
                    const auto code=support?(support.p->*supportedGetter)(&supported):ADLX_NOT_SUPPORTED;
                    r.apiStatus.push_back(std::format("{} support: result={} supported={}",api,static_cast<int>(code),bool(supported)));
                    return code==ADLX_OK&&supported?d(getter,api,0,1000):std::optional<double>{};
                };
                auto watts=powerReading(&adlx::IADLXGPUMetrics::GPUTotalBoardPower,&adlx::IADLXGPUMetricsSupport::IsSupportedGPUTotalBoardPower,"GPUTotalBoardPower");
                const auto gpuWatts=powerReading(&adlx::IADLXGPUMetrics::GPUPower,&adlx::IADLXGPUMetricsSupport::IsSupportedGPUPower,"GPUPower");
                r.additionalSensors.push_back({r.uuid,"total_board_power","W",r.source,watts?"ok":"not supported/unavailable",watts,{},GetTickCount64()});
                r.additionalSensors.push_back({r.uuid,"gpu_power","W",r.source+" · GPUPower (SDK generic GPU domain)",gpuWatts?"ok":"not supported/unavailable",gpuWatts,{},GetTickCount64()});
                const auto intake=d(&adlx::IADLXGPUMetrics::GPUIntakeTemperature,"GPUIntakeTemperature",-40,150);
                r.additionalSensors.push_back({r.uuid,"intake_temperature","C",r.source,intake?"ok":"not supported/unavailable",intake,{},GetTickCount64()});
                const auto voltage=integer(&adlx::IADLXGPUMetrics::GPUVoltage,"GPUVoltage",10000);
                r.additionalSensors.push_back({r.uuid,"voltage","mV",r.source,voltage?"ok":"not supported/unavailable",voltage?std::optional<double>(*voltage):std::nullopt,{},GetTickCount64()});
                r.powerDomain=GpuPowerDomain::Board;r.powerSource="AMD ADLX · GPUTotalBoardPower";
                if(!watts){watts=gpuWatts;r.powerDomain=GpuPowerDomain::Gpu;r.powerSource="AMD ADLX · GPUPower (generic GPU domain; not asserted ASIC)";}
                if(watts)r.powerMw=static_cast<unsigned>(std::lround(*watts*1000));
                else{r.powerDomain=GpuPowerDomain::Unknown;r.powerSource.clear();}
                r.graphicsMhz=integer(&adlx::IADLXGPUMetrics::GPUClockSpeed,"GPUClockSpeed",20000);
                r.memoryMhz=integer(&adlx::IADLXGPUMetrics::GPUVRAMClockSpeed,"GPUVRAMClockSpeed",30000);
                r.fanRpm=integer(&adlx::IADLXGPUMetrics::GPUFanSpeed,"GPUFanSpeed",30000);
                if(auto used=integer(&adlx::IADLXGPUMetrics::GPUVRAM,"GPUVRAM",1024*1024))r.usedBytes=std::uint64_t(*used)*1024*1024;
                if(r.totalBytes&&r.usedBytes&&*r.usedBytes<=*r.totalBytes)r.freeBytes=*r.totalBytes-*r.usedBytes;
            }else r.apiStatus.push_back(std::format("GetCurrentGPUMetrics: result={}; GPU может быть неактивна",static_cast<int>(metricsCode)));
            if(typeCode==ADLX_OK&&type==GPUTYPE_DISCRETE)try{auto power=impl_->power(gpu.p);ADLX_IntRange range{};int current{};
                checked(power->GetPowerLimitRange(&range),"GetPowerLimitRange");checked(power->GetPowerLimit(&current),"GetPowerLimit");
                if(range.minValue<=range.maxValue&&range.step>0){r.powerValue=current;r.powerMinimum=range.minValue;r.powerMaximum=range.maxValue;r.powerStep=range.step;r.powerControl=true;}
                Ref<adlx::IADLXManualPowerTuning1> extended;
                if(power->QueryInterface(adlx::IADLXManualPowerTuning1::IID(),reinterpret_cast<void**>(extended.out()))==ADLX_OK){int value{};if(extended->GetPowerLimitDefault(&value)==ADLX_OK)r.powerDefault=value;}
            }catch(const std::exception& e){r.apiStatus.push_back(e.what());}
            r.available=true;r.status=r.powerControl?"AMD ADLX · Power Limit в процентах; применение проверяется чтением":"AMD ADLX · мониторинг; Power Limit не предоставлен драйвером";
            if(metricsCode!=ADLX_OK)r.status+=" · показания не получены (карта может быть неактивна)";
            rows.push_back(std::move(r));
        }
    }catch(const std::exception& e){adlxError=e.what();}
    // Diagnostic mode keeps both providers as independent observations, including
    // ADL handle/BDF and raw PMLog IDs. Never join by name or copy APU values to a
    // discrete GPU. For the discrete card, ADL is a read-only fallback only.
    if(includeIntegrated||rows.empty()){
        auto legacy=impl_->adl.readAdapters();
        for(auto& row:legacy){
            if(!includeIntegrated&&row.type!=GpuType::Discrete)continue;
            if(!adlxError.empty())row.apiStatus.push_back("ADLX: "+adlxError);
            if(pnpNumber(row.pci,"VEN_",4)==0x1002)
                bindOsIdentity(row,adapters,pnpNumber(row.pci,"DEV_",4),pnpNumber(row.pci,"SUBSYS_",8),pnpNumber(row.pci,"REV_",2));
            rows.push_back(std::move(row));
        }
    }
    // A provider can expose the iGPU while a sleeping dGPU is missing. Account
    // for EACH OS identity; an unrelated provider UUID must never suppress it.
    for(const auto& a:adapters)if(a.vendorId==0x1002&&!a.software&&(includeIntegrated||a.type==GpuType::Discrete)){
        const auto marker="os_adapter_id="+a.id;
        if(std::any_of(rows.begin(),rows.end(),[&](const auto& row){return row.uuid==a.id||std::find(row.apiStatus.begin(),row.apiStatus.end(),marker)!=row.apiStatus.end();}))continue;
        GpuReading r;r.name=a.name;r.uuid=a.id;r.osAdapterId=a.id;r.physicalId=a.physicalId;r.identitySource=a.identitySource;r.identityStatus=a.identityStatus;r.location=a.location;r.type=a.type;r.driver=a.driver;r.pci=a.pci;r.totalBytes=a.dedicatedBytes;r.source="Windows · DXGI";r.powerUnit=GpuPowerUnit::Percent;r.status=adlxError.empty()?"ОС видит адаптер; ADLX/ADL не вернули его показания. Карта может быть неактивна.":adlxError;rows.push_back(r);}
    if(rows.empty()){GpuReading r;r.name="Radeon";r.status="Дискретный Radeon не обнаружен. Встроенная графика сюда не подставляется.";r.powerUnit=GpuPowerUnit::Percent;rows.push_back(r);}
    return rows;
}
void RadeonGpu::setPowerLimit(const std::string& id,int value){std::lock_guard lock(impl_->gate);impl_->apply(id,value);}
void RadeonGpu::resetPowerLimit(const std::string& id){std::lock_guard lock(impl_->gate);impl_->apply(id,{});}
}
