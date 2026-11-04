#include "src/gui/common/KeyboardBrightness.h"
#include "src/gui/common/KeysPolicy.h"
#include "OfflineTest.h"
#include "src/gui/windows/SettingsCardsModel.h"
#include "src/gui/linux/SettingsCardsModel.h"
#include "MagicControl/GpuPresentation.h"
#include <array>
#include <algorithm>
namespace mc::devices {
const DeviceProfile& x5sp4nagProfile() noexcept;
const DeviceProfile& gm6ir0cProfile() noexcept;
const DeviceProfile& hlylProfile() noexcept;
const DeviceProfile& hpbsdProfile() noexcept;
const DeviceProfile& hydroc16g2Profile() noexcept;
const DeviceProfile& unknownProfile() noexcept;
}
using namespace mc;
namespace ui=mc::gui::ui;
// These card fixtures have no manufacture dates. Do not link the process
// history/file writer or query a real clock merely to satisfy this boundary.
namespace mc {std::optional<unsigned> batteryAgeMonthsLocal(std::string_view date) noexcept {CHECK(date.empty());return {};}}
std::string source(std::string_view value){return value=="Windows"||value=="Linux"||value=="OS"?"OS":std::string(value);}
std::string label(std::string value){return value==ui::translate("Ток (расчёт W / V)")?ui::translate("Ток батареи"):value;}
void compare(const std::vector<ui::BatteryDetailRow>& win,const std::vector<ui::BatteryDetailRow>& linuxRows){
    CHECK(win.size()==linuxRows.size());for(std::size_t i=0;i<win.size();++i){if(label(win[i].label)!=label(linuxRows[i].label))std::fprintf(stderr,"row %zu: [%s] != [%s]\n",i,win[i].label.c_str(),linuxRows[i].label.c_str());CHECK(label(win[i].label)==label(linuxRows[i].label));CHECK(source(win[i].source)==source(linuxRows[i].source));}
}
int main(){
    for(unsigned language=0;language<4;++language){
        ui::setLanguage(language);
        const auto targets=ui::batteryVoltageTargets(17800,17000);
        CHECK(targets.find(ui::displayNumber(17.8,2))!=std::string::npos);
        CHECK(targets.find(ui::displayNumber(17.0,2))!=std::string::npos);
        CHECK(targets.find(ui::displayNumber(0.8,2))==std::string::npos);
        CHECK(ui::batteryVoltageTargets({},{})==ui::translate("Зарядное напряжение: данные не прочитаны"));
    }
    ui::setLanguage(0);
    {
        const auto& profile=devices::hpbsdProfile();BatteryReading os;os.present=false;
        CHECK(profile.fanPointCounts==std::array<int,2>{8,3});
        CHECK(devices::x5sp4nagProfile().fanPointCounts==std::array<int,2>{16,16});
        CHECK(devices::unknownProfile().fanPointCounts==std::array<int,2>{16,16});
        CHECK(devices::hlylProfile().fanPointCounts==std::array<int,2>{10,10});
        const auto absent=ui::settingsCardsModel(profile,os,{}, {},true);
        CHECK(!absent.visible[0]&&absent.visible[1]&&!absent.visible[2]&&!absent.showDiagnostics);
        os.present=true;
        CHECK(ui::settingsCardsModel(profile,os,{}, {},false).visible[0]);
        CHECK(ui::settingsCardsModel(devices::unknownProfile(),{}, {}, {},false).visible==std::array<bool,3>{true,true,true});
        BatteryReading missing;missing.present=false;
        CHECK(!ui::settingsCardsModel(devices::unknownProfile(),missing,{}, {},false).visible[0]);
        missing.dynamicStale=true;
        CHECK(ui::settingsCardsModel(devices::unknownProfile(),missing,{}, {},false).visible[0]);
        EcBatteryReading present;present.coherent=true;present.battery.present=true;
        missing.dynamicStale=false;
        CHECK(ui::settingsCardsModel(devices::unknownProfile(),missing,present,{},false).visible[0]);
    }
    // User-requested KEYs model audit: no page, backend, microphone or EC instance.
    const std::array<std::string_view,18> keyProfiles{
        "eluktronics-hydroc16g2","huawei-hlyl-wxx9","machenike-l16",
        "mechrevo-canglong-series-x6xr55xy","mechrevo-canglong-series-x8xr55xp-b2",
        "mechrevo-gm5hg0a","mechrevo-gm6bg0x","mechrevo-gm6ir0c","mechrevo-gm6ix9b",
        "mechrevo-gm6px7x","mechrevo-gx6hg0u-f","mechrevo-jiaolong-series-x6dr55xx-b2",
        "mechrevo-kuangshi-jiguang-series-x6xx55xw-b2","mechrevo-kuangshi-jiguang-series-x6xx57tw",
        "wu-jie-15-xpro","mechrevo-yaoshi-series-x6ar55xy","unknown","noname"};
    CHECK(ui::keyRowCount==11&&ui::keyQuickRowCount==6&&ui::microphoneRow==10);
    CHECK(ui::keysBackend("wu-jie-15-xpro",static_cast<ui::KeyAction>(-1),false)==ui::KeysBackend::Unavailable);
    CHECK(ui::keysBackend("wu-jie-15-xpro",static_cast<ui::KeyAction>(11),false)==ui::KeysBackend::Unavailable);
    CHECK(std::string_view(ui::keyTitles[ui::microphoneRow])=="Микрофон");
    for(const auto profile:keyProfiles)for(bool linuxHost:{false,true})for(int row=0;row<static_cast<int>(ui::keyRowCount);++row){
        using B=ui::KeysBackend;using A=ui::KeyAction;const auto action=static_cast<A>(row);
        B expected=B::Unavailable;
        if(row==1)expected=B::Preferences;
        else if(!linuxHost&&row==5)expected=B::Firmware;
        else if(row==10||(!linuxHost&&row>=6&&row<=9))expected=B::System;
        else if(profile=="huawei-hlyl-wxx9"&&row==2)expected=B::HuaweiHwmi;
        else if(!linuxHost&&row<6&&(profile=="wu-jie-15-xpro"||profile=="mechrevo-gm6ir0c"||
            profile=="mechrevo-gm5hg0a"||profile=="machenike-l16"||profile=="eluktronics-hydroc16g2"))expected=B::UniwillEc;
        CHECK(ui::keysBackend(profile,action,linuxHost)==expected);
        // Catalog preview keeps current-computer OS/preferences controls;
        // it never admits the selected model's EC/HWMI commands.
        CHECK(ui::keysBackend(profile,action,linuxHost,true)==
            ((expected==B::System||expected==B::Preferences)?expected:B::Unavailable));
    }
    for(int dpi:{96,144,192}){
        const auto g=ui::keysGeometry(1000*dpi/96,600*dpi/96,{dpi});
        const ui::Scale scale{dpi};
        const auto fans=ui::fansGeometry(1000*dpi/96,scale);
        CHECK(g.quickCard.top==fans.fans[0].top);
        CHECK(g.quickHeading.top==g.quickCard.top+scale(10));
        for(int statusHeight:{0,24,96}){
            const auto info=ui::biosHeaderGeometry(1000*dpi/96,600*dpi/96,
                scale(statusHeight),scale(statusHeight),scale);
            CHECK(info.card.top==g.quickCard.top);
            CHECK(info.heading.top==info.card.top+scale(8));
            CHECK(info.heading.bottom<=info.buttons[0].top);
            CHECK(info.buttons[5].right<info.card.right);
            CHECK(info.buttons[4].bottom<info.card.bottom);
            CHECK(info.inventory.top>info.card.bottom);
        }
        CHECK(g.rows.size()==ui::keyRowCount&&g.off.size()==g.rows.size());
        for(std::size_t row=0;row<ui::keyRowCount;++row){
            CHECK(g.off[row].top>=g.rows[row].top&&g.off[row].bottom<=g.rows[row].bottom);
            CHECK(g.on[row].left==g.off[row].right);
            if(row)CHECK(g.rows[row].top>=g.rows[row-1].bottom);
        }
        CHECK(g.rows[ui::microphoneRow].bottom<g.devicesCard.bottom);
        CHECK(g.result.top>=g.devicesCard.bottom);
    }
    GpuPresentationSnapshot gpus;GpuCardReading integrated,discrete;
    integrated.adapterId="intel";integrated.usagePercent=36;integrated.type=GpuType::Integrated;
    discrete.adapterId="nvidia";discrete.usagePercent=100;discrete.type=GpuType::Discrete;
    gpus.cards={integrated,discrete};std::string reason;
    CHECK(gpuLoadCard(gpus,reason)->adapterId=="nvidia");
    gpus.cards[1].usagePercent.reset();CHECK(gpuLoadCard(gpus,reason)->adapterId=="intel");
    gpus.cards[0].usagePercent=0;gpus.cards[1].usagePercent=0;CHECK(gpuLoadCard(gpus,reason)->adapterId=="nvidia");
    gpus.cards[1].usagePercent=101;CHECK(gpuLoadCard(gpus,reason)->adapterId=="intel");
    gpus.cards[0].usagePercent.reset();gpus.cards[1].usagePercent.reset();CHECK(gpuLoadCard(gpus,reason)->adapterId=="nvidia");
    CHECK(reason.find("недоступна")!=std::string::npos);
    const std::array profiles{&devices::x5sp4nagProfile(),&devices::gm6ir0cProfile(),&devices::hlylProfile(),&devices::hydroc16g2Profile(),&devices::unknownProfile()};
    const std::array maxima{4,4,2,4,0};
    const std::array<std::string,14> primary{"Полная / заводская ёмкость","Здоровье","До разряда","До полной зарядки","До лимита зарядки","Дата изготовления","Термочасы","Полная / заводская ёмкость","Здоровье","План питания","Предел частоты: БП / батарея","Приоритет экономии: БП / батарея","Дата изготовления",""};
    CHECK(std::string(ui::keyboardBrightnessDefaultCaption)=="Default");CHECK(ui::keyboardBrightnessButtonCaption(0)=="Выкл.");
    for(int level=1;level<=4;++level)CHECK(ui::keyboardBrightnessButtonCaption(level)==std::to_string(level));
    unsigned cases{};
    for(unsigned language=0;language<4;++language)for(std::size_t profile=0;profile<profiles.size();++profile)for(bool logging:{false,true})for(bool available:{false,true}){
        ui::setLanguage(language);++cases;
        BatteryReading os;os.source="Windows · Battery Class IOCTL";os.deviceId="offline/OS";os.percent=99;os.watts=30;os.volts=16;
        os.designCapacity=99072;os.fullCapacity=99072;os.remainingCapacity=50000;os.capacityUnit="mWh";os.healthPercent=100;
        auto linuxOs=os;linuxOs.source="Linux · power_supply";
        EcBatteryReading ec;ec.supported=ec.coherent=available;
        if(available){ec.battery.source="EC";ec.battery.percent=77;ec.battery.milliamps=1000;ec.battery.volts=16;ec.battery.watts=16;ec.battery.flow=BatteryFlow::Charging;ec.designMah=6400;ec.fullMah=5800;ec.remainingMah=4466;ec.upperRaw=85;}
        BatteryPowerPolicy policy;policy.applicable=true;policy.schemeName="offline scheme";policy.frequencyMHz.dc=2900;policy.epp.ac=20;policy.epp.dc=80;
        const auto w=gui::windowsSettingsCards(*profiles[profile],os,ec,policy,logging);
        const auto l=gtkui::linuxSettingsCards(*profiles[profile],linuxOs,ec,policy,logging);
        CHECK(w.order==std::array<int,3>{1,2,0}&&l.order==w.order);
        CHECK(w.brightnessMaximum==maxima[profile]&&l.brightnessMaximum==w.brightnessMaximum);
        CHECK(w.sources[0]=="EC"&&l.sources[0]=="EC"&&w.sources[1]=="Windows"&&l.sources[1]=="Linux");
        CHECK(w.primary.size()==primary.size());for(std::size_t i=0;i<primary.size();++i)CHECK(w.primary[i].label==primary[i]);
        compare(w.primary,l.primary);CHECK(!w.showDiagnostics&&!l.showDiagnostics);
        CHECK(w.current[0].percent==(available?std::optional<double>(77):std::nullopt));CHECK(w.current[1].percent==99);
        CHECK(w.current[0].source==l.current[0].source); // never OS->EC fallback
        if(w.showDiagnostics){
            CHECK(w.diagnostics[0].size()==(profile==1?63:104)&&w.diagnostics[1].size()==27);
            compare(w.diagnostics[0],l.diagnostics[0]);compare(w.diagnostics[1],l.diagnostics[1]);
            for(const auto& row:w.diagnostics[0])CHECK(row.source=="EC");
            for(const auto& row:w.diagnostics[1])CHECK(row.source!="EC");
            if(!available)CHECK(w.diagnostics[0][0].value.find("99")==std::string::npos);
        }else for(const auto& column:w.diagnostics)CHECK(column.empty());
        for(int dpi:{96,144,192}){
            const auto g=ui::setsGeometry(1200*dpi/96,{dpi},{20*dpi/96,15*dpi/96},3,true,true,w.showDiagnostics,{},{},w.brightnessMaximum);
            CHECK(g.cards[1].top<g.cards[2].top&&g.cards[2].top<g.cards[0].top);
            CHECK(g.batteryComparison.height()==0);
            CHECK(g.nativeButton.width()>0);const auto maximum=w.brightnessMaximum;
            for(int level=0;level<5;++level)CHECK((g.brightnessLevels[level].width()>0)==(level<=maximum));
            CHECK(g.nativeButton.right<g.brightnessLevels[0].left);CHECK(g.brightnessLevels[maximum].right<g.cards[2].right);
        }
    }
    ui::setLanguage(0);
    EcBatteryReading ec;ec.coherent=true;ec.designMah=0;ec.fullMah=5800;BatteryReading os;
    auto rows=ui::primaryBatteryDetails(os,ec,{});CHECK(rows[1].value.find("100")==std::string::npos);
    os.capacityRelative=true;os.capacityUnit="mWh";os.fullCapacity=100;os.designCapacity=200;
    CHECK(ui::primaryBatteryDetails(os,ec,{})[7].value.find("Вт")==std::string::npos);
    std::printf("SETs adapters: %u cases; KEYs18profiles/2OS/11rows + preview and DPI; %u checks; no GUI or platform library\n",cases,offlineChecks);
}
