#include "MagicControl/Overclock.h"
#include "src/gui/common/Geometry.h"
#include "src/gui/common/Fonts.h"
#include "MagicControl/ScrollModel.h"
#include <array>
#include <cstdio>
#include <cstdlib>
void check(bool ok){if(!ok){std::fputs("Wujie FwVars decoder failed\n",stderr);std::exit(1);}}
int main(){
    std::array<std::uint8_t,180> raw{};raw[6]=26;raw[0x43]=24;raw[0x6e]=255;raw[0x6f]=255;
    auto s=mc::decodeWujieOverclock(raw);
    check(s.readable&&s.project==26&&s.version==24&&s.cpuSupport==0&&s.memorySupport==0&&s.memorySwitch==0);
    check(s.menuSupport==255&&s.menuSwitch==255&&s.frequency==0&&s.voltage==0);
    raw[0x42]=1;raw[0x60]=1;raw[0x33]=1;raw[0x44]=0x78;raw[0x45]=0x56;raw[0x46]=0x34;raw[0x47]=0x12;
    s=mc::decodeWujieOverclock(raw);check(s.cpuSupport==1&&s.memorySupport==1&&s.memorySwitch==1&&s.frequency==0x12345678);
    check(!mc::decodeWujieOverclock(std::span(raw).first(179)).readable);
    raw[6]=25;check(!mc::decodeWujieOverclock(raw).readable);
    for(int dpi:{96,120,144,192}){
        const mc::gui::ui::Scale scale{dpi};
        const auto widths=mc::gui::ui::tabTextWidths(dpi);
        const auto visible=std::array<bool,7>{true,true,true,true,false,false,true};
        const auto g=mc::gui::ui::headerGeometry(scale(720),scale(560),widths,scale,0,7,visible);
        check(g.tabs[6].width()>0&&g.tabs[6].right<=g.captionButtons[0].left);
        const auto other=mc::gui::ui::headerGeometry(scale(720),scale(560),widths,scale,0,7,
            {true,true,true,true,false,false,false});
        check(other.tabs[6].width()==0);
    }
    mc::ScrollModel scroll;scroll.setExtent(880);scroll.setViewport(500);
    check(scroll.maximumPosition()==380);scroll.setPosition(900);check(scroll.position()==380);
    scroll.setViewport(1000);check(!scroll.scrollable()&&scroll.position()==0);
    std::puts("Wujie FwVars decoder: saved and synthetic cases passed; no hardware access");
}
