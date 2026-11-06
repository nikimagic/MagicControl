#include "MagicControl/MemoryTelemetry.h"
#include "MagicControl/DeviceProfile.h"
#include "MagicControl/BoardDescriptor.h"
#if defined(MC_MEMORY_TEST_RESOURCES)
#include "MagicControl/ResourceCatalog.h"
#include "../../src/gui/common/MonitoringCards.h"
#endif
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
unsigned checks{};
void check(bool ok,const char* what){++checks;if(!ok)throw std::runtime_error(what);}
struct FakeIntel {
    std::vector<std::uint32_t> status;
    std::size_t position{};
    std::uint32_t readStatus(){if(position>=status.size())throw std::runtime_error("unexpected I/O");return status[position++];}
};
}
int main(){try{
    using namespace mc;
    auto profiles=deviceProfiles();check(!profiles.empty(),"empty profile registry");
    std::vector<const DeviceProfile*> boards(profiles.begin(),profiles.end());
    BoardDescriptor unknown;unknown.manufacturer="unidentified";unknown.baseBoard="unidentified";
    const auto& unknownProfile=deviceProfile(unknown);boards.push_back(&unknownProfile);
    bool gm6=false,wujie=false;
    for(const auto* board:boards){
        check(board!=nullptr,"missing device profile");gm6|=board->id=="mechrevo-gm6ir0c";wujie|=board->id=="wu-jie-15-xpro";
        // Same CPU/type admission for every actual or unknown device: profile
        // metadata is intentionally not an input to the selection function.
        check(memoryClockBackend(MemoryCpuVendor::Intel,6,0xb7,false,true)==MemoryClockBackend::IntelQclk,"Intel B7 DDR5");
        check(memoryClockBackend(MemoryCpuVendor::Intel,6,0x97,true,false)==MemoryClockBackend::IntelQclk,"Intel 97 DDR4");
        check(memoryClockBackend(MemoryCpuVendor::Amd,0x1a,0x20,false,true)==MemoryClockBackend::AmdDdr5,"AMD Strix DDR5");
        check(memoryClockBackend(MemoryCpuVendor::Amd,0x19,0x75,false,true)==MemoryClockBackend::AmdDdr5,"AMD Phoenix DDR5");
        check(memoryClockBackend(MemoryCpuVendor::Amd,0x19,0x50,true,false)==MemoryClockBackend::AmdDdr4,"AMD DDR4");
        check(memoryClockBackend(MemoryCpuVendor::Intel,6,0xc6,false,true)==MemoryClockBackend::Unavailable,"unreviewed CPU");
        check(memoryClockBackend(MemoryCpuVendor::Amd,0x1a,0x20,true,false)==MemoryClockBackend::Unavailable,"wrong AMD memory type");
        check(memoryClockBackend(MemoryCpuVendor::Intel,6,0xb7,false,false)==MemoryClockBackend::Unavailable,"unknown type");
        check(memoryClockBackend(MemoryCpuVendor::Intel,6,0xb7,true,true)==MemoryClockBackend::Unavailable,"ambiguous type");
        check(memoryClockBackend(MemoryCpuVendor::Other,6,0xb7,false,true)==MemoryClockBackend::Unavailable,"unknown vendor");
        FakeIntel gm6Sample{{0x2c000484,0x1c000484}};
        const auto gm6Clock=captureIntelMemoryClock(true,[&]{return gm6Sample.readStatus();});
        check(gm6Clock.mhz&&*gm6Clock.mhz==3300.,"GM6 real QCLK sample decodes independently of laptop profile");
        const auto temperature=spd5118TemperatureCelsius(0x0280);
        check(temperature&&std::abs(*temperature-40.)<.001,"SPD5118 temperature works independently of device profile");
        check(!spd5118TemperatureCelsius(0x07e0),"invalid SPD5118 temperature stays unavailable");
        FakeIntel io{{0x480,0x480}};
        const auto reading=captureIntelMemoryClock(true,[&]{return io.readStatus();});
        check(reading.mhz&&std::abs(*reading.mhz-3200.)<.001&&reading.attempts==1&&io.position==2,
            "confirmed current QCLK published for device");
#if defined(MC_MEMORY_TEST_RESOURCES)
        MonitoringSnapshot sample;sample.memory[0].present=true;sample.memory[0].model="fixture DIMM";sample.memory[0].celsius=temperature;
        sample.memory[0].source="PawnIO fixture";sample.memory[1]=sample.memory[0];sample.memory[1].model="second fixture DIMM";
        sample.memory[1].celsius=37.;sample.memoryClockMhz=reading.mhz;
        sample.memoryClockSource="PawnIO fixture";sample.memoryClockStatus="approximate: fake QCLK";
        sample.memoryClockSampledAtMs=123;BoardDescriptor descriptor;
        descriptor.manufacturer=board->manufacturer;descriptor.baseBoard=board->baseBoard;descriptor.productName=board->productName;
        ResourceState state;state.update(descriptor,sample);ResourcePresentationView view(state);
        const auto actualTemperature=resourceDisplayValue(&view,ResourceKind::Memory,0,"temperature");
        check(actualTemperature&&*actualTemperature==40.,"core publishes actual SPD temperature for this profile");
        const auto rate=resourceDisplayValue(&view,ResourceKind::Memory,0,"data_rate");
        check(rate&&*rate==6400.,"core publishes current transfer rate for this profile");
        const auto card=gui::ui::monitoringCard(sample,1,&view);
        check(card.source.find("6400 MT/s")!=std::string::npos,"memory card consumes accepted resource");
        const auto secondRate=resourceDisplayValue(&view,ResourceKind::Memory,1,"data_rate");
        const auto secondTemp=resourceDisplayValue(&view,ResourceKind::Memory,1,"temperature");
        check(secondRate&&*secondRate==6400.&&secondTemp&&*secondTemp==37.,"second DIMM retains its own temperature and common clock");
        sample.memoryClockMhz=gm6Clock.mhz;state.update(descriptor,sample);
        ResourcePresentationView gm6View(state);
        check(resourceDisplayValue(&gm6View,ResourceKind::Memory,0,"data_rate")==6600.&&
              resourceDisplayValue(&gm6View,ResourceKind::Memory,1,"data_rate")==6600.,
              "both memory cards publish the real GM6 QCLK sample times two");
        sample.memoryClockMhz.reset();sample.memoryClockSampledAtMs=0;state.update(descriptor,sample);
        ResourcePresentationView unavailable(state);
        check(!resourceDisplayValue(&unavailable,ResourceKind::Memory,0,"data_rate"),"unknown current rate remains unknown in core");
#endif
    }
    check(gm6&&wujie&&unknownProfile.id.empty(),"required GM6, Wujie and unknown profile coverage");
    FakeIntel retry{{0x480,0x484,0x480,0x480}};
    const auto good=captureIntelMemoryClock(true,[&]{return retry.readStatus();});
    check(good.mhz&&good.attempts==2&&retry.position==4,"transition retry recovers fresh value");
    FakeIntel busy{{0x480,0x484,0x480,0x484,0x480,0x484}};
    const auto uncertain=captureIntelMemoryClock(true,[&]{return busy.readStatus();});
    check(!uncertain.mhz&&uncertain.status==IntelMemoryClockStatus::Transition&&uncertain.attempts==3,
        "unstable source never fabricates speed");
    FakeIntel bad{{0x400,0x400}};
    const auto invalid=captureIntelMemoryClock(true,[&]{return bad.readStatus();});
    check(!invalid.mhz&&invalid.status==IntelMemoryClockStatus::Unavailable,"invalid registers remain unknown");
    FakeIntel denied{};
    const auto noIo=captureIntelMemoryClock(false,[&]{return denied.readStatus();});
    check(!noIo.mhz&&noIo.attempts==0&&denied.position==0,"unsupported CPU performs no register reads");
    check(!memoryRetryDue(1000,6000,false)&&memoryRetryDue(1000,6000,true)&&memoryRetryDue(6000,6000,false),
        "retry is bounded and a diagnostic does not hide the failure");
    FakeIntel allOnes{{UINT32_MAX,UINT32_MAX}};
    const auto ff=captureIntelMemoryClock(true,[&]{return allOnes.readStatus();});
    check(!ff.mhz&&ff.status==IntelMemoryClockStatus::Unavailable,"all-ones registers never become frequency");
    std::array<std::uint32_t,2> state{0x30,0x30},goodConfig{2800,2800},different{2800,2400};
    check(zenMemoryChannelsClock(true,state,goodConfig,state)==2800.,"two AMD channels confirmed");
    check(!zenMemoryChannelsClock(true,state,different,state),"AMD channel disagreement remains unknown");
    std::cout<<"PawnIO memory: "<<boards.size()<<" profiles including unknown, "<<checks<<" checks; fake transport only\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
