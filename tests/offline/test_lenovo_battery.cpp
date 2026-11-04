#include "OfflineTest.h"
#include "MagicControl/SensorDecoders.h"
#include "src/protocols/lenovo/LegionWmi.h"
#include "src/gui/common/BatteryDetails.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <string_view>

using namespace mc;
namespace ui=mc::gui::ui;
namespace mc {std::optional<unsigned> batteryAgeMonthsLocal(std::string_view date) noexcept {CHECK(date.empty());return {};}}

namespace {
EcBatteryReading lenovo(){
    EcBatteryReading ec;ec.supported=true;ec.coherent=true;ec.profileId="lenovo-legion-kwcn";
    ec.battery.present=true;ec.battery.deviceId="EC/Lenovo-KWCN/BAT0";ec.battery.flow=BatteryFlow::Charging;
    ec.battery.milliamps=-2415;ec.battery.volts=16.156;ec.battery.watts=-39.017;
    ec.battery.percent=77;ec.battery.capacityUnit="mWh";ec.battery.fullCapacity=79590;
    ec.battery.designCapacity=80000;ec.battery.remainingCapacity=61640;
    return ec;
}
void signedCurrent(){
    CHECK(lenovoBatteryCurrent(std::nullopt)==std::nullopt);
    CHECK(lenovoBatteryCurrent(0)==0);
    CHECK(lenovoBatteryCurrent(0xf691)==-2415);
    CHECK(lenovoBatteryCurrent(0x0eb7)==3767);
    CHECK(lenovoBatteryCurrent(30000)==30000);
    CHECK(lenovoBatteryCurrent(static_cast<std::uint16_t>(65536-30000))==-30000);
    CHECK(!lenovoBatteryCurrent(30001));
    CHECK(!lenovoBatteryCurrent(static_cast<std::uint16_t>(65536-30001)));
}
void adapterCandidate(){
    using mc::lenovo::adapterWattCandidate;
    constexpr std::array<std::pair<std::uint8_t,std::uint16_t>,5> table{{{4,135},{5,170},{6,230},{7,300},{8,330}}};
    for(const auto [kind,watts]:table){
        for(const std::uint8_t gate:{0x01,0x02,0x10,0x13})
            CHECK(adapterWattCandidate(watts,kind,gate,0x88,0x88)==watts);
        CHECK(!adapterWattCandidate(static_cast<std::uint16_t>(watts+1),kind,1,0x88,0x88));
    }
    CHECK(!adapterWattCandidate(std::nullopt,7,1,0x88,0x88));
    CHECK(!adapterWattCandidate(300,std::nullopt,1,0x88,0x88));
    CHECK(!adapterWattCandidate(300,7,std::nullopt,0x88,0x88));
    CHECK(!adapterWattCandidate(300,7,1,std::nullopt,0x88));
    CHECK(!adapterWattCandidate(300,7,0,0x88,0x88));
    CHECK(!adapterWattCandidate(300,7,0xff,0x88,0x88));
    CHECK(!adapterWattCandidate(300,7,1,0xff,0xff));
    CHECK(!adapterWattCandidate(300,7,1,0x80,0x80));
    CHECK(!adapterWattCandidate(300,7,1,0x08,0x08));
    CHECK(!adapterWattCandidate(300,7,1,0x88,0x80));
    CHECK(!adapterWattCandidate(0,7,1,0x88,0x88));
    CHECK(!adapterWattCandidate(65535,7,1,0x88,0x88));
    CHECK(!adapterWattCandidate(300,0xff,1,0x88,0x88));
    CHECK(!adapterWattCandidate(300,3,1,0x88,0x88));
    CHECK(!adapterWattCandidate(300,9,1,0x88,0x88));
}
void adapterPresentation(){
    ui::setLanguage(0);
    BatteryReading os;os.onlineKnown=true;os.online=true;
    auto ec=lenovo();ec.adapterWattCandidate=300;ec.adapterCandidateAtMs=batteryMonotonicMs();
    const auto show=[&] {return ui::lenovoAdapterPowerSummary("lenovo-legion-kwcn",false,os,ec);};
    CHECK(show()&&show()->find("300")!=std::string::npos);
    CHECK(!ui::lenovoAdapterPowerSummary("mechrevo-gm6ir0c",false,os,ec));
    CHECK(!ui::lenovoAdapterPowerSummary("unknown",false,os,ec));
    const auto preview=ui::lenovoAdapterPowerSummary("lenovo-legion-kwcn",true,os,ec);
    CHECK(preview&&preview->find("300")==std::string::npos);
    auto old=ec.adapterCandidateAtMs;ec.adapterCandidateAtMs=0;CHECK(show()&&show()->find("300")==std::string::npos);
    ec.adapterCandidateAtMs=batteryMonotonicMs()+60000;CHECK(show()&&show()->find("300")==std::string::npos);
    if(old>16000){ec.adapterCandidateAtMs=old-16000;CHECK(show()&&show()->find("300")==std::string::npos);}
    ec.adapterCandidateAtMs=old;ec.retainedForDisplay=true;CHECK(show()&&show()->find("300")==std::string::npos);
    ec.retainedForDisplay=false;ec.battery.dynamicStale=true;CHECK(show()&&show()->find("300")==std::string::npos);
    ec.battery.dynamicStale=false;ec.coherent=false;CHECK(show()&&show()->find("300")==std::string::npos);
    ec.coherent=true;ec.profileId="mechrevo-gm6ir0c";CHECK(show()&&show()->find("300")==std::string::npos);
    ec.profileId="lenovo-legion-kwcn";os.dynamicStale=true;CHECK(!show());
    os.dynamicStale=false;os.online=false;CHECK(!show());
    os.online=true;os.onlineKnown=false;CHECK(!show());
    os.onlineKnown=true;ec.adapterWattCandidate.reset();CHECK(show()&&show()->find("300")==std::string::npos);
    ec.adapterWattCandidate=300;
    for(unsigned language=0;language<4;++language){ui::setLanguage(language);const auto text=show();CHECK(text&&text->find("300")!=std::string::npos&&text->find("{power}")==std::string::npos);}
    ui::setLanguage(0);
}
void stateAndDirection(){
    auto ec=lenovo();BatteryReading os;os.flow=BatteryFlow::Charging;os.watts=0;os.milliamps=0;
    auto rows=ui::primaryBatteryDetails(os,ec,{});
    // Both adapters index fixed-size geometry arrays by every produced row.
    // Lenovo must never exceed that bound or index a third source column.
    CHECK(rows.size()==ui::primaryBatteryRows);
    auto metrics=ui::batteryCurrentMetrics(ec.battery);
    CHECK(metrics[1].label=="Статус EC"&&metrics[1].value.find("Зарядка")!=std::string::npos);
    CHECK(metrics[5].label=="Направление тока"&&metrics[5].value.find("Разряд")!=std::string::npos);
    CHECK(ec.battery.watts&&*ec.battery.watts<0&&os.watts&&*os.watts==0);
    ec.battery.milliamps=0;metrics=ui::batteryCurrentMetrics(ec.battery);CHECK(metrics[5].value.find("Нулевой ток")!=std::string::npos);
    ec.battery.milliamps.reset();metrics=ui::batteryCurrentMetrics(ec.battery);CHECK(metrics[5].value.find("Неизвестно")!=std::string::npos);
    ec.battery.deviceId="EC/GM6/BAT0";metrics=ui::batteryCurrentMetrics(ec.battery);
    CHECK(metrics[1].label!="Статус EC"&&metrics[5].label!="Направление тока");
    metrics=ui::batteryCurrentMetrics(os);CHECK(metrics[1].label!="Статус EC"&&metrics[5].label!="Направление тока");
    const auto preview=ui::batteryCurrentMetrics(BatteryReading{},true);
    CHECK(preview[1].label=="Статус EC"&&preview[5].label=="Направление тока");
    CHECK(preview[1].value.find("Неизвестно")!=std::string::npos&&preview[5].value.find("Неизвестно")!=std::string::npos);
    ec.profileId="mechrevo-gm6ir0c";rows=ui::primaryBatteryDetails(os,ec,{});CHECK(rows.size()==ui::primaryBatteryRows);
}
void contradictoryChargeEstimate(){
    auto ec=lenovo();
    // 02.11 client evidence: charging flag but -2415mA / -39W and falling SOC.
    // Never claim time-to-full from magnitudes when direction contradicts flag.
    CHECK(ui::ecBatteryTimes(ec)[1].value=="—");
    ec.battery.watts=39.017;CHECK(ui::ecBatteryTimes(ec)[1].value=="—");
    ec.battery.milliamps=2415;ec.battery.watts=-39.017;CHECK(ui::ecBatteryTimes(ec)[1].value=="—");
    ec.battery.milliamps=2415;ec.battery.watts=39.017;
    CHECK(ui::ecBatteryTimes(ec)[1].value!="—");
    ec.battery.flow=BatteryFlow::Discharging;ec.battery.milliamps=-2415;ec.battery.watts=-39.017;
    CHECK(ui::ecBatteryTimes(ec)[0].value!="—");
    ec.battery.milliamps=2415;ec.battery.watts=39.017;
    CHECK(ui::ecBatteryTimes(ec)[0].value=="—");
}
void chargeModePresentation(){
    ui::setLanguage(0);auto ec=lenovo();BatteryReading os;
    const auto shown=[&]{const auto rows=ui::primaryBatteryDetails(os,ec,{});
        const auto it=std::find_if(rows.begin(),rows.end(),[](const auto& row){return row.source=="EC"&&row.label=="Режим зарядки";});
        CHECK(it!=rows.end());return it->value;};
    ec.conservationMode=false;ec.rapidChargeMode=false;CHECK(shown()=="Normal");
    ec.conservationMode=true;CHECK(shown()=="Conservation");
    ec.conservationMode=false;ec.rapidChargeMode=true;CHECK(shown()=="Rapid");
    ec.conservationMode=true;CHECK(shown()=="Конфликт режимов");
    ec.conservationMode.reset();CHECK(shown()=="—");
    ec.conservationMode=false;ec.rapidChargeMode.reset();CHECK(shown()=="—");
    ec.rapidChargeMode=false;ec.coherent=false;CHECK(shown()=="—");
}
}
int main(){signedCurrent();adapterCandidate();adapterPresentation();stateAndDirection();contradictoryChargeEstimate();chargeModePresentation();std::printf("Lenovo battery offline: %u checks; no hardware\n",offlineChecks);}
