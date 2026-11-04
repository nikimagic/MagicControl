#include "MagicControl/ControlAudit.h"
#include "MagicControl/LaptopControl.h"
#include "MagicControl/BoardIdentity.h"
#include "MagicControl/DeviceProfile.h"
#include "MagicControl/Logging.h"
#include "../../../protocols/huawei/Hwmi.h"
#include "../../../protocols/huawei/BatteryThresholds.h"
#include "../../../protocols/huawei/KeyboardTimeout.h"
#include "BacklightWindows.h"
#include <cmath>
#include <stdexcept>
#include <thread>
#include <chrono>
#include <windows.h>

namespace mc {
namespace {
void requireBoard(){if(deviceProfile(readBoardDescriptor()).id!="huawei-hlyl-wxx9")throw std::runtime_error("Huawei HWMI: модель/плата не соответствует HLYL-WXX9");}
std::pair<int,int> thresholds(huawei::Session& session){
    const auto data=huawei::checkedCall(session,huawei::command(huawei::getCharge),3);
    const int start=data[1],end=data[2];
    if(!huawei::validChargeThresholds({start,end}))throw std::runtime_error("HWMI getter0x1103: invalid charge pair "+huawei::chargeThresholdText({start,end}));
    return {start,end};
}
bool fn(huawei::Session& session){const auto data=huawei::checkedCall(session,huawei::command(huawei::getFn),2);if(data[1]!=1&&data[1]!=2)throw std::runtime_error("HWMI getter0x0604: invalid Fn Lock="+std::to_string(data[1]));return data[1]==2;}
bool turbo(huawei::Session& session){const auto data=huawei::checkedCall(session,huawei::command(huawei::getTurbo),2);if(data[1]>1)throw std::runtime_error("HWMI getter0x0E04: invalid Turbo="+std::to_string(data[1]));return data[1]==1;}
std::optional<double> temperature(huawei::Session& session,int index){
    const auto data=huawei::checkedCall(session,huawei::command(huawei::getTemperature,static_cast<std::uint8_t>(index)),4);
    if(data[1]>1||data[3]>9)throw std::runtime_error("HWMI GTMP/"+std::to_string(index)+": invalid sign/fraction="+std::to_string(data[1])+"/"+std::to_string(data[3]));
    const double value=(data[2]+data[3]/10.0)*(data[1]?-1.0:1.0);
    if(value<0||value>125)throw std::runtime_error("HWMI GTMP/"+std::to_string(index)+": invalid temperature="+std::to_string(value));return value;
}
int keyboardTimeout(huawei::Session& session){
    const auto data=huawei::checkedCall(session,huawei::command(huawei::getKeyboardTimeout),3);
    const int seconds=data[1]+(data[2]<<8);
    if(seconds>3600)throw std::runtime_error("HWMI getter0x1206: invalid timeout="+std::to_string(seconds));return seconds;
}
std::pair<int,int> stableThresholds(huawei::Session& session,std::optional<huawei::ChargeThresholds> expected={}){
    return huawei::stableChargeThresholds([&]{
        try{return thresholds(session);}
        catch(const std::exception& error){logMessage("huawei_hwmi",std::string("charge getter0x1103 retry observation: ")+error.what());throw;}
    },[]{std::this_thread::sleep_for(std::chrono::milliseconds(150));},expected);
}
void writeThresholds(huawei::Session& session,huawei::ChargeThresholds value){
    // Raw0/0 is legal only for restoration of a previously observed disabled
    // policy. Do not route it through the public travel0/100 normalization.
    huawei::checkedCall(session,huawei::command(huawei::setCharge,static_cast<std::uint8_t>(value.first),static_cast<std::uint8_t>(value.second)),1);
}
class HlylControl final:public LaptopControl {
    std::string lastLog,lastLightLog;
public:
    std::string_view chargePolicyId() const noexcept override{return "huawei-hlyl-wxx9";}
    std::string_view customPowerPolicyId() const noexcept override{return "huawei-hlyl-windows-smu";}
    void withChargePolicyTransaction(const std::function<void()>& action) override{
        requireBoard();huawei::withWmiGate(action);
    }
    LaptopControlSnapshot read() override {
        auto state=readTelemetry();
        // The interactive/page reader may enter the guarded I2EC session.
        // Isolated diagnostic telemetry must call readTelemetry() instead:
        // terminating a worker while PNPCFG is open would skip RAII cleanup.
        std::string lightStatus;
        try{requireBoard();const auto light=windows::readHlylBacklight();state.keyboardLevel=light.level;state.keyboardWritable=light.writable;
            state.keyboardActualDuty=light.actualDuty;state.keyboardPwmPeriod=light.period;state.keyboardStatus=light.status;state.keyboardPhase=light.phase;
            lightStatus=light.status;state.status+="; "+light.status;}
        catch(const std::exception& error){lightStatus=std::string("I2EC backlight getter: ")+error.what();state.keyboardStatus=lightStatus;state.status+="; "+lightStatus;}
        if(lightStatus!=lastLightLog){lastLightLog=lightStatus;logMessage("huawei_hwmi",lightStatus);}return state;
    }
    LaptopControlSnapshot readTelemetry() override {
        // Pure typed HWMI getters: no PawnIO module, I2EC, direct port access,
        // PNPCFG entry or backlight probing belongs to this diagnostic path.
        LaptopControlSnapshot state;state.source="Huawei HWMI";
        std::string issues;
        const auto failed=[&](const std::string& getter,const std::exception& error){
            const auto detail=getter+": "+error.what();if(!issues.empty())issues+="; ";issues+=detail;
            logMessage("huawei_hwmi",detail);
        };
        // Keep the six expected channels even when opening HWMI fails. Missing
        // transport must remain an unavailable observation, not a missing row.
        for(const auto [index,name]:{std::pair{0,"CTMP / CPU"},std::pair{5,"TSLO"},std::pair{7,"TNTC"},std::pair{8,"CNTC"},std::pair{11,"DNTC"},std::pair{14,"BTMP / Battery"}})
            state.temperatureSensors.push_back({"Honor EC / "+std::string(name),"temperature","C","Huawei HWMI · GTMP "+std::to_string(index),"HWMI session unavailable",{},{},GetTickCount64()});
        try {
            requireBoard();auto session=huawei::openSession();state.connected=true;
            try {const auto [start,end]=stableThresholds(*session);state.chargeStart=start;state.chargeEnd=end;state.chargeWritable=true;}catch(const std::exception& error){failed("charge getter0x1103",error);}
            try {state.fnLock=fn(*session);state.fnWritable=true;}catch(const std::exception& error){failed("Fn getter0x0604",error);}
            try {state.nativeTurbo=turbo(*session);state.turboWritable=true;
                state.stockCpuProfile=*state.nativeTurbo?1:0;state.stockCpuProfileWritable=true;
            }catch(const std::exception& error){failed("Turbo getter0x0E04",error);}
            // GLIV returns error and SLIV acknowledges without changing light
            // on BIOS1.09 (physical checks 18.10.2026). Real brightness comes
            // from the separate full read() I2EC path, never WMI success.
            try {state.keyboardTimeoutSeconds=keyboardTimeout(*session);state.keyboardTimeoutWritable=true;}catch(const std::exception& error){failed("timeout getter0x1206",error);}
            for(int i=0;i<2;++i)try {const auto data=huawei::checkedCall(*session,huawei::command(huawei::getFan,static_cast<std::uint8_t>(i)),3);
                const int rpm=data[1]+(data[2]<<8);if(rpm>15000)throw std::runtime_error("invalid RPM="+std::to_string(rpm));state.fanRpm[i]=rpm;
            }catch(const std::exception& error){failed("fan getter0x0802/"+std::to_string(i),error);}
            // Exact BIOS1.09 GTMP branches, confirmed against the factory DSDT
            // and getter observations19.10. Unknown locations keep OEM names.
            constexpr std::array indices{0,5,7,8,11,14};
            for(std::size_t i=0;i<indices.size();++i){const auto index=indices[i];auto& sensor=state.temperatureSensors[i];
                try{sensor.value=temperature(*session,index);sensor.status=sensor.value?"ok; OEM channel, physical location only known for CTMP/BTMP":"invalid/unavailable temperature";}
                catch(const std::exception& error){sensor.status=error.what();failed("GTMP getter0x0202/"+std::to_string(index),error);}
                sensor.sampledAtMs=GetTickCount64();
                if(index==0)state.cpuCelsius=sensor.value;if(index==14)state.batteryCelsius=sensor.value;
            }
        }catch(const std::exception& error){
            failed("HWMI session",error);
            for(auto& sensor:state.temperatureSensors){sensor.status=std::string("HWMI session: ")+error.what();sensor.sampledAtMs=GetTickCount64();}
        }
        state.status=issues.empty()?"Honor · HWMI · фактические показания":"Honor · неполные показания HWMI: "+issues;
        if(state.chargeStart==0&&state.chargeEnd==0)state.status+=" · OEM charge protection disabled (0/0), not a100% threshold";
        const auto integer=[](auto value){return value?std::to_string(*value):std::string("unknown");};
        const auto line="HLYL connected="+std::to_string(state.connected)+" thresholds="+integer(state.chargeStart)+"/"+integer(state.chargeEnd)+
            " fn="+integer(state.fnLock)+" turbo="+integer(state.nativeTurbo)+" rpm="+integer(state.fanRpm[0])+"/"+integer(state.fanRpm[1])+" cpuC="+integer(state.cpuCelsius)+
            " keyboard="+integer(state.keyboardLevel)+" timeout="+integer(state.keyboardTimeoutSeconds)+" status="+state.status;
        if(line!=lastLog){lastLog=line;logMessage("huawei_hwmi",line);}return state;
    }
    std::optional<double> readBatteryTemperature() override {
        try{requireBoard();auto session=huawei::openSession();return temperature(*session,14);}
        catch(const std::exception& error){logMessage("huawei_hwmi",std::string("Battery GTMP getter0x0202/14: ")+error.what());throw;}
    }
    void setChargeThresholds(int start,int end) override {
        if(start<0||end<1||start>end||end>100)throw std::invalid_argument("Huawei: неверные пороги зарядки");
        // PC Manager travel/full-charge is 95/100. 0/100 means disable
        // protection and can enable Smart Charge (observed fallback 40/70).
        if(start==0&&end==100)start=95;
        requireBoard();auto session=huawei::openSession();const auto before=stableThresholds(*session);
        const huawei::ChargeThresholds target{start,end};
        if(before==std::pair{start,end})return;
        logMessage("huawei_hwmi","Charge requested="+std::to_string(start)+"/"+std::to_string(end)+" before="+std::to_string(before.first)+"/"+std::to_string(before.second));
        try{
            writeThresholds(*session,target);
            stableThresholds(*session,target);logMessage("huawei_hwmi","Charge readback confirmed="+huawei::chargeThresholdText(target));
        }catch(const std::exception& failure){
            const std::string cause=failure.what();std::string rollbackStatus;
            try{
                const auto current=stableThresholds(*session);
                // WMI sets a pair atomically at the API level. A third pair,
                // including unrelated0/0, is not ours to replace after failure.
                if(current!=before){
                    if(current!=target)throw std::runtime_error("ownership lost; current="+huawei::chargeThresholdText(current));
                    writeThresholds(*session,before);
                }
                stableThresholds(*session,before);logMessage("huawei_hwmi","Charge rollback confirmed");rollbackStatus="прежние пороги восстановлены";
            }catch(const std::exception& error){logMessage("huawei_hwmi",std::string("Charge rollback failed: ")+error.what());rollbackStatus=std::string("откат не подтверждён; состояние неизвестно: ")+error.what();}
            throw std::runtime_error(cause+"; "+rollbackStatus);
        }
    }
    void setFnLock(bool enabled) override {
        requireBoard();auto session=huawei::openSession();if(fn(*session)==enabled)return;
        huawei::checkedCall(*session,huawei::command(huawei::setFn,enabled?2:1),1);
        if(fn(*session)!=enabled)throw std::runtime_error("Huawei: Fn Lock не подтверждён чтением");
    }
    void setNativeTurbo(bool enabled) override {
        requireBoard();SYSTEM_POWER_STATUS power{};
        if(!GetSystemPowerStatus(&power)||power.ACLineStatus!=1)throw std::runtime_error("Huawei: штатный Turbo переключается только от сети");
        auto session=huawei::openSession();if(turbo(*session)==enabled)return;
        huawei::checkedCall(*session,huawei::command(huawei::setTurbo,enabled?1:0),1);
        if(turbo(*session)!=enabled)throw std::runtime_error("Huawei: переключение Turbo не подтверждено чтением");
    }
    void setStockCpuProfile(int profile) override {
        if(profile<0||profile>1)throw std::invalid_argument("Honor: unknown native CPU profile");
        requireBoard();SYSTEM_POWER_STATUS power{};
        if(!GetSystemPowerStatus(&power)||power.ACLineStatus!=1)throw std::runtime_error("Huawei: штатный CPU-режим переключается только от сети");
        auto session=huawei::openSession();
        // Explicit selection reapplies the requested native policy after our
        // own SMU overrides. Never pulse an intermediate mode or write FANs.
        huawei::checkedCall(*session,huawei::command(huawei::setTurbo,profile),1);
        if(turbo(*session)!=(profile==1))throw std::runtime_error("Honor native CPU profile readback mismatch");
    }
    void setKeyboardLevel(int level) override {
        requireBoard();windows::setHlylBacklight(level);
    }
    void setKeyboardTimeout(int seconds) override {
        if(seconds<0||seconds>3600)throw std::invalid_argument("Huawei: таймаут 0–3600 секунд");
        requireBoard();auto session=huawei::openSession();
        const auto put=[&](int value){huawei::checkedCall(*session,huawei::command(huawei::setKeyboardTimeout,static_cast<std::uint8_t>(value),static_cast<std::uint8_t>(value>>8)),1);};
        const auto actual=huawei::applyKeyboardTimeout(seconds,[&]{return keyboardTimeout(*session);},put,
            []{std::this_thread::sleep_for(std::chrono::milliseconds(100));},[](const std::string& message){logMessage("huawei_hwmi",message);});
        logMessage("huawei_hwmi","Keyboard timeout confirmed="+std::to_string(actual.after)+" seconds; native GKBT/SKBT; wrote="+std::to_string(actual.wrote));
    }
};
}
std::unique_ptr<LaptopControl> createHlylControl(){
    if(wcsstr(GetCommandLineW(),L"--no-ec"))return {};
    if(deviceProfile(readBoardDescriptor()).id!="huawei-hlyl-wxx9")return {};
    return std::make_unique<HlylControl>();
}
}
