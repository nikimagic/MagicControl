#include "IntelPchWindows.h"
#include "IntelTelemetryWindows.h"
#include "SensorModuleWindows.h"
#include <format>
namespace mc::windows {
struct IntelPchReader::Impl {
    detail::Module module;ULONGLONG retryAt{};std::string identity,status;
    unsigned route{};std::optional<double> raw;
    void read(MonitoringSnapshot& s){
        if(nativeCpuVendor()!=ProcessorVendor::Intel||wcsstr(GetCommandLineW(),L"--no-ec"))return;
        const auto now=GetTickCount64();std::optional<double> value;raw.reset();
        if(now>=retryAt){try{
            if(!module.ready()){
                module.open(L"IntelPCHThermal.bin","478500ae78aebef3ae7a0b5fe4848d2d5737e8f7c5d51200c284a61c21d7865d");
                const auto id=module.pchRead<5>("ioctl_identity");route=static_cast<unsigned>(id[0]);
                if((id[1]&0xffff)!=0x8086||(route!=1&&route!=2))throw std::runtime_error("PCH: unexpected identity/route");
                identity=std::format("Intel PCH {:04X} @ {:02X}:{:02X}.{}; route={} MMIO=0x{:X} size=0x{:X}",id[1]>>16,(id[2]>>16)&255,(id[2]>>8)&255,id[2]&255,route,id[3],id[4]);
            }
            const auto data=module.pchRead<2>("ioctl_read_raw");
            if(data[1]!=route||data[0]>0xffffffffULL)throw std::runtime_error("PCH: changed route/invalid raw width");
            const auto word=static_cast<unsigned>(data[0]);raw=word;const auto t=word&0x1ff;
            // Signed upstream0.2.11 consumer contract: BAR0 half-degree offset;
            // PWRM TSS0 bit9 VALID, bits8:0 integer Celsius. Never write TSS0.
            if(word!=0xffffffff&&t&&t!=0x1ff&&(route==1||(word&0x200))){
                const double converted=route==1?t*.5-50.:double(t);
                if(converted>=-40&&converted<=150)value=converted;
            }
            status=value?"ok":"invalid/unavailable raw temperature or VALID bit";
        }catch(const std::exception& e){status=e.what();module.reset();retryAt=now+30000;}}
        s.pchCelsius=value;s.pchSource="PawnIO · IntelPCHThermal";
        s.additionalSensors.push_back({identity.empty()?"Intel PCH":identity,"temperature","C",s.pchSource,now<retryAt?"retry delayed; "+status:status,value,raw,now});
    }
};
IntelPchReader::IntelPchReader():impl_(std::make_unique<Impl>()){}
IntelPchReader::~IntelPchReader()=default;
void IntelPchReader::read(MonitoringSnapshot& s){impl_->read(s);}
}
