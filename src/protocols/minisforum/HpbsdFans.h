#pragma once
#include <array>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>
#include <algorithm>
#include "MagicControl/NativeFanCurve.h"
#include "MagicControl/FanControlLimits.h"
namespace mc::hpbsd {
using Byte=std::uint8_t;
using Snapshot=std::array<Byte,47>;
using Mutable=std::array<Byte,34>;
// Keep the original16/v2 and30/v3 fields first for exact receipt recovery.
// Last normal upper/hot lower boundaries may move EARLIER; hot MAX bases,
// hot upper100 and override registers remain immutable (including CPU >93).
inline constexpr std::array<int,34> mutableOffsets{3,6,9,12,15,18,21,27,28,29,30,31,32,33,35,38,4,7,10,13,16,19,8,11,14,17,20,23,36,40,22,39,26,43};
inline constexpr std::array<std::uint16_t,34> mutableAddresses{0x310,0x313,0x316,0x319,0x31c,0x31f,0x322,0x8b0,0x8b1,0x8b2,0x8b3,0x8b4,0x8b5,0x8b6,0x330,0x333,0x311,0x314,0x317,0x31a,0x31d,0x320,0x315,0x318,0x31b,0x31e,0x321,0x324,0x331,0x335,0x323,0x334,0x327,0x338};
inline constexpr std::array<int,2> bandCount{8,3},maximumCode{51,40};
inline constexpr std::array<int,2> minimumPercent{36,38}; // Min preset only: native18/51,15/40. NOT editor bounds.
inline Snapshot factory(Byte selector){
    if(selector!=0xb1&&selector!=0xb2)throw std::runtime_error("HPBSD: unknown BIOS fan selector");
    Snapshot s{};s[0]=selector;
    const std::array<Byte,8> up{25,45,54,66,Byte(selector==0xb1?76:80),88,93,100};
    const std::array<Byte,8> down{0,25,45,54,66,Byte(selector==0xb1?76:80),88,93};
    const std::array<Byte,8> bases=selector==0xb1?std::array<Byte,8>{0,18,22,24,28,32,34,51}:std::array<Byte,8>{0,18,21,28,30,33,36,51};
    const std::array<Byte,8> slopes=selector==0xb1?std::array<Byte,8>{0,14,16,21,28,10,154,0}:std::array<Byte,8>{0,15,77,16,21,37,255,0};
    for(int i=0;i<8;++i){s[3+3*i]=bases[i];s[4+3*i]=up[i];s[5+3*i]=down[i];s[27+i]=slopes[i];}
    const std::array<Byte,9> system{0,25,0,15,83,25,40,100,83};
    for(int i=0;i<9;++i)s[35+i]=system[i];
    return s;
}
inline void validateImmutable(const Snapshot& s){
    const auto expected=factory(s[0]);
    for(int i=0;i<47;++i){bool mutableByte=false;for(const auto index:mutableOffsets)mutableByte|=index==i;
        if(!mutableByte&&s[i]!=expected[i])throw std::runtime_error("HPBSD: BIOS fan bands/critical rows/slopes/override fingerprint mismatch");}
    // Even a partial transaction may never delay either firmware hot stage.
    if(s[22]>93||s[26]>93||s[39]>83||s[43]>83)throw std::runtime_error("HPBSD: delayed hot stage refused");
}
inline Mutable values(const Snapshot& s){Mutable v{};for(std::size_t i=0;i<v.size();++i)v[i]=s[mutableOffsets[i]];return v;}
inline Snapshot withValues(Byte selector,const Mutable& v){auto s=factory(selector);for(std::size_t i=0;i<v.size();++i)s[mutableOffsets[i]]=v[i];return s;}
inline Mutable manual(const std::array<int,2>& percent,Byte selector=0xb1){
    for(int i=0;i<2;++i)if(!fanInput::validPercent(percent[i]))throw std::invalid_argument("HPBSD: RPM target must be5..100%");
    Mutable v=values(factory(selector));for(int i=0;i<7;++i){v[i]=static_cast<Byte>((percent[0]*51+50)/100);v[i+7]=0;}
    v[14]=v[15]=static_cast<Byte>((percent[1]*40+50)/100);return v;
}
using Plan=std::vector<std::pair<int,Byte>>;
inline Plan plan(Mutable from,const Mutable& to){
    Plan out;const auto add=[&](int index,Byte value){if(from[index]!=value){out.emplace_back(index,value);from[index]=value;}};
    // Zero slopes before bases, then restore canonical slopes last. Critical
    // CPU row94+C and system row83+C are outside this write set throughout.
    bool temperatures=false;for(std::size_t i=16;i<from.size();++i)temperatures|=from[i]!=to[i];
    for(int i=7;i<14;++i)add(i,0);
    // Before moving boundaries, seed all normal rows to their safe maximum.
    // Every interrupted prefix retains the immutable CPU94+/System83+ stages.
    if(temperatures){for(int i=0;i<7;++i)add(i,51);for(int i=14;i<16;++i)add(i,40);for(std::size_t i=16;i<from.size();++i)add(static_cast<int>(i),to[i]);}
    for(int i=0;i<7;++i)add(i,to[i]);
    for(int i=14;i<16;++i)add(i,to[i]);
    for(int i=7;i<14;++i)add(i,to[i]);
    return out;
}
inline bool reachable(Mutable from,const Mutable& to,const Mutable& observed){
    if(from==observed)return true;for(const auto [i,v]:plan(from,to)){from[i]=v;if(from==observed)return true;}return false;
}
inline bool fixedAllowed(const Mutable& v,Byte selector){
    const auto base=values(factory(selector));for(std::size_t i=16;i<v.size();++i)if(v[i]!=base[i])return false;
    for(int i=0;i<7;++i)if(v[i]!=v[0]||v[i]<3||v[i]>51||v[i+7]!=0)return false;
    return v[14]==v[15]&&v[14]>=2&&v[14]<=40;
}
inline int percent(int code,int channel){return (code*100+maximumCode[channel]/2)/maximumCode[channel];}
inline NativeFanCurves points(const Snapshot& s){
    NativeFanCurves out;for(auto& row:out)for(auto& p:row)p={255,100};
    for(int ch=0;ch<2;++ch)for(int i=0;i<bandCount[ch];++i){const int at=(ch?35:3)+3*i;out[ch][i]={s[at+1],percent(s[at],ch)};}
    return out;
}
// Requested fields use the universal20..100C range. The separate native MAX
// stage is still active by94/83C, even when a saved user point lies above it.
// Keep requested points intact; only the encoded EC boundaries are projected.
inline NativeFanCurves protectedPoints(const NativeFanCurves& requested){
    auto effective=requested;
    for(int ch=0;ch<2;++ch){int previousT=0,previousDuty=fanInput::percentMinimum;
        // Inactive cells belong to the common16-cell profile. Preserve them;
        // only this device's admitted8/3 cells reach the physical table.
        for(int i=0;i<bandCount[ch]-1;++i){const auto& p=requested[ch][i];
            if(!fanInput::validTemperature(p.temperature))throw std::invalid_argument("HPBSD: editable temperature must be20..100C");
            if(p.temperature<previousT+2)throw std::invalid_argument("HPBSD: native bands require at least2C separation");
            if(!fanInput::validPercent(p.duty)||p.duty<previousDuty)throw std::invalid_argument("HPBSD: monotonic RPM curve5..100% required");
            previousT=p.temperature;previousDuty=p.duty;}
        if(requested[ch][bandCount[ch]-1]!=FanCurveCell{100,100})throw std::invalid_argument("HPBSD: final firmware MAX row must remain100C/100%");
        int last=ch?83:93;
        for(int i=bandCount[ch]-2;i>=0;--i){auto& p=effective[ch][i];p.temperature=std::min(p.temperature,last);last=p.temperature-2;}
    }return effective;
}
inline Mutable curve(const NativeFanCurves& requested,Byte selector){
    const auto points=protectedPoints(requested);
    auto s=factory(selector);
    for(int ch=0;ch<2;++ch){int lower=0,previous=fanInput::percentMinimum;
        for(int i=0;i<bandCount[ch];++i){const auto& p=points[ch][i];const bool hot=i==bandCount[ch]-1;
            if(!fanInput::validPercent(p.duty)||(hot&&p.duty!=100)||p.duty<previous)throw std::invalid_argument("HPBSD: monotonic RPM curve5..100% required");
            if(p.temperature<lower+2||p.temperature>100)throw std::invalid_argument("HPBSD: native bands require at least2C separation");
            if(hot&&p.temperature!=100)throw std::invalid_argument("HPBSD: final firmware MAX row must remain100C/100%");
            const int at=(ch?35:3)+3*i;
            if(!hot){s[at]=static_cast<Byte>((p.duty*maximumCode[ch]+50)/100);s[at+1]=static_cast<Byte>(p.temperature);s[at+2]=static_cast<Byte>(lower);}
            else s[at+2]=static_cast<Byte>(lower);
            lower=p.temperature;previous=p.duty;
        }
    }
    for(int i=0;i<7;++i){const int at=3+3*i,span=s[at+1]-s[at+2],delta=s[at+3]-s[at];s[27+i]=static_cast<Byte>(std::min(255,100*delta/span));}
    validateImmutable(s);return values(s);
}
// Migrate old saved recipes in memory. Preserve all native ordinary bands;
// reserve2C for each remaining band instead of collapsing them at100C.
inline NativeFanCurves normalizeDraft(NativeFanCurves p){
    for(int ch=0;ch<2;++ch){int previousT=fanInput::temperatureMinimum-2,previousDuty=fanInput::percentMinimum;
        for(int i=0;i<bandCount[ch]-1;++i){auto& point=p[ch][i];const int last=fanInput::temperatureMaximum-2*(bandCount[ch]-2-i);
            point.temperature=std::clamp(point.temperature,std::max(fanInput::temperatureMinimum,previousT+2),last);
            point.duty=std::max(previousDuty,fanInput::percent(point.duty));previousT=point.temperature;previousDuty=point.duty;}
        p[ch][bandCount[ch]-1]={100,100};
    }return p;
}
inline NativeFanCurves defaultDraft(Byte selector){auto p=points(factory(selector));for(int ch=0;ch<2;++ch)for(int i=0;i<bandCount[ch];++i)p[ch][i].duty=std::max(minimumPercent[ch],p[ch][i].duty);return normalizeDraft(p);}
// Receipt admission is deliberately distinct from NEW user-input validation:
// v2/v3 policies must still restore exactly, including original25/83/93 bands.
inline bool curveAllowed(const Mutable& v,Byte selector){try{
    const auto s=withValues(selector,v);validateImmutable(s);
    for(int ch=0;ch<2;++ch){int lower=0,previous=ch?2:3;
        for(int i=0;i<bandCount[ch];++i){const int at=(ch?35:3)+3*i;const bool hot=i==bandCount[ch]-1;
            if(s[at]<previous||s[at]>maximumCode[ch]||s[at+2]!=lower||s[at+1]<lower+2)return false;
            if(!hot&&(s[at+1]<20||s[at+1]>(ch?83:93)))return false;
            if(!ch&&!hot){const int expected=std::min(255,100*(s[at+3]-s[at])/(s[at+1]-s[at+2]));if(s[27+i]!=expected)return false;}
            lower=s[at+1];previous=s[at];}
    }return true;
}catch(...){return false;}}
inline bool allowed(const Mutable& v,Byte selector){return v==values(factory(selector))||fixedAllowed(v,selector)||curveAllowed(v,selector);}
inline int targetPercent(const NativeFanCurves& points,std::span<const int> slopes,int ch,int temperature){
    int i=0;while(i+1<bandCount[ch]&&(ch?temperature>=points[ch][i].temperature:temperature>points[ch][i].temperature))++i;
    const int base=(points[ch][i].duty*maximumCode[ch]+50)/100,lower=i?points[ch][i-1].temperature:0;
    const int code=ch?base:base+std::max(0,temperature-lower)*slopes[i]/100;
    return percent(std::clamp(code,0,maximumCode[ch]),ch);
}
}
