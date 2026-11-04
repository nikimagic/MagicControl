#include "MagicControl/LaptopControl.h"
#include "MagicControl/LaptopChargeResource.h"
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>

namespace mc::linux_platform {std::unique_ptr<LaptopControl> createHlylSysfsTestControl(const std::filesystem::path&,bool);}
namespace {
namespace fs=std::filesystem;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void write(const fs::path& path,const std::string& value){std::ofstream out(path);out<<value<<'\n';check(bool(out),"fixture write failed");}
std::string read(const fs::path& path){std::ifstream in(path);std::string value;std::getline(in,value);return value;}
}
int main(){
    const fs::path root=fs::path(MAGICCONTROL_TEST_ROOT)/
        ("hlyl-sysfs-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    check(fs::create_directory(root),"fixture directory already exists");
    fs::create_directories(root/"platform");fs::create_directories(root/"keyboard");fs::create_directories(root/"supplies"/"BAT0");
    const auto outside=root.parent_path()/(root.filename().string()+"-outside.txt");
    struct Cleanup {fs::path root,outside;bool outsideCreated{};~Cleanup(){std::error_code e;
        for(const auto& file:{root/"platform"/"charge_control_thresholds",root/"supplies"/"BAT0"/"type",
            root/"supplies"/"BAT0"/"charge_control_start_threshold",root/"supplies"/"BAT0"/"charge_control_end_threshold"})fs::remove(file,e);
        if(outsideCreated)fs::remove(outside,e);
        for(const auto& dir:{root/"platform",root/"keyboard",root/"supplies"/"BAT0",root/"supplies",root})fs::remove(dir,e);
    }} cleanup{root,outside};
    const auto pair=root/"platform"/"charge_control_thresholds";write(pair,"80 95");
    bool rejected=false;try{static_cast<void>(mc::linux_platform::createHlylSysfsTestControl(fs::path("/"),true));}
    catch(const std::runtime_error&){rejected=true;}check(rejected,"fixture outside build root must be rejected");
    auto control=mc::linux_platform::createHlylSysfsTestControl(root,true);
    const auto initial=control->read();
    check(initial.connected&&initial.chargeWritable&&initial.chargeStart==80&&initial.chargeEnd==95,"pair fixture read");
    const auto result=mc::executeBatteryResource(*control,mc::BatteryWriteRequest{mc::BatteryWrite::Upper,85},{[](int){return 80;},
        [](int){throw std::logic_error("unexpected preference save");},[]{},{}});
    check(!result.empty()&&read(pair)=="80 85","pair fixture write and readback");
    auto denied=mc::linux_platform::createHlylSysfsTestControl(root,false);rejected=false;
    try{denied->setChargeThresholds(70,80);}catch(const std::runtime_error&){rejected=true;}
    check(rejected&&read(pair)=="80 85","rejected board must not write fixture");
    fs::remove(pair);
    const auto battery=root/"supplies"/"BAT0";write(battery/"type","Battery");
    write(battery/"charge_control_start_threshold","70");write(battery/"charge_control_end_threshold","80");
    control=mc::linux_platform::createHlylSysfsTestControl(root,true);
    const auto split=control->read();check(split.chargeWritable&&split.chargeStart==70&&split.chargeEnd==80,"split fixture read");
    control->setChargeThresholds(60,75);
    check(read(battery/"charge_control_start_threshold")=="60"&&read(battery/"charge_control_end_threshold")=="75",
        "split fixture write and readback");
    check(!fs::exists(outside),"outside guard fixture already exists");cleanup.outsideCreated=true;write(outside,"70 90");
    std::error_code symlinkError;fs::create_symlink(outside,pair,symlinkError);
    if(!symlinkError){
        rejected=false;try{control->setChargeThresholds(60,75);}catch(const std::runtime_error&){rejected=true;}
        check(rejected&&read(outside)=="70 90","symlink escape must not read or overwrite outside fixture");
    }
    std::printf("HLYL fixture: pair=passed split=passed identity=passed symlink=%s; real sysfs writes=0\n",
        symlinkError?"unavailable":"passed");
}
