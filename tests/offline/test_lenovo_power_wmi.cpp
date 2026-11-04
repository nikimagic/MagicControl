#include "../../src/protocols/lenovo/LegionWmi.h"
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
unsigned checks{};
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
template<class F> void rejects(F&& operation,const char* message){
    bool rejected=false;try{operation();}catch(const std::exception&){rejected=true;}
    check(rejected,message);
}
using mc::lenovo::Row;
Row range(std::uint32_t id,std::uint32_t flags,std::uint32_t initial,
          std::uint32_t min,std::uint32_t max,std::uint32_t step){
    return {{"IDs",{id}},{"_flags",{flags}},{"DefaultValue",{initial}},
            {"MinValue",{min}},{"MaxValue",{max}},{"Step",{step}}};
}
Row discrete(std::uint32_t id,std::uint32_t value){return {{"IDs",{id}},{"Value",{value}}};}
}

int main(){try{
    using namespace mc::lenovo;
    // Sanitized ranges from the documented conditional KWCN Custom table.
    // This fixture exercises parser admission only, not the active owner SKU.
    constexpr auto cross=0x0106ff00u,pl1=0x0102ff00u,pl2=0x0101ff00u,tau=0x0107ff00u;
    const auto catalog=Catalog::decode({range(cross,7,55,20,110,1),range(pl1,7,70,30,140,1),
        range(pl2,7,119,55,190,1),range(tau,7,56,20,160,0),
        range(0x01060100u,7,25,20,110,5)},
        {discrete(tau,56),discrete(tau,20),discrete(tau,56),discrete(tau,128),
         discrete(0xdeadbeefu,42)});
    const auto c=catalog.setting(cpuIds[0],255,"cpu.cross","Cross Loading","W");
    const auto l=catalog.setting(cpuIds[1],255,"cpu.pl1","PL1","W");
    const auto s=catalog.setting(cpuIds[2],255,"cpu.pl2","PL2","W");
    check(c.key=="cpu.cross"&&c.label=="Cross Loading"&&c.unit=="W", "Cross Loading identity/units lost");
    check(c.defaultValue==55&&l.defaultValue==70&&s.defaultValue==119,"distinct CPU defaults lost");
    check(c.accepts(110)&&l.accepts(103)&&s.accepts(115),"independent live-like CPU limits rejected");
    check(c.accepts(20)&&c.accepts(110)&&!c.accepts(19)&&!c.accepts(111),"cross-loading boundaries");
    check(l.accepts(30)&&l.accepts(140)&&!l.accepts(29)&&!l.accepts(141),"PL1 boundaries");
    check(s.accepts(55)&&s.accepts(190)&&!s.accepts(54)&&!s.accepts(191),"PL2 boundaries");
    const auto quiet=catalog.setting(cpuIds[0],1,"quiet","quiet","W");
    check(quiet.defaultValue==25&&quiet.accepts(20)&&quiet.accepts(25)&&!quiet.accepts(21),"mode-specific range/step mixed with Custom");
    const auto t=catalog.setting(0x01070000,255,"cpu.tau","PL1 Tau","s");
    check(t.discrete==std::vector<int>({20,56,128}),"discrete values must sort and deduplicate");
    check(t.minimum==20&&t.maximum==128&&t.accepts(20)&&t.accepts(128)&&!t.accepts(21),"Tau must accept declared values only");
    rejects([&]{(void)catalog.setting(cpuIds[0],2,"x","x","W");},"missing mode silently fell back");
    rejects([&]{(void)catalog.setting(cpuIds[0],256,"x","x","W");},"out-of-range mode accepted");
    rejects([&]{(void)catalog.setting(0x02010000,255,"x","x","W");},"undeclared GPU setting accepted");

    for(auto flags:{0u,1u,4u,5u}){
        const auto r=Catalog::decode({range(cross,flags,55,20,110,1)},{}).setting(cpuIds[0],255,"","","");
        check(!r.writable&&!r.accepts(55),"read-only capability admitted a write");
    }
    for(auto flags:{2u,3u,6u,7u}){
        const auto r=Catalog::decode({range(cross,flags,55,20,110,1)},{}).setting(cpuIds[0],255,"","","");
        check(r.writable&&r.accepts(55),"writable WMI capability rejected");
    }
    const auto disabled=Catalog::decode({range(cross,7,55,20,110,0)},{}).setting(cpuIds[0],255,"","","");
    check(!disabled.writable&&!disabled.accepts(55),"zero-step numeric range admitted a write");
    const auto inverted=Catalog::decode({range(cross,7,55,110,20,1)},{}).setting(cpuIds[0],255,"","","");
    check(!inverted.writable&&!inverted.accepts(55),"inverted WMI range admitted a write");

    auto missing=range(cross,7,55,20,110,1);missing.erase("IDs");
    rejects([&]{(void)Catalog::decode({missing},{});},"missing WMI id admitted");
    auto multi=range(cross,7,55,20,110,1);multi["MinValue"]={20,30};
    rejects([&]{(void)Catalog::decode({multi},{});},"array accepted for scalar range");
    auto overflow=range(cross,7,55,20,110,1);overflow["MaxValue"]={100001};
    rejects([&]{(void)Catalog::decode({overflow},{});},"oversized power capability admitted");
    auto negative=range(cross,7,55,20,110,1);negative["MinValue"]={std::numeric_limits<std::uint32_t>::max()};
    rejects([&]{(void)Catalog::decode({negative},{});},"negative encoded WMI minimum admitted");
    rejects([&]{(void)Catalog::decode({},{});},"missing capabilities admitted");
    rejects([&]{(void)Catalog::decode({range(tau,7,56,20,160,0)},{discrete(tau,100001)});},"oversized discrete value admitted");
    auto duplicated=range(cross,7,55,20,110,1);
    rejects([&]{(void)Catalog::decode({duplicated,duplicated},{});},"duplicate capability accepted");
    std::cout<<"Lenovo WMI power offline: "<<checks<<" checks; no OS/hardware\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
