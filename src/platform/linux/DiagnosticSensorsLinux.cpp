#include "../common/DiagnosticDumpInternal.h"
#include "GpuLinuxInternal.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <cerrno>
#include <cstring>
#include <algorithm>
#include <cmath>
namespace mc::diagnostics {
namespace fs=std::filesystem;
namespace {
std::string text(const fs::path& p){std::ifstream f(p);std::string s;std::getline(f,s);return s;}
}
struct PlatformSensors::Impl {
    std::vector<SensorReading> read(bool slow,std::stop_token token){
        std::vector<SensorReading> rows;if(slow)return rows;
        const auto add=[&](const fs::path& path,std::string device,std::string metric,std::string unit,double divisor){
            rows.push_back(linuxgpu::readSensorValue(path,std::move(device),std::move(metric),std::move(unit),divisor));
        };
        std::error_code ec;
        rows=linuxgpu::hwmonReadings({},token);
        ec.clear();for(const auto& zone:fs::directory_iterator("/sys/class/thermal",ec)){
            if(zone.path().filename().string().starts_with("thermal_zone"))add(zone.path()/"temp",zone.path().string()+" / "+text(zone.path()/"type"),"thermal_zone_temperature","C",1000.);
        }
        ec.clear();for(const auto& supply:fs::directory_iterator("/sys/class/power_supply",ec)){
            if(token.stop_requested())break;
            for(const auto& field:fs::directory_iterator(supply.path(),ec)){
                const auto n=field.path().filename().string();std::string unit;double divisor=1.;
                if(n=="temp"||n=="temp_now"){unit="C";divisor=10.;}
                else if(n.starts_with("voltage_")){unit="V";divisor=1000000.;}
                else if(n.starts_with("current_")){unit="A";divisor=1000000.;}
                else if(n.starts_with("power_")){unit="W";divisor=1000000.;}
                else if(n.starts_with("energy_")){unit="Wh";divisor=1000000.;}
                else if(n.starts_with("charge_")){unit="Ah";divisor=1000000.;}
                else if(n=="capacity"){unit="percent";}
                else if(n=="cycle_count"||n=="online"||n=="present"){unit="count";}
                else continue;
                add(field.path(),supply.path().string(),n,unit,divisor);
            }
        }
        ec.clear();for(const auto& cpu:fs::directory_iterator("/sys/devices/system/cpu",ec)){
            const auto n=cpu.path().filename().string();if(!n.starts_with("cpu")||n.size()<4||n.find_first_not_of("0123456789",3)!=std::string::npos)continue;
            const auto path=cpu.path()/"cpufreq/scaling_cur_freq";if(fs::exists(path,ec))add(path,n,"frequency","MHz",1000.);
        }
        return rows;
    }
};
PlatformSensors::PlatformSensors():impl_(std::make_unique<Impl>()){}
PlatformSensors::~PlatformSensors()=default;
std::vector<SensorReading> PlatformSensors::read(bool slow,std::stop_token token){return impl_->read(slow,token);}
}
