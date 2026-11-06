#include "../../src/protocols/uniwill/gm6ir0c/NativeDefaultFan.h"
#include "../../src/gui/common/DefaultFanGraph.h"
#include "../TestBoardFixture.h"
#include "MagicControl/BoardDescriptor.h"
#include "MagicControl/DeviceProfile.h"
#include "MagicControl/EcAccess.h"
#include "MagicControl/Logging.h"
#include "MagicControl/Monitoring.h"
#include "MagicControl/NativeDefaultFanCache.h"
#include "MagicControl/PortableSettings.h"
#include "MagicControl/ResourceCatalog.h"
#include <array>
#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace mc {std::string executableDirectory(){throw std::runtime_error("unexpected test storage access");}}
namespace {
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
std::uint64_t now(){return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
mc::gm6::DefaultFanContext baseline(){
    mc::gm6::DefaultFanContext c;
    c.context={0x4c,25,0x18,1,1,0,0,0,0,0,0,0,1,0,0,0,0,2};
    c.selectors={0x5d,0x48,0,0};c.temperature={55,42};c.request={60,40};c.pwm={66,50};return c;
}
double point(const mc::NativeDefaultFanChannel& channel,int temperature){
    for(const auto& p:channel.points)if(p.temperature==temperature)return p.duty;
    throw std::runtime_error("temperature absent from graph");
}
struct Fake final:mc::IEcBackend {
    mc::BackendInfo metadata{"windows-acpi-driver","fake",mc::EcTransport::WindowsAcpiDriver,65535,true,false};
    std::array<std::uint8_t,65536> ram{};
    std::array<std::uint8_t,4> selector{0x5d,0x48,0,0};
    bool selectorUnavailable{},changeSelector{};int selectorReads{},writes{};
    const mc::BackendInfo& info()const noexcept override{return metadata;}
    std::uint8_t read(std::uint16_t address)override{return ram[address];}
    void write(std::uint16_t,std::uint8_t)override{++writes;throw std::runtime_error("no writes allowed");}
    std::optional<std::array<std::uint8_t,4>> readNativeFanFirmwareSelectors()override{
        ++selectorReads;if(selectorUnavailable)return {};auto value=selector;
        if(changeSelector&&selectorReads>1)value[1]=0x60;
        return value;
    }
};
constexpr std::array<std::uint16_t,18> addresses{
    0x454,0x740,0x74c,0x7ca,0x6e6,0x466,0x4fe,0x6c2,0x751,
    0x759,0x75a,0x768,0x741,0x7c6,0x7a4,0x7c7,0x7c8,0x49f};
void put(Fake& f,const mc::gm6::DefaultFanContext& c){
    for(std::size_t i=0;i<addresses.size();++i)f.ram[addresses[i]]=c.context[i];
    f.ram[0x43e]=c.temperature[0];f.ram[0x44f]=c.temperature[1];
    f.ram[0x461]=c.request[0];f.ram[0x469]=c.request[1];
    f.ram[0x75b]=c.pwm[0];f.ram[0x75c]=c.pwm[1];f.selector=c.selectors;
}
}
int main(){try{
    mc::setLoggingEnabled(false);mc::setBatteryLoggingEnabled(false);
    using O=mc::NativeDefaultFanObservation;
    auto c=baseline();
    const auto game=mc::gm6::decodeFirmwareDefaultFans("mechrevo-gm6ir0c",c,now());
    check(game.observation==O::Available&&game.fans,"validated Gaming base is available");
    check(game.fans->channels[0].table==0x52f8&&game.fans->channels[1].table==0x5838,"group 5D48 Gaming table pair");
    check(point(game.fans->channels[0],54)==0&&point(game.fans->channels[0],55)==25&&point(game.fans->channels[0],82)==90,"CPU curve uses strict threshold and raw/2");
    check(point(game.fans->channels[1],56)==0&&point(game.fans->channels[1],57)==30,"GPU duplicate start threshold handled");
    check(game.fans->channels[0].temperatureAddress==0x43e&&game.fans->channels[1].temperatureAddress==0x44f&&
          game.fans->channels[0].temperatureCelsius==55&&game.fans->channels[1].temperatureCelsius==42,"each fan keeps its EC temperature");
    check(game.fans->channels[0].requestedPercent==30&&game.fans->actualPercent[0]==33&&
          game.fans->channels[1].requestedPercent==20&&game.fans->actualPercent[1]==25&&
          game.fans->channels[0].requestPreOverride,"base requests and physical PWM remain separate");
    c.selectors={0x5d,0x60,0x15,0};auto variant=mc::gm6::decodeFirmwareDefaultFans("mechrevo-gm6ir0c",c,now());
    check(variant.fans&&variant.fans->channels[0].table==0x5358&&variant.fans->channels[1].table==0x5898&&
          point(variant.fans->channels[0],82)==85,"5D60 variant selects its own lower top step");
    for(const auto selectors:{std::array<std::uint8_t,4>{0x5d,0x48,0,0},std::array<std::uint8_t,4>{0x5d,0x60,0x15,0}}){
        c.selectors=selectors;c.context[8]=0x80;const auto office=mc::gm6::decodeFirmwareDefaultFans("mechrevo-gm6ir0c",c,now());
        check(office.fans&&office.fans->channels[0].table==0x5388&&office.fans->channels[1].table==0x58c8&&
              point(office.fans->channels[0],100)==50,"Office base is the same in both groups");
        c.context[8]=0x10;const auto turbo=mc::gm6::decodeFirmwareDefaultFans("mechrevo-gm6ir0c",c,now());
        check(turbo.fans&&turbo.fans->channels[0].table==(selectors[1]==0x48?0x52c8:0x5328),"Turbo selects its actual group");
        check(point(turbo.fans->channels[0],85)==(selectors[1]==0x48?95:90),"Turbo group maxima differ");
    }
    c=baseline();c.selectors={0xff,0xff,0xff,0xff};
    check(mc::gm6::decodeFirmwareDefaultFans("mechrevo-gm6ir0c",c,now()).observation==O::Unavailable,"FF selector stays unknown");
    c=baseline();c.selectors[2]=0xff;
    check(mc::gm6::decodeFirmwareDefaultFans("mechrevo-gm6ir0c",c,now()).observation==O::Unavailable,"FF variant is not treated as group 5D48");
    c=baseline();c.selectors[3]=0xff;
    check(mc::gm6::decodeFirmwareDefaultFans("mechrevo-gm6ir0c",c,now()).observation==O::Unavailable,"FF override status is unknown");
    c=baseline();c.selectors={0x5d,0x60,0,0};
    check(mc::gm6::decodeFirmwareDefaultFans("mechrevo-gm6ir0c",c,now()).observation==O::Unavailable,"inconsistent selector stays unknown");
    c=baseline();c.context[8]=0x40;check(mc::gm6::decodeFirmwareDefaultFans("mechrevo-gm6ir0c",c,now()).observation==O::Inactive,"Max does not advertise Default");
    c=baseline();c.context[13]=4;check(mc::gm6::decodeFirmwareDefaultFans("mechrevo-gm6ir0c",c,now()).observation==O::Inactive,"active RAM Manual does not advertise Default");
    c=baseline();c.selectors[3]=0x08;const auto forced=mc::gm6::decodeFirmwareDefaultFans("mechrevo-gm6ir0c",c,now());
    check(forced.fans&&forced.fans->channels[0].forced&&!forced.fans->channels[0].requestedPercent,"forced branch never asserts a fresh base request");

    int profiles{};
    for(const auto* p:mc::deviceProfiles()){
        const auto decoded=mc::gm6::decodeFirmwareDefaultFans(p->id,baseline(),now());
        check((p->id=="mechrevo-gm6ir0c")==(decoded.observation==O::Available),"firmware curve leaked to another profile");++profiles;
        mc::BoardDescriptor candidate{std::string(p->manufacturer),std::string(p->baseBoard),{},std::string(p->productName)};
        mc::tests::setTestBoard(candidate);
        auto fake=std::make_unique<Fake>();auto* observed=fake.get();fake->metadata.id=std::string(p->ecBackendId);
        if(p->id=="mechrevo-gm6ir0c")put(*fake,baseline());
        for(const auto& identity:p->identity)
            fake->ram[identity.address]=static_cast<std::uint8_t>((fake->ram[identity.address]&~identity.mask)|(identity.expected&identity.mask));
        mc::EcAccess access(std::move(fake));const bool verified=access.verifyBoard().verified;
        const auto actual=mc::gm6::readFirmwareDefaultFans(access);
        check(observed->writes==0,"profile observation attempted an EC write");
        if(p->id=="mechrevo-gm6ir0c")
            check(verified&&actual.observation==O::Available&&observed->selectorReads==2,"fake GM6 does not reach its typed reader");
        else check(actual.observation==O::Inactive&&observed->selectorReads==0,"other model reached GM6 selector reader");
    }
    check(mc::gm6::decodeFirmwareDefaultFans("",baseline(),now()).observation==O::Inactive,"unknown profile received a GM6 curve");
    mc::tests::setTestBoard({"Unknown","Unknown",{},"Unknown"});
    auto unknownBackend=std::make_unique<Fake>();auto* unknown=unknownBackend.get();mc::EcAccess unknownEc(std::move(unknownBackend));
    check(!unknownEc.verifyBoard().verified&&mc::gm6::readFirmwareDefaultFans(unknownEc).observation==O::Inactive&&
          unknown->selectorReads==0&&unknown->writes==0,"unknown board reached a GM6 reader");

    mc::BoardDescriptor board{"MECHREVO","GM6IR0C",{},"JiguangX Series GM6IR0C"};mc::tests::setTestBoard(board);
    auto backend=std::make_unique<Fake>();auto* f=backend.get();put(*f,baseline());mc::EcAccess ec(std::move(backend));
    check(ec.verifyBoard().verified,"fake GM6 verified");const auto read=mc::gm6::readFirmwareDefaultFans(ec);
    check(read.fans&&f->selectorReads==2&&f->writes==0,"existing EC reader obtains selector twice and never writes");
    f->ram[0x751]=0x40;f->selectorReads=0;
    check(mc::gm6::readFirmwareDefaultFans(ec).observation==O::Inactive&&f->selectorReads==0&&f->writes==0,"Max causes no I2EC selector reads or writes");
    f->ram[0x751]=0;f->ram[0x7c6]=4;f->selectorReads=0;
    check(mc::gm6::readFirmwareDefaultFans(ec).observation==O::Inactive&&f->selectorReads==0&&f->writes==0,"Manual causes no I2EC selector reads or writes");
    f->ram[0x7c6]=0;f->ram[0x74c]=0xff;f->selectorReads=0;
    check(mc::gm6::readFirmwareDefaultFans(ec).observation==O::Unavailable&&f->selectorReads==0&&f->writes==0,"FF context is unknown, not Default");
    f->ram[0x74c]=0x18;f->selectorReads=0;f->changeSelector=true;
    check(mc::gm6::readFirmwareDefaultFans(ec).observation==O::Unavailable&&f->writes==0,"changed selector is not published");
    mc::tests::resetTestBoard();

    mc::MonitoringSnapshot snapshot;snapshot.nativeDefaultFans=game.fans;
    snapshot.processors[0].fanPwm=33;snapshot.processors[1].fanPwm=25;
    mc::ResourceState state;state.update(board,snapshot);const mc::ResourcePresentationView view(state);
    const auto cpu=mc::gui::ui::defaultFanGraph(view,0,true),gpu=mc::gui::ui::defaultFanGraph(view,1,true);
    check(cpu.curve&&gpu.curve&&cpu.legend.find("EC CPU")!=std::string::npos&&gpu.legend.find("EC GPU")!=std::string::npos,
          "accepted resource presents correct per-fan temperature source");
    check(cpu.legend.find("(база)")!=std::string::npos&&cpu.source.find("Образ EC 1.12")!=std::string::npos,
          "UI marks firmware image and pre-override base request");
    check(!mc::gui::ui::defaultFanGraph(view,0,false).curve,"Manual/Max presentation cannot show Default");
    mc::FanCurveProfiles staleDraft;staleDraft.ecManual=true;staleDraft.selected.fill(mc::FanCurvePreset::Manual);
    const auto* observed=view.defaultFanCurve(0);
    check(mc::showObservedDefaultFan(false,true,false,false,false,false,observed)&&staleDraft.ecManual&&
          staleDraft.currentPreset(0)==mc::FanCurvePreset::Manual,"confirmed EC Default ignores and preserves stale Manual draft");
    check(!mc::showObservedDefaultFan(false,false,true,false,false,false,observed)&&
          !mc::showObservedDefaultFan(false,false,false,true,false,false,observed)&&
          !mc::showObservedDefaultFan(false,true,false,false,false,true,observed),"actual Manual/Max and pending mode hide retained Default view");
    mc::NativeDefaultFanCache cache;const mc::NativeDefaultFanCacheKey key{1,2};
    cache.store(game,1000,key);
    check(!cache.needsRefresh(3999,key)&&cache.needsRefresh(4000,key)&&
          cache.reading().fans->channels[0].sampledAtMs==game.fans->channels[0].sampledAtMs,
          "three-second cache keeps original firmware observation time");
    check(cache.needsRefresh(1001,{2,2})&&cache.needsRefresh(1001,{1,0}),"profile or power-gate change refreshes immediately");
    cache.clearInactive();check(!cache.reading().fans&&cache.needsRefresh(1002,key),"Manual/Max clears cached Default immediately");
    std::cout<<"GM6 Default FAN: "<<profiles<<" profiles + unknown; selectors, modes, EC sensors, no writes passed\n";return 0;
}catch(const std::exception& e){mc::tests::resetTestBoard();std::cerr<<e.what()<<'\n';return 1;}}
