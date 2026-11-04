#pragma once
#include "BatteryDecode.h"
#include <cmath>
#include <utility>

namespace mc::gm6 {
// Exact GM6 EC1.12 static trace: AE23 publishes abs(050A) to0434;
// D347 averages0370..77, D851/D969 use0399 and multiply by34;
// D9F5 publishes050A..511. Read-only evidence, not admission for other boards.
// EB85 writes0382/83 in BE order. DE0F copies0522/23 to0836/37;
// C935..C943 sends that voltage as ChargingVoltage15, in mV, not current.
// See research/gm6-current-idle-20261022/REPORT.md and raw instructions.
struct CurrentDiagnosticField {uniwill::BatteryRegisterDefinition field;bool signedValue{};};
inline constexpr CurrentDiagnosticField currentDiagnosticFields[]{
    {{0x50A,2,false,"internal_current_050A_raw",true,true},true},
    {{0x50C,2,false,"internal_current_050C_raw",true,true},true},
    {{0x50E,2,false,"internal_current_050E_raw",true,true},true},
    {{0x510,2,false,"internal_current_0510_raw",true,true},true},
    {{0x370,2,true,"calculation_sample0_raw",true,true}},
    {{0x372,2,true,"calculation_sample1_raw",true,true}},
    {{0x374,2,true,"calculation_sample2_raw",true,true}},
    {{0x376,2,true,"calculation_sample3_raw",true,true}},
    {{0x399,1,false,"zero_reference_offset_raw",true,true}},
    {{0x382,2,true,"calculation_intermediate_raw",true,true},true},
    {{0x367,1,false,"calculation_flags_raw",true,true}},
    {{0x3A5,1,false,"calculation_phase_flags_raw",true,true}},
    {{0x522,2,false,"BMS_requested_voltage_mV_not_measured",true,true}},
    {{0x834,2,false,"prepared_charger_current_raw_not_ACK",true,true}},
};

// No worker or hardware operations. The existing reader owns this schedule.
// Current fluctuations never restart the burst; only zero crossings do.
class CurrentDiagnosticSchedule {
    bool initialized_{};
    int percent_{-1};
    std::optional<std::uint16_t> state_;
    std::optional<std::uint8_t> ready_,isdb_;
    std::optional<bool> zero_;
    bool acKnown_{},ac_{};
    std::uint64_t lastAt_{},sequence_{};
    unsigned remaining_{};
    std::string event_;
public:
    std::optional<std::pair<std::uint64_t,std::string>> take(const EcBatteryReading& r,std::uint64_t now){
        const auto& b=r.battery;
        const int percent=b.percent?static_cast<int>(std::lround(*b.percent)):-1;
        std::optional<bool> zero;
        for(const auto& f:r.registers)if(f.address==0x434&&f.value)zero=*f.value==0;
        std::string reason;
        if(!initialized_)reason="initial";
        else if(percent!=percent_)reason="soc_change";
        else if(r.state!=state_)reason="state_change";
        else if(b.onlineKnown!=acKnown_||(b.onlineKnown&&b.online!=ac_))reason="power_change";
        else if(r.readyRaw!=ready_)reason="ready_change";
        else if(r.isdbRaw!=isdb_)reason="isdb_change";
        else if(zero!=zero_)reason="published_current_zero_crossing";
        const bool initial=!initialized_;
        initialized_=true;percent_=percent;state_=r.state;ready_=r.readyRaw;isdb_=r.isdbRaw;
        acKnown_=b.onlineKnown;ac_=b.online;zero_=zero;
        if(!reason.empty()){remaining_=10;event_=reason;}
        if(!initial&&now-lastAt_<1000)return {};
        if(remaining_){--remaining_;reason=event_+"/burst_"+std::to_string(10-remaining_)+"_of_10";}
        else if(now-lastAt_>=10000)reason="periodic_10s";
        else return {};
        lastAt_=now;
        return std::pair{++sequence_,std::move(reason)};
    }
};
}
