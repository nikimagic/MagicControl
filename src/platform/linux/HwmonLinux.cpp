#include "GpuLinuxInternal.h"
#include <chrono>
#include <cmath>
#include <cerrno>
#include <cstring>
namespace mc::linuxgpu {
namespace fs=std::filesystem;
SensorReading readSensorValue(const fs::path& path,std::string device,std::string metric,std::string unit,double divisor){
    errno=0;std::ifstream file(path);double raw{};const bool ok=bool(file>>raw)&&std::isfinite(raw);
    const auto sampled=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    return {std::move(device),std::move(metric),std::move(unit),"Linux · "+path.string(),ok?"ok":errno?std::string("read failed: ")+std::strerror(errno):"value absent/non-numeric",ok?std::optional<double>(raw/divisor):std::nullopt,ok?std::optional<double>(raw):std::nullopt,static_cast<std::uint64_t>(sampled),SensorTimestampOrigin::SoftwareObservation};
}
std::vector<SensorReading> hwmonReadings(const fs::path& deviceFilter,std::stop_token token){
    std::vector<SensorReading> rows;std::error_code error;
    const auto belongs=[&](const fs::path& p){const auto s=p.string(),filter=deviceFilter.string();return filter.empty()||s==filter||s.starts_with(filter+"/");};
    for(const auto& hw:fs::directory_iterator("/sys/class/hwmon",error)){
        if(token.stop_requested())break;
        const auto name=text(hw.path()/"name");const auto canonical=fs::weakly_canonical(hw.path(),error);error.clear();
        const auto devicePath=fs::weakly_canonical(hw.path()/"device",error);error.clear();
        if(!belongs(canonical)&&!belongs(devicePath))continue;
        const auto runtime=text(devicePath/"power/runtime_status");
        if(runtime=="suspended"||runtime=="suspending"){
            rows.push_back({canonical.string(),"provider_status","","Linux · hwmon","runtime suspended; identity retained, live reads skipped",{},{},0});continue;
        }
        for(const auto& field:fs::directory_iterator(hw.path(),error)){
            if(token.stop_requested())break;
            const auto n=field.path().filename().string();const auto underscore=n.find('_');
            const auto prefix=n.substr(0,underscore);const auto suffix=underscore==std::string::npos?"":n.substr(underscore+1);
            const bool numeric=suffix=="input"||suffix=="average"||suffix=="max"||suffix=="min"||suffix=="crit"||suffix=="emergency"||suffix=="rated_max"||suffix=="cap"||suffix=="cap_min"||suffix=="cap_max"||suffix=="cap_default";
            std::string unit;double divisor=1.;
            if(numeric&&prefix.starts_with("temp")){unit="C";divisor=1000.;}
            else if(numeric&&prefix.starts_with("power")){unit="W";divisor=1000000.;}
            else if(numeric&&prefix.starts_with("energy")){unit="J";divisor=1000000.;}
            else if(numeric&&prefix.starts_with("in")){unit="V";divisor=1000.;}
            else if(numeric&&prefix.starts_with("curr")){unit="A";divisor=1000.;}
            else if(numeric&&prefix.starts_with("freq")){unit="Hz";}
            else if(numeric&&prefix.starts_with("fan")){unit="RPM";}
            else if(n.starts_with("pwm")&&suffix.empty()){unit="raw PWM (0..255)";}
            else continue;
            rows.push_back(readSensorValue(field.path(),name+" / "+canonical.string()+" / "+text(hw.path()/(prefix+"_label")),n,unit,divisor));
        }
    }
    return rows;
}
}
