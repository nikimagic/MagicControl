#include "../../../src/devices/minisforum/hpbsd/TelemetryWindows.h"
#include "../../../src/devices/minisforum/hpbsd/I2EcWindows.h"
#include "../../../src/devices/minisforum/hpbsd/FanControlWindows.h"
#include "../../../src/protocols/minisforum/HpbsdFans.h"
#include "PawnTelemetryWindows.h"
#include "ApuPowerWindows.h"
#include "MagicControl/BoardIdentity.h"
#include "MagicControl/Logging.h"
#include "MagicControl/PortableSettings.h"
#include <windows.h>
#include <iostream>
#include <stdexcept>
#include <fstream>
#include <thread>
#include <atomic>
#include <cmath>
#include <vector>
int main(int argc,char** argv){
    try{
        const auto board=mc::readBoardDescriptor();
        if(board.manufacturer!="Micro Computer (HK) Tech Limited"||board.baseBoard!="HPBSD"||board.productName!="EliteMini Series")throw std::runtime_error("Exact HPBSD required");
        mc::windows::HpbsdTelemetryReader ec;
        mc::windows::PawnTelemetryReader cpu(true,true);
        mc::windows::ApuPowerReader smu;
        mc::windows::HpbsdFanController fans;
        const auto value=[](const auto& v){return v?std::to_string(*v):std::string("null");};
        if(argc==2&&std::string(argv[1])=="--curve-cycle"){
            const auto initial=fans.read();if(!initial.writable||initial.mode!=0||!initial.curves)throw std::runtime_error("Curve test requires native Default");
            const auto before=[&](){mc::windows::HpbsdI2EcSession io;auto data=io.readRamBlock(0x310,24);const auto append=[&](int a,int n){for(int i=0;i<n;++i)data.push_back(io.read(static_cast<std::uint16_t>(a+i)));};append(0x8b0,8);append(0x330,9);append(0x8c0,3);append(0x88a,2);io.finish();return data;}();
            auto recipe=initial.curves->draft;recipe[0][0].temperature=32;recipe[1][0].temperature=34;
            const std::array<int,8> cpuPercent{40,45,50,60,70,80,90,100};for(int i=0;i<8;++i)recipe[0][i].duty=cpuPercent[i];recipe[1][0].duty=40;recipe[1][1].duty=55;
            try{fans.setCurves(recipe);for(int i=0;i<12;++i){mc::MonitoringSnapshot s;ec.read(s);const auto state=fans.read();mc::windows::HpbsdI2EcSession io;
                const int cpuTarget=io.read(0x884),systemTarget=io.read(0x885),cpuDuty=io.read(0x1803),systemDuty=io.read(0x1804),cycle=io.read(0x1841);io.finish();
                std::cout<<"CURVE second="<<i<<" mode="<<state.mode<<" applied="<<(state.curves&&state.curves->appliedCurve)<<" CPU_RPM="<<value(s.processors[0].fanRpm)<<" SYSTEM_RPM="<<value(s.processors[1].fanRpm)<<" CPU_PWM="<<value(s.processors[0].fanPwm)<<" SYSTEM_PWM="<<value(s.processors[1].fanPwm)<<" duty_raw="<<cpuDuty<<'/'<<systemDuty<<" cycle="<<cycle<<" target_codes="<<cpuTarget<<'/'<<systemTarget<<std::endl;Sleep(1000);}
                fans.setAutomatic();const auto restored=fans.read();if(restored.mode!=0)throw std::runtime_error("Default not restored");
                mc::windows::HpbsdI2EcSession io;auto after=io.readRamBlock(0x310,24);for(const auto [a,n]:{std::pair{0x8b0,8},std::pair{0x330,9},std::pair{0x8c0,3},std::pair{0x88a,2}})for(int i=0;i<n;++i)after.push_back(io.read(static_cast<std::uint16_t>(a+i)));io.finish();
                if(after!=before)throw std::runtime_error("Default exact-policy mismatch");std::cout<<"CURVE_CYCLE_COMPLETE factory_bytes_restored=true hot_stages_retained=true\n";
            }catch(...){try{fans.setAutomatic();}catch(const std::exception& e){std::cerr<<"RESTORE_FAILURE "<<e.what()<<'\n';}throw;}
            return 0;
        }
        if(argc==2&&(std::string(argv[1])=="--power-cycle"||std::string(argv[1])=="--power-cycle-low")){
            mc::initializeLogging(true,std::filesystem::current_path()/"logs");
            const auto policy=[](){std::vector<unsigned> bytes;mc::windows::HpbsdI2EcSession io;
                for(const auto [a,n]:{std::pair{0x310,24},std::pair{0x8b0,8},std::pair{0x330,9},std::pair{0x8c0,3},std::pair{0x88a,2},std::pair{0x32f,1}})for(int i=0;i<n;++i)bytes.push_back(io.read(static_cast<std::uint16_t>(a+i)));io.finish();return bytes;};
            const auto beforePolicy=policy();const auto before=smu.read(true);
            if(before.stale||before.tableVersion!=0x4c0009||!before.stapmLimitWatts||!before.slowLimitWatts||!before.fastLimitWatts)throw std::runtime_error("Fresh baseline unavailable");
            const std::array<int,3> baseline{int(std::lround(*before.stapmLimitWatts*1000)),int(std::lround(*before.slowLimitWatts*1000)),int(std::lround(*before.fastLimitWatts*1000))};
            std::ofstream file("hpbsd-power-baseline.txt",std::ios::out|std::ios::trunc);if(!file)throw std::runtime_error("Cannot persist power baseline");file<<baseline[0]<<' '<<baseline[1]<<' '<<baseline[2]<<'\n';file.flush();if(!file)throw std::runtime_error("Power baseline flush failed");file.close();
            std::cout<<"BASELINE_mW="<<baseline[0]<<'/'<<baseline[1]<<'/'<<baseline[2]<<std::endl;
            const auto phase=[&](const char* name,int seconds,bool load){
                std::atomic<unsigned long long> work{};std::vector<std::jthread> threads;
                if(load)for(unsigned i=0;i<std::min(16u,std::max(1u,std::thread::hardware_concurrency()));++i)threads.emplace_back([&](std::stop_token stop){double x=1.0001;while(!stop.stop_requested()){for(int j=0;j<2000;++j)x=std::sqrt(x+1.0001);work.fetch_xor(static_cast<unsigned long long>(x*1e9),std::memory_order_relaxed);}});
                for(int i=0;i<seconds;++i){mc::MonitoringSnapshot s;s.diagnostic=true;cpu.read(s);ec.read(s);const auto pm=smu.read(true);
                    std::cout<<"phase="<<name<<" second="<<i<<" STAPM="<<value(pm.stapmLimitWatts)<<" Slow="<<value(pm.slowLimitWatts)<<" Fast="<<value(pm.fastLimitWatts)<<" APU_W="<<value(pm.fastPowerWatts)<<" package_W="<<value(s.apuPackagePowerWatts)<<" CPU_C="<<value(s.processors[0].celsius)<<" RAM0_C="<<value(s.memory[0].celsius)<<" RAM1_C="<<value(s.memory[1].celsius)<<std::endl;
                    if(s.processors[0].celsius&&*s.processors[0].celsius>=85)throw std::runtime_error("Probe thermal cutoff85C");Sleep(1000);
                }
            };
            const std::array<int,3> target=std::string(argv[1])=="--power-cycle-low"?std::array<int,3>{8000,10000,12000}:std::array<int,3>{20000,25000,30000};
            try{phase("BASELINE",5,true);smu.applyHpbsdLimitsMilliwatts(target);phase("LIMITED",10,true);smu.applyHpbsdLimitsMilliwatts(baseline);phase("RESTORED",3,false);
                if(policy()!=beforePolicy)throw std::runtime_error("Fan policy changed during power probe");
                std::cout<<"POWER_CYCLE_COMPLETE baseline_verified=true fan_policy_unchanged=true\n";
            }catch(...){try{smu.applyHpbsdLimitsMilliwatts(baseline);std::cerr<<"RESTORE_CONFIRMED\n";}catch(const std::exception& e){std::cerr<<"RESTORE_FAILURE "<<e.what()<<'\n';}throw;}
            return 0;
        }
        {mc::windows::HpbsdI2EcSession io;std::cout<<"CPU_SELECTOR="<<int(io.read(0x32f))<<" CPU_TABLE=";for(int i=0;i<24;++i)std::cout<<int(io.read(0x310+i))<<',';
            std::cout<<" CPU_SLOPES=";for(int i=0;i<8;++i)std::cout<<int(io.read(0x8b0+i))<<',';
            std::cout<<" SYSTEM_TABLE=";for(int i=0;i<9;++i)std::cout<<int(io.read(0x330+i))<<',';std::cout<<'\n';io.finish();}
        const auto initial=fans.read();std::cout<<"FAN_ADMISSION writable="<<initial.writable<<" mode="<<initial.mode<<" status="<<initial.status<<'\n';
        if(argc==2&&std::string(argv[1])=="--fan-cycle"){
            if(!initial.writable||initial.mode!=0)throw std::runtime_error("Fan test requires confirmed factory Default");
            const auto phase=[&](const char* name,int seconds){for(int i=0;i<seconds;++i){mc::MonitoringSnapshot s;ec.read(s);const auto state=fans.read();std::cout<<"phase="<<name<<" second="<<i<<" mode="<<state.mode<<" cpuRPM="<<value(s.processors[0].fanRpm)<<" systemRPM="<<value(s.processors[1].fanRpm)<<" status="<<state.status<<std::endl;Sleep(1000);}};
            try{fans.setFixed({100,100});phase("MAX",10);fans.setAutomatic();phase("DEFAULT",5);
                fans.setFixed({65,65});phase("MANUAL",8);fans.setFixed({36,38});phase("MIN",8);fans.setAutomatic();phase("DEFAULT_FINAL",8);
                if(fans.read().mode!=0)throw std::runtime_error("Final Default not confirmed");std::cout<<"FAN_CYCLE_COMPLETE default_verified=true\n";
            }catch(...){try{fans.setAutomatic();}catch(const std::exception& e){std::cerr<<"RESTORE_FAILURE "<<e.what()<<'\n';}throw;}
            return 0;
        }
        for(int i=0;i<3;++i){
            mc::MonitoringSnapshot s;s.diagnostic=true;cpu.read(s);ec.read(s);
            const auto pm=smu.read(true);
            std::cout<<"PM="<<std::hex<<pm.tableVersion<<std::dec<<" STAPM="<<value(pm.stapmLimitWatts)<<" Slow="<<value(pm.slowLimitWatts)<<" Fast="<<value(pm.fastLimitWatts)<<" status="<<pm.status<<'\n';
            std::cout<<"sample="<<i<<" cpuC="<<value(s.processors[0].celsius)<<" cpuW="<<value(s.processors[0].powerWatts)<<" packageW="<<value(s.apuPackagePowerWatts)<<" cpuRPM="<<value(s.processors[0].fanRpm)<<" systemRPM="<<value(s.processors[1].fanRpm)<<" ram0C="<<value(s.memory[0].celsius)<<" ram1C="<<value(s.memory[1].celsius)<<" status="<<s.processorStatus<<'\n';
            for(const auto& sensor:s.additionalSensors)std::cout<<sensor.device<<" / "<<sensor.metric<<" = "<<value(sensor.value)<<" / "<<sensor.status<<'\n';
            if(i<2)Sleep(1000);
        }
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
