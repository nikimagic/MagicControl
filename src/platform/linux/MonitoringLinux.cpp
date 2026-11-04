#include "MagicControl/Monitoring.h"
#include "MagicControl/BatteryIdentityHistory.h"
#include "MagicControl/BatteryJournal.h"
#include "MagicControl/NvidiaGpu.h"
#include "MagicControl/GpuDevice.h"
#include "MagicControl/IntelGpu.h"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace mc {
namespace {
namespace fs = std::filesystem;
std::string readText(const fs::path& path) {
    std::ifstream file(path);
    std::string text;
    std::getline(file, text);
    while (!text.empty() && (text.back() == '\r' || text.back() == ' ')) text.pop_back();
    return text;
}
std::optional<double> readNumber(const fs::path& path, double divisor = 1) {
    const auto text = readText(path);
    if (text.empty()) return {};
    try {
        std::size_t end{};
        const auto value = std::stod(text, &end) / divisor;
        if (end == text.size() && std::isfinite(value)) return value;
    } catch (...) {}
    return {};
}
std::vector<GpuDisplayRoute> displayRoutes(const std::vector<GraphicsAdapter>& adapters){
    std::vector<GpuDisplayRoute> routes;std::error_code error;
    for(const auto& entry:fs::directory_iterator("/sys/class/drm",error)){
        const auto name=entry.path().filename().string();const auto separator=name.find('-');
        if(separator==std::string::npos||!name.starts_with("card")||readText(entry.path()/"status")!="connected"||readText(entry.path()/"enabled")!="enabled")continue;
        auto parent=fs::canonical(entry.path(),error);if(error){error.clear();continue;}
        const GraphicsAdapter* match{};unsigned count{};
        for(int depth=0;depth<8&&!parent.empty();++depth,parent=parent.parent_path())
            for(const auto& adapter:adapters)if(!adapter.software&&!adapter.pci.empty()&&parent.filename().string()==adapter.pci){match=&adapter;++count;}
        if(count!=1)continue;
        routes.push_back({name.substr(separator+1),match->id,match->id,"Linux · DRM connected/enabled connector + PCI","Compositor primary not reported",false});
    }
    // boot_vga is not a compositor's current monitor. Only a single active
    // route can be the qualified default without a GDK connector hint.
    if(routes.size()==1)routes.front().primary=true;
    return routes;
}
std::vector<fs::path> directories(const fs::path& root) {
    std::vector<fs::path> result;
    std::error_code error;
    for (fs::directory_iterator i(root, error), end; !error && i != end; i.increment(error))
        if (i->is_directory(error)) result.push_back(i->path());
    std::sort(result.begin(), result.end());
    return result;
}
std::string processorModel() {
    std::ifstream input("/proc/cpuinfo");
    std::string line;
    while (std::getline(input, line)) {
        const auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        const auto keyEnd = line.find_last_not_of(" \t", colon - 1);
        const auto key = line.substr(0, keyEnd + 1);
        if (key != "model name" && key != "Hardware") continue;
        const auto start = line.find_first_not_of(" \t", colon + 1);
        return start == std::string::npos ? std::string{} : line.substr(start);
    }
    return {};
}
// The installed WSL DXCore library exposes the same read-only interfaces as
// the Windows SDK dxcore_interface.h, with the native Linux calling convention:
// microsoft/DirectX-Headers/include/wsl/stubs/basetsd.h. No renderer is created.
struct DxGuid { std::uint32_t a; std::uint16_t b, c; std::uint8_t d[8]; };
static_assert(sizeof(DxGuid) == 16);
template<class T> T dxMethod(void* object, int index) {
    return reinterpret_cast<T>((*reinterpret_cast<void***>(object))[index]);
}
struct DxLibrary {
    void* value{dlopen("/usr/lib/wsl/lib/libdxcore.so", RTLD_NOW | RTLD_LOCAL)};
    ~DxLibrary() { if (value) dlclose(value); }
};
struct DxObject {
    void* value{};
    ~DxObject() { if (value) dxMethod<std::uint32_t(*)(void*)>(value, 2)(value); }
};
std::string wslGraphicsModel() {
    constexpr DxGuid factoryId{0x78ee5945,0xc36e,0x4b13,{0xa6,0x69,0x00,0x5d,0xd1,0x1c,0x0f,0x06}};
    constexpr DxGuid listId{0x526c7776,0x40e9,0x459b,{0xb7,0x11,0xf3,0x2a,0xd7,0x6d,0xfc,0x28}};
    constexpr DxGuid adapterId{0xf0db4c7f,0xfe5a,0x42a2,{0xbd,0x62,0xf2,0xa6,0xcf,0x6f,0xc8,0x3e}};
    constexpr DxGuid graphics{0x0c9ece4d,0x2f6e,0x4f01,{0x8c,0x96,0xe8,0x9e,0x33,0x1b,0x47,0xb1}};
    using Result = std::int32_t;
    using Property = Result(*)(void*, std::uint32_t, std::size_t, void*);
    DxLibrary library;
    if (!library.value) return {};
    const auto create = reinterpret_cast<Result(*)(const DxGuid&, void**)>(dlsym(library.value,"DXCoreCreateAdapterFactory"));
    DxObject factory, list;
    if (!create || create(factoryId,&factory.value) < 0 || !factory.value) return {};
    if (dxMethod<Result(*)(void*,std::uint32_t,const DxGuid*,const DxGuid&,void**)>(factory.value,3)(
            factory.value,1,&graphics,listId,&list.value) < 0 || !list.value) return {};
    const auto count = std::min(32u,dxMethod<std::uint32_t(*)(void*)>(list.value,4)(list.value));
    std::string first;
    for (std::uint32_t index=0; index<count; ++index) {
        DxObject adapter;
        if (dxMethod<Result(*)(void*,std::uint32_t,const DxGuid&,void**)>(list.value,3)(list.value,index,adapterId,&adapter.value) < 0 || !adapter.value) continue;
        bool hardware{};
        if (dxMethod<Property>(adapter.value,6)(adapter.value,11,sizeof(hardware),&hardware) < 0 || !hardware) continue;
        std::size_t bytes{};
        if (dxMethod<Result(*)(void*,std::uint32_t,std::size_t*)>(adapter.value,7)(adapter.value,2,&bytes) < 0 || bytes < 2 || bytes > 4096) continue;
        std::vector<char> description(bytes);
        if (dxMethod<Property>(adapter.value,6)(adapter.value,2,bytes,description.data()) < 0) continue;
        const auto end = std::find(description.begin(),description.end(),'\0');
        if (end == description.end()) continue;
        std::string name(description.begin(),end);
        if (first.empty()) first = name;
        std::array<std::uint32_t,4> id{};
        if (dxMethod<Property>(adapter.value,6)(adapter.value,3,sizeof(id),id.data()) >= 0 && id[0] == 0x1002) return name;
    }
    return first;
}
std::string nativeGraphicsModel() {
    fs::path device;
    for (const auto& card : directories("/sys/class/drm")) {
        const auto name = card.filename().string();
        if (!name.starts_with("card") || name.find('-') != std::string::npos) continue;
        if (device.empty()) device = card / "device";
        if (readText(card / "device/vendor") == "0x1002") { device = card / "device"; break; }
    }
    if (device.empty()) return {};
    auto vendor = readText(device / "vendor"), product = readText(device / "device");
    if (!vendor.starts_with("0x") || !product.starts_with("0x")) return {};
    vendor.erase(0,2); product.erase(0,2);
    // Reuse the distribution's PCI name database. No external utility or new
    // package is required; a missing description stays unknown.
    for (const char* path : {"/usr/share/hwdata/pci.ids","/usr/share/misc/pci.ids"}) {
        std::ifstream input(path); std::string line; bool matchedVendor{};
        while (std::getline(input,line)) {
            if (line.empty() || line.front()=='#') continue;
            if (line.front()!='\t') {
                if (matchedVendor) break;
                matchedVendor=line.size()>4 && line.compare(0,4,vendor)==0;
            } else if (matchedVendor && line.size()>5 && line[1]!='\t' && line.compare(1,4,product)==0) {
                const auto start=line.find_first_not_of(" \t",5);
                if(start!=std::string::npos)return line.substr(start);
            }
        }
    }
    return {};
}
std::array<std::string, 2> memorySerials() {
    std::ifstream file("/sys/firmware/dmi/tables/DMI", std::ios::binary);
    std::vector<unsigned char> data;
    for (char c; data.size() < 1024 * 1024 && file.get(c);) data.push_back(static_cast<unsigned char>(c));
    std::vector<std::pair<std::string, std::string>> modules;
    for (std::size_t p = 0; p + 4 <= data.size();) {
        const auto length = data[p + 1];
        if (length < 4 || p + length > data.size()) break;
        auto end = p + length;
        while (end + 1 < data.size() && (data[end] || data[end + 1])) ++end;
        if (end + 1 >= data.size()) break;
        const auto field = [&](int offset) {
            std::string text;
            if (offset >= length || !data[p + offset]) return text;
            auto start = p + length;
            for (int n = 1; n < data[p + offset] && start < end; ++n) {
                while (start < end && data[start]) ++start;
                ++start;
            }
            while (start <= end && data[start]) text += static_cast<char>(data[start++]);
            return text;
        };
        if (data[p] == 17 && length > 24 && (data[p + 12] || data[p + 13])) modules.emplace_back(field(17) + "/" + field(16), field(24));
        if (data[p] == 127) break;
        p = end + 2;
    }
    std::stable_sort(modules.begin(), modules.end());
    std::array<std::string, 2> result{};
    for (std::size_t i = 0; i < std::min(result.size(), modules.size()); ++i) result[i] = modules[i].second;
    return result;
}
}
ProcessorVendor linuxCpuVendor() {
    std::ifstream file("/proc/cpuinfo"); std::string line;
    while(std::getline(file,line))if(line.starts_with("vendor_id")) {
        if(line.find("GenuineIntel")!=std::string::npos)return ProcessorVendor::Intel;
        if(line.find("AuthenticAMD")!=std::string::npos)return ProcessorVendor::Amd;
    }
    return ProcessorVendor::Unknown;
}
struct MonitoringReader::Impl {
    const ProcessorVendor cpuVendor{linuxCpuVendor()};
    const bool hasNvidia{nvidiaGpuPresent()};
    NvidiaGpu nvidia;
    IntelGpu displayIntel;
    std::vector<GpuReading> radeonRows;
    std::uint64_t radeonAtMs{};
    bool wsl{}, diagnostic{},collectGpuPresentation{};
    std::optional<std::array<std::string,2>> models;
    MonitoringSnapshot cache;
    std::chrono::steady_clock::time_point next{};
    std::optional<unsigned long long> previousTotal, previousIdle;
    std::optional<double> previousPackageEnergy;
    std::chrono::steady_clock::time_point packageAt{};
    Impl(bool detailed,bool presentation) : diagnostic(detailed),collectGpuPresentation(presentation) {
        auto release = readText("/proc/sys/kernel/osrelease");
        std::transform(release.begin(), release.end(), release.begin(), [](unsigned char c){return static_cast<char>(std::tolower(c));});
        wsl = release.find("microsoft") != std::string::npos;
    }
    void readProcessors(MonitoringSnapshot& sample, const std::vector<fs::path>& monitors) {
        auto& cpu = sample.processors[0]; auto& gpu = sample.processors[1];
        sample.processorSampledAtMs = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
        cpu.usageSource = "Linux · /proc/stat"; cpu.clockSource = "Linux · cpufreq";
        sample.processorVendors[0]=cpuVendor;
        {
            std::ifstream file("/proc/stat"); std::string name; file >> name;
            std::array<unsigned long long,8> times{};
            bool valid = name == "cpu";
            for (auto& value : times) if (!(file >> value)) valid = false;
            unsigned long long total{}; for (auto value : times) total += value;
            const auto idle = times[3] + times[4];
            if (valid && previousTotal && previousIdle && total > *previousTotal && idle >= *previousIdle)
                cpu.usagePercent = std::clamp(100.0 * (1.0 - static_cast<double>(idle-*previousIdle)/(total-*previousTotal)),0.0,100.0);
            if (valid) {previousTotal=total; previousIdle=idle;}
        }
        cpu.clockMhz = readNumber("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq",1000.0);
        for (const auto& path : monitors) {
            const auto name=readText(path/"name");
            if(name=="k10temp") {
                cpu.temperatureSource="Linux · k10temp Tctl";
                for(int index=1;index<=8;++index) {
                    const auto prefix="temp"+std::to_string(index);
                    const auto label=readText(path/(prefix+"_label"));
                    if(label=="Tctl" || (index==1&&label.empty())) {
                        cpu.celsius=readNumber(path/(prefix+"_input"),1000.0); cpu.temperatureSensorId=index;
                        if(cpu.celsius&&(*cpu.celsius<=0||*cpu.celsius>=150))cpu.celsius.reset();
                        break;
                    }
                }
            }
            if(name=="coretemp") {
                sample.processorVendors[0]=ProcessorVendor::Intel;
                for(int index=1;index<=16;++index) {
                    const auto prefix="temp"+std::to_string(index);
                    if(readText(path/(prefix+"_label"))=="Package id 0") {
                        const auto value=readNumber(path/(prefix+"_input"),1000.);
                        if(value&&*value>=0&&*value<=150){cpu.celsius=value;cpu.temperatureSource="Linux · Intel coretemp";}
                        sample.cpuTemperatureTargetCelsius=readNumber(path/(prefix+"_crit"),1000.);
                        break;
                    }
                }
            }
            if(name=="amdxdna") {
                sample.npuPowerWatts=readNumber(path/"power1_input",1000000.0);
                if(sample.npuPowerWatts&&(*sample.npuPowerWatts<0||*sample.npuPowerWatts>250))sample.npuPowerWatts.reset();
                sample.npuPowerSource="Linux · amdxdna";
            }
            if(name.find("uniwill")!=std::string::npos) {
                for(int channel=0;channel<2;++channel) {
                    auto& processor=sample.processors[channel];
                    processor.fanRpm=readNumber(path/("fan"+std::to_string(channel+1)+"_input"));
                    auto raw=readNumber(path/("pwm"+std::to_string(channel+1)));
                    if(raw&&*raw>=0&&*raw<=255)processor.fanPwm=*raw*100.0/255.0;
                    processor.fanSource="Linux · uniwill hwmon";
                }
            }
        }
        if(!wsl&&!hasNvidia){
            // One backend snapshot per stable adapter ID. Never combine hwmon
            // from the last card with clocks from the first DRM card.
            radeonAtMs=batteryMonotonicMs();radeonRows=radeon.read(true);const auto& devices=radeonRows;const GpuReading* selected=nullptr;
            for(const auto& r:devices)if(!r.uuid.empty()&&r.available&&r.type==GpuType::Discrete){selected=&r;break;}
            if(!selected)for(const auto& r:devices)if(!r.uuid.empty()&&r.available){selected=&r;break;}
            if(selected){const auto& r=*selected;sample.processorVendors[1]=ProcessorVendor::Amd;sample.processorModels[1]=r.name;
                gpu.celsius=r.temperature;gpu.usagePercent=r.utilization;gpu.clockMhz=r.graphicsMhz;
                if(r.powerMw)gpu.powerWatts=*r.powerMw/1000.;
                gpu.temperatureSource=gpu.usageSource=gpu.clockSource=gpu.powerSource=r.source+" · "+r.uuid;
                sample.additionalSensors.insert(sample.additionalSensors.end(),r.additionalSensors.begin(),r.additionalSensors.end());
            }
        }
        if(!wsl)for(const auto& zone:directories("/sys/class/powercap")) {
            if(readText(zone/"name")!="package-0")continue;
            const auto energy=readNumber(zone/"energy_uj");const auto now=std::chrono::steady_clock::now();
            if(energy&&previousPackageEnergy) {
                double delta=*energy-*previousPackageEnergy;
                if(delta<0)if(auto maximum=readNumber(zone/"max_energy_range_uj"))delta+=*maximum;
                const auto seconds=std::chrono::duration<double>(now-packageAt).count();
                const auto watts=delta/1000000.0/seconds;
                if(seconds>.05&&seconds<15&&std::isfinite(watts)&&watts>=0&&watts<250)cpu.powerWatts=watts;
            }
            if(energy){previousPackageEnergy=energy;packageAt=now;cpu.powerSource="Linux · RAPL";}
            break;
        }
    }
    std::string primaryBatteryId;
    std::vector<BatteryReading> readBatteryGroup() {
        std::vector<BatteryReading> readings;
        std::error_code enumerationError;std::vector<fs::path> supplies;
        for(fs::directory_iterator it("/sys/class/power_supply",enumerationError),end;!enumerationError&&it!=end;it.increment(enumerationError))supplies.push_back(it->path());
        std::sort(supplies.begin(),supplies.end());
        bool online{},onlineKnown{},externalSeen{},externalUnknown=bool(enumerationError);std::string powerStatus="power_supply external inputs:";
        for(const auto& supply:supplies)if(readText(supply/"type")!="Battery"){
            externalSeen=true;
            const auto value=readText(supply/"online");
            powerStatus+=" "+supply.filename().string()+".online="+(value.empty()?"not reported":value)+";";
            if(value=="1")online=true;
            else if(value!="0")externalUnknown=true;
        }
        // One known offline input cannot prove all other inputs are offline.
        // Battery discharge can also occur on an insufficient external supply.
        onlineKnown=online||(externalSeen&&!externalUnknown);
        for(const auto& path:supplies){
            if(readText(path/"type")!="Battery")continue;
            if(readText(path/"scope")=="Device")continue; // Peripheral batteries are not the laptop supply.
            BatteryReading b;b.deviceId=path.string();const auto serial=readText(path/"serial_number");
            if(!serial.empty())b.deviceId+=" / "+serial;
            b.model=readText(path/"model_name");b.source="Linux · power_supply";
            b.manufacturer=readText(path/"manufacturer");b.serial=serial;b.chemistry=readText(path/"technology");
            b.designCapacity=readNumber(path/"energy_full_design",1000.);b.fullCapacity=readNumber(path/"energy_full",1000.);b.remainingCapacity=readNumber(path/"energy_now",1000.);b.capacityUnit="mWh";
            if(!b.designCapacity||!b.fullCapacity){b.designCapacity=readNumber(path/"charge_full_design",1000.);b.fullCapacity=readNumber(path/"charge_full",1000.);b.remainingCapacity=readNumber(path/"charge_now",1000.);b.capacityUnit="mAh";}
            b.powerStatus=powerStatus;
            const auto present=readText(path/"present");if(present=="0"||present=="1")b.present=present=="1";
            b.online=online;b.onlineKnown=onlineKnown;
            if(b.present==false){readings.push_back(std::move(b));continue;}
            b.percent=readNumber(path/"capacity");
            if(b.percent&&(*b.percent<0||*b.percent>100))b.percent.reset();
            if(!b.percent){auto full=readNumber(path/"energy_full"),now=readNumber(path/"energy_now");
                if(!full||!now){full=readNumber(path/"charge_full");now=readNumber(path/"charge_now");}
                if(full&&now&&*full>0)b.percent=std::clamp(100.* *now/ *full,0.,100.);}
            auto full=readNumber(path/"energy_full"),design=readNumber(path/"energy_full_design");
            if(!full||!design){full=readNumber(path/"charge_full");design=readNumber(path/"charge_full_design");}
            if(full&&design&&*full>0&&*design>0)b.healthPercent=100.* *full/ *design;
            b.healthReadUnavailable=!full||!design;
            b.healthUnavailable=!b.healthReadUnavailable&&!b.healthPercent.has_value();
            const auto status=readText(path/"status");
            b.powerStatus+=" battery.status="+(status.empty()?"not reported":status);
            // Available kernel battery flags/thresholds, common to all models.
            // Missing sysfs attributes stay explicit, never assumed zero.
            for(const auto* field:{"present","health","capacity_level","charge_type","charge_behaviour",
                                  "charge_control_start_threshold","charge_control_end_threshold","alarm"}){
                const auto raw=readText(path/field);
                b.powerStatus+=" "+std::string(field)+"="+(raw.empty()?"not reported":raw)+";";
            }
            b.flow=status=="Charging"?BatteryFlow::Charging:status=="Discharging"?BatteryFlow::Discharging:
                status=="Full"||status=="Not charging"?BatteryFlow::Idle:BatteryFlow::Unknown;
            if(const auto seconds=readNumber(path/"time_to_empty_now");seconds&&*seconds>=0&&*seconds<UINT32_MAX)b.estimatedDischargeSeconds=static_cast<std::uint32_t>(*seconds);
            if(const auto seconds=readNumber(path/"time_to_full_now");seconds&&*seconds>=0&&*seconds<UINT32_MAX)b.estimatedChargeSeconds=static_cast<std::uint32_t>(*seconds);
            b.celsius=readNumber(path/"temp",10.);if(!b.celsius)b.celsius=readNumber(path/"temp_now",10.);
            if(b.celsius&&(*b.celsius < -40||*b.celsius>100))b.celsius.reset();
            b.temperatureSource="Linux · power_supply";b.temperatureStatus=b.celsius?"ok":"power_supply: temperature not reported";
            b.volts=readNumber(path/"voltage_now",1000000.);if(b.volts&&*b.volts<=0)b.volts.reset();
            b.milliamps=readNumber(path/"current_now",1000.);if(b.milliamps)b.milliamps=std::abs(*b.milliamps);
            if(const auto cycles=readNumber(path/"cycle_count");cycles&&*cycles>=0&&*cycles<=UINT32_MAX&&std::floor(*cycles)==*cycles)b.cycles=static_cast<std::uint32_t>(*cycles);
            b.watts=readNumber(path/"power_now",1000000.);if(!b.watts&&b.milliamps&&b.volts)b.watts=*b.milliamps* *b.volts/1000.;
            stampBatteryReading(b);readings.push_back(std::move(b));
        }
        if(readings.empty()){
            BatteryReading b;b.deviceId="Linux/power_supply/aggregate";b.source=wsl?"Linux · WSL: нет доступа":"Linux · power_supply";
            b.online=online;b.onlineKnown=onlineKnown;
            b.powerStatus=powerStatus+(enumerationError?" enumeration_error="+enumerationError.message():" no system battery interfaces");
            if(!wsl&&!enumerationError)b.present=false;
            b.temperatureStatus=enumerationError?enumerationError.message():"no system battery interfaces";readings.push_back(std::move(b));
        }
        return readings;
    }
    std::string readBattery(MonitoringSnapshot& sample) {
        sample.batteries=readBatteryGroup();
        auto primary=std::find_if(sample.batteries.begin(),sample.batteries.end(),[&](const auto& b){return b.deviceId==primaryBatteryId&&b.present!=false;});
        if(primary==sample.batteries.end())primary=std::find_if(sample.batteries.begin(),sample.batteries.end(),[](const auto& b){return b.present!=false;});
        if(primary==sample.batteries.end())primary=sample.batteries.begin();
        primaryBatteryId=primary->deviceId;sample.battery=*primary;
        const auto suffix=sample.battery.deviceId.find(" / ");
        return fs::path(sample.battery.deviceId.substr(0,suffix)).filename().string();
    }
    RadeonGpu radeon;
    MonitoringSnapshot read() {
        if (!diagnostic && std::chrono::steady_clock::now() < next) return cache;
        next = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        if (!models) models = std::array<std::string,2>{processorModel(),wsl ? wslGraphicsModel() : nativeGraphicsModel()};
        MonitoringSnapshot sample;sample.diagnostic=diagnostic;
        sample.processorModels = *models;
        sample.battery.source = wsl ? "Linux · WSL: нет доступа" : "Linux · power_supply";
        const auto serials = memorySerials();
        for (int i = 0; i < 2; ++i) {
            sample.memory[i].serial = serials[i];
            sample.memory[i].source = "Linux · hwmon";
            sample.disks[i].source = wsl ? "Linux · WSL: нет доступа" : "Linux · накопители";
        }
        const auto batteryDevice=readBattery(sample);
        int diskIndex{},memoryIndex{};
        const auto monitors = directories("/sys/class/hwmon");
        readProcessors(sample,monitors);
        std::vector<GpuProviderReading> displayReadings;
        if(hasNvidia&&!wsl){if(collectGpuPresentation){const auto at=batteryMonotonicMs();auto rows=nvidia.read();readNvidiaMonitoring(rows,sample);for(auto& row:rows)displayReadings.push_back({0x10de,std::move(row),at});}else readNvidiaMonitoring(nvidia,sample);}
        if(collectGpuPresentation)sample.gpuCards.initialized=true;
        if(collectGpuPresentation&&!wsl)try{
            const auto adapters=graphicsAdapters();
            const auto routes=displayRoutes(adapters);
            sample.gpuCards=gpuPresentation(adapters,{},routes);
            if(std::any_of(adapters.begin(),adapters.end(),[](const auto& a){return a.vendorId==0x8086&&!a.software;})){const auto at=batteryMonotonicMs();auto rows=displayIntel.read();for(auto& row:rows)displayReadings.push_back({0x8086,std::move(row),at});}
            if(hasNvidia){radeonAtMs=batteryMonotonicMs();radeonRows=radeon.read(true);}
            for(const auto& row:radeonRows)displayReadings.push_back({0x1002,row,radeonAtMs});
            sample.gpuCards=gpuPresentation(adapters,std::move(displayReadings),routes,batteryMonotonicMs());
        }catch(const std::exception& error){sample.gpuCards.status=std::string("GPU display catalog unavailable: ")+error.what();}
        for (const auto& path : monitors) {
            const auto name = readText(path / "name");
            auto celsius = readNumber(path / "temp1_input", 1000.0);
            if (celsius && (*celsius < -20 || *celsius > 150)) celsius.reset();
            if (!sample.battery.celsius && !batteryDevice.empty() && name == batteryDevice &&
                celsius && *celsius >= -40 && *celsius <= 100)
                {sample.battery.celsius = celsius;sample.battery.temperatureSource="Linux · hwmon";sample.battery.temperatureStatus="ok";sample.battery.dynamicAtMs[2]=batteryMonotonicMs();}
            if (name == "spd5118") {
                std::error_code error;
                const auto device = fs::canonical(path / "device", error).filename().string();
                const int index = memoryIndex<2?memoryIndex++:-1;
                if (index >= 0) {
                    sample.memory[index].present = true;
                    sample.memory[index].celsius = celsius;
                    sample.memory[index].serial.clear(); // SMBIOS order is not an I2C address mapping.
                    sample.memory[index].model="DDR5 · I2C "+device;
                    sample.memory[index].source = "Linux · spd5118";
                    sample.memory[index].status=celsius?"ok":"hwmon: температура не сообщается";
                }
            }
        }
        if (!wsl) {
            for (const auto& nvme : directories("/sys/class/nvme")) {
                if (diskIndex == 2) break;
                auto& disk = sample.disks[diskIndex++];
                disk.present = true; disk.model = readText(nvme / "model"); disk.serial = readText(nvme / "serial");
                disk.source = "Linux · NVMe";
                std::error_code error;
                const auto controller = fs::canonical(nvme, error);
                if (error) continue;
                for (const auto& path : monitors) {
                    if (readText(path / "name") != "nvme") continue;
                    const auto device = fs::canonical(path / "device", error);
                    if (!error && device == controller) {
                        disk.celsius = readNumber(path / "temp1_input", 1000.0);
                        if (disk.celsius && (*disk.celsius < 0 || *disk.celsius >= 150)) disk.celsius.reset();
                    }
                }
            }
            // A missing hwmon sensor does not establish that a disk is absent.
            // Enumerate native SCSI/SATA disks too; permissions remain unknown.
            for (const auto& block : directories("/sys/block")) {
                if (diskIndex == 2) break;
                if (!block.filename().string().starts_with("sd")) continue;
                auto& disk = sample.disks[diskIndex++]; disk.present = true;
                disk.model = readText(block / "device/model"); disk.serial = readText(block / "device/serial");
                disk.source = "Linux · накопители";
            }
            while (diskIndex < 2) sample.disks[diskIndex++].present = false;
        }
        if(!diagnostic)retainBatteryReadings(sample.battery, cache.battery);
        for(auto& battery:sample.batteries)if(battery.deviceId==sample.battery.deviceId)battery=sample.battery;
        cache = std::move(sample);
        return cache;
    }
};
MonitoringReader::MonitoringReader(EcAccess*, bool diagnostic,bool collectGpuPresentation) : impl_(std::make_unique<Impl>(diagnostic,collectGpuPresentation)) {}
MonitoringReader::~MonitoringReader() = default;
MonitoringSnapshot MonitoringReader::read() { auto sample=impl_->read();journalBattery(sample.battery,"OS");
    if(!impl_->diagnostic)observeBatteryIdentity(sample.battery.manufactured,"Linux battery",false);
    return sample; }
BatteryReading MonitoringReader::readBatteryOnly(BatteryReading* osObservation) {
    MonitoringSnapshot sample;impl_->readBattery(sample);
    if(osObservation)*osObservation=sample.battery;
    if(!impl_->diagnostic)retainBatteryReadings(sample.battery,impl_->cache.battery);
    impl_->cache.battery=sample.battery;journalBattery(sample.battery,"OS");
    if(!impl_->diagnostic)observeBatteryIdentity(sample.battery.manufactured,"Linux battery",false);
    return sample.battery;
}
EcBatteryReading MonitoringReader::readEcBatteryOnly(){return {};}
BatteryPowerPolicy MonitoringReader::readBatteryPowerPolicy(){return {};}
BatteryReading MonitoringReader::readOsBatteryOnly(){MonitoringSnapshot sample;impl_->readBattery(sample);return sample.battery;}
std::vector<BatteryReading> MonitoringReader::readBatteries(){
    MonitoringSnapshot sample;impl_->readBattery(sample);
    if(!impl_->diagnostic)retainBatteryReadings(sample.battery,impl_->cache.battery);
    impl_->cache.battery=sample.battery;
    for(auto& battery:sample.batteries)if(battery.deviceId==sample.battery.deviceId)battery=sample.battery;
    return sample.batteries;
}
} // namespace mc
