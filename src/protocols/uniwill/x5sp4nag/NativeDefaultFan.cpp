#include "MagicControl/NativeDefaultFan.h"
#include "MagicControl/EcAccess.h"
#include "MagicControl/DeviceOperation.h"
#include <algorithm>
#include <utility>

namespace mc {
namespace {
// Exact saved Wujie EC SHA2565d4ec03a...; live selector62C0 corroborated.
// research/wujie-default-curves-20261030/DECODED.md. No transplant to another
// board/revision/table.6598/730 are APU power data, NOT fan-curve parameters.
constexpr std::array<std::array<int,6>,4> records{{
    {65,80,83,130,18,28},{60,70,70,90,17,20},
    {75,110,90,180,22,47},{75,110,90,180,22,47}}};
constexpr std::array<std::array<int,4>,4> second{{
    {50,130,10,60},{50,90,10,60},{50,170,10,60},{50,170,10,60}}};
int base(int t,const std::array<int,6>& r){
    // Rising/cold reference.47C itself is history-dependent in the firmware;
    // the observed request remains separate and may lie off this base line.
    if(t<48)return 0;
    if(t>r[2])return r[3];
    if(t>=r[0])return std::min(r[3],r[1]+(t-r[0])*r[5]/10);
    return 50+(t-48)*r[4]/10;
}
std::optional<int> secondary(int request,int primary,const std::array<int,4>& r){
    if(request<r[3]&&primary<r[3])return 0;
    if(request>=r[0]&&request<=r[1]){
        if(primary<r[2])return {}; // Do not invent a request for a torn ramp sample.
        return std::max(r[0],primary-r[2]);
    }
    return primary;
}
}
NativeDefaultFanReading readNativeDefaultFans(EcAccess& ec){
    if(deviceOperations().passiveEcReadsPaused()||ec.observationsPaused())return {};
    if(deviceProfile(ec.verifyBoard().board).id!="wu-jie-15-xpro")return {NativeDefaultFanObservation::Inactive,{}};
    const EcAccess::Transaction transaction(ec);
    if(ec.readStable(0x454)!=82||ec.readStable(0x456)!=72||ec.readStable(0x740)!=26)return {NativeDefaultFanObservation::Inactive,{}};
    // Compare only selector/FAN bits. Other functions share these bytes;
    // their unrelated changes do not invalidate a native FAN sample.
    const auto context=[&]{return std::array<std::uint8_t,11>{
        ec.readStable(0x6e6),static_cast<std::uint8_t>(ec.readStable(0x751)&0xd0),
        static_cast<std::uint8_t>(ec.readStable(0x726)&0x80),static_cast<std::uint8_t>(ec.readStable(0x727)&0x40),
        static_cast<std::uint8_t>(ec.readStable(0x7c6)&4),ec.readStable(0x8e7),ec.readStable(0x8e8),
        static_cast<std::uint8_t>(ec.readStable(0x49f)&2),static_cast<std::uint8_t>(ec.readStable(0x741)&1),
        static_cast<std::uint8_t>(ec.readStable(0x728)&1),static_cast<std::uint8_t>(ec.readStable(0x7c5)&0x80)};};
    const auto before=context();
    if(before[0]!=1||(before[1]&64)||(before[2]&128)||(before[3]&64)||(before[4]&4)||
       before[5]!=0x62||before[6]!=0xc0)return {NativeDefaultFanObservation::Inactive,{}};
    // Also confirm the secondary transfer table selected by the firmware.
    if(ec.readStable(0x906d)!=0x62||ec.readStable(0x906e)!=0xc4)return {NativeDefaultFanObservation::Inactive,{}};
    const int mode=(before[1]&128)?1:((before[7]&2)&&(before[1]&144)==16)?
        ((before[8]&1)&&(before[9]&1)?3:2):0;
    const bool directCpu=(before[1]&16)&&(before[7]&2);
    const std::uint16_t input=directCpu?0x43e:0x9178;
    const int t=ec.readStable(input);
    const int cpuRequest=ec.readStable(0x461),gpuInputRequest=ec.readStable(0x469);
    const int shared=ec.readStable(0x670),primary=ec.readStable(0x75b),other=ec.readStable(0x75c);
    const bool forced=(ec.readStable(0x8eb)&0x48)||(ec.readStable(0x913f)&128);
    if(context()!=before||primary>200||other>200||cpuRequest>200||gpuInputRequest>200||shared>200)return {};
    NativeDefaultFans result;
    result.actualPercent={primary/2.,other/2.};
    result.baseInputRequests={cpuRequest/2.,gpuInputRequest/2.};result.sharedRequestedPercent=shared/2.;
    const auto at=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    for(int ch=0;ch<2;++ch){auto& v=result.channels[ch];
        v.profile=mode==1?"Office":mode>=2?"Turbo":"Gaming";
        // The selected GPU input records at627C are all zero. BOTH physical
        // fan graphs therefore use the actual EC CPU input, never ADLX GPU T.
        v.temperatureSource="EC CPU";v.temperatureAddress=input;
        if(t>0&&t<128)v.temperatureCelsius=t;
        v.requestDerived=ch==1;v.forced=forced;v.table=0x62c0;v.sampledAtMs=at;
        v.curveSource="Wujie EC firmware · native Default base request";
        v.requestSource=ch?"Decoded EC secondary transfer · shared request + primary PWM":"EC0x670 · shared request after limits";
        if(!forced){if(!ch)v.requestedPercent=shared/2.;
            else if(const auto r=secondary(shared,primary,second[mode]))v.requestedPercent=*r/2.;}
        for(int temperature=20;temperature<=100;++temperature){const int raw=base(temperature,records[mode]);
            const int target=ch?secondary(raw,raw,second[mode]).value_or(0):raw;
            v.points.push_back({static_cast<double>(temperature),target/2.});}
    }
    return {NativeDefaultFanObservation::Available,std::move(result)};
}
}
