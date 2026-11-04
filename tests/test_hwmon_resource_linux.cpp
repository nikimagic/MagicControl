#include "../src/platform/linux/HwmonResourceProvider.h"
#include "MagicControl/EcAccess.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs=std::filesystem;
int main(){
    const auto check=[](bool ok,const char* what){if(!ok){std::cerr<<what<<'\n';std::exit(1);}};
    const auto root=fs::temp_directory_path()/("magiccontrol-hwmon-test-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto active=root/"hwmon0",asleep=root/"hwmon1";
    fs::create_directories(active/"device/power");fs::create_directories(asleep/"device/power");
    const auto put=[](const fs::path& path,const char* value){std::ofstream file(path);file<<value;if(!file)throw std::runtime_error("fixture write failed");};
    put(active/"name","mockchip\n");put(active/"temp1_label","CPU\n");put(active/"temp1_input","42625\n");
    put(active/"temp2_input","invalid\n");put(active/"fan1_input","1800\n");put(active/"in1_input","1250\n");
    put(active/"temp3_input","39500\n");put(active/"temp3_fault","1\n");
    put(active/"fan2_input","2000\n");put(active/"fan2_fault","invalid\n");put(active/"fan1_fault","0\n");
    put(active/"power1_input","12500000\n");put(active/"pwm1","200\n");put(active/"tempX_input","1\n");
    put(active/"temp3_max","99000\n");put(active/"device/power/runtime_status","active\n");
    put(asleep/"name","suspended\n");put(asleep/"temp1_input","99000\n");put(asleep/"device/power/runtime_status","suspended\n");
    struct Cleanup {fs::path root;~Cleanup(){std::error_code ignored;
        for(const auto& f:{"name","temp1_label","temp1_input","temp2_input","temp3_input","temp3_fault","fan1_input","fan1_fault","fan2_input","fan2_fault","in1_input","power1_input","pwm1","tempX_input","temp3_max","device/power/runtime_status"})fs::remove(root/"hwmon0"/f,ignored);
        for(const auto& f:{"name","temp1_input","device/power/runtime_status"})fs::remove(root/"hwmon1"/f,ignored);
        for(const auto& d:{"hwmon0/device/power","hwmon0/device","hwmon0","hwmon1/device/power","hwmon1/device","hwmon1"})fs::remove(root/d,ignored);
        fs::remove(root,ignored);}} cleanup{root};
    const auto scan=mc::linux_platform::scanHwmonResources(root);
    check(scan.availability==mc::ResourceAvailability::Available&&scan.readings.size()==7,"only active standard inputs are read");
    const auto find=[&](const char* metric)->const mc::SensorReading*{
        const auto it=std::find_if(scan.readings.begin(),scan.readings.end(),[&](const auto& r){return r.metric==metric;});
        return it==scan.readings.end()?nullptr:&*it;};
    const auto* temp=find("temp1_input");const auto* fan=find("fan1_input");const auto* power=find("power1_input");
    const auto* voltage=find("in1_input");const auto* invalid=find("temp2_input");
    check(temp&&temp->value==42.625&&temp->raw==42625&&temp->unit=="C"&&temp->device.find("CPU")!=std::string::npos&&
        temp->timestampOrigin==mc::SensorTimestampOrigin::SoftwareObservation,"temperature units label and software observation time");
    check(fan&&fan->value==1800&&fan->unit=="RPM"&&power&&power->value==12.5&&power->unit=="W"&&
        voltage&&voltage->value==1.25&&voltage->unit=="V","fan power voltage units");
    check(invalid&&!invalid->value&&!invalid->raw&&invalid->status!="ok","invalid input remains unknown");
    const auto* faultTemp=find("temp3_input");const auto* invalidFault=find("fan2_input");
    check(faultTemp&&!faultTemp->value&&faultTemp->raw==39500&&faultTemp->status=="hwmon sensor fault",
        "faulted temperature has no trusted measurement but retains raw input");
    check(invalidFault&&!invalidFault->value&&invalidFault->raw==2000&&invalidFault->status=="hwmon fault status unreadable or invalid",
        "invalid fault status is unknown and preserves raw fan input");
    check(!find("pwm1")&&!find("tempX_input")&&!find("temp3_max"),"no PWM or configuration fields are accessed");
    std::ifstream pwm(active/"pwm1");int unchanged{};pwm>>unchanged;check(unchanged==200,"fixture PWM remains unchanged");
    mc::BoardDescriptor board;int scans=0;
    mc::linux_platform::HwmonResourceProvider provider(board,[&]{++scans;return scan;});
    auto temporary=mc::linux_platform::HwmonResourceProvider(mc::BoardDescriptor{"temporary hwmon machine",{}, {},{}},[&]{return scan;});
    check(temporary.observe().machine.manufacturer=="temporary hwmon machine","hwmon provider owns machine snapshot");
    const auto catalog=provider.observe();
    check(scans==1&&!catalog.machine.catalogRecognized&&catalog.sources.size()==1&&
        catalog.sources[0].availability==mc::ResourceAvailability::Available&&catalog.resources.size()==7,"read-only source catalog");
    for(const auto& row:catalog.resources)check(row.kind==mc::ResourceKind::Sensor&&row.identity.resource.empty()&&
        row.facets[0].availability==mc::ResourceAvailability::Unknown,"sensor identity and capability are not invented");
    const auto missing=mc::linux_platform::scanHwmonResources(root/"missing");
    check(missing.availability==mc::ResourceAvailability::Unknown&&missing.readings.empty(),"missing hwmon remains unknown");
    std::stop_source stop;stop.request_stop();const auto cancelled=mc::linux_platform::scanHwmonResources(root,stop.get_token());
    check(cancelled.availability==mc::ResourceAvailability::Unknown&&cancelled.readings.empty(),"cancelled scan remains unknown");
    std::cout<<"hwmon resource checks passed\n";
}
