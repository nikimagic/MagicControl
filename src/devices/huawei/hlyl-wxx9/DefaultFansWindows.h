#pragma once
#include "I2EcWindows.h"
#include "../../../protocols/huawei/NativeDefaultFans.h"
#include <chrono>
namespace mc::windows {
// Same HLYL I2EC session/admission as the existing fixed FAN observation.
// This function has no setter, flash access, mode change or persistence.
inline NativeDefaultFanReading readHlylDefaultFans(HlylI2EcSession& io){
    using namespace huawei::defaultFans;
    const auto word=[&](std::uint16_t address){
        const int high=io.read(address),low=io.read(static_cast<std::uint16_t>(address+1));
        if(high!=io.read(address))throw std::runtime_error("Honor: torn RPM observation");
        return high*256+low;
    };
    // 06A6/06A9 are compiler scratch pointers during the thermal routine,
    // not persistent configuration. Never use them as an idle-time identity.
    const auto context=[&]{return std::array<int,5>{io.read(0x42c)&7,io.read(0x315)&0x40,
        io.read(0x536),io.read(0x400),io.read(0x1801)};};
    const auto before=context();
    if(before[0]||before[3]!=0x10||before[4]!=200)
        return {NativeDefaultFanObservation::Inactive,{}};
    Snapshot s;s.flags=before[0];s.mode=before[1]?1:0;s.stage=before[2];
    for(int i=0;i<8;++i){s.temperatures[i]=io.read(inputs[i].address);s.stages[i]=io.read(inputs[i].stage);}
    s.requestedRpm={word(0x534),word(0x53a)};
    s.actualRpm={word(0x3cc),word(0x3ce)};s.actualDuty={io.read(0x1808),io.read(0x1809)};
    for(int ch=0;ch<2;++ch)if(s.actualDuty[ch]>200)throw std::runtime_error("Honor: invalid actual PWM");
    if(context()!=before)throw std::runtime_error("Honor: Default mode/stage changed during observation");
    for(int i=0;i<8;++i)if(s.stages[i]!=io.read(inputs[i].stage))
        throw std::runtime_error("Honor: thermal demand changed during observation");
    const auto now=std::chrono::steady_clock::now().time_since_epoch();
    return {NativeDefaultFanObservation::Available,project(s,std::chrono::duration_cast<std::chrono::milliseconds>(now).count())};
}
}
