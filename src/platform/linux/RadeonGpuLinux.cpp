#include "GpuLinuxInternal.h"
#include "MagicControl/Logging.h"
#include <mutex>
#include <format>
#include <cerrno>
#include <cstring>
#include <unistd.h>
namespace mc {
namespace {
std::filesystem::path hwmon(const std::filesystem::path& device){std::error_code error;for(const auto& p:std::filesystem::directory_iterator(device/"hwmon",error))if(linuxgpu::text(p.path()/"name")=="amdgpu")return p.path();return {};}
}
struct RadeonGpu::Impl {
    std::mutex gate;
    void apply(const std::string& id,std::optional<int> requested){
        for(const auto& d:linuxgpu::devices())if(d.adapter.id==id){
            if(d.adapter.vendorId!=0x1002||d.adapter.type!=GpuType::Discrete||d.adapter.driver!="amdgpu")throw std::runtime_error("Не подтверждён дискретный amdgpu");
            const auto monitor=hwmon(d.path);if(monitor.empty())throw std::runtime_error("amdgpu hwmon недоступен");
            const auto low=linuxgpu::number(monitor/"power1_cap_min"),high=linuxgpu::number(monitor/"power1_cap_max");
            const auto value=requested&&*requested>=0?std::optional<std::uint64_t>(std::uint64_t(*requested)*1000000):requested?std::nullopt:linuxgpu::number(monitor/"power1_cap_default");
            if(!value||!low||!high||*low>*high||*value<*low||*value>*high)throw std::runtime_error("Лимит вне подтверждённого диапазона power1_cap");
            std::ofstream output(monitor/"power1_cap");output<<*value;output.close();
            if(!output)throw std::runtime_error("Не удалось записать power1_cap: "+std::string(std::strerror(errno)));
            if(linuxgpu::number(monitor/"power1_cap")!=value)throw std::runtime_error("amdgpu не подтвердил Power Limit; другое состояние не перезаписано");
            logMessage("radeon",std::format("amdgpu power1_cap id={} microwatts={} readback confirmed",id,*value));return;
        }
        throw std::runtime_error("Radeon больше не найден по PCI/unique_id");
    }
};
RadeonGpu::RadeonGpu():impl_(std::make_unique<Impl>()){}RadeonGpu::~RadeonGpu()=default;
std::vector<GpuReading> RadeonGpu::read(bool includeIntegrated){
    std::lock_guard lock(impl_->gate);std::vector<GpuReading> rows;
    for(const auto& d:linuxgpu::devices()){
        const auto& a=d.adapter;if(a.vendorId!=0x1002||(!includeIntegrated&&a.type!=GpuType::Discrete))continue;
        GpuReading r;r.name=a.name;r.uuid=a.id;r.pci=a.pci;r.type=a.type;r.driver=a.driver;r.source="Linux · amdgpu";r.totalBytes=a.dedicatedBytes;r.vbios=linuxgpu::text(d.path/"vbios_version");r.available=true;
        r.physicalId=a.physicalId;r.osAdapterId=a.id;r.identitySource=a.identitySource;r.identityStatus=a.identityStatus;r.location=a.location;
        const auto runtime=linuxgpu::text(d.path/"power/runtime_status");
        if(runtime=="suspended"||runtime=="suspending"){r.status="dGPU неактивна (runtime suspended); показания не запрашивались";rows.push_back(std::move(r));continue;}
        const auto monitor=hwmon(d.path);
        const auto metric=[&](const char* file,std::uint64_t divisor,std::uint64_t maximum)->std::optional<unsigned>{auto v=linuxgpu::number(monitor/file);return v&&*v/divisor<=maximum?std::optional<unsigned>(static_cast<unsigned>(*v/divisor)):std::nullopt;};
        if(!monitor.empty()){
            r.temperature=metric("temp1_input",1000,150);if(r.temperature)r.temperatureSource="Linux amdgpu hwmon · temp1_input";
            if(auto hot=metric("temp2_input",1,200000))r.hotspotCelsius=*hot/1000.;
            r.fanRpm=metric("fan1_input",1,30000);r.powerMw=metric("power1_average",1000,1000000);r.powerSource="Linux amdgpu hwmon · power1_average (GPU domain)";
            if(!r.powerMw){r.powerMw=metric("power1_input",1000,1000000);r.powerSource="Linux amdgpu hwmon · power1_input (GPU domain)";}
            if(r.powerMw)r.powerDomain=GpuPowerDomain::Gpu;else r.powerSource.clear();
            r.limitMw=metric("power1_cap",1000,1000000);r.minimumMw=metric("power1_cap_min",1000,1000000);r.maximumMw=metric("power1_cap_max",1000,1000000);r.defaultMw=metric("power1_cap_default",1000,1000000);
            r.powerControl=a.type==GpuType::Discrete&&r.limitMw&&r.minimumMw&&r.maximumMw&&::access((monitor/"power1_cap").c_str(),W_OK)==0;
            if(r.limitMw)r.powerValue=static_cast<int>(*r.limitMw/1000);if(r.minimumMw)r.powerMinimum=static_cast<int>((*r.minimumMw+999)/1000);if(r.maximumMw)r.powerMaximum=static_cast<int>(*r.maximumMw/1000);if(r.defaultMw)r.powerDefault=static_cast<int>(*r.defaultMw/1000);
            // freq1/freq2 are amdgpu hwmon GPU/VRAM clocks in Hz.
            r.graphicsMhz=metric("freq1_input",1000000,20000);r.memoryMhz=metric("freq2_input",1000000,30000);
        }
        if(auto v=linuxgpu::number(d.path/"gpu_busy_percent");v&&*v<=100)r.utilization=static_cast<unsigned>(*v);
        r.usedBytes=linuxgpu::number(d.path/"mem_info_vram_used");if(r.totalBytes&&r.usedBytes&&*r.usedBytes<=*r.totalBytes)r.freeBytes=*r.totalBytes-*r.usedBytes;
        r.status=r.powerControl?"amdgpu · Power Limit доступен в границах hwmon; применение проверяется чтением":"amdgpu · мониторинг; Power Limit недоступен по правам/возможностям драйвера";
        r.apiStatus={"runtime_status="+runtime,a.status};rows.push_back(std::move(r));
    }
    if(rows.empty()){GpuReading r;r.name="Radeon";r.status="Дискретный Radeon не обнаружен или тип GPU не подтверждён; см.graphics.json в дампе";rows.push_back(r);}return rows;
}
void RadeonGpu::setPowerLimit(const std::string& id,int value){std::lock_guard lock(impl_->gate);impl_->apply(id,value);}
void RadeonGpu::resetPowerLimit(const std::string& id){std::lock_guard lock(impl_->gate);impl_->apply(id,{});}
}
