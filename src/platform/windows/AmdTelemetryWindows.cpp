#include "AmdTelemetryWindows.h"
#include "AdlxRuntime.h"
#include "../../third_party/adlx/include/IPerformanceMonitoring2.h"
#include <windows.h>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <format>
#include <utility>

namespace mc::windows {
namespace {
template<class T> T symbol(HMODULE library, const char* name) {
    return reinterpret_cast<T>(GetProcAddress(library, name));
}
void* __cdecl allocate(int size) { return size > 0 ? std::malloc(static_cast<std::size_t>(size)) : nullptr; }
// ADLPMLogDataOutput and AdapterInfo use the context-aware ABI used by the
// reference. AdapterInfo's fixed size and field offsets come from ADL-structs.h.
struct Sensor { int supported{}, value{}; };
struct PmLog { int size{sizeof(PmLog)}; std::array<Sensor, 256> sensors{}; };
static_assert(sizeof(PmLog) == 2052);
using AdlCreate = int (__cdecl*)(void* (__cdecl*)(int), int, void**);
using AdlDestroy = int (__cdecl*)(void*);
using AdapterCount = int (__cdecl*)(void*, int*);
using AdapterInfo = int (__cdecl*)(void*, void*, int);
using AsicType = int (__cdecl*)(void*, int, int*, int*);
using ReadLog = int (__cdecl*)(void*, int, PmLog*);
// Exact AMD ADL SDK adl_defines.h ADL_PMLOG_SENSORS. These are independent
// power domains, not interchangeable aliases and never additive components.
constexpr int asicPowerId=23,gfxPowerId=30,boardPowerId=73;
std::optional<double> sensor(const PmLog& log, int id, double minimum, double maximum) {
    const auto& sample = log.sensors[id];
    return sample.supported && sample.value >= minimum && sample.value <= maximum
        ? std::optional<double>(sample.value) : std::nullopt;
}
}
struct AmdTelemetryReader::Impl {
    HMODULE adl{};void* context{};
    adlx::IADLXPerformanceMonitoringServices* performance{};
    AdlDestroy destroy{};ReadLog readLog{};
    struct Adapter {
        int index{},bus{},device{},function{};
        std::string udid,name,pnp;
        GpuType type{GpuType::Unknown};
        std::string typeStatus;
    };
    std::vector<Adapter> adapters;
    std::string adlStatus;
    std::shared_ptr<AdlxRuntime> sharedAdlx;
    ULONGLONG retryAdl{}, retryAdlx{};
    ~Impl() {
        if(performance)performance->Release();
        if (context && destroy) destroy(context);
        if (adl) FreeLibrary(adl);
    }
    bool prepareAdl() {
        if (context && !adapters.empty()) return true;
        if (GetTickCount64() < retryAdl) return false;
        retryAdl = GetTickCount64() + 30000;
        if (!adl) adl = LoadLibraryExW(L"atiadlxx.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!adl) {adlStatus="atiadlxx.dll: Win32="+std::to_string(GetLastError());return false;}
        const auto create = symbol<AdlCreate>(adl, "ADL2_Main_Control_Create");
        destroy = symbol<AdlDestroy>(adl, "ADL2_Main_Control_Destroy");
        const auto count = symbol<AdapterCount>(adl, "ADL2_Adapter_NumberOfAdapters_Get");
        const auto info = symbol<AdapterInfo>(adl, "ADL2_Adapter_AdapterInfo_Get");
        readLog = symbol<ReadLog>(adl, "ADL2_New_QueryPMLogData_Get");
        if (!create || !destroy || !count || !info || !readLog) {adlStatus="ADL2: required export unavailable";return false;}
        if (!context) {
            const int code=create(allocate,1,&context);
            if(code!=0||!context){adlStatus="ADL2_Main_Control_Create: result="+std::to_string(code);return false;}
        }
        int size{};
        const int countCode=count(context,&size);
        if(countCode!=0||size<1||size>64){adlStatus=std::format("ADL2_Adapter_NumberOfAdapters_Get: result={}, count={}",countCode,size);return false;}
        constexpr int bytes = 1572;
        std::vector<unsigned char> entries(static_cast<std::size_t>(size) * bytes);
        for (int i = 0; i < size; ++i) std::memcpy(entries.data() + i * bytes, &bytes, sizeof(bytes));
        const int infoCode=info(context,entries.data(),static_cast<int>(entries.size()));
        if(infoCode!=0){adlStatus="ADL2_Adapter_AdapterInfo_Get: result="+std::to_string(infoCode);return false;}
        const auto asicType=symbol<AsicType>(adl,"ADL2_Adapter_ASICFamilyType_Get");
        for (int i = 0; i < size; ++i) {
            const auto number = [&](int offset) { int value{}; std::memcpy(&value, entries.data() + i * bytes + offset, 4); return value; };
            if(number(792)==0||(number(276)!=1002&&number(276)!=0x1002))continue;
            const auto text=[&](int offset){
                const char* first=reinterpret_cast<const char*>(entries.data()+i*bytes+offset);
                int length{};while(length<256&&first[length])++length;
                const int n=MultiByteToWideChar(CP_ACP,0,first,length,nullptr,0);
                std::wstring wide(n,L'\0');if(n)MultiByteToWideChar(CP_ACP,0,first,length,wide.data(),n);
                const int m=WideCharToMultiByte(CP_UTF8,0,wide.data(),n,nullptr,0,nullptr,nullptr);
                std::string result(m,'\0');if(m)WideCharToMultiByte(CP_UTF8,0,wide.data(),n,result.data(),m,nullptr,nullptr);
                return result;
            };
            Adapter adapter;adapter.index=number(4);adapter.bus=number(264);adapter.device=number(268);adapter.function=number(272);
            adapter.udid=text(8);adapter.name=text(280);adapter.pnp=text(1312);
            int types{},valid{};
            const int typeCode=asicType?asicType(context,adapter.index,&types,&valid):-1;
            adapter.typeStatus=std::format("ADL2_Adapter_ASICFamilyType_Get: result={}, types=0x{:X}, valid=0x{:X}",typeCode,types,valid);
            if(typeCode==0){
                const bool discrete=(valid&1)&&(types&1),integrated=(valid&2)&&(types&2);
                if(discrete!=integrated)adapter.type=integrated?GpuType::Integrated:GpuType::Discrete;
            }
            // ADL can expose two handles for one GPU. Keep each handle and its
            // BDF/UDID: neither names nor primary-adapter order prove identity.
            adapters.push_back(std::move(adapter));
        }
        adlStatus=adapters.empty()?"ADL2: no present AMD adapters":"ok";
        return !adapters.empty();
    }
    int queryAdl(int adapter,PmLog& log){return readLog(context,adapter,&log);}
    std::vector<GpuReading> readAdapters(){
        std::vector<GpuReading> rows;
        if(!prepareAdl()){
            GpuReading r;r.name="AMD ADL";r.source="AMD ADL · PMLog";r.status=adlStatus;
            r.apiStatus.push_back(adlStatus);rows.push_back(std::move(r));return rows;
        }
        for(const auto& adapter:adapters){
            GpuReading r;r.name=adapter.name;r.source="AMD ADL · PMLog";r.type=adapter.type;
            r.uuid=std::format("adl:{}:{}",adapter.index,adapter.udid);
            r.pci=std::format("bus={} device={} function={}; {}",adapter.bus,adapter.device,adapter.function,adapter.pnp);
            if(adapter.bus>=0&&adapter.bus<=255&&adapter.device>=0&&adapter.device<=31&&adapter.function>=0&&adapter.function<=7){
                r.location.bus=adapter.bus;r.location.device=adapter.device;r.location.function=adapter.function;r.location.source="ADL AdapterInfo PCI bus/device/function; segment not reported";}
            r.apiStatus.push_back(adapter.typeStatus);
            PmLog log;const int code=queryAdl(adapter.index,log);
            r.apiStatus.push_back("ADL2_New_QueryPMLogData_Get: result="+std::to_string(code));
            r.available=code==0;
            r.status=code==0?"AMD ADL · read-only PMLog; independent adapter handle":"AMD ADL · PMLog unavailable; result="+std::to_string(code);
            if(code==0){
                const auto integer=[&](int id,int high)->std::optional<unsigned>{
                    const auto value=sensor(log,id,0,high);return value?std::optional<unsigned>(static_cast<unsigned>(*value)):std::nullopt;
                };
                r.temperature=integer(8,150);r.temperatureSource="AMD ADL · PMLog/8 Edge";
                if(!r.temperature){r.temperature=integer(28,150);r.temperatureSource="AMD ADL · PMLog/28 GFX";}if(!r.temperature)r.temperatureSource.clear();
                r.hotspotCelsius=sensor(log,27,0,200);r.utilization=integer(19,100);r.memoryUtilization=integer(20,100);
                r.graphicsMhz=integer(1,20000);r.memoryMhz=integer(2,30000);r.fanRpm=integer(14,30000);
                // Whole-board/ASIC are preferred to the narrower GFX domain.
                // A supported0W stays valid: no nonzero heuristic or summation.
                auto watts=sensor(log,boardPowerId,0,2000);int powerId=boardPowerId;r.powerDomain=GpuPowerDomain::Board;
                if(!watts){watts=sensor(log,asicPowerId,0,2000);powerId=asicPowerId;r.powerDomain=GpuPowerDomain::Asic;}
                if(!watts){watts=sensor(log,gfxPowerId,0,2000);powerId=gfxPowerId;r.powerDomain=GpuPowerDomain::Gfx;}
                if(watts){r.powerMw=static_cast<unsigned>(*watts*1000);r.powerSource="AMD ADL · "+std::string(gpuPowerDomainName(r.powerDomain))+" · PMLog/"+std::to_string(powerId);r.apiStatus.push_back("powerMw source: PMLog/"+std::to_string(powerId));}
                else r.powerDomain=GpuPowerDomain::Unknown;
                for(const auto [id,domain]:{std::pair{boardPowerId,GpuPowerDomain::Board},std::pair{asicPowerId,GpuPowerDomain::Asic},std::pair{gfxPowerId,GpuPowerDomain::Gfx}}){
                    const auto value=sensor(log,id,0,2000);
                    r.additionalSensors.push_back({r.uuid,"power/"+std::string(gpuPowerDomainName(domain)),"W",r.source+" · PMLog/"+std::to_string(id),value?"supported; separate domain, not summed":"unsupported/out of range",value,{},GetTickCount64()});}
                for(std::size_t id=0;id<log.sensors.size();++id)if(log.sensors[id].supported){
                    const char* unit="raw driver units";
                    switch(id){
                    case 1:case 2:case 3:case 34:unit="MHz";break;
                    case 8:case 9:case 10:case 11:case 12:case 13:case 24:case 25:case 26:case 27:case 28:case 29:case 32:case 74:unit="C";break;
                    case 14:unit="RPM";break;
                    case 15:case 19:case 20:unit="percent";break;
                    case 17:case 23:case 30:case 33:case 73:unit="W";break;
                    default:break;
                    }
                    const double raw=log.sensors[id].value;
                    r.additionalSensors.push_back({r.uuid,"PMLog/"+std::to_string(id),unit,r.source,
                        "driver supported; raw value retained",raw,raw,GetTickCount64()});
                }
                if(r.additionalSensors.empty())r.status+="; no supported sensors (adapter may be inactive)";
            }
            // PMLog is not a power/fan setter capability. Missing VRAM size,
            // voltage units or unsupported fields remain unknown.
            rows.push_back(std::move(r));
        }
        return rows;
    }
    bool prepareAdlx() {
        if (performance) return true;
        if (GetTickCount64() < retryAdlx) return false;
        retryAdlx = GetTickCount64() + 30000;
        try{
            if(!sharedAdlx)sharedAdlx=acquireAdlxRuntime();
            std::lock_guard lock(sharedAdlx->gate);
            return sharedAdlx->system->GetPerformanceMonitoringServices(&performance)==ADLX_OK&&performance;
        }catch(const std::exception&){return false;}
    }
    void readAdl(MonitoringSnapshot& sample) {
        auto& cpu = sample.processors[0]; auto& gpu = sample.processors[1];
        cpu.temperatureSource = "AMD ADL · CPU";
        gpu.temperatureSource = "AMD ADL · PMLog";
        sample.socSource = "AMD ADL · SoC";
        cpu.clockSource = gpu.clockSource = gpu.usageSource = "AMD ADL";
        sample.npuUsageSource = sample.npuClockSource = "AMD ADL · PMLog";
        if (!prepareAdl()) { sample.processorStatus = "AMD ADL недоступен"; return; }
        // Prefer the APU adapter that actually reports CPU sensor 32. Never mix
        // one adapter's temperature with another adapter's frequency/load.
        PmLog selected; int selectedAdapter{-1};
        for (const auto& entry : adapters) {
            const int adapter=entry.index;
            PmLog log;
            const auto status=queryAdl(adapter,log);
            if(sample.diagnostic){
                if(status)sample.additionalSensors.push_back({"AMD adapter "+std::to_string(adapter),"PMLog/provider_status","","AMD ADL · PMLog","result="+std::to_string(status),{},{},GetTickCount64()});
                else for(std::size_t i=0;i<log.sensors.size();++i)if(log.sensors[i].supported)
                    sample.additionalSensors.push_back({"AMD adapter "+std::to_string(adapter),"PMLog/"+std::to_string(i),"raw driver units","AMD ADL · PMLog","supported; units depend on sensor ID",double(log.sensors[i].value),double(log.sensors[i].value),GetTickCount64()});
            }
            if (status != 0) continue;
            if (selectedAdapter < 0 || (!sensor(selected,32,1,149) && sensor(log, 32, 1, 149))) { selected = log; selectedAdapter = adapter; }
            if (sensor(log, 32, 1, 149) && !sample.diagnostic) break;
        }
        if (selectedAdapter < 0) { sample.processorStatus = "AMD ADL PMLog: нет свежего снимка"; return; }

        cpu.adapterIndex = gpu.adapterIndex = selectedAdapter;
        cpu.celsius = sensor(selected, 32, 1, 149); cpu.temperatureSensorId = 32;
        gpu.celsius = sensor(selected, 8, 1, 149); gpu.temperatureSensorId = 8;
        if (!gpu.celsius) { gpu.celsius = sensor(selected, 28, 1, 149); gpu.temperatureSensorId = 28; }
        cpu.driverCelsius = cpu.celsius;
        gpu.driverCelsius = gpu.celsius;
        gpu.temperatureSource = gpu.temperatureSensorId == 28 ? "AMD ADL · GFX" : "AMD ADL · Edge";
        sample.socCelsius = sensor(selected, 29, 1, 149);
        cpu.clockMhz = sensor(selected, 34, 0, 10000);
        gpu.clockMhz = sensor(selected, 1, 0, 10000);
        gpu.usagePercent = sensor(selected, 19, 0, 100);
        const auto selectedInfo=std::find_if(adapters.begin(),adapters.end(),[&](const auto& entry){return entry.index==selectedAdapter;});
        const bool discrete=selectedInfo!=adapters.end()&&selectedInfo->type==GpuType::Discrete;
        // On an APU, ASIC/GPUPower may include non-graphics domains. Keep GFX
        // explicit; lack of GFX is unknown, not permission to substitute ASIC.
        if(discrete){gpu.powerWatts=sensor(selected,boardPowerId,0,2000);gpu.powerSource="AMD ADL · Board · PMLog/73";
            if(!gpu.powerWatts){gpu.powerWatts=sensor(selected,asicPowerId,0,2000);gpu.powerSource="AMD ADL · ASIC · PMLog/23";}}
        if(!gpu.powerWatts){gpu.powerWatts=sensor(selected,gfxPowerId,0,2000);gpu.powerSource="AMD ADL · GFX · PMLog/30";}
        if(!gpu.powerWatts)gpu.powerSource.clear();
        sample.npuUsagePercent = sensor(selected, 72, 0, 100);
        sample.npuClockMhz = sensor(selected, 71, 0, 10000);
    }
    void readAdlx(MonitoringSnapshot& sample) {
        if(!prepareAdlx())return;
        std::lock_guard lock(sharedAdlx->gate);
        AdlxRef<adlx::IADLXSystemMetricsSupport> support;
        AdlxRef<adlx::IADLXSystemMetrics> metrics;adlx_bool supported{};double usage{};
        if(performance->GetSupportedSystemMetrics(support.out())==ADLX_OK&&support&&
            support->IsSupportedCPUUsage(&supported)==ADLX_OK&&supported&&
            performance->GetCurrentSystemMetrics(metrics.out())==ADLX_OK&&metrics&&
            metrics->CPUUsage(&usage)==ADLX_OK&&std::isfinite(usage)&&usage>=0&&usage<=100){
            sample.processors[0].usagePercent=usage;sample.processors[0].usageSource="AMD ADLX";
        }
        AdlxRef<adlx::IADLXGPUList> list;
        if(sharedAdlx->system->GetGPUs(list.out())!=ADLX_OK||!list||list->Size()!=1)return;
        AdlxRef<adlx::IADLXGPU> gpu;if(list->At(0,gpu.out())!=ADLX_OK||!gpu)return;
        AdlxRef<adlx::IADLXGPUMetricsSupport> gpuSupport;
        AdlxRef<adlx::IADLXGPUMetrics> gpuMetrics;
        if(performance->GetSupportedGPUMetrics(gpu.p,gpuSupport.out())!=ADLX_OK||!gpuSupport||
            performance->GetCurrentGPUMetrics(gpu.p,gpuMetrics.out())!=ADLX_OK||!gpuMetrics)return;
        double watts{};supported=false;
        ADLX_GPU_TYPE type{};const bool discrete=gpu->Type(&type)==ADLX_OK&&type==GPUTYPE_DISCRETE;
        // Generic power may be used for a discrete GPU, never over APU GFX.
        const bool onlyGfx=sample.processors[1].powerSource=="AMD ADL · GFX · PMLog/30";
        if(discrete&&(!sample.processors[1].powerWatts||onlyGfx)&&gpuSupport->IsSupportedGPUPower(&supported)==ADLX_OK&&supported&&
            gpuMetrics->GPUPower(&watts)==ADLX_OK&&std::isfinite(watts)&&watts>=0&&watts<=250){
            sample.processors[1].powerWatts=watts;sample.processors[1].powerSource="AMD ADLX · GPUPower (generic GPU domain)";
        }
        AdlxRef<adlx::IADLXGPUMetricsSupport1> extendedSupport;
        AdlxRef<adlx::IADLXGPUMetrics1> extendedMetrics;
        if(gpuSupport->QueryInterface(adlx::IADLXGPUMetricsSupport1::IID(),reinterpret_cast<void**>(extendedSupport.out()))!=ADLX_OK||!extendedSupport||
            gpuMetrics->QueryInterface(adlx::IADLXGPUMetrics1::IID(),reinterpret_cast<void**>(extendedMetrics.out()))!=ADLX_OK||!extendedMetrics)return;
        int activity{},clock{};supported=false;
        if(extendedSupport->IsSupportedNPUActivityLevel(&supported)==ADLX_OK&&supported&&
            extendedMetrics->NPUActivityLevel(&activity)==ADLX_OK&&activity>=0&&activity<=100){sample.npuUsagePercent=activity;sample.npuUsageSource="AMD ADLX";}
        supported=false;
        if(extendedSupport->IsSupportedNPUFrequency(&supported)==ADLX_OK&&supported&&
            extendedMetrics->NPUFrequency(&clock)==ADLX_OK&&clock>=0&&clock<=10000){sample.npuClockMhz=clock;sample.npuClockSource="AMD ADLX";}
    }
    void read(MonitoringSnapshot& sample) {
        const auto started = GetTickCount64();
        sample.processorSampledAtMs = started;
        readAdl(sample); readAdlx(sample);
        if (GetTickCount64() - started > 10000) {
            sample.processors = {}; sample.socCelsius.reset(); sample.npuUsagePercent.reset(); sample.npuClockMhz.reset();
            sample.processorStatus = "Данные AMD устарели во время чтения";
        }
    }
};
AmdTelemetryReader::AmdTelemetryReader() : impl_(std::make_unique<Impl>()) {}
AmdTelemetryReader::~AmdTelemetryReader() = default;
std::vector<GpuReading> AmdTelemetryReader::readAdapters() { return impl_->readAdapters(); }
void AmdTelemetryReader::read(MonitoringSnapshot& sample) { impl_->read(sample); }
void AmdTelemetryReader::readTemperatures(MonitoringSnapshot& sample) {
    const auto started = GetTickCount64();
    sample.processorSampledAtMs = started;
    impl_->readAdl(sample);
    if (GetTickCount64() - started > 10000) {
        sample.processors = {};
        sample.processorStatus = "Температуры AMD ADL устарели во время чтения";
    }
}
}
