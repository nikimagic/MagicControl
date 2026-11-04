#include "PawnTelemetryWindows.h"
#include "IntelTelemetryWindows.h"
#include "HardwareMutexWindows.h"
#include "SensorModuleWindows.h"
#include "MagicControl/PortableSettings.h"
#include "MagicControl/Temperature.h"
#include "../../../devices/minisforum/hpbsd/TelemetryWindows.h"
#include <windows.h>
#include <intrin.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <stdexcept>
#include <vector>

namespace mc::windows {
bool pawnAmdCpuSupported() noexcept {
    if(nativeCpuVendor()!=ProcessorVendor::Amd)return false;
    int registers[4]{};__cpuid(registers,1);
    const auto eax=static_cast<unsigned>(registers[0]);
    const unsigned base=(eax>>8)&0xf, family=base==0xf?base+((eax>>20)&0xff):base;
    // Match the bundled AMDFamily17 module's own CPU-family admission.
    return family>=0x17&&family<=0x1a;
}
namespace {
// The same signed modules and operations as PawnIoSensorReader.cs. The public
// adapter exposes only two SPD hubs, read-only energy MSRs and CPU Tctl SMN.
using Module = detail::Module;
struct ProcessorCoreLocation { WORD group{}; KAFFINITY mask{}; };
struct CoreEnergySample { std::uint32_t energy{}; ULONGLONG at{}; bool ready{}; };
std::vector<ProcessorCoreLocation> processorCores() {
    DWORD bytes{};
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &bytes);
    if (!bytes || bytes > 4 * 1024 * 1024)
        throw std::runtime_error("PawnIO: топология ядер CPU недоступна");
    std::vector<unsigned char> data(bytes);
    if (!GetLogicalProcessorInformationEx(RelationProcessorCore,
            reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(data.data()), &bytes))
        throw std::runtime_error("PawnIO: не удалось перечислить ядра CPU");
    std::vector<ProcessorCoreLocation> result;
    for (std::size_t offset = 0; offset + sizeof(DWORD) * 2 <= bytes;) {
        const auto* entry = reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(data.data() + offset);
        if (!entry->Size || offset + entry->Size > bytes) break;
        if (entry->Relationship == RelationProcessorCore && entry->Processor.GroupCount) {
            const auto mask = entry->Processor.GroupMask[0];
            if (mask.Mask) result.push_back({mask.Group, mask.Mask & (~mask.Mask + 1)});
        }
        offset += entry->Size;
    }
    if (result.empty()) throw std::runtime_error("PawnIO: список физических ядер пуст");
    return result;
}
class CoreAffinity {
    GROUP_AFFINITY previous_{};
public:
    explicit CoreAffinity(const ProcessorCoreLocation& core) {
        GROUP_AFFINITY target{};
        target.Group = core.group;
        target.Mask = core.mask;
        if (!SetThreadGroupAffinity(GetCurrentThread(), &target, &previous_))
            throw std::runtime_error("PawnIO: не удалось выбрать ядро CPU");
    }
    ~CoreAffinity() { SetThreadGroupAffinity(GetCurrentThread(), &previous_, nullptr); }
    CoreAffinity(const CoreAffinity&) = delete;
    CoreAffinity& operator=(const CoreAffinity&) = delete;
};
}
struct PawnTelemetryReader::Impl {
    bool boardVerified;
    bool smbusAllowed,legacyMemoryOrder;
    bool hpbsdPagedSpd{},smbusPoisoned{},ddr4Discovery{};
    Module smbus, cpu;
    HANDLE busMutex{}, pciMutex{};
    std::optional<std::uint32_t> previousEnergy;
    std::optional<double> cachedApuPackagePowerWatts;
    ULONGLONG packageSampleAt{};
    std::vector<ProcessorCoreLocation> coreTopology;
    std::vector<CoreEnergySample> previousCoreEnergy;
    // Discover JEDEC hubs by identity on every AMD device, not by model name.
    // HPBSD keeps its separately verified page-recovery path unchanged.
    std::vector<std::uint8_t> genericSpdAddresses;
    ULONGLONG nextSpdDiscovery{};
    ULONGLONG previousTime{}, retryCpu{}, retrySmbus{}, retryTemperature{};
    double joulesPerTick{};
    Impl(bool verified,bool smbus,bool memoryOrder) : boardVerified(verified),smbusAllowed(smbus),legacyMemoryOrder(memoryOrder),
        hpbsdPagedSpd(smbus&&!wcsstr(GetCommandLineW(),L"--no-ec")&&HpbsdTelemetryReader::supportedHost()) {}
    ~Impl() { if (busMutex) CloseHandle(busMutex); if (pciMutex) CloseHandle(pciMutex); }
    bool prepareCpu(MonitoringSnapshot& sample) {
        const auto now = GetTickCount64();
        if (!boardVerified || now < retryCpu) return false;
        try {
            if (!cpu.ready()) {
                cpu.open(L"AMDFamily17.bin");
                joulesPerTick = std::pow(0.5, static_cast<int>((cpu.energyUnits() >> 8) & 31));
                previousEnergy.reset();
                for (auto& previous : previousCoreEnergy) previous.ready = false;
            }
            return true;
        } catch (const std::exception& error) {
            cpu.reset(); previousEnergy.reset(); retryCpu = now + 30000;
            cachedApuPackagePowerWatts.reset();packageSampleAt=0;
            sample.processorStatus += std::string(" ") + error.what();
            return false;
        }
    }
    // Called under Access_PCI by the existing telemetry poll. Both channels
    // must confirm one stable common clock; SPD order is never treated as a
    // channel mapping. Exact CPU/type admission excludes LPDDR and old layouts.
    void readMemoryClock(MonitoringSnapshot& sample){
        if(!sample.ordinaryDdr5&&!sample.ordinaryDdr4)return;
        int registers[4]{};__cpuid(registers,1);const auto eax=static_cast<unsigned>(registers[0]);
        const unsigned base=(eax>>8)&15,family=base==15?base+((eax>>20)&255):base;
        const unsigned model=((eax>>4)&15)|((eax>>12)&240);
        const bool strix=family==0x1a&&(model==0x20||model==0x24);
        const bool phoenix=family==0x19&&(model==0x74||model==0x75);
        const bool legacyDdr4=(family==0x17&&(model==0x60||model==0x71))||(family==0x19&&(model==0x21||model==0x50));
        if(sample.ordinaryDdr5?!(strix||phoenix):!legacyDdr4)return;
        sample.memoryClockSource=sample.ordinaryDdr5?"PawnIO · UMC0/1 · active DDR5 P-state":"PawnIO · UMC0/1 · DDR4 P0";
        sample.memoryClockStatus="UMC transition or unavailable reading";
        try{
            for(int attempt=0;attempt<3;++attempt){
                std::array<std::uint32_t,2> before{},config{},after{};
                bool known=true;
                for(unsigned ch=0;ch<2;++ch){before[ch]=cpu.memoryStateRegister(ch);if((before[ch]&7)>3)known=false;}
                // DDR4's P-state pages beyond P0 are not corroborated yet.
                if(sample.ordinaryDdr4&&((before[0]&7)||(before[1]&7)))known=false;
                if(!known)continue;
                for(unsigned ch=0;ch<2;++ch)config[ch]=cpu.memoryClockRegister(ch,before[ch]&7);
                for(unsigned ch=0;ch<2;++ch)after[ch]=cpu.memoryStateRegister(ch);
                const auto first=zenMemoryChannelsClock(sample.ordinaryDdr5,before,config,after);
                if(!first)continue;
                sample.memoryClockMhz=*first;sample.memoryClockSampledAtMs=GetTickCount64();
                sample.memoryPstate={before[0]&7,before[1]&7};
                sample.memoryClockStatus=strix?"active nominal clock; no measured BCLK correction":"approximate: nominal BCLK; UMC interpretation not hardware validated";
                if(sample.diagnostic)for(unsigned ch=0;ch<2;++ch)
                    sample.additionalSensors.push_back({"RAM UMC"+std::to_string(ch),"frequency","MHz",sample.memoryClockSource,
                        "P"+std::to_string(before[ch]&7)+"; "+sample.memoryClockStatus,double(*first),double(config[ch]),sample.memoryClockSampledAtMs});
                return;
            }
        }catch(const std::exception& error){sample.memoryClockStatus=error.what();}
    }
    void readTemperatures(MonitoringSnapshot& sample) {
        if (!prepareCpu(sample)) return;
        if (!pciMutex) pciMutex = openSharedHardwareMutex(kPciMutexName);
        if (!pciMutex) {sample.processorStatus+=" PawnIO: mutex PCI недоступен, Win32="+std::to_string(GetLastError());return;}
        const DWORD acquired = WaitForSingleObject(pciMutex, 250);
        if (acquired != WAIT_OBJECT_0 && acquired != WAIT_ABANDONED) {
            if(acquired!=WAIT_TIMEOUT){sample.processorStatus+=" PawnIO: ошибка ожидания mutex PCI, Win32="+std::to_string(GetLastError());return;}
            sample.processorStatus += " PawnIO Tctl: занят доступ PCI; используется AMD ADL";
            return;
        }
        if(GetTickCount64()>=retryTemperature)try {
            const auto celsius = decodeZenTctl(cpu.temperatureRegister());
            if (!celsius) throw std::runtime_error("PawnIO Tctl: недействительный SMN 0x59800");
            applyZenCpuTemperature(sample, celsius);
        } catch (const std::exception& error) {
            retryTemperature = GetTickCount64() + 30000;
            sample.processorStatus += std::string(" ") + error.what();
        }
        readMemoryClock(sample);
        ReleaseMutex(pciMutex);
    }
    void readCorePower(MonitoringSnapshot& sample) {
        try {
            if (coreTopology.empty()) {
                coreTopology = processorCores();
                previousCoreEnergy.assign(coreTopology.size(), {});
            }
            if (previousCoreEnergy.size() != coreTopology.size())
                previousCoreEnergy.assign(coreTopology.size(), {});
            double totalWatts{};
            bool complete = true;
            for (std::size_t i = 0; i < coreTopology.size(); ++i) {
                try {
                    CoreAffinity affinity(coreTopology[i]);
                    const auto energy = cpu.coreEnergyCounter();
                    const auto at = GetTickCount64();
                    auto& previous = previousCoreEnergy[i];
                    if (previous.ready && at > previous.at) {
                        const double seconds = (at - previous.at) / 1000.0;
                        const auto delta = static_cast<std::uint32_t>(energy - previous.energy);
                        const double watts = delta * joulesPerTick / seconds;
                        if (seconds > .05 && seconds < 15 && std::isfinite(watts) && watts >= 0 && watts < 250)
                            totalWatts += watts;
                        else complete = false;
                    } else complete = false;
                    previous = {energy, at, true};
                } catch (const std::exception& error) {
                    previousCoreEnergy[i].ready = false;
                    complete = false;
                    sample.processorStatus += std::string(" PawnIO core power: ") + error.what();
                }
            }
            if (complete && std::isfinite(totalWatts) && totalWatts < 250) {
                sample.processors[0].powerWatts = totalWatts;
                sample.processors[0].powerSource = "PawnIO / CPU core energy";
            }
        } catch (const std::exception& error) {
            coreTopology.clear();
            previousCoreEnergy.clear();
            sample.processorStatus += std::string(" PawnIO core power: ") + error.what();
        }
    }
    void read(MonitoringSnapshot& sample) {
        // A cached MSR delta must not survive a failed provider or overwrite
        // the provenance of a different CPU-power source in this snapshot.
        sample.apuPackagePowerWatts.reset();
        sample.apuPackagePowerSource.clear();
        if(smbusAllowed){sample.memory[0].source = "PawnIO · SMBus 0x50";sample.memory[1].source = "PawnIO · SMBus 0x51";}
        const auto now = GetTickCount64();
        if (boardVerified) readTemperatures(sample);
        else sample.processorStatus += " PawnIO CPU: плата не подтверждена";
        if (boardVerified && prepareCpu(sample)) {
            try {
                const auto energy = cpu.energyCounter();
                const auto at = GetTickCount64();
                if (previousEnergy && at > previousTime) {
                    const double seconds = (at - previousTime) / 1000.0;
                    const auto delta = static_cast<std::uint32_t>(energy - *previousEnergy);
                    const double watts = delta * joulesPerTick / seconds;
                    if (seconds > .05 && seconds < 15 && std::isfinite(watts) && watts >= 0 && watts < 250)
                        {cachedApuPackagePowerWatts = watts;packageSampleAt=at;}
                    else if(seconds>=15||!std::isfinite(watts)||watts<0||watts>=250){cachedApuPackagePowerWatts.reset();packageSampleAt=0;}
                }
                if(cachedApuPackagePowerWatts&&packageSampleAt&&at-packageSampleAt<=5000&&(!sample.diagnostic||packageSampleAt==at)){
                    sample.apuPackagePowerWatts=cachedApuPackagePowerWatts;
                    sample.apuPackagePowerSource="PawnIO / package MSR";
                }
                previousEnergy = energy; previousTime = at;
                readCorePower(sample);
            } catch (const std::exception& error) {
                cpu.reset(); previousEnergy.reset();
                cachedApuPackagePowerWatts.reset();packageSampleAt=0;
                sample.apuPackagePowerWatts.reset();sample.apuPackagePowerSource.clear();
                for (auto& previous : previousCoreEnergy) previous.ready = false;
                retryCpu = now + 30000;
                sample.processorStatus += std::string(" ") + error.what();
            }
        }
        if (!smbusAllowed || smbusPoisoned || now < retrySmbus) return;
        if (!busMutex) busMutex = CreateMutexW(nullptr, FALSE, L"Global\\Access_SMBUS.HTP.Method");
        if (!busMutex) {if(sample.diagnostic)sample.additionalSensors.push_back({"RAM","temperature","C","PawnIO · AMD SMBus / SPD5118","SMBus mutex unavailable",{},{},now});return;}
        const DWORD acquired = WaitForSingleObject(busMutex, 250);
        if (acquired != WAIT_OBJECT_0 && acquired != WAIT_ABANDONED) {if(sample.diagnostic)sample.additionalSensors.push_back({"RAM","temperature","C","PawnIO · AMD SMBus / SPD5118","SMBus busy/unavailable",{},{},now});return;}
        try {
            if (!smbus.ready()) { smbus.open(L"SmbusPIIX4.bin",hpbsdPagedSpd?"91f9b4b1c39e3d399ce48477a89d8f6bd3e58a2241064a4daec3a1513dff56e5":""); smbus.verifySmbus();nextSpdDiscovery=0; }
            smbus.verifyPort();
            const bool useHpbsd=hpbsdPagedSpd&&!sample.ordinaryDdr4;
            if(ddr4Discovery!=sample.ordinaryDdr4){nextSpdDiscovery=0;ddr4Discovery=sample.ordinaryDdr4;}
            if(!useHpbsd&&now>=nextSpdDiscovery){
                genericSpdAddresses.clear();nextSpdDiscovery=now+60000;
                for(std::uint8_t address=0x50;address<=0x57&&genericSpdAddresses.size()<2;++address)
                    try{if(sample.ordinaryDdr4?(smbus.ddr4SpdThermal(address)&&smbus.ddr4TsodTemperature(address).has_value()):smbus.spd5118(address))genericSpdAddresses.push_back(address);}catch(const std::exception&){}
                if(genericSpdAddresses.empty())nextSpdDiscovery=now+10000;
            }
            for (int i = 0; i < 2; ++i) {
                if(!useHpbsd&&static_cast<std::size_t>(i)>=genericSpdAddresses.size()){
                    sample.memory[i].source=sample.ordinaryDdr4?"PawnIO · AMD SMBus / JC42":"PawnIO · AMD SMBus / SPD5118";sample.memory[i].status="Датчик не найден или недоступен";
                    if(sample.diagnostic)sample.additionalSensors.push_back({"RAM hub "+std::to_string(i),"temperature","C",sample.memory[i].source,sample.memory[i].status,{},{},GetTickCount64()});
                    continue;
                }
                const auto address=useHpbsd?static_cast<std::uint8_t>(0x50+i):genericSpdAddresses[i];
                try { sample.memory[i].celsius = useHpbsd?smbus.hpbsdTemperature(address):sample.ordinaryDdr4?smbus.ddr4TsodTemperature(address):smbus.temperature(address);
                    if(!useHpbsd){sample.memory[i].present=true;if(sample.ordinaryDdr4||!legacyMemoryOrder||address!=0x50+i)sample.memory[i].serial.clear();sample.memory[i].model=sample.ordinaryDdr4?std::format("DDR4 · SPD 0x{:02X}",address):std::format("DDR5 · SMBus 0x{:02X}",address);}
                    sample.memory[i].source=sample.ordinaryDdr4?std::format("PawnIO · JC42 · SPD 0x{:02X}",address):std::format("PawnIO · SPD5118 · SMBus 0x{:02X}",address);
                    sample.memory[i].status=sample.memory[i].celsius?"ok":"Датчик выключен или показание недоступно";
                    if(sample.diagnostic)sample.additionalSensors.push_back({"RAM SMBus "+std::to_string(address),"temperature","C",sample.memory[i].source,sample.memory[i].celsius?"ok":"not reported",sample.memory[i].celsius,{},GetTickCount64()}); }
                catch (const spd5118::RestoreError&) {smbusPoisoned=true;throw;}
                catch (const std::exception& error) { sample.memory[i].status=error.what();if(!useHpbsd)nextSpdDiscovery=0;sample.processorStatus += std::string(" ") + error.what();
                    if(sample.diagnostic)sample.additionalSensors.push_back({"RAM SMBus "+std::to_string(address),"temperature","C",sample.memory[i].source,error.what(),{},{},GetTickCount64()}); }
            }
        } catch (const std::exception& error) {
            smbus.reset(); nextSpdDiscovery=0;retrySmbus = now + 30000; sample.processorStatus += std::string(" ") + error.what();
            for(auto& memory:sample.memory)memory.status=error.what();
            if(sample.diagnostic)sample.additionalSensors.push_back({"RAM","temperature","C","PawnIO · AMD SMBus / SPD5118",error.what(),{},{},now});
        }
        ReleaseMutex(busMutex);
    }
};
PawnTelemetryReader::PawnTelemetryReader(bool boardVerified) : PawnTelemetryReader(boardVerified,boardVerified,boardVerified) {}
PawnTelemetryReader::PawnTelemetryReader(bool cpuAllowed,bool smbusAllowed,bool legacyMemoryOrder) : impl_(std::make_unique<Impl>(cpuAllowed,smbusAllowed,legacyMemoryOrder)) {}
PawnTelemetryReader::~PawnTelemetryReader() = default;
void PawnTelemetryReader::read(MonitoringSnapshot& sample) { impl_->read(sample); }
void PawnTelemetryReader::readTemperatures(MonitoringSnapshot& sample) { impl_->readTemperatures(sample); }
}
