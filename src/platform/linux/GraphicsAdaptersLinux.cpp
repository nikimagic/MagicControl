#include "GpuLinuxInternal.h"
// Raw upstream UAPI annotations are empty in user space.
#ifndef __user
#define __user
#define MC_UNDEF_USER
#endif
#include "../../third_party/drm/include/amdgpu_drm.h"
#ifdef MC_UNDEF_USER
#undef __user
#undef MC_UNDEF_USER
#endif
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <format>
#include <algorithm>
#include <charconv>
#include <string_view>
namespace mc::linuxgpu {
namespace {
void pciIdentity(GraphicsAdapter& adapter){
    const std::string_view bdf=adapter.pci;if(bdf.size()!=12||bdf[4]!=':'||bdf[7]!=':'||bdf[10]!='.')return;
    const auto hex=[](std::string_view text)->std::optional<unsigned>{unsigned value{};const auto result=std::from_chars(text.data(),text.data()+text.size(),value,16);return result.ec==std::errc{}&&result.ptr==text.data()+text.size()?std::optional<unsigned>(value):std::nullopt;};
    const auto domain=hex(bdf.substr(0,4)),bus=hex(bdf.substr(5,2)),device=hex(bdf.substr(8,2)),function=hex(bdf.substr(11,1));
    if(!domain||!bus||!device||!function||*device>31||*function>7)return;
    adapter.location={domain,bus,device,function,"Canonical PCI sysfs device path (domain:bus:device.function)"};
    adapter.physicalId="linux-pci:"+adapter.pci;adapter.identitySource=adapter.location.source;adapter.identityStatus="Canonical full PCI location; no name matching";
}
}
std::vector<Device> devices(){
    std::vector<Device> rows;std::error_code error;
    try{
    for(const auto& entry:std::filesystem::directory_iterator("/sys/class/drm",error)){
        const auto name=entry.path().filename().string();if(!name.starts_with("card")||name.size()<5||name.find_first_not_of("0123456789",4)!=std::string::npos)continue;
        const auto path=std::filesystem::canonical(entry.path()/"device",error);if(error)continue;
        const auto vendor=number(path/"vendor",16),device=number(path/"device",16);if(!vendor||!device)continue;
        Device row;row.path=path;auto& r=row.adapter;r.vendorId=static_cast<unsigned>(*vendor);r.deviceId=static_cast<unsigned>(*device);
        const auto subDevice=number(path/"subsystem_device",16),subVendor=number(path/"subsystem_vendor",16);
        if(subDevice&&subVendor&&*subDevice<=65535&&*subVendor<=65535)r.subsystemId=static_cast<unsigned>((*subDevice<<16)|*subVendor);
        r.revision=static_cast<unsigned>(number(path/"revision",16).value_or(0));
        r.pci=path.filename().string();r.id="pci:"+r.pci+":"+std::format("{:04x}:{:04x}",r.vendorId,r.deviceId)+":"+text(path/"unique_id");
        pciIdentity(r);
        r.name=text(path/"product_name");if(r.name.empty())r.name=std::format("{} GPU [{:04X}:{:04X}]",r.vendorId==0x1002?"AMD Radeon":r.vendorId==0x8086?"Intel":r.vendorId==0x10de?"NVIDIA":"PCI",r.vendorId,r.deviceId);
        auto driver=std::filesystem::canonical(path/"driver",error);if(!error)r.driver=driver.filename().string();error.clear();
        if(const auto boot=number(path/"boot_vga"))r.bootVga=*boot==1;
        r.primarySource="Linux · desktop primary unavailable; boot_vga retained separately";r.dedicatedBytes=number(path/"mem_info_vram_total");
        r.status="PCI/sysfs: тип integrated/discrete не подтверждён";
        const auto runtime=text(path/"power/runtime_status");
        if(r.vendorId==0x1002&&r.driver=="amdgpu"&&runtime!="suspended"&&runtime!="suspending"){
            for(const auto& node:std::filesystem::directory_iterator(path/"drm",error)){
                const auto render=node.path().filename().string();if(!render.starts_with("renderD"))continue;
                const int fd=::open(("/dev/dri/"+render).c_str(),O_RDONLY|O_CLOEXEC);if(fd<0)continue;
                drm_amdgpu_info_device info{};drm_amdgpu_info query{};query.return_pointer=reinterpret_cast<std::uintptr_t>(&info);query.return_size=sizeof(info);query.query=AMDGPU_INFO_DEV_INFO;
                if(ioctl(fd,DRM_IOCTL_AMDGPU_INFO,&query)==0){r.type=(info.ids_flags&AMDGPU_IDS_FLAGS_FUSION)?GpuType::Integrated:GpuType::Discrete;r.status="AMDGPU_INFO_DEV_INFO: FUSION/APU flag подтверждён";}
                ::close(fd);break;
            }
            error.clear();
        }
        rows.push_back(std::move(row));
    }
    }catch(const std::exception&){} // Hot-unplug must not terminate a GPU page worker.
    std::sort(rows.begin(),rows.end(),[](const auto& a,const auto& b){return a.adapter.id<b.adapter.id;});return rows;
}
}
namespace mc {
std::vector<GraphicsAdapter> graphicsAdapters(){std::vector<GraphicsAdapter> rows;for(auto& d:linuxgpu::devices())rows.push_back(std::move(d.adapter));return rows;}
}
