#include "MagicControl/BatteryAging.h"
#include "MagicControl/BatteryIdentityHistory.h"
#include "MagicControl/Logging.h"
#include "MagicControl/EcAccess.h"
#include "MagicControl/EcRegister.h"
#include "MagicControl/PortableSettings.h"
#include "TestBoardFixture.h"
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
namespace {std::filesystem::path testRoot;}
namespace mc {std::string executableDirectory(){return testRoot.string();}}
namespace {
void check(bool b,const char* text){if(!b)throw std::runtime_error(text);}
struct Fake final:mc::IEcBackend {
    mc::BackendInfo metadata{"windows-acpi-driver","fake",mc::EcTransport::WindowsAcpiDriver,65535,true,false};
    std::array<std::uint8_t,65536> ram{};int writes{},failAt{-1},priorityCalls{},priorityMmioWrites{};bool failLock{},priorityWritable{true};std::string host="test-host";
    Fake(){ram[0x454]=82;ram[0x456]=72;ram[0x740]=26;ram[mc::ec::kRamTableCapability]=mc::ec::kRamTableSupportedBit;
        ram[0x314]=0x5a;ram[0x315]=0x52;ram[0x402]=0;ram[0x403]=25;ram[0x4a6]=44;
        ram[0x9c9]=0x23;ram[0x9ca]=0x7a;ram[0x726]=0x80;ram[0x72d]=0x26;ram[0x72e]=0x10;}
    const mc::BackendInfo& info()const noexcept override{return metadata;}
    std::uint8_t read(std::uint16_t a)override{return ram[a];}
    void write(std::uint16_t a,std::uint8_t v)override{if(a==0x7cc)++priorityMmioWrites;ram[a]=v;}
    std::optional<mc::UsbPowerPriorityStatus> readUsbPriorityStatus()override{return mc::UsbPowerPriorityStatus{ram[0x7cc],priorityWritable,"unsupported fixture firmware"};}
    void setUsbPowerPriority(bool performance)override{++priorityCalls;ram[0x7cc]=std::uint8_t((ram[0x7cc]&0x7f)|(performance?0x80:0));}
    void lockTransaction()override{if(failLock){failLock=false;throw std::runtime_error("injected gate failure");}}
    std::optional<mc::BatteryAgingReading> readBatteryAging()override{
        const auto be=[&](int a){return std::uint16_t((ram[a]<<8)|ram[a+1]);};
        const auto date=be(0x7f2);const auto effective=date?date:be(0x314);
        const int month=((ram[0x72e]>>4)*10+(ram[0x72e]&15));
        const int year=2000+(ram[0x72d]>>4)*10+(ram[0x72d]&15);
        const auto age=std::uint8_t((year-(1980+(effective>>9)))*12+month-((effective>>5)&15));
        return mc::BatteryAgingReading{host,be(0x314),6400,44,date,be(0x9c9),age,age,ram[0x726],0,ram[0x72d],ram[0x72e]};
    }
    void writeBatteryAgingByte(std::uint16_t a,std::uint8_t v)override{
        check(std::filesystem::exists(mc::portableSettingsPath("battery-aging-override.state"))&&std::filesystem::exists(mc::portableSettingsPath("battery-aging-original.state")),"both backups must exist before hardware writes");
        ++writes;if(a==failAt){failAt=-1;throw std::runtime_error("injected byte failure");}
        if(a==0x726){check(v==1,"save mask only");return;}
        check(a==0x7f2||a==0x7f3||a==0x9c9||a==0x9ca,"write escaped aging allowlist");ram[a]=v;
    }
};
}
int main(){
    testRoot=std::filesystem::temp_directory_path()/("magiccontrol-aging-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    if(testRoot.parent_path()!=std::filesystem::temp_directory_path()||!testRoot.filename().string().starts_with("magiccontrol-aging-")||std::filesystem::exists(testRoot))return 2;
    try{
        auto backend=std::make_unique<Fake>();auto* f=backend.get();mc::EcAccess ec(std::move(backend));
        check(ec.verifyBoard().verified,"fixture board verified");
        f->ram[0x7cc]=0xa4;ec.setUsbPowerPriority(false);
        check(ec.readUsbPriorityStatus()->raw==0x24&&f->priorityCalls==1&&f->priorityMmioWrites==0,"USB priority uses typed transport, never old MMIO writer");
        ec.setUsbPowerPriority(true);check(ec.readUsbPriorityStatus()->raw==0xa4,"USB priority preserves port flags");
        f->priorityWritable=false;bool priorityDenied=false;
        try{ec.setUsbPowerPriority(false);}catch(const std::exception&){priorityDenied=true;}
        check(priorityDenied&&f->priorityCalls==2&&f->ram[0x7cc]==0xa4,"Unsupported firmware cannot reach any USB priority writer");
        mc::setBatteryAgingOverride(ec,true);
        auto s=mc::observeBatteryAging(ec);check(s.enabled==true&&s.reading->history==0&&s.reading->months==1,"enable confirmed");
        f->failLock=true;const auto unavailable=mc::observeBatteryAging(ec);
        check(!unavailable.authoritative&&!unavailable.enabled&&!unavailable.writable,"optional aging gate failure stays unknown without throwing");
        f->ram[0x9ca]=3;mc::setBatteryAgingOverride(ec,true,true);
        s=mc::observeBatteryAging(ec);check(s.reading->history==0&&s.realHistory==9085,"startup preserves accumulated history across reset");
        f->ram[0x7f2]=0;f->ram[0x7f3]=0;f->ram[0x9c9]=0x23;f->ram[0x9ca]=0x7a;
        check(mc::observeBatteryAging(ec).realHistory==9085,"lost EC override must not double count restored original history");
        mc::setBatteryAgingOverride(ec,true,true);
        check(mc::observeBatteryAging(ec).realHistory==9085,"cold restore preserves real history");
        f->ram[0x72e]=0x11;mc::setBatteryAgingOverride(ec,true,true);
        check(mc::observeBatteryAging(ec).reading->date==0x5d41,"startup refreshes one-month date");
        f->ram[0x72d]=0x27;f->ram[0x72e]=1;mc::setBatteryAgingOverride(ec,true,true);
        check(mc::observeBatteryAging(ec).reading->date==0x5d81,"one-month date crosses year boundary");
        mc::setBatteryAgingOverride(ec,false);
        s=mc::observeBatteryAging(ec);check(s.enabled==false&&s.reading->date==0&&s.reading->history==9085,"off restores original date and complete real history");
        f->failAt=0x9ca;bool rejected=false;try{mc::setBatteryAgingOverride(ec,true);}catch(...){rejected=true;}
        check(rejected,"write failure reported");s=mc::observeBatteryAging(ec);
        check(s.enabled==false&&s.reading->date==0&&s.reading->history==9085,"partial write rolled back without history loss");
        const auto temporary=mc::portableSettingsPath("battery-aging-override.state").string()+".tmp";
        std::filesystem::create_directory(temporary);const auto preStorage=f->writes;rejected=false;
        try{mc::setBatteryAgingOverride(ec,true);}catch(...){rejected=true;}
        check(rejected&&f->writes==preStorage,"storage failure must stop before hardware writes");std::filesystem::remove(temporary);
        const auto count=f->writes;f->host="another-host";rejected=false;
        try{mc::setBatteryAgingOverride(ec,true);}catch(...){rejected=true;}
        check(rejected&&f->writes==count,"foreign-host backup refused before writes");f->host="test-host";
        std::ofstream(mc::portableSettingsPath("battery-aging-override.state"))<<"corrupt";
        rejected=false;try{mc::setBatteryAgingOverride(ec,false);}catch(...){rejected=true;}
        check(rejected&&f->writes==count,"corrupt backup cannot write");
        check(f->ram[0x726]==0x80&&f->ram[0x4a6]==44&&f->ram[0x314]==0x5a&&f->ram[0x315]==0x52,"fan/cycle/manufacture preserved");
        std::filesystem::remove(mc::portableSettingsPath("battery-aging-override.state"));
        std::filesystem::remove(mc::portableSettingsPath("battery-aging-original.state"));
        mc::tests::setTestBoard(mc::BoardDescriptor{"Other","Unknown",{}, {}});
        auto foreign=std::make_unique<Fake>();auto* other=foreign.get();mc::EcAccess denied(std::move(foreign));
        check(!denied.verifyBoard().verified,"unknown board is not admitted");rejected=false;
        try{mc::setBatteryAgingOverride(denied,true);}catch(...){rejected=true;}
        check(rejected&&other->writes==0,"other device cannot write aging registers");mc::tests::resetTestBoard();
        const auto identity=mc::defaultLogDirectory()/"battery_id.txt";
        check(identity.lexically_relative(testRoot).native().find(std::filesystem::path("..").native())!=0,"identity history stays in isolated test directory");
        std::filesystem::create_directories(identity.parent_path());std::ofstream(identity)<<"previous startup\n";
        const auto readIdentity=[&]{std::ifstream in(identity);return std::string(std::istreambuf_iterator<char>(in),{});};
        mc::beginBatteryIdentityHistory(true);
        mc::observeBatteryIdentity("2025-02-18","EC fixture",true);
        mc::observeBatteryIdentity("2025-02-18","OS fixture",false);
        check(mc::batteryIdentityHistoryNeedsEcSample()&&readIdentity()=="previous startup\n","startup row waits for thermal history without overwriting older rows");
        mc::observeBatteryIdentity("2025-02-18","EC fixture",true,s.realHistory);
        const auto recorded=readIdentity();
        check(!mc::batteryIdentityHistoryNeedsEcSample()&&recorded.starts_with("previous startup\n")&&
            recorded.find("thermal_hours=9085")!=std::string::npos&&recorded.find("date=2025-02-18")!=std::string::npos,
            "startup identity appends original plus new exposure rather than spoofed raw zero");
        mc::observeBatteryIdentity("2025-02-18","EC fixture",true,0);
        check(readIdentity()==recorded,"later polling cannot append a second startup row or replace real history");
        std::filesystem::remove_all(testRoot);std::cout<<"Battery aging: enable, restore, month refresh, rollback, host and corruption guards passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';std::filesystem::remove_all(testRoot);return 1;}
}
