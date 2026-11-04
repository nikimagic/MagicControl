#include "MagicControl/SensorDecoders.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <optional>

namespace {
unsigned checks{};
void check(bool condition,const char* name){
    ++checks;if(!condition){std::fprintf(stderr,"Telemetry decoder failed: %s\n",name);std::exit(1);}
}
bool near(std::optional<double> value,double expected){return value&&std::abs(*value-expected)<0.001;}
}
int main(){
    using namespace mc;
    for(unsigned state=0;state<4;++state)
        check(zenDdr5ActiveClock(0x30|state,3200,0x30|state)==3200,"DDR5 active states");
    check(zenDdr5ActiveClock(0x32,0xabcd0c80,0x32)==3200,"DDR5 low word");
    check(zenDdr5ActiveClock(0x30,500,0x30)==500,"DDR5 minimum");
    check(zenDdr5ActiveClock(0x30,10000,0x30)==10000,"DDR5 maximum");
    check(!zenDdr5ActiveClock(0x30,499,0x30)&&!zenDdr5ActiveClock(0x30,10001,0x30),"DDR5 clock limits");
    check(!zenDdr5ActiveClock(0x30,3200,0x31),"DDR5 transition");
    check(!zenDdr5ActiveClock(0x34,3200,0x34),"DDR5 unsupported state");
    check(!zenDdr5ActiveClock(0x20,3200,0x20)&&!zenDdr5ActiveClock(0x30,3200,0x20),"DDR5 valid bits");
    check(!zenDdr5ActiveClock(UINT32_MAX,3200,0x30)&&!zenDdr5ActiveClock(0x30,UINT32_MAX,0x30)&&
        !zenDdr5ActiveClock(0x30,3200,UINT32_MAX),"DDR5 unavailable registers");
    check(near(zenDdr4ActiveClock(0x30,48,0x30),1600),"DDR4 nominal");
    check(near(zenDdr4ActiveClock(0x30,15,0x30),500)&&near(zenDdr4ActiveClock(0x30,63,0x30),2100),"DDR4 limits");
    check(!zenDdr4ActiveClock(0x30,14,0x30)&&!zenDdr4ActiveClock(0x30,64,0x30),"DDR4 bad ratio");
    check(!zenDdr4ActiveClock(0x31,48,0x31)&&!zenDdr4ActiveClock(0x30,48,0x31),"DDR4 only stable P0");
    check(!zenDdr4ActiveClock(UINT32_MAX,48,0x30)&&!zenDdr4ActiveClock(0x30,UINT32_MAX,0x30),"DDR4 unavailable");
    std::array<std::uint32_t,2> state{0x30,0x30},config{3200,3200};
    check(near(zenMemoryChannelsClock(true,state,config,state),3200),"two DDR5 channels");
    config[1]=3000;check(!zenMemoryChannelsClock(true,state,config,state),"disagreeing channels");
    config={48,48};check(near(zenMemoryChannelsClock(false,state,config,state),1600),"two DDR4 channels");
    auto after=state;after[1]=0x31;check(!zenMemoryChannelsClock(false,state,config,after),"channel transition");
    const auto intel=[](unsigned ratio,unsigned gear,bool ref100){
        const unsigned status=(ratio<<2)|(ref100?0x400:0);
        return intelMemoryClock(status,gear<<12,status);
    };
    check(intel(16,1,true).status==IntelMemoryClockStatus::Valid&&std::abs(intel(16,1,true).mhz-3200)<.001,"Intel 100 MHz reference");
    check(intel(12,1,false).status==IntelMemoryClockStatus::Valid&&std::abs(intel(12,1,false).mhz-3200)<.001,"Intel 133 MHz reference");
    check(intel(4,0,true).mhz==400&&intel(50,0,true).mhz==5000,"Intel clock limits");
    check(intel(3,0,true).status==IntelMemoryClockStatus::Unavailable&&intel(51,0,true).status==IntelMemoryClockStatus::Unavailable,"Intel outside limits");
    check(intel(2,0,true).status==IntelMemoryClockStatus::Unavailable&&intel(16,3,true).status==IntelMemoryClockStatus::Unavailable,"Intel invalid ratio or gear");
    check(intelMemoryClock(0x440,0x1000,0x444).status==IntelMemoryClockStatus::Transition,"Intel transition requires retry");
    check(intelMemoryClock(0x440,0x1000,0xc40).status==IntelMemoryClockStatus::Valid,"Intel unrelated status bit");
    check(!lenovoBatteryCurrent(std::nullopt),"current unavailable");
    check(lenovoBatteryCurrent(0)==0&&lenovoBatteryCurrent(1)==1&&lenovoBatteryCurrent(0xffff)==-1,"signed current direction");
    check(lenovoBatteryCurrent(30000)==30000&&lenovoBatteryCurrent(35536)==-30000,"current boundaries");
    check(!lenovoBatteryCurrent(30001)&&!lenovoBatteryCurrent(35535)&&!lenovoBatteryCurrent(0x8000),"current outside limits");
    check(signedBatteryWord(0x8000)==-32768&&signedBatteryWord(0x7fff)==32767,"raw signed word boundaries");
    std::printf("Telemetry decoders: %u checks; no hardware access\n",checks);
}
