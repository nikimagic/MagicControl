#include "src/protocols/uniwill/x5sp4nag/PowerState.h"
#include "src/gui/common/CpuPowerLabels.h"
#include <stdexcept>
#include <iostream>
using namespace mc;
void check(bool good,const char* message){if(!good)throw std::runtime_error(message);}
int main(){try{
    const std::array<EcPowerLimits,3> presets{{{45,45,65},{54,54,65},{100,100,105}}};
    unsigned cases{};
    for(int mode=0;mode<256;++mode)for(int a:{0,1,200,201,255})for(int b:{0,1,200,201,255})for(int c:{0,1,200,201,255}){
        const auto s=x5sp4nag::powerState(static_cast<std::uint8_t>(mode),presets,{static_cast<std::uint8_t>(a),static_cast<std::uint8_t>(b),static_cast<std::uint8_t>(c)});
        check(s.overrides==EcPowerLimits{a,b,c},"Read zero/high override lost");
        check(x5sp4nag::overridesCleared(s.overrides)==(a==0&&b==0&&c==0),"Partial or unknown override called Auto");
        const int index=x5sp4nag::nativePowerButton(static_cast<std::uint8_t>(mode))-1;
        if(index>=0){const auto& p=presets[index];check(s.active==EcPowerLimits{a?std::optional(a):p.sustained,b?std::optional(b):p.shortBoost,c?std::optional(c):p.peak},"Zero replaced a factory limit with0W");}
        else check(s.active==EcPowerLimits{},"Unknown native profile invented a preset");
        ++cases;
    }
    check(!x5sp4nag::overridesCleared({}),"Missing read treated as cleared");
    check(!x5sp4nag::overridesCleared({0,0,{}}),"Incomplete read treated as cleared");
    for(const auto [raw,button]:{std::pair{0xa0,1},std::pair{0x00,2},std::pair{0x10,3}}){
        const auto native=x5sp4nag::powerState(raw,presets,{0,0,0});
        check(x5sp4nag::powerButton(native,false,false)==button,"Native zero overrides not highlighted");
        check(x5sp4nag::powerButton(native,true,false)==4,"Explicit Auto not highlighted");
        check(x5sp4nag::powerButton(native,false,true)==button,"Stale Custom intent masked cleared override");
        check(x5sp4nag::powerButton(x5sp4nag::powerState(raw,presets,{65,65,70}),false,true)==5,"Explicit Custom confused with Turbo");
        check(x5sp4nag::powerButton(x5sp4nag::powerState(raw,presets,{0,30,0}),true,false)==5,"Partial overrides highlighted Auto");
    }
    check(x5sp4nag::powerButton({},true,false)==-1,"Unknown read highlighted Auto");
    check(x5sp4nag::savedPowerButton(4,true,false)==5,"Legacy ApuMode4 became Auto");
    check(x5sp4nag::savedPowerButton(4,false,true)==4,"Explicit Auto setting ignored");
    check(x5sp4nag::savedPowerButton({},true,false)==5,"Legacy manual setting ignored");
    check(x5sp4nag::savedPowerButton(3,false,false)==3,"Legacy Turbo setting changed");
    check(gui::ui::cpuPowerButtonCount("wu-jie-15-xpro")==5,"Wujie Auto missing");
    check(gui::ui::cpuPowerButtonCount("huawei-hlyl-wxx9")==3,"Honor buttons changed");
    check(gui::ui::cpuPowerButtonCount("mechrevo-gm6ir0c")==5,"GM6 buttons changed");
    check(gui::ui::cpuPowerButtonCount("eluktronics-hydroc16g2")==4,"HYDROC buttons changed");
    for(const auto [raw,expected]:{std::pair{0x80,1},std::pair{0x00,2},std::pair{0x10,3},std::pair{0x50,3}})
        check(x5sp4nag::nativePowerButton(raw)==expected,"Native profile or boost bit mapping");
    std::cout<<"PASS "<<cases<<" Wujie readback cases; zero policy, partial/high Custom, native flags and factory fallback; hardware writes=0\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
