// Explicit diagnostic executable, NOT a CTest. Read getters only: no tuning,
// EC access, startup restore, driver installation or stress workload.
#include "MagicControl/Monitoring.h"
#include "MagicControl/GpuDevice.h"
#include "MagicControl/ControlAudit.h"
#include "MagicControl/Logging.h"
#include "../src/platform/windows/AdlxRuntime.h"
#include "../src/platform/windows/ApuPowerWindows.h"
#include "../src/third_party/adlx/include/IPerformanceMonitoring2.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <cmath>
int main(){
    using namespace mc;using windows::AdlxRef;using audit::quote;using audit::optional;
    initializeLogging(false);
    try{
        auto runtime=windows::acquireAdlxRuntime();MonitoringReader monitor(nullptr,false,false);
        RadeonGpu radeon;windows::ApuPowerReader apu;
        for(int sample=0;sample<6;++sample){
            const auto cpu=monitor.read();const auto smu=apu.read(true);const auto raw=radeon.read(true);
            std::cout<<"{\"sample\":"<<sample<<",\"at_ms\":"<<GetTickCount64()
                <<",\"cpu_usage\":"<<optional(cpu.processors[0].usagePercent)
                <<",\"cpu_watts\":"<<optional(cpu.processors[0].powerWatts)
                <<",\"cpu_source\":"<<quote(cpu.processors[0].powerSource)
                <<",\"package_watts\":"<<optional(cpu.apuPackagePowerWatts)
                <<",\"package_source\":"<<quote(cpu.apuPackagePowerSource)
                <<",\"smu_fast_watts\":"<<optional(smu.fastPowerWatts)<<",\"smu_status\":"<<quote(smu.status)
                <<",\"observed_npu_usage\":"<<optional(cpu.npuUsagePercent)<<",\"observed_npu_mhz\":"<<optional(cpu.npuClockMhz)
                <<",\"observed_npu_source\":"<<quote(cpu.npuClockSource)
                <<",\"smu_limits\":["<<optional(smu.stapmLimitWatts)<<','<<optional(smu.slowLimitWatts)<<','<<optional(smu.fastLimitWatts)<<']'
                <<",\"providers\":[";
            bool first=true;for(const auto& row:raw){if(!first)std::cout<<',';first=false;
                std::cout<<"{\"id\":"<<quote(row.uuid)<<",\"source\":"<<quote(row.source)<<",\"power_source\":"<<quote(row.powerSource)
                    <<",\"power_mw\":"<<optional(row.powerMw)<<",\"load\":"<<optional(row.utilization)<<",\"sensors\":[";
                bool begin=true;for(const auto& sensor:row.additionalSensors){if(!begin)std::cout<<',';begin=false;
                    std::cout<<"{\"metric\":"<<quote(sensor.metric)<<",\"value\":"<<optional(sensor.value)<<",\"source\":"<<quote(sensor.source)<<'}';}
                std::cout<<"]}";
            }
            std::cout<<"],\"adlx_support\":[";
            {std::lock_guard lock(runtime->gate);AdlxRef<adlx::IADLXPerformanceMonitoringServices> perf;
                AdlxRef<adlx::IADLXGPUList> list;first=true;
                if(runtime->system->GetPerformanceMonitoringServices(perf.out())==ADLX_OK&&perf&&runtime->system->GetGPUs(list.out())==ADLX_OK&&list)
                for(adlx_uint i=0;i<list->Size();++i){AdlxRef<adlx::IADLXGPU> gpu;AdlxRef<adlx::IADLXGPUMetricsSupport> support;AdlxRef<adlx::IADLXGPUMetrics> metrics;
                    if(list->At(i,gpu.out())!=ADLX_OK||!gpu||perf->GetSupportedGPUMetrics(gpu.p,support.out())!=ADLX_OK||!support||perf->GetCurrentGPUMetrics(gpu.p,metrics.out())!=ADLX_OK||!metrics)continue;
                    adlx_bool ps=false,bs=false;const auto psc=support->IsSupportedGPUPower(&ps),bsc=support->IsSupportedGPUTotalBoardPower(&bs);
                    double pv=0,bv=0;const auto pc=metrics->GPUPower(&pv),bc=metrics->GPUTotalBoardPower(&bv);
                    const char* name=nullptr;gpu->Name(&name);if(!first)std::cout<<',';first=false;
                    std::cout<<"{\"name\":"<<quote(name?name:"")<<",\"power_support_code\":"<<int(psc)<<",\"power_supported\":"<<(ps?"true":"false")
                        <<",\"power_get_code\":"<<int(pc)<<",\"power_watts\":"<<optional(pc==ADLX_OK&&std::isfinite(pv)?std::optional<double>(pv):std::nullopt)
                        <<",\"board_support_code\":"<<int(bsc)<<",\"board_supported\":"<<(bs?"true":"false")<<",\"board_get_code\":"<<int(bc)
                        <<",\"board_watts\":"<<optional(bc==ADLX_OK&&std::isfinite(bv)?std::optional<double>(bv):std::nullopt);
                    AdlxRef<adlx::IADLXGPUMetricsSupport1> ns;AdlxRef<adlx::IADLXGPUMetrics1> nm;
                    const auto sc=support->QueryInterface(adlx::IADLXGPUMetricsSupport1::IID(),reinterpret_cast<void**>(ns.out()));
                    const auto mc=metrics->QueryInterface(adlx::IADLXGPUMetrics1::IID(),reinterpret_cast<void**>(nm.out()));
                    adlx_bool us=false,fs=false;int usage{},frequency{};
                    const auto usc=ns?ns->IsSupportedNPUActivityLevel(&us):ADLX_NOT_SUPPORTED;
                    const auto fsc=ns?ns->IsSupportedNPUFrequency(&fs):ADLX_NOT_SUPPORTED;
                    const auto uc=us&&nm?nm->NPUActivityLevel(&usage):ADLX_NOT_SUPPORTED;
                    const auto fc=fs&&nm?nm->NPUFrequency(&frequency):ADLX_NOT_SUPPORTED;
                    std::cout<<",\"npu_support_interface\":"<<int(sc)<<",\"npu_metrics_interface\":"<<int(mc)
                        <<",\"npu_usage_support_code\":"<<int(usc)<<",\"npu_usage_supported\":"<<(us?"true":"false")
                        <<",\"npu_usage_code\":"<<int(uc)<<",\"npu_usage\":"<<optional(uc==ADLX_OK?std::optional<int>(usage):std::nullopt)
                        <<",\"npu_frequency_support_code\":"<<int(fsc)<<",\"npu_frequency_supported\":"<<(fs?"true":"false")
                        <<",\"npu_frequency_code\":"<<int(fc)<<",\"npu_mhz\":"<<optional(fc==ADLX_OK?std::optional<int>(frequency):std::nullopt)<<'}';
                }}
            std::cout<<"]}\n"<<std::flush;
            if(sample<5)std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
