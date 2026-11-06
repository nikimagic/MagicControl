#include "IntelTelemetryWindows.h"
#include "MagicControl/SensorDecoders.h"
#include "MagicControl/Logging.h"
#include "../../../devices/lenovo/legion-kwcn/ControlWindows.h"
#include "SensorModuleWindows.h"
#include "HardwareMutexWindows.h"
#include "../PowerDiagnosticsWindows.h"
#include "../ProcessorFrequencyWindows.h"
#include <intrin.h>
#include <powrprof.h>
#include <cmath>
#include <cstring>
#include <vector>

namespace mc::windows {
ProcessorVendor nativeCpuVendor() noexcept {
    int info[4]{}; __cpuid(info,0); char vendor[13]{};
    memcpy(vendor,info+1,4); memcpy(vendor+4,info+3,4); memcpy(vendor+8,info+2,4);
    return !strcmp(vendor,"GenuineIntel") ? ProcessorVendor::Intel :
           !strcmp(vendor,"AuthenticAMD") ? ProcessorVendor::Amd : ProcessorVendor::Unknown;
}
struct IntelTelemetryReader::Impl {
    detail::Module module;
    detail::Module mchbar;
    HANDLE pciMutex{};
    ULONGLONG memoryRetry{};
    bool supported{};
    bool powerDiagnostics{};
    bool lenovoPowerDiagnostics{};
    ULONGLONG lastPowerDiagnosticAt{};
    std::optional<IntelPackagePowerObservation> lastPowerDiagnostic;
    std::string msrModuleError;
    ULONGLONG retry{}, previousAt{};
    std::optional<std::uint32_t> previousEnergy;
    std::optional<std::uint32_t> previousCoreEnergy;
    ULONGLONG previousCoreAt{},coreRetry{};
    bool coreSupported{},memorySupported{},coreCounterObserved{};
    ~Impl(){if(pciMutex)CloseHandle(pciMutex);}
    explicit Impl(bool collectDiagnostics):powerDiagnostics(collectDiagnostics) {
        if (nativeCpuVendor()!=ProcessorVendor::Intel) return;
        int info[4]{}; __cpuid(info,1);
        const unsigned eax=static_cast<unsigned>(info[0]);
        const unsigned family=((eax>>8)&15), model=((eax>>4)&15)|((eax>>12)&0xF0);
        // Raptor Lake and reviewed Arrow Lake C5/C6 (HYDROC275HX): Intel MSR
        // package DTS/RAPL layout, corroborated by Intel powertelemetry and LHM.
        // Runtime valid bits/units still gate readings; this grants no EC writes.
        supported=family==6&&(model==0xB7||model==0xBA||model==0xBF||model==0xC5||model==0xC6);
        coreSupported=family==6&&(model==0xB7||model==0xBA||model==0xBF);
        memorySupported=family==6&&(model==0x97||model==0x9A||model==0xB7||model==0xBA||model==0xBF);
        try{lenovoPowerDiagnostics=coreSupported&&legionHostSupported();}catch(...){}
    }
    void read(MonitoringSnapshot& sample) {
        if (nativeCpuVendor()!=ProcessorVendor::Intel) return;
        sample.processorVendors[0]=ProcessorVendor::Intel;
        sample.processorSampledAtMs=GetTickCount64();
        auto& cpu=sample.processors[0];
        readWindowsProcessorFrequency(cpu);
        readMemoryClock(sample);
        if(!supported){sample.processorStatus+=" Intel DTS/RAPL: модель CPU пока не подключена";return;}
        const auto now=GetTickCount64();
        // Reuse the existing read-only Raptor Lake collector during an action
        // burst or an explicit diagnostic. Lenovo keeps its prior baseline.
        const bool collectPower=powerDiagnostics&&coreSupported&&(sample.diagnostic||powerDiagnosticBurstActive()||
            (lenovoPowerDiagnostics&&loggingEnabled()));
        if(now<retry){if(collectPower)readPackagePowerDiagnostic(sample);return;}
        try {if(!module.ready())module.open(L"IntelMSR.bin","d6ed85d65ab17a22f813ef98207d6d537155ee2ded5976a21cb48413c9b92e5f");msrModuleError.clear();}
        catch(const std::exception& error){msrModuleError=error.what();module.reset();previousEnergy.reset();previousCoreEnergy.reset();coreCounterObserved=false;retry=now+30000;sample.processorStatus+=" Intel/PawnIO: "+msrModuleError;if(collectPower)readPackagePowerDiagnostic(sample);return;}
        try {
            const auto target=(module.intelMsr(0x1A2)>>16)&0xFF;
            const auto thermal=module.intelMsr(0x1B1);
            const auto delta=(thermal>>16)&0x7F;
            if(target>=50&&target<=125) {
                sample.cpuTemperatureTargetCelsius=static_cast<double>(target);
                if((thermal&(1ULL<<31))&&delta<=target) {
                    cpu.celsius=static_cast<double>(target-delta);
                    cpu.temperatureSource="PawnIO · Intel Package DTS";
                }
            }
        }catch(const std::exception& error){sample.processorStatus+=" Intel DTS: "+std::string(error.what());}
        try {
            const auto exponent=(module.intelMsr(0x606)>>8)&0x1F;
            const auto energy=static_cast<std::uint32_t>(module.intelMsr(0x611));
            const auto sampledAt=GetTickCount64();
            if(exponent>0&&previousEnergy) {
                const double seconds=static_cast<double>(sampledAt-previousAt)/1000.;
                const auto ticks=static_cast<std::uint32_t>(energy-*previousEnergy);
                const double watts=static_cast<double>(ticks)*std::ldexp(1.,-static_cast<int>(exponent))/seconds;
                if(seconds>.05&&seconds<15&&std::isfinite(watts)&&watts>=0&&watts<500) {
                    cpu.powerWatts=watts;cpu.powerSource="PawnIO · Intel Package RAPL";
                }
            }
            previousEnergy=energy;previousAt=sampledAt;
        }catch(const std::exception& error){previousEnergy.reset();sample.processorStatus+=" Intel RAPL: "+std::string(error.what());}
        // Independent PP0 counter (0x639), corroborated by LHM IntelCpu.
        // Optional on reviewed Raptor Lake only; never synthesize from Package.
        if(coreSupported&&now>=coreRetry)try{
            const auto exponent=(module.intelMsr(0x606)>>8)&0x1F;
            const auto energy=static_cast<std::uint32_t>(module.intelMsr(0x639));
            const auto at=GetTickCount64();
            if(exponent>0&&previousCoreEnergy){
                const double seconds=static_cast<double>(at-previousCoreAt)/1000.;
                const auto ticks=static_cast<std::uint32_t>(energy-*previousCoreEnergy);
                if(ticks)coreCounterObserved=true;
                if(seconds>.05&&seconds<15&&coreCounterObserved){
                    const double watts=static_cast<double>(ticks)*std::ldexp(1.,-static_cast<int>(exponent))/seconds;
                    if(std::isfinite(watts)&&watts>=0&&watts<500){sample.cpuCorePowerWatts=watts;sample.cpuCorePowerSource="PawnIO · Intel Cores RAPL (PP0)";}
                }
            }
            previousCoreEnergy=energy;previousCoreAt=at;
        }catch(const std::exception& error){previousCoreEnergy.reset();coreCounterObserved=false;coreRetry=now+30000;
            sample.processorStatus+=" Intel Cores RAPL unavailable: "+std::string(error.what());}
        if(collectPower)readPackagePowerDiagnostic(sample);
    }
    void readPackagePowerDiagnostic(MonitoringSnapshot& sample){
        const auto now=GetTickCount64();
        const bool burst=powerDiagnosticBurstActive()||sample.diagnostic;
        const auto interval=burst?1000ULL:10000ULL;
        if(lastPowerDiagnostic&&now>=lastPowerDiagnosticAt&&now-lastPowerDiagnosticAt<interval){sample.intelPackagePower=lastPowerDiagnostic;return;}
        lastPowerDiagnosticAt=now;
        IntelPackagePowerObservation out;
        out.legacyLenovoDiagnostic=lenovoPowerDiagnostics;
        const auto addError=[&](std::string_view which,const std::exception& e){if(!out.status.empty())out.status+="; ";out.status+=std::string(which)+": "+e.what();};
        const auto window=[](std::uint64_t field,int timeExp){const auto y=static_cast<int>((field>>17)&31),z=static_cast<int>((field>>22)&3);return std::ldexp(1.0,y-timeExp)*(1.0+z/4.0);};
        if(module.ready()){
        try{const auto units=module.intelMsr(0x606);if(units==UINT64_MAX)throw std::runtime_error("invalid all-ones MSR");out.unitsRaw=units;}
        catch(const std::exception& e){addError("MSR 606",e);}
        try{
            const auto limit=module.intelMsr(0x610);
            if(limit==UINT64_MAX)throw std::runtime_error("invalid all-ones MSR");
            out.msrLimitRaw=limit;
            out.msrLimit1Enabled=(limit&(1ULL<<15))!=0;out.msrLimit2Enabled=(limit&(1ULL<<47))!=0;
            if(out.unitsRaw){const auto powerExp=static_cast<int>(*out.unitsRaw&0xf),timeExp=static_cast<int>((*out.unitsRaw>>16)&0xf);
                out.msrLimit1Watts=std::ldexp(static_cast<double>(limit&0x7fff),-powerExp);
                out.msrLimit2Watts=std::ldexp(static_cast<double>((limit>>32)&0x7fff),-powerExp);
                out.msrWindow1Seconds=window(limit,timeExp);out.msrWindow2Seconds=window(limit>>32,timeExp);}
        }catch(const std::exception& e){addError("MSR 610",e);}
        try{const auto perf=module.intelMsr(0x613);if(perf==UINT64_MAX)throw std::runtime_error("invalid all-ones MSR");out.msrPerformanceRaw=perf;}
        catch(const std::exception& e){addError("MSR 613",e);}
        // Intel SDM Vol.4, Tables 2-45/2-46: on reviewed family 6 models
        // B7/BA/BF, MSR 601[12:0] is PL4 current in 0.125 A steps; bit31
        // is lock. This is separate from the EC PL4 request and RAPL watts.
        try{
            const auto current=module.intelMsr(0x601);
            if(current==UINT64_MAX)throw std::runtime_error("invalid all-ones MSR");
            out.msrPl4CurrentRaw=current;
            out.msrPl4CurrentAmps=double(current&0x1fff)*0.125;
            out.msrPl4CurrentLocked=(current&(1ULL<<31))!=0;
        }catch(const std::exception& e){addError("MSR 601",e);}
        out.msrSampledAtMs=GetTickCount64();
        }else out.status="MSR: "+(msrModuleError.empty()?std::string("IntelMSR module unavailable or retry pending"):msrModuleError);
        if(!pciMutex)pciMutex=openSharedHardwareMutex(kPciMutexName);
        const auto acquired=pciMutex?WaitForSingleObject(pciMutex,250):WAIT_FAILED;
        if(acquired==WAIT_OBJECT_0||acquired==WAIT_ABANDONED){
            try{
                if(!mchbar.ready())mchbar.open(L"IntelMCHBAR.bin","3f82b832d99b4aac37d2a20fdb7c9baa2a3bc0488612c9019c9484eb0e8a6eae");
                const auto decodeMmio=[&](std::uint32_t raw,bool first,std::optional<std::uint32_t>& rawOut,std::optional<bool>& enabled,std::optional<bool>& clamp,std::optional<double>& watts,std::optional<double>& seconds){
                    if(raw==UINT32_MAX)throw std::runtime_error("invalid all-ones MMIO");
                    rawOut=raw;enabled=(raw&(1u<<15))!=0;
                    // 13th-gen MMIO bits 62:48 are reserved. Clamp and the
                    // shared PL1/PL2 time window are only in low dword 59A0.
                    if(first)clamp=(raw&(1u<<16))!=0;
                    if(out.unitsRaw){const auto powerExp=static_cast<int>(*out.unitsRaw&0xf),timeExp=static_cast<int>((*out.unitsRaw>>16)&0xf);
                        watts=std::ldexp(double(raw&0x7fff),-powerExp);if(first)seconds=window(raw,timeExp);}
                };
                try{decodeMmio(mchbar.intelPackageMmioLimit1(),true,out.mmioLimit1Raw,out.mmioLimit1Enabled,out.mmioLimit1Clamp,out.mmioLimit1Watts,out.mmioWindow1Seconds);}catch(const std::exception& e){addError("MMIO 59A0",e);}
                try{decodeMmio(mchbar.intelPackageMmioLimit2(),false,out.mmioLimit2Raw,out.mmioLimit2Enabled,out.mmioLimit2Clamp,out.mmioLimit2Watts,out.mmioWindow2Seconds);}catch(const std::exception& e){addError("MMIO 59A4",e);}
                out.mmioSampledAtMs=GetTickCount64();
            }catch(const std::exception& e){addError("MMIO 59A0/59A4",e);mchbar.reset();}
            ReleaseMutex(pciMutex);
        }else{if(!out.status.empty())out.status+="; ";out.status+="MMIO: PCI mutex busy or unavailable";}
        if(out.status.empty())out.status="ok; MSR and MMIO sampled separately; physical limit may differ";
        lastPowerDiagnostic=out;sample.intelPackagePower=std::move(out);
    }
    void readMemoryClock(MonitoringSnapshot& sample){
        if(!memorySupported||(!sample.ordinaryDdr5&&!sample.ordinaryDdr4))return;
        const auto now=GetTickCount64();if(now<memoryRetry)return;
        sample.memoryClockSource="PawnIO · Intel MCHBAR QCLK";
        sample.memoryClockStatus="approximate: BIOS Gear and nominal BCLK; hardware validation pending";
        if(!pciMutex)pciMutex=openSharedHardwareMutex(kPciMutexName);
        if(!pciMutex){sample.memoryClockStatus="PCI mutex unavailable";return;}
        const auto acquired=WaitForSingleObject(pciMutex,250);
        if(acquired!=WAIT_OBJECT_0&&acquired!=WAIT_ABANDONED){sample.memoryClockStatus="PCI mutex busy or unavailable";return;}
        try{
            if(!mchbar.ready())mchbar.open(L"IntelMCHBAR.bin","3f82b832d99b4aac37d2a20fdb7c9baa2a3bc0488612c9019c9484eb0e8a6eae");
            for(int attempt=0;attempt<3;++attempt){
                const auto before=mchbar.intelMemoryStatus(),bios=mchbar.intelMemoryBiosData(),after=mchbar.intelMemoryStatus();
                const auto decoded=intelMemoryClock(before,bios,after);
                if(decoded.status==IntelMemoryClockStatus::Transition)continue;
                if(decoded.status!=IntelMemoryClockStatus::Valid)break;
                const double ddrClock=decoded.mhz;
                sample.memoryClockMhz=ddrClock;sample.memoryClockSampledAtMs=GetTickCount64();
                sample.memoryClockStatus="approximate: current QCLK, last BIOS-requested Gear, nominal BCLK; not hardware validated";
                if(sample.diagnostic)sample.additionalSensors.push_back({"Intel memory controller","frequency","MHz",sample.memoryClockSource,sample.memoryClockStatus,ddrClock,double(after),sample.memoryClockSampledAtMs});
                break;
            }
        }catch(const std::exception& error){sample.memoryClockStatus=error.what();mchbar.reset();memoryRetry=now+30000;}
        ReleaseMutex(pciMutex);
    }
};
IntelTelemetryReader::IntelTelemetryReader(bool powerDiagnostics):impl_(std::make_unique<Impl>(powerDiagnostics)){}
IntelTelemetryReader::~IntelTelemetryReader()=default;
void IntelTelemetryReader::read(MonitoringSnapshot& sample){impl_->read(sample);}
}
