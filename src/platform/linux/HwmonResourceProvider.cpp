#include "HwmonResourceProvider.h"
#include "GpuLinuxInternal.h"
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <utility>

namespace mc::linux_platform {
namespace fs=std::filesystem;
HwmonResourceScan scanHwmonResources(const fs::path& root,std::stop_token stop){
    HwmonResourceScan result;
    if(stop.stop_requested()){result.status="hwmon scan cancelled";return result;}
    std::error_code error;fs::directory_iterator hwmons(root,error);
    if(error){result.status="hwmon directory unavailable: "+error.message();return result;}
    result.availability=ResourceAvailability::Available;
    result.status="hwmon directory enumerated; individual readings may be unavailable";
    const fs::directory_iterator end;
    for(;hwmons!=end;hwmons.increment(error)){
        if(error){result.availability=ResourceAvailability::Unknown;result.status="hwmon enumeration interrupted: "+error.message();break;}
        const auto hw=*hwmons;
        if(stop.stop_requested()){result.availability=ResourceAvailability::Unknown;result.status="hwmon scan cancelled";break;}
        const auto runtime=linuxgpu::text(hw.path()/"device/power/runtime_status");
        if(runtime=="suspended"||runtime=="suspending")continue;
        std::error_code fieldError;fs::directory_iterator fields(hw.path(),fieldError);
        if(fieldError){result.availability=ResourceAvailability::Unknown;result.status="hwmon channel directory unavailable: "+fieldError.message();continue;}
        const auto chip=linuxgpu::text(hw.path()/"name");
        for(;fields!=end;fields.increment(fieldError)){
            if(fieldError){result.availability=ResourceAvailability::Unknown;result.status="hwmon channel enumeration interrupted: "+fieldError.message();break;}
            const auto field=*fields;
            if(stop.stop_requested()){result.availability=ResourceAvailability::Unknown;result.status="hwmon scan cancelled";break;}
            const auto name=field.path().filename().string();
            if(!name.ends_with("_input"))continue;
            const auto channel=name.substr(0,name.size()-6);std::size_t prefix=0;
            while(prefix<channel.size()&&std::isalpha(static_cast<unsigned char>(channel[prefix])))++prefix;
            if(prefix==0||prefix==channel.size()||!std::all_of(channel.begin()+prefix,channel.end(),[](unsigned char c){return std::isdigit(c)!=0;}))continue;
            const auto type=channel.substr(0,prefix);std::string unit;double divisor=1.;
            if(type=="temp"){unit="C";divisor=1000.;}else if(type=="fan")unit="RPM";
            else if(type=="in"){unit="V";divisor=1000.;}else if(type=="curr"){unit="A";divisor=1000.;}
            else if(type=="power"){unit="W";divisor=1000000.;}else continue;
            const auto label=linuxgpu::text(hw.path()/(channel+"_label"));
            auto reading=linuxgpu::readSensorValue(field.path(),chip+" / "+hw.path().string()+" / "+label,name,unit,divisor);
            // hwmon ABI: temp/fan fault=1 means the channel's measurement
            // cannot be trusted. Keep the raw input as diagnostic evidence;
            // optional missing fault files do not imply a fault-free sensor.
            if(type=="temp"||type=="fan"){
                const auto faultPath=hw.path()/(channel+"_fault");std::error_code existsError;
                const bool faultPresent=fs::exists(faultPath,existsError);
                if(existsError){reading.value.reset();reading.status="fault status unavailable: "+existsError.message();}
                else if(faultPresent){const auto fault=linuxgpu::text(faultPath);
                    if(fault=="1"){reading.value.reset();reading.status="hwmon sensor fault";}
                    else if(fault!="0"){reading.value.reset();reading.status="hwmon fault status unreadable or invalid";}
                }
            }
            result.readings.push_back(std::move(reading));
        }
        if(fieldError){result.availability=ResourceAvailability::Unknown;result.status="hwmon channel enumeration interrupted: "+fieldError.message();}
    }
    if(error){result.availability=ResourceAvailability::Unknown;result.status="hwmon enumeration interrupted: "+error.message();}
    return result;
}
HwmonResourceProvider::HwmonResourceProvider(const BoardDescriptor& board,std::function<HwmonResourceScan()> scan)
    :board_(board),scan_(std::move(scan)){if(!scan_)throw std::invalid_argument("hwmon provider requires a scanner");}
ResourceCatalog HwmonResourceProvider::observe(){
    const auto scan=scan_();auto catalog=observeMachineResources(board_,{});
    catalog.sources.push_back({"linux.hwmon","Linux · hwmon",scan.status,scan.availability,
        scan.availability==ResourceAvailability::Available?ResourceEvidence::Observed:ResourceEvidence::Unknown});
    catalog.resources=observeSensorResources(scan.readings);return catalog;
}
}
