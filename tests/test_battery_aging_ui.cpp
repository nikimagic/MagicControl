#include "../src/gui/common/BatteryDetails.h"
#include "MagicControl/DisplayContinuity.h"
#include <iostream>
#include <stdexcept>
namespace {void check(bool v,const char* s){if(!v)throw std::runtime_error(s);}}
int main(){try{
    mc::BatteryAgingStatus confirmed;confirmed.authoritative=true;confirmed.enabled=true;confirmed.writable=true;confirmed.sampledAtMs=1000;confirmed.realHistory=9085;
    auto retained=mc::retainBatteryAgingDisplay({},confirmed,1500);
    check(retained.enabled==true&&retained.realHistory==9085&&retained.retainedForDisplay&&!retained.writable,"missing read retains selection, never permission");
    retained=mc::retainBatteryAgingDisplay({},retained,9000);
    check(retained.sampledAtMs==1000,"retention cannot refresh old timestamp");
    check(!mc::retainBatteryAgingDisplay({},retained,11001).enabled,"expired observation clears display");
    const auto expiredCopy=mc::retainBatteryAgingDisplay(retained,retained,11001);
    check(!expiredCopy.enabled&&!expiredCopy.writable,"copied stale input cannot bypass expiry or retain permission");
    auto off=confirmed;off.enabled=false;off.sampledAtMs=1600;
    check(mc::retainBatteryAgingDisplay(off,confirmed,1600).enabled==false,"confirmed Off is immediate");
    off.enabled.reset();off.error="confirmed mismatch";
    check(!mc::retainBatteryAgingDisplay(off,confirmed,1600).enabled,"confirmed mismatch cannot retain On");
    mc::EcBatteryDisplayCache cache;mc::EcBatteryReading initial;initial.coherent=true;initial.supported=true;initial.profileId="wu-jie-15-xpro";
    initial.aging=confirmed;initial.aging.sampledAtMs=mc::batteryMonotonicMs();cache.update(initial);
    auto missing=initial;missing.aging={};
    check(cache.update(missing).aging.enabled==true,"independent aging miss does not clear accepted battery state");
    missing.coherent=false;
    const auto oldBattery=cache.update(missing);
    check(oldBattery.aging.enabled==true&&!oldBattery.aging.writable&&oldBattery.aging.retainedForDisplay,"whole battery miss retains bounded selection without writes");
    mc::BatteryReading os;mc::EcBatteryReading ec;mc::BatteryPowerPolicy policy;
    ec.coherent=true;ec.battery.manufactured="2025-02-18";ec.battery.cycles=44;
    ec.aging.realHistory=9085;ec.aging.reading=mc::BatteryAgingReading{};ec.aging.reading->history=0;
    const auto rows=mc::gui::ui::primaryBatteryDetails(os,ec,policy);
    check(rows.size()==mc::gui::ui::primaryBatteryRows,"source row count matches layout");
    check(rows[5].value.starts_with("2025-02-18"),"manufacture date stays real");
    check(rows[6].label=="Термочасы"&&rows[6].value=="9085","card displays true accumulated history, not substituted zero");
    ec.aging={};check(mc::gui::ui::primaryBatteryDetails(os,ec,policy)[6].value=="—","unknown transport cannot invent thermal hours");
    for(const auto dpi:{96,192})for(const auto width:{640,800,1000}){
        mc::gui::ui::Scale s{dpi};auto g=mc::gui::ui::setsGeometry(s(width),s,{.height=s(24)});
        mc::gui::ui::placeBatteryIdentity(g,s,s(260));
        mc::gui::ui::placeCombinedBatteryControls(g,s,s(260));
        mc::gui::ui::placeCombinedBatteryStatus(g,s,s(120),s(260),s(30));
        check(g.agingCaption.left>=g.headings[0].right&&g.agingCaption.right<g.agingOff.left,"caption fits both DPI scales");
        check(g.agingOff.right==g.agingOn.left&&g.agingOn.right<=g.batteryVoltageSummary.left,"toggle remains separate from voltage values");
        std::array<std::array<int,6>,2> widths{};std::array<int,mc::gui::ui::primaryBatteryRows> details{};
        const auto geom=mc::gui::ui::batteryPrimaryRowsGeometry(g,s,widths,details);
        check(geom.details.labels[6].bottom<=g.batteryInfoCards[0].bottom,"thermal hours stay inside EC card");
    }
    std::cout<<"Battery card: real date/history, unknown source, 100% and 200% geometry passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
