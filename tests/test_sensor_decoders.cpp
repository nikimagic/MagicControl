#include "MagicControl/SensorDecoders.h"
#include "MagicControl/Monitoring.h"
#include "../src/gui/common/MonitoringCards.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
namespace {
unsigned checks{};
void check(bool condition){++checks;if(!condition){std::fprintf(stderr,"Sensor check %u failed\n",checks);std::exit(1);}}
template<class T>void put(std::vector<std::uint8_t>& bytes,std::size_t p,T value){std::memcpy(bytes.data()+p,&value,sizeof(value));}
}
int main(){
    using namespace mc;
    check(!batteryTemperatureCelsius(0));check(!batteryTemperatureCelsius(UINT32_MAX));
    check(std::abs(*batteryTemperatureCelsius(3031)-29.95)<.001);
    check(!batteryTemperatureCelsius(4000));
    check(spd5118TemperatureCelsius(160*4)==40.0);
    check(spd5118TemperatureCelsius(0x7ec*4)==-5.0);
    check(spd5118TemperatureCelsius(0)==0);
    check(!spd5118TemperatureCelsius(800*4));
    std::vector<std::uint8_t> storage(56);
    put<std::uint32_t>(storage,0,40);put<std::uint32_t>(storage,4,56);put<std::uint16_t>(storage,12,2);
    put<std::uint16_t>(storage,24,0);put<std::uint16_t>(storage,26,0x8000);
    put<std::uint16_t>(storage,40,1);put<std::uint16_t>(storage,42,41);
    check(storageTemperatureCelsius(storage)==41);
    put<std::int16_t>(storage,26,0);check(storageTemperatureCelsius(storage)==0);
    put<std::uint16_t>(storage,12,3);check(!storageTemperatureCelsius(storage));
    storage.resize(12);check(!storageTemperatureCelsius(storage));
    std::vector<std::uint8_t> nvme(560);
    put<std::uint32_t>(nvme,0,48);put<std::uint32_t>(nvme,4,48);
    put<std::uint32_t>(nvme,8,3);put<std::uint32_t>(nvme,12,2);
    put<std::uint32_t>(nvme,24,40);put<std::uint32_t>(nvme,28,512);put<std::uint16_t>(nvme,49,314);
    check(std::abs(*nvmeSmartTemperatureCelsius(nvme)-40.85)<.001);
    put<std::uint32_t>(nvme,24,UINT32_MAX);check(!nvmeSmartTemperatureCelsius(nvme));
    put<std::uint32_t>(nvme,24,40);put<std::uint16_t>(nvme,49,65535);check(!nvmeSmartTemperatureCelsius(nvme));
    put<std::uint16_t>(nvme,49,0);check(!nvmeSmartTemperatureCelsius(nvme));
    nvme.resize(559);check(!nvmeSmartTemperatureCelsius(nvme));
    for(std::size_t n=0;n<48;++n)check(!nvmeSmartTemperatureCelsius(std::span(nvme).first(n)));
    BatteryReading previous;previous.present=true;previous.celsius=31.5;previous.temperatureSource="test";
    BatteryReading current;current.temperatureStatus="failed";retainBatteryReadings(current,previous);
    check(current.celsius==31.5);check(current.temperatureSource=="test");
    check(current.temperatureStatus.find("failed")!=std::string::npos);
    current.present=false;retainBatteryReadings(current,previous);check(!current.celsius);check(current.temperatureSource.empty());
    MonitoringSnapshot sample;sample.memory[0].celsius=40;sample.memory[0].model="DDR5 · SMBus 0x50";
    check(gui::ui::monitoringCard(sample,1).detail==sample.memory[0].model);
    sample.memory[0].serial="F504BBC2";check(gui::ui::monitoringCard(sample,1).detail=="S/N: F504BBC2");
    std::printf("Sensor decoders: %u checks passed; no hardware access\n",checks);
}
