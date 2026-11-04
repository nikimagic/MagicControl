#include "src/protocols/lenovo/LegionFirmware.h"
#include "src/protocols/lenovo/LegionWmi.h"
#include "src/gui/common/Geometry.h"
#include "MagicControl/BiosSupport.h"
#include "MagicControl/DiagnosticDump.h"
#include <iostream>
#include <numeric>
namespace mc::devices {const DeviceProfile& legionKwcnProfile()noexcept;}
namespace {
unsigned checks{};
void check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
template<class F>void refuses(F action){bool refused{};try{action();}catch(const std::exception&){refused=true;}check(refused,"invalid payload accepted");}
mc::lenovo::Row range(std::uint32_t id,int minimum,int maximum,int step=1){return {{"IDs",{id}},{"_flags",{7}},{"DefaultValue",{56}},{"Step",{static_cast<std::uint32_t>(step)}},{"MinValue",{static_cast<std::uint32_t>(minimum)}},{"MaxValue",{static_cast<std::uint32_t>(maximum)}}};}
struct Firmware {
    int flag{},powerMode{255};bool ignoreWrite{},failWrite{};
    mc::lenovo::FanPacket manual{};std::vector<std::pair<std::uint32_t,int>> writes;
    int feature(std::uint32_t id){check(id==mc::lenovo::fanFullSpeedId,"Max read a non-FAN feature");return flag;}
    void feature(std::uint32_t id,int value){check(id==mc::lenovo::fanFullSpeedId,"Max changed a power/curve feature");writes.emplace_back(id,value);
        if(failWrite)throw std::runtime_error("firmware refused flag");if(!ignoreWrite)flag=value;}
};
}
int main(){try{
    using namespace mc;using namespace mc::lenovo;
    check(effectiveDiagnosticDumpMode(profileId,DiagnosticDumpMode::WithFanProbes)==DiagnosticDumpMode::WithFanProbes,"Lenovo dump lost the shared native Max phase");
    check(effectiveDiagnosticDumpMode(profileId,DiagnosticDumpMode::ReadOnly)==DiagnosticDumpMode::ReadOnly,"Lenovo read-only policy lost");
    check(effectiveDiagnosticDumpMode("minisforum-hpbsd",DiagnosticDumpMode::WithFanProbes)==DiagnosticDumpMode::ReadOnly,"HPBSD passive dump regressed");
    for(const auto profile:{"wu-jie-15-xpro","mechrevo-gm6ir0c","huawei-hlyl-wxx9","unknown"}){
        check(effectiveDiagnosticDumpMode(profile,DiagnosticDumpMode::WithFanProbes)==DiagnosticDumpMode::WithFanProbes,"another device probe policy changed");
        check(effectiveDiagnosticDumpMode(profile,DiagnosticDumpMode::ReadOnly)==DiagnosticDumpMode::ReadOnly,"another device read-only policy changed");
    }
    for(int mode:{1,2,3,255}){
        Firmware firmware;firmware.powerMode=mode;firmware.manual.fill(0x5a);const auto original=firmware.manual;
        check(setFullSpeedFlag(firmware,true)&&firmware.flag==1,"native Max flag not enabled");
        check(firmware.writes==std::vector<std::pair<std::uint32_t,int>>{{0x04020000,1}},"native Max wrote more than FNST");
        check(!setFullSpeedFlag(firmware,true)&&firmware.writes.size()==1,"repeated Max rewrote the flag");
        check(setFullSpeedFlag(firmware,false)&&!readFullSpeedFlag(firmware),"Default/Manual did not clear Full Speed");
        check(firmware.powerMode==mode&&firmware.manual==original,"Max changed mode or Manual bytes");
    }
    {Firmware firmware;firmware.flag=2;refuses([&]{setFullSpeedFlag(firmware,true);});check(firmware.writes.empty(),"invalid flag allowed a write");}
    {Firmware firmware;firmware.ignoreWrite=true;refuses([&]{setFullSpeedFlag(firmware,true);});check(firmware.writes.size()==1,"ignored native flag retried/fell back to another writer");}
    {Firmware firmware;firmware.failWrite=true;refuses([&]{setFullSpeedFlag(firmware,true);});check(firmware.writes.size()==1,"failed native Max fell back to Manual");}
    // Sanitized FAN table only, captured from owner0.1.435; no full/private RAM.
    FanPacket live{0,0,5,5,59,0,57,0,40,0,0,0,0,0,0,0,17,17,5,5,59,56,57,54,45,35,0,0,0,0,0,0,19,18,5,5,59,56,57,54,50,40,0,0,0,0,0,0,21,21,5,5,62,56,57,54,127,45,0,0,0,0,0,0,22,23,2,2,65,59,61,54,127,127,0,0,0,0,0,0,26,26,2,2,65,61,65,57,127,127,0,0,0,0,0,0,28,28,2,2,75,61,72,61,127,127,0,0,0,0,0,0,33,33,2,2,77,70,72,68,127,127,0,0,0,0,0,0,35,36,2,2,85,73,80,69,127,127,0,0,0,0,0,0,44,44,2,2,92,80,83,77,127,127,0,0,0,0,0,0,51,51,2,2,127,87,127,80,127,127,0,0,0,0,0,0};
    check(factoryPacketKnown(6,11,live,7,3),"real Default rejected after E63C masked copy");
    auto changed=live;changed[4]++;check(!factoryPacketKnown(6,11,changed,7,3),"changed active temperature admitted");
    changed=live;changed[10]=127;check(!factoryPacketKnown(6,11,changed,7,3),"nonzero disabled column admitted");
    Row lower{{"value",{42}}};check(scalar(lower,"Value")==42,"WMI property case changed meaning");
    Row named=range(0x0106ff00,20,110);named.erase("_flags");named["capability"]={1};
    check(!Catalog::decode({named},{}).setting(cpuIds[0],255,"","","").accepts(50),"Capability fallback granted readonly writes");
    named["capability"]={7};check(Catalog::decode({named},{}).setting(cpuIds[0],255,"","","").accepts(50),"verified named capability rejected");

    const auto& profile=devices::legionKwcnProfile();check(profile.powerOptions.size()==6,"preview omitted declared GPU/Tau/temperature fields");
    check(profile.fanPointCounts==std::array<int,2>{10,10},"Manual point count lost");
    for(const auto& p:profile.powerOptions)check(!p.key.empty()&&!p.label.empty()&&!p.unit.empty(),"preview field descriptor incomplete");
    std::array<int,10> map{};map.fill(12);check(activeLevelMapValid(map),"factory map index12 rejected");map[9]=255;check(!activeLevelMapValid(map),"active sentinel map accepted");
    BoardDescriptor board;board.manufacturer="LENOVO";board.baseBoard="LNVNB161216";board.productName="82WK";
    check(std::wstring_view(biosSupportLinks(board).supportUrl).find(L"82wk/downloads")!=std::wstring_view::npos,"82WK support link guessed from marketing name");
    board.productName="82WQ";check(std::wstring_view(biosSupportLinks(board).supportUrl).find(L"82wq/downloads")!=std::wstring_view::npos,"82WQ support link missing");
    std::vector<Row> ranges{range(0x0106ff00,20,110),range(0x0107ff00,0,0,0)};
    auto catalog=Catalog::decode(ranges,{{{"IDs",{0x0107ff00}},{"Value",{56}}},{{"IDs",{0x0107ff00}},{"Value",{32}}}});
    const auto cross=catalog.setting(cpuIds[0],255,"cpu.cross","Cross Loading","W");
    check(cross.accepts(20)&&cross.accepts(110)&&!cross.accepts(19)&&!cross.accepts(111),"live WMI bounds lost");
    const auto tau=catalog.setting(0x01070000,255,"cpu.tau","PL1 Tau","s");
    check(tau.accepts(32)&&tau.accepts(56)&&!tau.accepts(33),"discrete Tau became a guessed numeric range");
    refuses([&]{catalog.setting(0x01060000,3,"","","");});
    ranges.push_back(ranges[0]);refuses([&]{Catalog::decode(ranges,{});});
    ranges.pop_back();ranges[0]["_flags"]={1};
    check(!Catalog::decode(ranges,{}).setting(cpuIds[0],255,"","","").accepts(50),"readonly WMI row became writable");
    NativeFanCurves curves;for(auto& c:curves)for(auto& p:c)p={255,100};
    for(int i=0;i<10;++i)curves[0][i]=curves[1][i]={20+8*i,5+10*i};
    check(curvePoints(curves)==10,"ten active points lost");
    FanPacket original{};for(std::size_t i=0;i<original.size();++i)original[i]=static_cast<std::uint8_t>(i%128);
    const FanBounds rpm{{1700,1800},{7100,6900}};
    const auto encoded=encodeSharedCurve(curves,original,13,rpm);
    for(int i=0;i<11;++i){check(encoded[i*16+2]==original[i*16+2]&&encoded[i*16+3]==original[i*16+3],"ramp fields changed");
        for(int j=8;j<16;++j)check(encoded[i*16+j]==original[i*16+j],"other thermal input changed");
        for(int ch=0;ch<2;++ch)check(encoded[i*16+ch]*100>=rpm.minimumRpm[ch]&&encoded[i*16+ch]*100<=rpm.maximumRpm[ch],"RPM quantization escaped WMI limits");}
    check(encoded[10*16]==71&&encoded[10*16+1]==69&&encoded[10*16+4]==127,"last safety band lost");
    for(std::size_t prefix=0;prefix<=original.size();++prefix){auto partial=original;std::copy_n(encoded.begin(),prefix,partial.begin());check(transitionPrefix(original,encoded,partial),"apply prefix not recoverable");
        partial=encoded;std::copy_n(original.begin(),prefix,partial.begin());check(transitionPrefix(encoded,original,partial),"Default prefix not recoverable");}
    auto foreign=original;foreign[0]=static_cast<std::uint8_t>(encoded[0]+1);if(foreign[0]==original[0])++foreign[0];check(!transitionPrefix(original,encoded,foreign),"foreign packet admitted for rollback");
    for(int channel=0;channel<2;++channel){auto bad=curves;bad[channel][0].temperature=19;refuses([&]{curvePoints(bad);});bad=curves;bad[channel][0].duty=4;refuses([&]{curvePoints(bad);});}
    curves[0][9]=curves[1][9]={100,100};check(curvePoints(curves)==10,"inclusive input maximum rejected");
    auto unequal=curves;unequal[1][3].duty++;refuses([&]{encodeSharedCurve(unequal,original,13,rpm);});
    auto hole=curves;hole[0][3]=hole[1][3]={255,100};refuses([&]{curvePoints(hole);});
    check(!factoryPacketKnown(0,4,original),"arbitrary RAM accepted as factory table");
    for(int dpi:{96,120,144,192,240})for(int logicalWidth:{900,1200}){
        namespace ui=mc::gui::ui;ui::Scale scale{dpi};const int width=scale(logicalWidth);
        auto old=ui::setsGeometry(width,scale,{},3,true,true,false);
        auto extended=ui::setsGeometry(width,scale,{},3,true,true,false,{}, {},4,{ui::ecBatteryDetailRows,ui::osBatteryDetailRows},3,2,{true,true,true},2);
        ui::placePowerFields(extended,scale,{scale(110),scale(50),scale(50)},scale(24));
        ui::placePowerOptionFields(extended,scale,{scale(115),scale(50),scale(120),scale(65),scale(90),scale(90)},scale(24),2);
        check(extended.cards[1].height()==old.cards[1].height()+scale(64),"new rows changed unrelated geometry");
        for(int i=0;i<6;++i){check(extended.powerOptionCaptions[i].left>=extended.cards[1].left,"new caption outside card");
            check(extended.powerOptionSpins[i].right<extended.cards[1].right,"new editor outside card");
            check(extended.powerOptionEdits[i].bottom<extended.apuDiagnostics[2].top,"new editor overlaps diagnostics");}
        check(old.cards[2].top+scale(64)==extended.cards[2].top,"new power rows overlap next card");
    }
    std::cout<<"Legion pure protocol: "<<checks<<" checks; no OS/hardware\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
