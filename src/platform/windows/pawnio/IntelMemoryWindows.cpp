#include "IntelMemoryWindows.h"
#include "IntelTelemetryWindows.h"
#include "SensorModuleWindows.h"
#include <format>
#include <vector>
namespace mc::windows {
struct IntelMemoryReader::Impl {
    // Memory telemetry belongs to the hardware, not the laptop profile.
    // Verify the Intel SMBus and SPD5118 at runtime on known and unknown PCs.
    const bool allowed=!wcsstr(GetCommandLineW(),L"--no-ec") &&
        nativeCpuVendor()==ProcessorVendor::Intel;
    detail::Module module;
    HANDLE mutex{};ULONGLONG next{},discoverAt{};
    bool ddr4Discovery{};
    std::vector<std::uint8_t> addresses;
    std::array<DeviceTemperature,8> cache{};
    ULONGLONG sampledAt{};
    ~Impl(){if(mutex)CloseHandle(mutex);}
    void read(MonitoringSnapshot& sample){
        if(!allowed){if(sample.diagnostic)sample.additionalSensors.push_back({"RAM","temperature","C","PawnIO · Intel I801","disabled by --no-ec or not an Intel CPU",{},{},GetTickCount64()});return;}
        const auto now=GetTickCount64();
        if(now>=next||sample.diagnostic){
            next=now+5000;
            if(!mutex)mutex=CreateMutexW(nullptr,FALSE,L"Global\\Access_SMBUS.HTP.Method");
            const auto acquired=mutex?WaitForSingleObject(mutex,250):WAIT_FAILED;
            if(acquired!=WAIT_OBJECT_0&&acquired!=WAIT_ABANDONED){
                for(auto& d:sample.memory)d.status="Intel I801: SMBus занят/недоступен";
                if(sample.diagnostic)sample.additionalSensors.push_back({"RAM","temperature","C","PawnIO · Intel I801","SMBus занят/недоступен",{},{},now});
                return;
            }
            try{
                if(!module.ready()){
                    module.open(L"SmbusI801.bin",moduleHash);module.verifyIntelSmbus();discoverAt=0;
                }
                if(ddr4Discovery!=sample.ordinaryDdr4){discoverAt=0;ddr4Discovery=sample.ordinaryDdr4;}
                if(now>=discoverAt){
                    addresses.clear();discoverAt=now+60000;
                    for(std::uint8_t a=0x50;a<=0x57&&addresses.size()<(sample.diagnostic?8u:2u);++a)
                        try{if(sample.ordinaryDdr4?(module.ddr4SpdThermal(a)&&module.ddr4TsodTemperature(a).has_value()):module.spd5118(a))addresses.push_back(a);}catch(const std::exception&){}
                    if(addresses.empty())discoverAt=now+10000;
                }
                cache={};sampledAt=now;
                for(auto& d:cache){d.source=sample.ordinaryDdr4?"PawnIO · Intel I801 / JC42":"PawnIO · Intel I801 / SPD5118";d.status="Датчик не найден или недоступен";}
                for(std::size_t i=0;i<addresses.size();++i){
                    auto& d=cache[i];d.present=true;
                    // No guessed pairing between SMBIOS locator and SMBus address.
                    // Reading SPD serials would require changing the NVM page;
                    // this monitor makes no such writes and labels the real hub.
                    d.model=sample.ordinaryDdr4?std::format("DDR4 · SPD 0x{:02X}",addresses[i]):std::format("DDR5 · SMBus 0x{:02X}",addresses[i]);
                    d.source=sample.ordinaryDdr4?"PawnIO · Intel I801 / JC42":"PawnIO · Intel I801 / SPD5118";
                    try{d.celsius=sample.ordinaryDdr4?module.ddr4TsodTemperature(addresses[i]):module.temperature(addresses[i]);d.status=d.celsius?"ok":"Датчик выключен или показание недоступно";}
                    catch(const std::exception& e){d.status=e.what();discoverAt=0;}
                }
            }catch(const std::exception& e){module.reset();cache={};sampledAt=now;next=now+30000;for(auto& d:cache){d.source=sample.ordinaryDdr4?"PawnIO · Intel I801 / JC42":"PawnIO · Intel I801 / SPD5118";d.status=e.what();}}
            ReleaseMutex(mutex);
        }
        for(std::size_t i=0;i<cache.size();++i){
            if(i<sample.memory.size()){
                if(cache[i].present==true)sample.memory[i]=cache[i];
                else sample.memory[i].status=cache[i].status;
            }
            if(sample.diagnostic&&(cache[i].present==true||i<2))sample.additionalSensors.push_back({cache[i].model.empty()?"RAM hub "+std::to_string(i):cache[i].model,"temperature","C",cache[i].source,cache[i].status,cache[i].celsius,{},sampledAt});
        }
    }
    static constexpr auto moduleHash="a0f7d066e7efda28c0e754c1f52dbd8dc280d388ba8575b514a80cb67490530c";
};
IntelMemoryReader::IntelMemoryReader():impl_(std::make_unique<Impl>()){}
IntelMemoryReader::~IntelMemoryReader()=default;
void IntelMemoryReader::read(MonitoringSnapshot& sample){impl_->read(sample);}
}
