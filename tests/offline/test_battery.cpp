#include "OfflineTest.h"
#include "src/protocols/uniwill/x5sp4nag/BatteryDecode.h"
#include "src/protocols/uniwill/x5sp4nag/BatteryContract.h"
#include "src/protocols/uniwill/x5sp4nag/BatteryCandidates.h"
#include "src/protocols/uniwill/x5sp4nag/Registers.h"
#include "src/protocols/uniwill/gm6ir0c/BatteryAgingGuard.h"
#include "MagicControl/BatteryJournal.h"
#include "MagicControl/BatteryEcPolicy.h"
#include "MagicControl/BatteryProtection.h"
#include "src/gui/common/BatteryDetails.h"
#include <fstream>
#include <set>
#include <map>
#include <cmath>
using namespace mc;
namespace x5=mc::uniwill::x5sp4nag;
std::vector<std::pair<std::string,std::string>> messages;
namespace mc {void logMessage(std::string_view channel,std::string_view message) noexcept {messages.emplace_back(channel,message);}}
using Sample=std::array<std::uint8_t,std::size(x5::batteryCoreAddresses)>;
Sample fixture(const char* name){
    const auto read=[](std::string path){std::ifstream in(path,std::ios::binary);CHECK(in.good());return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in),{});};
    const auto bytes=read(std::string(MC_FIXTURES)+"/"+name+".bin"),mask=read(std::string(MC_FIXTURES)+"/"+name+".valid");
    CHECK(bytes.size()==4096&&mask.size()==4096);
    CHECK(x5::exactBatteryIdentity({bytes[0x454],bytes[0x456],bytes[0x740]}));
    Sample sample{};for(std::size_t i=0;i<sample.size();++i){CHECK(mask[x5::batteryCoreAddresses[i]]!=0);sample[i]=bytes[x5::batteryCoreAddresses[i]];}
    return sample;
}
void put(Sample& sample,std::uint16_t address,std::uint16_t value,int width=2){
    for(int b=0;b<width;++b){auto i=std::find(std::begin(x5::batteryCoreAddresses),std::end(x5::batteryCoreAddresses),address+b);CHECK(i!=std::end(x5::batteryCoreAddresses));sample[i-std::begin(x5::batteryCoreAddresses)]=static_cast<std::uint8_t>(value>>(8*b));}
}
unsigned count(std::string_view text){unsigned result{};for(const auto& [channel,message]:messages)if(message.find(text)!=std::string::npos)++result;return result;}
void decode(){
    const auto fast=x5::decodeBatteryCore(fixture("fast")),slow=x5::decodeBatteryCore(fixture("slow"));
    CHECK(fast.coherent&&slow.coherent);CHECK(fast.raw522==17000&&slow.raw522==17000);
    CHECK(fast.raw520==6300&&fast.rawBase==17800);CHECK(fast.battery.percent==77&&slow.battery.percent==86);
    CHECK(fast.battery.milliamps==1802&&slow.battery.milliamps==612);
    CHECK(fast.designMah==6400&&fast.fullMah==5800&&fast.remainingMah==4466);
    CHECK(std::abs(*fast.battery.healthPercent-90.625)<1e-8&&std::abs(*fast.battery.celsius-30.85)<1e-8);
    CHECK(std::abs(*fast.battery.volts-16.818)<1e-8);CHECK(std::abs(*fast.battery.watts-30.306036)<1e-8);
    CHECK(std::abs(*slow.battery.watts-10.336068)<1e-8);CHECK(fast.battery.flow==BatteryFlow::Charging);
    // Equal raw522 does not classify fast/slow or identify its firmware writer.
    CHECK(*fast.battery.watts>25&&*slow.battery.watts<15);
    CHECK(!x5::decodeBatteryCore({}).coherent);
    auto raw=fixture("fast");put(raw,0x432,1);auto discharge=x5::decodeBatteryCore(raw);
    CHECK(discharge.battery.flow==BatteryFlow::Discharging&&*discharge.battery.watts<0);
    put(raw,0x432,3);CHECK(!x5::decodeBatteryCore(raw).battery.watts);
    put(raw,0x432,0);put(raw,0x434,0);put(raw,0x436,0);put(raw,0x4A6,0);
    auto zero=x5::decodeBatteryCore(raw);CHECK(zero.battery.watts==0&&zero.battery.milliamps==0&&zero.remainingMah==0&&zero.battery.cycles==0);
    put(raw,0x402,0);put(raw,0x404,0xFFFF);put(raw,0x4AB,101,1);put(raw,0x434,0xFFFF);put(raw,0x438,0);put(raw,0x4A2,0);put(raw,0x4A6,0xFFFF);
    const auto invalid=x5::decodeBatteryCore(raw);CHECK(!invalid.designMah&&!invalid.fullMah&&!invalid.battery.healthPercent);
    CHECK(!invalid.battery.percent&&!invalid.battery.milliamps&&!invalid.battery.volts&&!invalid.battery.celsius&&!invalid.battery.cycles);
    put(raw,0x490,0,1);const auto notReady=x5::decodeBatteryCore(raw);CHECK(notReady.coherent&&!notReady.battery.percent);
    EcBatteryReading estimate=fast;estimate.remainingMah=2900;estimate.fullMah=5800;estimate.battery.milliamps=1000;
    estimate.battery.flow=BatteryFlow::Discharging;CHECK(gui::ui::ecBatteryTimes(estimate)[0].value.find("02:54")!=std::string::npos);
    estimate.battery.milliamps=0;CHECK(gui::ui::ecBatteryTimes(estimate)[0].value=="—");
    estimate.battery.dynamicStale=true;for(const auto& row:gui::ui::ecBatteryTimes(estimate))CHECK(row.value=="—");
    raw=fixture("fast");put(raw,0x400,0);
    const auto units=x5::decodeBatteryCore(raw);
    CHECK(units.coherent&&units.designMah==fast.designMah&&units.fullMah==fast.fullMah&&units.remainingMah==fast.remainingMah);
    CHECK(units.battery.milliamps==fast.battery.milliamps&&units.battery.watts==fast.battery.watts&&units.battery.healthPercent==fast.battery.healthPercent&&!units.battery.healthUnavailable);
    CHECK(units.battery.volts==fast.battery.volts&&units.battery.cycles==fast.battery.cycles&&units.battery.celsius==fast.battery.celsius);
    CHECK(units.battery.percent==77); // Native SOC does not depend on capacity units.
    for(unsigned ready:{0u,1u,4u,255u}){raw=fixture("fast");put(raw,0x490,ready,1);const auto absent=x5::decodeBatteryCore(raw);
        CHECK(!absent.battery.volts&&!absent.battery.cycles&&!absent.battery.percent&&!absent.battery.milliamps);}
    raw=fixture("fast");put(raw,0x432,0);CHECK(!x5::decodeBatteryCore(raw).battery.watts); // Idle+nonzero current is transitional.
    BatteryReading old,currentReading;old.present=true;old.healthPercent=100;currentReading.present=true;currentReading.healthUnavailable=true;
    retainBatteryReadings(currentReading,old);CHECK(!currentReading.healthPercent);
}
void writes(){
    using Space=x5::BatteryWriteSpace;unsigned writes{},reads{};
    CHECK(rejected([&]{x5::requireBatteryIdentity(false,[&](auto){++reads;return 82;});++writes;}));CHECK(reads==0&&writes==0);
    CHECK(rejected([&]{x5::requireBatteryIdentity(true,[&](auto){++reads;return 0;});++writes;}));CHECK(reads==1&&writes==0);
    x5::requireBatteryIdentity(true,[](auto address){return address==0x454?82:address==0x456?72:26;});
    for(unsigned address=0;address<65536;++address)for(auto mask:{0x7F,0xFF,0xFB}){
        const bool expected=(mask==0x7F&&(address==0x7B9||address==0x7D0))||(mask==0xFF&&address==0x7C3)||(mask==0xFB&&address==0x770);
        CHECK(x5::batteryWriteAllowed(Space::HostEc,address,mask)==expected);
    }
    for(int percent=-1;percent<=101;++percent)for(bool automatic:{false,true}){
        const bool allowed=percent==100||percent==95||percent==85||percent==60||percent==10;
        CHECK(rejected([&]{x5::requireBatteryRequest(percent,automatic);})!=allowed);
    }
    for(int i=0;i<256;++i){
        CHECK(x5::exactBatteryIdentity({static_cast<std::uint8_t>(i),72,26})==(i==82));
        CHECK(x5::exactBatteryIdentity({82,static_cast<std::uint8_t>(i),26})==(i==72));
        CHECK(x5::exactBatteryIdentity({82,72,static_cast<std::uint8_t>(i)})==(i==26));
        CHECK(rejected([&]{x5::requireStoredBatteryThreshold(i);})==((i&0x7F)>100));
        CHECK(rejected([&]{x5::requireBatteryPlatform(i,0xFF);})!=(i==4||i==7));
        CHECK(rejected([&]{x5::requireBatteryPlatform(4,i);})!=(i==0xFF||i==4));
    }
    for(auto address:{0x520,0x522,0x742,0x7A6,0x751,0x87F}){
        CHECK(rejected([&]{x5::checkedBatteryWrite(address,0x7F,[&]{++writes;});}));CHECK(writes==0);
    }
    CHECK(!x5::batteryWriteAllowed(static_cast<Space>(99),0x7B9,0x7F));
    CHECK(!x5::batteryWriteAllowed(Space::StoredWmi,0x520,0x7F));
    CHECK(x5::mergeStoredBatteryWord(0xAA80,85,0x7F)==0xAAD5);
    CHECK(rejected([&]{x5::mergeStoredBatteryWord(0xAA80,85,0xFF);}));
    for(unsigned current=0;current<65536;++current){const auto merged=x5::mergeStoredBatteryWord(current,97,0x7F);CHECK((merged&0xFF80)==(current&0xFF80));CHECK((merged&0x7F)==97);}
    CHECK(ec::kRequestedBatteryLowerPercent==97); // explicit experiment, never silently clamp to95
    std::uint8_t current=0xDF;
    const auto read=[&](auto){return current;};
    const auto write=[&](auto,auto mask,auto original){++writes;current=(current&~mask)|(original&mask);};
    x5::restoreBatteryBits(0x7B9,0x7F,95,85,read,write);CHECK(current==0xD5&&writes==1);
    x5::restoreBatteryBits(0x7B9,0x7F,95,85,read,write);CHECK(writes==1);
    current=86;CHECK(rejected([&]{x5::restoreBatteryBits(0x7B9,0x7F,95,85,read,write);}));CHECK(current==86&&writes==1);
    CHECK(rejected([&]{x5::restoreBatteryBits(0x522,0xFF,95,85,read,write);}));CHECK(writes==1);
    std::set<unsigned> addresses;for(const auto& candidate:x5::batteryCandidates)CHECK(addresses.insert(candidate.address).second);
    CHECK(addresses.size()==62);for(const auto& c:x5::batteryCandidates)if(c.address==0x520||c.address==0x522||c.address==0x7D0)CHECK(!c.confirmed);
}
void journal(){
    setLoggingEnabled(true);setBatteryLoggingEnabled(true);messages.clear();
    BatteryReading b;b.deviceId="offline/discharge";b.source="offline";b.flow=BatteryFlow::Discharging;
    for(int percent=70;percent>=5;--percent){b.percent=percent;journalBattery(b,"offline");}
    CHECK(count("SOC ")==66);CHECK(count("skipped_percent_samples=0")==66);
    // Partial telemetry staleness no longer discards independently fresh SOC.
    // An unavailable SOC must still leave the last observed percent intact.
    const auto before=count("SOC ");b.dynamicStale=true;b.percent.reset();journalBattery(b,"offline");CHECK(count("SOC ")==before);
    b.dynamicStale=false;b.percent=2;journalBattery(b,"offline");CHECK(count("skipped_percent_samples=2")==1);
    messages.clear();EcBatteryReading e;e.coherent=true;e.battery.deviceId="offline/flags";e.battery.percent=30;
    e.registers.push_back({0x490,1,false,false,"status?","ok",15});
    journalBatteryRegisters(e,1000);CHECK(count("SNAPSHOT reason=initial")==1);
    journalBatteryRegisters(e,1100);CHECK(messages.size()==1);
    e.registers[0].value=7;journalBatteryRegisters(e,1200);CHECK(count("cleared=0x0008")==1);
    messages.clear();e.registers[0].value.reset();e.registers[0].status="unstable";journalBatteryRegisters(e,1300);
    CHECK(count("CHANGED")==1&&count("cleared=")==0);
    messages.clear();e.registers[0].value=7;e.registers[0].status="ok";journalBatteryRegisters(e,1400);CHECK(count("xor=")==0);
    e.battery.percent=31;journalBatteryRegisters(e,2000);CHECK(count("SNAPSHOT reason=soc_change")==1);
    journalBatteryRegisters(e,12000);CHECK(count("SNAPSHOT reason=periodic_10s")==1);
    const auto countBefore=messages.size();e.coherent=false;journalBatteryRegisters(e,13000);CHECK(messages.size()==countBefore);
    e.coherent=true;setBatteryLoggingEnabled(false);journalBatteryRegisters(e,14000);CHECK(messages.size()==countBefore);
    setLoggingEnabled(false);b.percent=1;journalBattery(b,"offline");CHECK(messages.size()==countBefore);
}
void lenovoBatteryJournal(){
    setLoggingEnabled(true);setBatteryLoggingEnabled(true);messages.clear();
    EcBatteryReading e;e.coherent=true;e.profileId="lenovo-legion-kwcn";
    e.battery.deviceId="offline/lenovo/current-sources";e.battery.percent=60;
    e.battery.flow=BatteryFlow::Discharging;e.battery.milliamps=-123;
    e.registers.push_back({0xc4d0,2,false,true,"sbs_current_signed_ma","ok",65413,true,true});
    e.registers.push_back({0xc4d2,2,false,true,"bios_b1ac_signed_raw","ok",65535,true,true});
    e.registers.push_back({0xc250,2,false,false,"ec_aux_0250_signed_raw","ok",42,true,true});
    journalBatteryRegisters(e,1000);
    CHECK(count("SNAPSHOT reason=initial")==1&&count("ec_status=discharging current_direction=negative current_source=0xC4D0")==1);
    CHECK(count("name=sbs_current_signed_ma")==1&&count("int16_signed=-123")==1);
    CHECK(count("name=bios_b1ac_signed_raw")==1&&count("int16_signed=-1")==1);
    CHECK(count("name=ec_aux_0250_signed_raw")==1&&count("meaning=candidate_unverified")>=1);
    messages.clear();e.battery.percent=61;e.battery.flow=BatteryFlow::Charging;e.battery.milliamps=0;
    e.registers[0].value=0;journalBatteryRegisters(e,2000);
    CHECK(count("ec_status=charging current_direction=zero current_source=0xC4D0")==1);
    CHECK(count("int16_signed=0")==1&&count("CHANGED")==0);
    messages.clear();e.battery.percent=62;e.battery.milliamps.reset();e.registers[0].value.reset();
    e.registers[0].status="unstable";journalBatteryRegisters(e,3000);
    CHECK(count("current_direction=unknown")>=1&&count("int16_signed=unknown")==1);
    CHECK(count("CHANGED")==1); // A lost source is immediate; it is never zero.
    setLoggingEnabled(false);setBatteryLoggingEnabled(false);
}
void gm6Battery(){
    namespace gm6=mc::gm6;
    CHECK(gm6::agingEcIdentity(0x4c,25,0x40,7,true));
    CHECK(!gm6::agingEcIdentity(0x4b,25,0x40,7,true)&&!gm6::agingEcIdentity(0x4c,26,0x40,7,true));
    CHECK(!gm6::agingEcIdentity(0x4c,25,0xff,7,true)&&!gm6::agingEcIdentity(0x4c,25,0x40,0xff,true));
    CHECK(gm6::agingEcIdentity(0x4c,25,0x40,0,false)&&!gm6::agingEcIdentity(0x4c,25,0x40,0,true));
    CHECK(gm6::agingI2ecConfiguration(6,0x80,1)&&!gm6::agingI2ecConfiguration(6,0x80,0xff));
    CHECK(gm6::agingExternalPower(7)&&!gm6::agingExternalPower(6)&&!gm6::agingExternalPower(0xff));
    CHECK(!gm6::agingI2ecConfiguration(6,0x81,1)&&!gm6::agingI2ecConfiguration(7,0x80,1));
    CHECK(gm6::agingExposureDividers(0,59)&&!gm6::agingExposureDividers(0xff,0xff)&&!gm6::agingExposureDividers(60,1));
    CHECK(gm6::agingSku18(0xd8,0x81)&&!gm6::agingSku18(0xff,0xff)&&!gm6::agingSku18(0xd8,0x80));
    CHECK(gm6::agingPrivateWindow(0x5d,0x48,0)&&gm6::agingPrivateWindow(0x5d,0x60,0x15));
    CHECK(!gm6::agingPrivateWindow(0,0,0)&&!gm6::agingPrivateWindow(0xff,0xff,0xff)&&!gm6::agingPrivateWindow(0x5d,0x48,0xff)&&!gm6::agingPrivateWindow(0x5d,0x60,0));
    CHECK(gm6::batteryTemperature(2970,7)&&std::abs(*gm6::batteryTemperature(2970,7)-23.85)<1e-8);
    CHECK(!gm6::batteryTemperature(0xffff,7)&&!gm6::batteryTemperature(2970,0)&&!gm6::batteryTemperature(2970,0xff));
    using Raw=std::array<std::uint8_t,gm6::batteryCoreAddresses.size()>;
    const auto load=[](const char* name){
        const auto read=[](std::string path){std::ifstream in(path,std::ios::binary);CHECK(in.good());return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in),{});};
        const auto bytes=read(std::string(MC_FIXTURES)+"/"+name+".bin"),valid=read(std::string(MC_FIXTURES)+"/"+name+".valid");
        CHECK(bytes.size()==4096&&valid.size()==4096);CHECK(bytes[0x740]==25&&(bytes[0x78E]&0x40));
        Raw raw{};for(std::size_t i=0;i<raw.size();++i){CHECK(valid[gm6::batteryCoreAddresses[i]]);raw[i]=bytes[gm6::batteryCoreAddresses[i]];}return raw;
    };
    const auto edit=[](Raw& raw,unsigned address,unsigned value,int width=2){for(int b=0;b<width;++b){const auto i=std::find(gm6::batteryCoreAddresses.begin(),gm6::batteryCoreAddresses.end(),address+b);CHECK(i!=gm6::batteryCoreAddresses.end());raw[i-gm6::batteryCoreAddresses.begin()]=static_cast<std::uint8_t>(value>>(8*b));}};
    const auto unplugged=gm6::decodeBatteryCore(load("gm6-unplugged")),charging=gm6::decodeBatteryCore(load("gm6-charging")),full=gm6::decodeBatteryCore(load("gm6-full"));
    for(const auto* e:{&unplugged,&charging,&full}){
        CHECK(e->supported&&e->coherent&&e->profileId=="mechrevo-gm6ir0c");
        CHECK(e->designMah==5200&&e->fullMah==4300&&e->battery.cycles==42);
        CHECK(std::abs(*e->battery.healthPercent-100.*4300/5200)<1e-8);
        CHECK(!e->battery.celsius&&e->registers.size()==27);CHECK(e->upperRaw==0&&e->restartRaw==0);
        CHECK(e->battery.powerStatus.find("firmware_BIF_design_substitution=true")!=std::string::npos);
        for(const auto& f:e->registers)CHECK(f.address!=0x4A2&&f.address!=0x520&&f.address!=0x522);
        CHECK(gui::ui::ecBatteryTimes(*e)[2].value=="—");
    }
    CHECK(!unplugged.battery.online&&unplugged.battery.onlineKnown&&unplugged.battery.flow==BatteryFlow::Discharging);
    CHECK(unplugged.battery.milliamps==4047&&unplugged.battery.percent==100);CHECK(std::abs(*unplugged.battery.watts+47.090892)<1e-8);
    CHECK(charging.battery.online&&charging.battery.flow==BatteryFlow::Charging&&charging.battery.milliamps==2006);
    CHECK(charging.battery.percent==76&&std::abs(*charging.battery.watts-23.714932)<1e-8);
    CHECK(full.battery.online&&full.battery.flow==BatteryFlow::Idle&&full.battery.watts==0&&full.battery.percent==100);
    CHECK(!gm6::decodeBatteryCore({}).coherent);
    auto raw=load("gm6-charging");edit(raw,0x432,3);CHECK(!gm6::decodeBatteryCore(raw).battery.watts);
    edit(raw,0x490,0,1);CHECK(!gm6::decodeBatteryCore(raw).battery.percent);
    edit(raw,0x400,0);auto unknown=gm6::decodeBatteryCore(raw);CHECK(!unknown.designMah&&!unknown.battery.watts&&unknown.coherent);
    raw=load("gm6-charging");edit(raw,0x434,0xFFFF);edit(raw,0x438,0);edit(raw,0x404,0xFFFF);
    unknown=gm6::decodeBatteryCore(raw);CHECK(!unknown.battery.milliamps&&!unknown.battery.volts&&!unknown.battery.healthPercent&&!unknown.battery.percent);
    raw=load("gm6-charging");edit(raw,0x400,0);edit(raw,0x404,4000);edit(raw,0x436,4000);edit(raw,0x438,12338);edit(raw,0x4A6,43);edit(raw,0x490,7,1);
    const auto reset=gm6::decodeBatteryCore(raw);
    CHECK(reset.coherent&&reset.battery.volts==12.338&&reset.battery.cycles==43);
    CHECK(!reset.designMah&&!reset.fullMah&&!reset.remainingMah&&!reset.battery.percent);
    CHECK(!reset.battery.milliamps&&!reset.battery.watts&&!reset.battery.healthPercent&&reset.battery.healthUnavailable);
    edit(raw,0x490,1,1);edit(raw,0x4A6,0);const auto notReady=gm6::decodeBatteryCore(raw);CHECK(!notReady.battery.cycles&&!notReady.battery.volts);
    raw=load("gm6-charging");edit(raw,0x432,0);CHECK(!gm6::decodeBatteryCore(raw).battery.watts);
    setLoggingEnabled(true);setBatteryLoggingEnabled(true);messages.clear();
    auto e=charging;e.battery.deviceId="offline/GM6/registers";
    journalBatteryRegisters(e,20000);CHECK(count("SNAPSHOT reason=initial")==1);
    e.registers[12].value=7;journalBatteryRegisters(e,20100);CHECK(count("cleared=0x0008")==1);
    e.battery.percent=77;journalBatteryRegisters(e,21000);CHECK(count("SNAPSHOT reason=soc_change")==1);
    CHECK(count("4A2")==0&&count("0520")==0&&count("0522")==0);
    messages.clear();auto initial=charging;initial.battery.deviceId="offline/GM6/capacity-reset";journalBatteryRegisters(initial,30000);
    auto changed=reset;changed.battery.deviceId=initial.battery.deviceId;journalBatteryRegisters(changed,30100);
    CHECK(count("full_capacity_raw=4000")>=1&&count("capacity_unit_raw=0")>=1);
    CHECK(count("previous_decimal=4300 current_decimal=4000")==1);
    const auto beforeQuiet=messages.size();
    for(unsigned i=1;i<=100;++i){for(auto& field:changed.registers)if(field.address==0x436)field.value=4000-i;journalBatteryRegisters(changed,30100+i);}
    CHECK(messages.size()==beforeQuiet); // Remaining is sampled; no per-tick log growth.
    setLoggingEnabled(false);
}
int main(){
    for(const auto id:{"wu-jie-15-xpro","mechrevo-gm6ir0c"}){
        CHECK(nativeOwnedVoltageTransition(id,7,4,true));
        CHECK(!nativeOwnedVoltageTransition(id,7,4,false));
        for(unsigned platform:{0u,4u,5u,6u,13u,255u})
            CHECK(!nativeOwnedVoltageTransition(id,platform,4,true));
        CHECK(!nativeOwnedVoltageTransition(id,7,255,true));
    }
    for(const auto id:{"mechrevo-gm5hg0a","eluktronics-hydroc16g2","machenike-l16","unknown"})
        CHECK(!nativeOwnedVoltageTransition(id,7,4,true));
    CHECK(!nativeBatteryVoltageContext("wu-jie-15-xpro",7,4));
    CHECK(nativeBatteryVoltageContext("wu-jie-15-xpro",7,255));
    for(const int limit:{10,60,85,95,100}){
        CHECK(batteryPolicyForChargeLimit(7,limit)==(limit==100?BatteryEcPolicy::Platform7Original:BatteryEcPolicy::Platform7Percent));
        CHECK(batteryPolicyForChargeLimit(4,limit)==BatteryEcPolicy::Platform4Original);
        for(unsigned platform:{0u,5u,6u,13u,255u})CHECK(!batteryPolicyForChargeLimit(platform,limit));
    }
    CHECK(!batteryPolicyForChargeLimit(7,0)&&!batteryPolicyForChargeLimit(7,101));
    CHECK(!nativeEcPolicyProfile("machenike-l16")&&!nativeEcPolicyProfile("huawei-hlyl-wxx9")&&!nativeEcPolicyProfile("minisforum-hpbsd"));
    decode();writes();journal();lenovoBatteryJournal();gm6Battery();std::printf("Battery offline: %u checks; hardware transports linked=0\n",offlineChecks);
}
