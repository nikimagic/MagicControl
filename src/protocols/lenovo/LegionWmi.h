#pragma once
#include "MagicControl/PowerSettings.h"
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string_view>
#include <vector>
#include <set>
#include <stdexcept>
namespace mc::lenovo {
inline constexpr std::string_view profileId="lenovo-legion-kwcn";
// Firmware table branch only: the separately stabilised bytes must agree with
// the battery snapshot. This is an inferred adapter rating, not measured watts
// or a connector identity. Missing/unstable inputs remain unknown.
inline std::optional<unsigned> adapterWattCandidate(std::optional<std::uint16_t> raw,
    std::optional<std::uint8_t> adapterClass,std::optional<std::uint8_t> gate,
    std::optional<std::uint8_t> flags,std::uint8_t batteryMuxFlags) noexcept {
    if(!raw||!adapterClass||!gate||!flags||*flags!=batteryMuxFlags||*flags==0xff||*gate==0xff||
       (*flags&0x88)!=0x88||!(*gate&0x13))return {};
    unsigned expected=0;
    switch(*adapterClass){case 4:expected=135;break;case 5:expected=170;break;case 6:expected=230;break;case 7:expected=300;break;case 8:expected=330;break;default:break;}
    return expected&&*raw==expected?std::optional<unsigned>(expected):std::nullopt;
}
// KWCN54WW WMAE reads/writes FNST (EC 0x41D.2) under LFCM. Same native
// Full Speed feature as LLT GodModeControllerV2, NOT a FAN table or power mode.
inline constexpr std::uint32_t fanFullSpeedId=0x04020000;
template<class Firmware> bool readFullSpeedFlag(Firmware& firmware){
    const int value=firmware.feature(fanFullSpeedId);
    if(value!=0&&value!=1)throw std::runtime_error("Lenovo: invalid native Full Speed flag");
    return value==1;
}
template<class Firmware> bool setFullSpeedFlag(Firmware& firmware,bool enabled){
    const bool before=readFullSpeedFlag(firmware);
    if(before!=enabled)firmware.feature(fanFullSpeedId,enabled?1:0);
    if(readFullSpeedFlag(firmware)!=enabled)throw std::runtime_error("Lenovo: native Full Speed flag readback mismatch");
    return before!=enabled;
}
inline constexpr std::array<std::uint32_t,3> cpuIds{0x01060000,0x01020000,0x01010000};
// KWCN54WW WMAE: 01070000 -> F5E4 -> CTAU at MCHBAR 59A0[23:17],
// Intel PACKAGE_RAPL_LIMIT PL1 time window. This is MMIO, not MSR readback.
struct OptionId {const char* key;const char* label;const char* unit;std::uint32_t id;};
inline constexpr std::array<OptionId,6> optionIds{{
 {"gpu.offset","Power Offset","W",0x02040000},{"gpu.ctgp","cTGP","W",0x02020000},
 {"gpu.boost","Dynamic Boost","W",0x02010000},{"cpu.tau","PL1 Tau","s",0x01070000},
 {"cpu.temperature","CPU Temperature","°C",0x01040000},{"gpu.temperature","GPU Temperature","°C",0x02030000}}};
struct WmiNameLess {
    bool operator()(const std::string& a,const std::string& b)const noexcept{
        return std::lexicographical_compare(a.begin(),a.end(),b.begin(),b.end(),[](unsigned char x,unsigned char y){
            return (x>='A'&&x<='Z'?x+32:x)<(y>='A'&&y<='Z'?y+32:y);});
    }
};
using Row=std::map<std::string,std::vector<std::uint32_t>,WmiNameLess>;
inline std::uint32_t scalar(const Row& row,const char* key){
    const auto i=row.find(key);if(i==row.end()||i->second.size()!=1)throw std::runtime_error(std::string("Lenovo WMI: missing scalar ")+key);
    return i->second.front();
}
struct Capability {std::uint32_t id{},flags{};int initial{},step{},minimum{},maximum{};std::vector<int> values;};
struct Catalog {
    std::map<std::uint32_t,Capability> capabilities;
    static Catalog decode(const std::vector<Row>& ranges,const std::vector<Row>& discrete){
        if(ranges.empty()||ranges.size()>1024||discrete.size()>4096)throw std::runtime_error("Lenovo: invalid WMI capability count");
        Catalog out;
        for(const auto& row:ranges){
            Capability c;c.id=scalar(row,"IDs");c.flags=scalar(row,row.contains("_flags")?"_flags":"Capability"); // WmiDataId2, not a guessed property name.
            c.initial=static_cast<int>(scalar(row,"DefaultValue"));c.step=static_cast<int>(scalar(row,"Step"));
            c.minimum=static_cast<int>(scalar(row,"MinValue"));c.maximum=static_cast<int>(scalar(row,"MaxValue"));
            if(c.minimum<0||c.maximum<0||c.maximum>100000||c.step<0||c.initial<0||c.initial>100000||!out.capabilities.emplace(c.id,c).second)
                throw std::runtime_error("Lenovo: duplicate/invalid capability row");
        }
        for(const auto& row:discrete){const auto id=scalar(row,"IDs");const auto value=scalar(row,"Value");
            if(value>100000)throw std::runtime_error("Lenovo: invalid discrete power value");
            if(auto it=out.capabilities.find(id);it!=out.capabilities.end())it->second.values.push_back(static_cast<int>(value));}
        for(auto& [id,c]:out.capabilities){std::sort(c.values.begin(),c.values.end());c.values.erase(std::unique(c.values.begin(),c.values.end()),c.values.end());}
        return out;
    }
    PowerSetting setting(std::uint32_t plain,int mode,std::string key,std::string label,std::string unit)const{
        if(mode<0||mode>255)throw std::runtime_error("Lenovo: invalid range mode");
        const auto id=(plain&0xffff00ffu)|(static_cast<std::uint32_t>(mode)<<8);
        const auto it=capabilities.find(id);if(it==capabilities.end())throw std::runtime_error("Lenovo: WMI does not declare requested power setting");
        const auto& c=it->second;PowerSetting p{std::move(key),std::move(label),std::move(unit),{},c.initial,c.minimum,c.maximum,c.step,c.values,(c.flags&2)!=0};
        if(!p.discrete.empty()){p.minimum=p.discrete.front();p.maximum=p.discrete.back();}
        if(p.maximum<p.minimum||(p.discrete.empty()&&p.step<=0))p.writable=false;
        return p;
    }
};
struct FanRow {
    int mode{},fan{},sensor{};std::vector<int> rpm,temperature;
    int maximumRpm{},minimumRpm{},maximumLevel{};
};
inline std::vector<FanRow> fanRows(const std::vector<Row>& rows){
    if(rows.empty()||rows.size()>512)throw std::runtime_error("Lenovo: FAN WMI metadata unavailable");
    std::vector<FanRow> out;
    for(const auto& r:rows){FanRow f;f.mode=static_cast<int>(scalar(r,"Mode"));f.fan=static_cast<int>(scalar(r,"Fan_Id"));f.sensor=static_cast<int>(scalar(r,"Sensor_ID"));
        const auto a=r.find("FanTable_Data"),b=r.find("SensorTable_Data");
        if(a==r.end()||b==r.end()||a->second.empty()||a->second.size()>16||b->second.size()!=a->second.size())throw std::runtime_error("Lenovo: incompatible FAN table dimensions");
        for(auto v:a->second){if(v<100||v>20000)throw std::runtime_error("Lenovo: invalid WMI RPM ladder");f.rpm.push_back(static_cast<int>(v));}
        for(auto v:b->second){if(v>255)throw std::runtime_error("Lenovo: invalid WMI FAN temperature");f.temperature.push_back(static_cast<int>(v));}
        if(!std::is_sorted(f.rpm.begin(),f.rpm.end()))throw std::runtime_error("Lenovo: unordered WMI RPM ladder");
        f.minimumRpm=f.rpm.front();f.maximumRpm=f.rpm.back();f.maximumLevel=static_cast<int>(f.rpm.size());out.push_back(std::move(f));}
    return out;
}
}
