#include "MagicControl/IntelGpu.h"
#include "GpuLinuxInternal.h"
#include <chrono>
#include <mutex>
#include <map>
#include <algorithm>
#include <cmath>
namespace mc {
namespace fs=std::filesystem;
struct IntelGpu::Impl {
    std::mutex gate;
    struct Energy {double value{};std::uint64_t sampled{};};std::map<std::string,Energy> previous;
    std::vector<GpuReading> read(){
        std::vector<GpuReading> rows;
        for(const auto& device:linuxgpu::devices()){
            const auto& a=device.adapter;if(a.vendorId!=0x8086||a.software)continue;
            GpuReading r;r.name=a.name;r.uuid=a.id;r.pci=a.pci;r.driver=a.driver;r.type=a.type;r.source="Intel · Linux i915/xe sysfs/hwmon";
            const auto runtime=linuxgpu::text(device.path/"power/runtime_status");r.apiStatus.push_back("runtime_status="+runtime);
            if(runtime=="suspended"||runtime=="suspending"){r.status="Intel GPU runtime suspended; live reads skipped";rows.push_back(std::move(r));continue;}
            if(a.driver!="i915"&&a.driver!="xe"){r.status="Intel GPU: драйвер i915/xe не обнаружен";rows.push_back(std::move(r));continue;}
            // Same hwmon parser as the all-device diagnostic timeline. Filter
            // by the canonical PCI device to avoid CPU/another GPU sensors.
            r.additionalSensors=linuxgpu::hwmonReadings(device.path);
            std::vector<SensorReading> derived;
            for(const auto& s:r.additionalSensors){
                if(!s.value)continue;
                if(s.metric=="temp1_input"&&s.unit=="C"&&*s.value>=-40&&*s.value<=200&&!r.temperature)r.temperature=static_cast<unsigned>(std::lround(std::max(0.,*s.value)));
                if(s.metric=="fan1_input"&&s.unit=="RPM"&&*s.value>=0&&*s.value<30000)r.fanRpm=static_cast<unsigned>(*s.value);
                if((s.metric=="power1_average"||s.metric=="power1_input")&&s.unit=="W"&&*s.value>=0&&*s.value<5000)r.powerMw=static_cast<unsigned>(std::lround(*s.value*1000));
                if(s.metric=="energy1_input"&&s.unit=="J"){
                    const auto key=r.uuid+"/"+s.source;const auto it=previous.find(key);std::optional<double> watts;
                    if(it!=previous.end()&&s.sampledAtMs>it->second.sampled){const auto dt=static_cast<double>(s.sampledAtMs-it->second.sampled)/1000.;
                        if(dt>.05&&dt<30&&*s.value>=it->second.value){const auto v=(*s.value-it->second.value)/dt;if(std::isfinite(v)&&v>=0&&v<5000)watts=v;}}
                    previous[key]={*s.value,s.sampledAtMs};derived.push_back({s.device,"power_derived","W",s.source,watts?"energy delta/time":"two valid non-reset samples required",watts,{},s.sampledAtMs});
                    // GT/tile counters remain separate. Do not sum them into
                    // a board power or substitute CPU package RAPL for GPU.
                    if(watts&&!r.powerMw&&s.device.starts_with("i915 /"))r.powerMw=static_cast<unsigned>(std::lround(*watts*1000));
                }
            }
            r.additionalSensors.insert(r.additionalSensors.end(),derived.begin(),derived.end());
            const auto frequency=[&](const fs::path& root,const std::vector<const char*>& names){std::error_code ec;
                for(const auto* name:names){const auto file=root/name;if(!fs::exists(file,ec))continue;
                    auto s=linuxgpu::readSensorValue(file,r.uuid+" / "+root.string(),name,"MHz");
                    if(s.value&&*s.value>=0&&*s.value<=30000&&(std::string_view(name)=="gt_act_freq_mhz"||std::string_view(name)=="rps_act_freq_mhz"||std::string_view(name)=="act_freq")&&!r.graphicsMhz)r.graphicsMhz=static_cast<unsigned>(std::lround(*s.value));
                    r.additionalSensors.push_back(std::move(s));
                }};
            std::error_code error;
            for(const auto& card:fs::directory_iterator(device.path/"drm",error)){
                const auto n=card.path().filename().string();if(!n.starts_with("card")||n.size()<5||n.find_first_not_of("0123456789",4)!=std::string::npos)continue;
                frequency(card.path(),{"gt_act_freq_mhz","gt_cur_freq_mhz","gt_min_freq_mhz","gt_max_freq_mhz","gt_RP0_freq_mhz","gt_RP1_freq_mhz","gt_RPn_freq_mhz"});
                for(const auto& gt:fs::directory_iterator(card.path()/"gt",error))frequency(gt.path(),{"rps_act_freq_mhz","rps_cur_freq_mhz","rps_min_freq_mhz","rps_max_freq_mhz","rps_RP0_freq_mhz","rps_RP1_freq_mhz","rps_RPn_freq_mhz"});
                error.clear();
            }
            error.clear();for(const auto& tile:fs::directory_iterator(device.path,error)){
                if(!tile.path().filename().string().starts_with("tile"))continue;
                for(const auto& gt:fs::directory_iterator(tile.path(),error))if(gt.path().filename().string().starts_with("gt"))frequency(gt.path()/"freq0",{"act_freq","cur_freq","min_freq","max_freq","rpn_freq","rpe_freq","rp0_freq"});
                error.clear();
            }
            r.available=std::any_of(r.additionalSensors.begin(),r.additionalSensors.end(),[](const auto& s){return s.value.has_value();});
            r.status=r.available?"Intel i915/xe · экспортируемые hwmon/частоты; только чтение":"Intel GPU видна; live hwmon/частоты не экспортируются или недоступны";
            r.apiStatus.push_back("GPU load is not inferred from idle residency. Windows engine counters and Linux hwmon/sysfs are separate sources.");
            rows.push_back(std::move(r));
        }
        return rows;
    }
};
IntelGpu::IntelGpu():impl_(std::make_unique<Impl>()){}IntelGpu::~IntelGpu()=default;
std::vector<GpuReading> IntelGpu::read(){std::lock_guard lock(impl_->gate);return impl_->read();}
}
