#include "src/protocols/lenovo/LegionFans.h"
#include "src/protocols/lenovo/LegionWmi.h"
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
unsigned checks{};
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F&& f,const char* message){bool failed=false;try{f();}catch(const std::exception&){failed=true;}check(failed,message);}

struct ReadbackFault {
    int flag{},reads{},writes{};
    bool failFirstRead{},failReadback{},invalidReadback{};
    mc::lenovo::FanPacket manual{};
    int powerProfile{255};
    int feature(std::uint32_t id){
        check(id==mc::lenovo::fanFullSpeedId,"Max attempted a foreign WMI read");
        ++reads;if((failFirstRead&&reads==1)||(failReadback&&reads==2))throw std::runtime_error("read unavailable");
        return invalidReadback&&reads==2?2:flag;
    }
    void feature(std::uint32_t id,int value){
        check(id==mc::lenovo::fanFullSpeedId,"Max attempted a foreign WMI write");
        ++writes;flag=value;
    }
};
}

int main(){try{
    using namespace mc;using namespace mc::lenovo;
    // Failure before the write must never switch to a curve writer.
    for(int variant=0;variant<3;++variant){
        ReadbackFault f;f.manual.fill(0x63);const auto original=f.manual;
        f.failFirstRead=variant==0;f.failReadback=variant==1;f.invalidReadback=variant==2;
        rejects([&]{setFullSpeedFlag(f,true);},"unavailable/invalid flag falsely confirmed Max");
        check(f.writes==(variant==0?0:1),"wrong number of Full Speed writes after read failure");
        check(f.manual==original&&f.powerProfile==255,"flag failure changed curve or power");
    }

    NativeFanCurves curve{};for(auto& channel:curve)for(auto& point:channel)point={255,100};
    curve[0][0]=curve[1][0]={20,5};
    // Inactive storage belongs to the shared editor and must not enter Lenovo's packet.
    curve[0][15]={35,44};curve[1][15]={85,99};
    FanPacket baseline{};for(std::size_t i=0;i<baseline.size();++i)baseline[i]=static_cast<std::uint8_t>((i*37+11)&255);
    const auto original=baseline;const auto originalCurve=curve;
    const FanBounds bounds{{1700,1800},{7100,6900}};
    const auto packet=encodeSharedCurve(curve,baseline,13,bounds);
    check(curve==originalCurve&&baseline==original,"encoding mutated stored curves or firmware baseline");
    check(packet[0]==17&&packet[1]==18,"low input escaped minimum RPM admission");
    check(packet[4]==20&&packet[6]==20&&packet[5]==0&&packet[7]==0,"first linked threshold lost");
    check(packet[16]==71&&packet[17]==69&&packet[20]==127&&packet[22]==127,"guard row lost max RPM or sentinel");
    for(int row=0;row<2;++row){
        check(packet[row*16+2]==baseline[row*16+2]&&packet[row*16+3]==baseline[row*16+3],"firmware ramp changed");
        for(int col=8;col<16;++col)check(packet[row*16+col]==baseline[row*16+col],"unrelated thermal input changed");
    }
    for(int i=32;i<nativeCapacity*rowBytes;++i)check(packet[i]==baseline[i],"inactive firmware row changed");

    // Wider user values are quantized to the firmware's 100-RPM unit, never
    // rounding through either limit reported by this board's WMI ladder.
    for(int duty=5;duty<=100;++duty){
        auto point=curve;point[0][0]=point[1][0]={20,duty};
        const FanBounds nonAligned{{1701,1817},{2000,2300}};
        const auto p=encodeSharedCurve(point,baseline,13,nonAligned);
        for(int channel=0;channel<2;++channel){const int rpm=p[channel]*100;
            check(rpm>=nonAligned.minimumRpm[channel]&&rpm<=nonAligned.maximumRpm[channel],"100-RPM encoding escaped WMI bounds");}
    }
    const FanBounds impossible{{1701,1800},{1799,2300}};
    rejects([&]{encodeSharedCurve(curve,baseline,13,impossible);},"accepted RPM range with no representable 100-RPM value");
    rejects([&]{encodeSharedCurve(curve,baseline,0,bounds);},"accepted empty firmware baseline");
    rejects([&]{encodeSharedCurve(curve,baseline,14,bounds);},"accepted oversized firmware baseline");
    const FanBounds missing{{0,1800},{7100,6900}};
    rejects([&]{encodeSharedCurve(curve,baseline,13,missing);},"accepted missing WMI minimum");

    const std::array<int,10> minMap{0,1,2,3,4,5,6,7,8,12};
    check(activeLevelMapValid(minMap),"valid physical level 12 rejected");
    auto invalid=minMap;invalid[0]=-1;check(!activeLevelMapValid(invalid),"negative fan level accepted");
    invalid=minMap;invalid[9]=13;check(!activeLevelMapValid(invalid),"out-of-range fan level accepted");
    const std::array<std::uint8_t,3> before{1,2,3},after{4,5,6},mixed{4,2,6};
    check(!transitionPrefix(before,after,mixed),"non-prefix writes treated as safe rollback");
    check(!transitionPrefix(std::span(before).first(2),after,before),"different packet lengths treated as rollback");
    std::cout<<"Lenovo FAN failure and quantization: "<<checks<<" checks; fake and pure data only\n";
}catch(const std::exception& e){std::cerr<<"Lenovo FAN offline: "<<e.what()<<'\n';return 1;}}
