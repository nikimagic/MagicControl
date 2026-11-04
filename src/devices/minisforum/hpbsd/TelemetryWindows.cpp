#include "TelemetryWindows.h"
#include "I2EcWindows.h"
#include "MagicControl/BoardIdentity.h"
#include "MagicControl/DeviceProfile.h"
#include <windows.h>
#include <intrin.h>
#include <array>
#include <format>
#include <mutex>
#include <stdexcept>

namespace mc::windows {
namespace {
bool hostMatches(){
    const auto b=readBoardDescriptor();
    if(b.manufacturer!="Micro Computer (HK) Tech Limited"||b.baseBoard!="HPBSD"||b.productName!="EliteMini Series")return false;
    wchar_t bios[64]{};DWORD bytes=sizeof(bios);
    if(RegGetValueW(HKEY_LOCAL_MACHINE,L"HARDWARE\\DESCRIPTION\\System\\BIOS",L"BIOSVersion",RRF_RT_REG_SZ,nullptr,bios,&bytes)!=ERROR_SUCCESS||std::wstring_view(bios)!=L"1.06")return false;
    int cpu[4]{};__cpuid(cpu,0);
    if(cpu[1]!=0x68747541||cpu[3]!=0x69746e65||cpu[2]!=0x444d4163)return false;
    __cpuid(cpu,1);
    return (((cpu[0]>>8)&15)+((cpu[0]>>20)&255))==0x19&&(((cpu[0]>>4)&15)|((cpu[0]>>12)&240))==0x75;
}
}
struct HpbsdTelemetryReader::Impl {
    std::mutex gate;
    ULONGLONG retryAt{};
    std::string status;
    bool poisoned{};
    std::optional<double> rpm(HpbsdI2EcSession& io,std::uint16_t address){
        for(int attempt=0;attempt<4;++attempt){const auto lo=io.read(address),hi=io.read(address+1),check=io.read(address);
            if(lo!=check)continue;
            const unsigned counter=lo|(unsigned(hi)<<8);
            if(!counter)return 0.;
            const double value=2156250./counter;
            if(value<=20000)return value;
            return {};
        }return {};
    }
    void read(MonitoringSnapshot& sample){
        std::lock_guard lock(gate);
        if(!hostMatches())return;
        const auto now=GetTickCount64();
        if(poisoned||now<retryAt){sample.processorStatus+=" "+status;return;}
        try{
            HpbsdI2EcSession io;
            const auto cpuRpm=rpm(io,0x181e),systemRpm=rpm(io,0x1820);
            const auto cpuTemperature=io.read(0x0309),systemTemperature=io.read(0x0305);
            const auto cpuDuty=io.read(0x1803),systemDuty=io.read(0x1804),period=io.read(0x1841);
            io.finish();
            sample.processors[0].fanRpm=cpuRpm;
            sample.processors[0].fanSource="EC · CPU fan";
            sample.processors[1].fanRpm=systemRpm;
            sample.processors[1].fanSource="EC · System fan";
            sample.processors[0].fanPwm.reset();sample.processors[1].fanPwm.reset();
            if(period==127&&cpuDuty<=period&&systemDuty<=period){sample.processors[0].fanPwm=100.*cpuDuty/period;sample.processors[1].fanPwm=100.*systemDuty/period;}
            sample.additionalSensors.push_back({"HPBSD CPU PWM","fan_pwm","percent","ITE5571 duty0x1803 / cycle0x1841",sample.processors[0].fanPwm?"ok":"invalid",sample.processors[0].fanPwm,double(cpuDuty),now});
            sample.additionalSensors.push_back({"HPBSD System PWM","fan_pwm","percent","ITE5571 duty0x1804 / cycle0x1841",sample.processors[1].fanPwm?"ok":"invalid",sample.processors[1].fanPwm,double(systemDuty),now});
            // Keep CPU Tctl and GPU Edge ownership. System EC temperature is a
            // distinct board sensor, never a guessed GPU/DIMM temperature.
            if(cpuTemperature>0&&cpuTemperature<=120&&!sample.processors[0].celsius){sample.processors[0].celsius=cpuTemperature;sample.processors[0].temperatureSource="HPBSD ITE5571 · EC CPU";}
            sample.additionalSensors.push_back({"HPBSD CPU fan","fan_speed","RPM",sample.processors[0].fanSource,cpuRpm?"ok":"unstable/invalid counter",cpuRpm,{},now});
            sample.additionalSensors.push_back({"HPBSD system fan","fan_speed","RPM",sample.processors[1].fanSource,systemRpm?"ok":"unstable/invalid counter",systemRpm,{},now});
            sample.additionalSensors.push_back({"HPBSD CPU","temperature","C","ITE5571 XRAM0x0309",cpuTemperature>0&&cpuTemperature<=120?"ok":"invalid",cpuTemperature>0&&cpuTemperature<=120?std::optional<double>(cpuTemperature):std::nullopt,double(cpuTemperature),now});
            sample.additionalSensors.push_back({"HPBSD system","temperature","C","ITE5571 XRAM0x0305",systemTemperature>0&&systemTemperature<=120?"ok":"invalid",systemTemperature>0&&systemTemperature<=120?std::optional<double>(systemTemperature):std::nullopt,double(systemTemperature),now});
            sample.processorStatus+=" HPBSD ITE5571: telemetry verified";
            status.clear();
        }catch(const std::exception& error){status=error.what();retryAt=GetTickCount64()+30000;sample.processorStatus+=" "+status;
            sample.additionalSensors.push_back({"HPBSD ITE5571","status","","PawnIO LpcIO",status,{},{},now});}
    }
};
HpbsdTelemetryReader::HpbsdTelemetryReader():impl_(std::make_unique<Impl>()){}
HpbsdTelemetryReader::~HpbsdTelemetryReader()=default;
bool HpbsdTelemetryReader::supportedHost(){return hostMatches();}
void HpbsdTelemetryReader::read(MonitoringSnapshot& sample){impl_->read(sample);}
}
