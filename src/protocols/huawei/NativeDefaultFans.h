#pragma once
// Honor HLYL-WXX9 BIOS1.09 embedded IT8987 image SHA256
// d4622371275d6f409d50a2ab6390e0ca45e1bd585828041bcba385dc7d9ab819.
// Decoded from the owner ROM; this is a read-only reference, never a writer.
// Evidence: research/honor-fan-curves-20261101. Offsets below are EC RAM
// except the compiled code pointers. No Uniwill/HPBSD addresses or policies.
#include "MagicControl/NativeDefaultFan.h"
#include <algorithm>
#include <array>
#include <stdexcept>
#include <string_view>
namespace mc::huawei::defaultFans {
struct Level {int up,down;std::array<int,2> rpm;};
struct Input {std::uint16_t address,previous,stage,counter;int dwell;std::string_view name;std::array<std::uint16_t,2> tables;std::array<std::array<Level,10>,2> modes;};
inline constexpr std::array<Input,8> inputs{{
    {0x320,0x520,0x510,0x508,8,"EC CPU",{0x6A9B,0x6CED},{{
        {{{1,0,{0,0}},{82,75,{0,2300}},{82,75,{3000,2400}},{82,75,{3500,2700}},{82,75,{3800,3100}},{82,75,{4600,3600}},{82,75,{5200,3800}},{82,75,{5800,4200}},{128,125,{6200,4600}},{128,127,{6700,5000}}}},
        {{{1,0,{0,0}},{85,80,{0,2300}},{85,80,{3000,2400}},{85,80,{3500,2700}},{85,80,{3800,3100}},{85,80,{4600,3600}},{85,80,{5200,3800}},{85,80,{5800,4200}},{85,80,{6200,4600}},{128,127,{6700,5000}}}},
    }}},
    {0x322,0x523,0x513,0x50B,5,"EC 0x322",{0x6AD7,0x6D29},{{
        {{{1,0,{0,0}},{126,125,{0,2300}},{126,125,{3000,2400}},{126,125,{3500,2700}},{126,125,{3800,3100}},{126,125,{4600,3600}},{126,125,{5200,3800}},{126,125,{5800,4200}},{128,127,{6200,4600}},{128,127,{6700,5000}}}},
        {{{1,0,{0,0}},{126,125,{0,2300}},{126,125,{3000,2400}},{126,125,{3500,2700}},{126,125,{3800,3100}},{126,125,{4600,3600}},{126,125,{5200,3800}},{126,125,{5800,4200}},{128,127,{6200,4600}},{128,127,{6700,5000}}}},
    }}},
    {0x323,0x521,0x511,0x509,5,"EC TSLO",{0x6B13,0x6D65},{{
        {{{1,0,{0,0}},{43,38,{0,2300}},{45,40,{3000,2400}},{47,42,{3500,2700}},{51,44,{3800,3100}},{54,47,{4600,3600}},{56,50,{5200,3800}},{58,52,{5800,4200}},{128,127,{6200,4600}},{128,127,{6700,5000}}}},
        {{{1,0,{0,0}},{43,38,{0,2300}},{45,40,{3000,2400}},{47,42,{3500,2700}},{51,44,{3800,3100}},{54,47,{4600,3600}},{56,50,{5200,3800}},{58,52,{5800,4200}},{60,54,{6200,4600}},{128,127,{6700,5000}}}},
    }}},
    {0x321,0x522,0x512,0x50A,5,"EC 0x321",{0x6B4F,0x6DA1},{{
        {{{1,0,{0,0}},{126,125,{0,2300}},{126,125,{3000,2400}},{126,125,{3500,2700}},{126,125,{3800,3100}},{126,125,{4600,3600}},{126,125,{5200,3800}},{126,125,{5800,4200}},{128,127,{6200,4600}},{128,127,{6700,5000}}}},
        {{{1,0,{0,0}},{126,125,{0,2300}},{126,125,{3000,2400}},{126,125,{3500,2700}},{126,125,{3800,3100}},{126,125,{4600,3600}},{126,125,{5200,3800}},{126,125,{5800,4200}},{128,127,{6200,4600}},{128,127,{6700,5000}}}},
    }}},
    {0x324,0x524,0x514,0x50C,5,"EC DNTC",{0x6B8B,0x6DDD},{{
        {{{1,0,{0,0}},{67,60,{0,2300}},{67,60,{3000,2400}},{67,60,{3500,2700}},{67,60,{3800,3100}},{67,60,{4600,3600}},{67,60,{5200,3800}},{67,60,{5800,4200}},{128,127,{6200,4600}},{128,127,{6700,5000}}}},
        {{{1,0,{0,0}},{66,60,{0,2300}},{66,60,{3000,2400}},{66,60,{3500,2700}},{66,60,{3800,3100}},{66,60,{4600,3600}},{66,60,{5200,3800}},{66,60,{5800,4200}},{68,64,{6200,4600}},{128,127,{6700,5000}}}},
    }}},
    {0x326,0x526,0x516,0x50E,5,"EC STM0",{0x6BC7,0x6E19},{{
        {{{1,0,{0,0}},{126,125,{0,2300}},{126,125,{3000,2400}},{126,125,{3500,2700}},{126,125,{3800,3100}},{126,125,{4600,3600}},{126,125,{5200,3800}},{126,125,{5800,4200}},{128,127,{6200,4600}},{128,127,{6700,5000}}}},
        {{{1,0,{0,0}},{126,125,{0,2300}},{126,125,{3000,2400}},{126,125,{3500,2700}},{126,125,{3800,3100}},{126,125,{4600,3600}},{126,125,{5200,3800}},{126,125,{5800,4200}},{128,127,{6200,4600}},{128,127,{6700,5000}}}},
    }}},
    {0x325,0x525,0x5E7,0x5E6,5,"EC CNTC",{0x6C03,0x6E55},{{
        {{{1,0,{0,0}},{126,125,{0,2300}},{126,125,{3000,2400}},{126,125,{3500,2700}},{126,125,{3800,3100}},{126,125,{4600,3600}},{126,125,{5200,3800}},{126,125,{5800,4200}},{128,127,{6200,4600}},{128,127,{6700,5000}}}},
        {{{1,0,{0,0}},{126,125,{0,2300}},{126,125,{3000,2400}},{126,125,{3500,2700}},{126,125,{3800,3100}},{126,125,{4600,3600}},{126,125,{5200,3800}},{126,125,{5800,4200}},{128,127,{6200,4600}},{128,127,{6700,5000}}}},
    }}},
    {0x32B,0x527,0x517,0x50F,5,"EC Battery",{0x6CB1,0x6EFD},{{
        {{{1,0,{0,0}},{55,49,{0,2300}},{55,49,{3000,2400}},{55,49,{3500,2700}},{55,49,{3800,3100}},{55,49,{4600,3600}},{55,49,{5200,3800}},{55,49,{5800,4200}},{128,127,{6200,4600}},{128,127,{6700,5000}}}},
        {{{1,0,{0,0}},{54,49,{0,2300}},{54,49,{3000,2400}},{54,49,{3500,2700}},{54,49,{3800,3100}},{54,49,{4600,3600}},{54,49,{5200,3800}},{54,49,{5800,4200}},{56,52,{6200,4600}},{128,127,{6700,5000}}}},
    }}},
}};
struct Snapshot {
    int flags{},mode{},stage{};
    std::array<int,8> temperatures{},stages{};
    std::array<int,2> requestedRpm{},actualRpm{},actualDuty{};
};
inline NativeDefaultFans project(const Snapshot& s,std::uint64_t sampledAt){
    if((s.flags&7)||s.mode<0||s.mode>1||s.stage<0||s.stage>9)throw std::runtime_error("Honor: non-factory FAN context");
    // Tie preference is TSLO, then CPU, then the remaining OEM inputs. Every
    // contributor remains in the DTO; selection is by actual sensor STAGE,
    // never by comparing unrelated temperatures or substituting GPU/Tctl.
    int controlling=2,demand=-1;
    for(int i:{2,0,1,3,4,5,6,7}){
        if(s.temperatures[i]<0||s.temperatures[i]>125||s.stages[i]<0||s.stages[i]>9)
            throw std::runtime_error("Honor: invalid thermal input/stage");
        if(s.stages[i]>demand){demand=s.stages[i];controlling=i;}
    }
    NativeDefaultFans result;
    for(int ch=0;ch<2;++ch){
        if(s.requestedRpm[ch]<0||s.requestedRpm[ch]>9000||s.actualRpm[ch]<0||s.actualRpm[ch]>15000)
            throw std::runtime_error("Honor: invalid request/feedback RPM");
        auto& f=result.channels[ch];f.rpm=true;f.profile=s.mode?"Performance":"Balanced";
        f.curveSource="Honor BIOS1.09 / IT8987 documented thermal tables";
        f.temperatureSource=inputs[controlling].name;f.temperatureAddress=inputs[controlling].address;
        f.temperatureCelsius=s.temperatures[controlling];f.requestedRpm=s.requestedRpm[ch];f.actualRpm=s.actualRpm[ch];
        f.requestSource=ch?"EC 0x53A / requested RPM":"EC 0x534 / requested RPM";
        f.sampledAtMs=sampledAt;f.stage=s.stage;f.demandStage=demand;f.table=inputs[controlling].tables[s.mode];
        result.actualPercent[ch]=s.actualDuty[ch]/2.0;
        for(int i=0;i<8;++i){
            NativeDefaultFanInput input;input.source=inputs[i].name;input.address=inputs[i].address;
            input.temperature=s.temperatures[i];input.stage=s.stages[i];
            input.rising.push_back({20,0});input.falling.push_back({20,0});
            const auto& levels=inputs[i].modes[s.mode];
            for(int n=1;n<10;++n){
                if(levels[n].up<=100)input.rising.push_back({double(levels[n].up),double(levels[n].rpm[ch])});
                if(levels[n].up<=100)input.falling.push_back({double(levels[n].down),double(levels[n].rpm[ch])});
            }
            f.inputs.push_back(std::move(input));
        }
        f.points=f.inputs[controlling].rising;
        const auto& levels=inputs[controlling].modes[s.mode];f.startCelsius=levels[1].up;f.offBelowCelsius=levels[1].down;
    }
    return result;
}
}
