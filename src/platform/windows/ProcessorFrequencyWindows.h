#pragma once
#include "MagicControl/Monitoring.h"
#include <windows.h>
#include <powrprof.h>
#include <vector>
namespace mc::windows {
inline void readWindowsProcessorFrequency(ProcessorReading& cpu){
    if(cpu.clockMhz)return;
    struct ProcessorPower {ULONG number,maxMhz,currentMhz,mhzLimit,maxIdleState,currentIdleState;};
    static_assert(sizeof(ProcessorPower)==24);
    SYSTEM_INFO info{};GetSystemInfo(&info);
    if(!info.dwNumberOfProcessors||info.dwNumberOfProcessors>1024)return;
    std::vector<ProcessorPower> power(info.dwNumberOfProcessors);
    if(CallNtPowerInformation(ProcessorInformation,nullptr,0,power.data(),static_cast<ULONG>(power.size()*sizeof(power[0])))!=0)return;
    double sum{};unsigned count{};
    for(const auto& row:power)if(row.currentMhz>0&&row.currentMhz<20000){sum+=row.currentMhz;++count;}
    if(count){cpu.clockMhz=sum/count;cpu.clockSource="Windows · ProcessorInformation";}
}
}
