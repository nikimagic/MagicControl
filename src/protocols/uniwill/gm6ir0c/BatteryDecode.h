#pragma once
#include "../BatteryCatalog.h"
#include "MagicControl/Monitoring.h"
#include <algorithm>
#include <array>
#include <span>
namespace mc::gm6 {
// Exact GM6IR0C DSDT: ECXP=ECMA+400, XIF0..8, XST0..3,
// XCIN/ACIC/BPST/BSOK, CYCN and ISDB. OEM BatteryInfo confirms494/4A6;
// BatteryProtection2 confirms7A6.mask30 and separate7B9/7D0 parameters.
// Charger sideband values remain raw, without borrowed X5/HYDROC units.
inline constexpr uniwill::BatteryRegisterDefinition batteryCandidates[]{
    {0x400,2,false,"XIF0_capacity_unit",true},
    {0x402,2,false,"XIF1_design_capacity",true},
    {0x404,2,false,"XIF2_full_capacity",true},
    {0x406,2,false,"XIF3_technology",true},
    {0x408,2,false,"XIF4_design_voltage_mV",true},
    {0x40A,2,false,"XIF5_warning_capacity",true},
    {0x40C,2,false,"XIF6_low_capacity",true},
    {0x432,2,false,"XST0_battery_state",true},
    {0x434,2,false,"XST1_present_rate",true,true},
    {0x436,2,false,"XST2_remaining_capacity",true,true},
    {0x438,2,false,"XST3_voltage_mV",true,true},
    {0x43C,1,false,"XCIN_XTIN",true},
    {0x490,1,false,"ACIC_BPST_BSOK",true},
    {0x494,1,false,"OEM_battery_alert",true},
    {0x497,1,false,"ISDB_status",true},
    {0x4A6,2,false,"CYCN_cycles",true},
    {0x7A6,1,false,"BatteryProtection2_policy_bits",true},
    {0x7B9,1,false,"configured_upper_low7",true},
    {0x7D0,1,false,"configured_lower_low7_role_unverified",true},
    {0x49F,1,false,"ADPT_packed_class",true},
    {0x7CC,1,false,"OEM_power_input_raw",true},
    {0x7CD,1,false,"OEM_TypeC_A_raw_units_unverified",false},
    {0x7CE,1,false,"OEM_TypeC_B_raw_units_unverified",false},
    {0x7B3,2,false,"PMAX_raw_units_unverified",false},
    {0x7B5,2,false,"PBSS_raw_units_unverified",false},
    {0x7B7,1,false,"PSRC_raw",false},
    {0x7B8,1,false,"DTTF_raw",false},
};
inline constexpr uniwill::BatteryBitDefinition batteryCandidateBits[]{
    {0x43C,0,"XCIN · внешнее питание"},{0x43C,2,"XTIN"},
    {0x490,0,"ACIC · внешнее питание"},{0x490,1,"BPST · батарея установлена"},
    {0x490,2,"BSOK · данные батареи готовы"},{0x497,7,"ISDB bit7 (назначение?)"},
};
inline constexpr auto batteryCoreAddresses=[] {
    constexpr auto count=[] {std::size_t n{};for(const auto& f:batteryCandidates)n+=f.width;return n;}();
    std::array<std::uint16_t,count> a{};std::size_t i{};
    for(const auto& f:batteryCandidates)for(unsigned j=0;j<f.width;++j)a[i++]=f.address+j;
    return a;
}();
inline EcBatteryReading emptyBatteryReading(const std::string& model="GM6IR0C"){
    EcBatteryReading e;e.supported=true;e.profileId="mechrevo-gm6ir0c";
    e.battery.deviceId="EC/"+model+"/BAT0";e.battery.source="EC · "+model+" · DSDT/OEM";
    for(const auto& def:batteryCandidates){EcBatteryRegisterReading f;
        f.address=def.address;f.width=def.width;f.confirmed=def.confirmed;f.name=def.name;f.status="unavailable";f.sampledTelemetry=def.sampledTelemetry;
        for(const auto& bit:batteryCandidateBits)if(bit.address==def.address)f.name+="|"+std::string(bit.name)+".bit"+std::to_string(bit.bit);
        e.registers.push_back(std::move(f));}
    return e;
}
// Pure decode. Caller supplies two matching sets after exact-board admission.
inline EcBatteryReading decodeBatteryCore(std::span<const std::uint8_t> raw,const std::string& model="GM6IR0C"){
    auto e=emptyBatteryReading(model);
    if(raw.size()!=batteryCoreAddresses.size()){e.status="Неполный снимок батареи "+model;return e;}
    const auto byte=[&](std::uint16_t a){return raw[std::find(batteryCoreAddresses.begin(),batteryCoreAddresses.end(),a)-batteryCoreAddresses.begin()];};
    const auto word=[&](std::uint16_t a){return static_cast<std::uint16_t>(byte(a)|(unsigned(byte(a+1))<<8));};
    for(auto& f:e.registers){f.value=f.width==1?byte(f.address):word(f.address);f.status="ok";}
    e.coherent=true;e.status="Согласованный снимок EC · "+model;
    e.state=word(0x432);e.readyRaw=byte(0x490);e.isdbRaw=byte(0x497);
    e.upperRaw=byte(0x7B9);e.restartRaw=byte(0x7D0);e.voltagePolicyRaw=byte(0x7A6);e.chargerRaw=byte(0x7CC);
    auto& b=e.battery;
    if(*e.readyRaw!=255)b.present=bool(*e.readyRaw&2);
    if(byte(0x43C)!=255){b.onlineKnown=true;b.online=byte(0x43C)&1;}
    b.temperatureSource="EC · "+model;b.temperatureStatus="Регистр температуры не подтверждён в родном коде";
    const bool ready=(*e.readyRaw&4)&&*e.readyRaw!=255&&b.present!=false;
    if(ready&&word(0x4A6)!=0xFFFF)b.cycles=word(0x4A6);
    const auto unit=word(0x400);const auto design=word(0x402),full=word(0x404),remaining=word(0x436);
    // The logs show XIF0 stuck at0 after a battery-data reset while XST3
    // voltage and CYCN recover. Do not reinterpret those capacities as mWh or
    // hide independently documented mV/state/cycles. XST1 rate depends on XIF0.
    if(unit!=1)e.status+=" · единицы ёмкости/тока неизвестны; напряжение и циклы независимы";
    if(ready&&unit==1){
        b.capacityUnit="mAh";
        if(design&&design!=0xFFFF){e.designMah=design;b.designCapacity=design;}
        if(full&&full!=0xFFFF){e.fullMah=full;b.fullCapacity=full;}
    }
    if(e.designMah&&e.fullMah&&full<=design)b.healthPercent=100.*full/design;
    b.healthUnavailable=!b.healthPercent.has_value();
    if(!ready){e.status+=" · батарея не готова";return e;}
    if(unit==1&&remaining!=0xFFFF){e.remainingMah=remaining;b.remainingCapacity=remaining;}
    if(e.fullMah&&e.remainingMah&&remaining<=full)b.percent=100.*remaining/full;
    const auto voltage=word(0x438),rate=word(0x434);
    if(voltage>=5000&&voltage<=25000)b.volts=voltage/1000.;
    if(unit==1&&rate<=30000)b.milliamps=rate;
    if(*e.state!=0xFFFF&&(*e.state&3)!=3){
        b.flow=(*e.state&1)?BatteryFlow::Discharging:(*e.state&2)?BatteryFlow::Charging:BatteryFlow::Idle;
        if(b.volts&&b.milliamps&&(b.flow!=BatteryFlow::Idle||*b.milliamps==0))b.watts=b.flow==BatteryFlow::Idle?0.:(b.flow==BatteryFlow::Discharging?-1.:1.)* *b.volts* *b.milliamps/1000.;
    }
    b.powerStatus=model+" DSDT: XIF/XST; ISDB="+std::to_string(*e.isdbRaw)+" cycles="+(b.cycles?std::to_string(*b.cycles):"unknown")+
        " firmware_BIF_design_substitution="+(b.cycles?std::string((*e.isdbRaw&1)&&*b.cycles<50?"true":"false"):"unknown")+
        " upper7B9="+std::to_string(*e.upperRaw)+" lower7D0="+std::to_string(*e.restartRaw)+" lower_role=unverified policy7A6="+std::to_string(*e.voltagePolicyRaw);
    b.powerStatus+=" state432="+std::to_string(*e.state)+" ready490="+std::to_string(*e.readyRaw)+
        " XCIN43C="+std::to_string(byte(0x43C))+" alert494="+std::to_string(byte(0x494))+" charger7CC="+std::to_string(*e.chargerRaw);
    return e;
}
}
