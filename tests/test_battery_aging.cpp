#include "MagicControl/BatteryAging.h"
#include "MagicControl/BatteryDate.h"
#include "MagicControl/BatteryIdentityHistory.h"
#include "MagicControl/Logging.h"
#include "MagicControl/EcAccess.h"
#include "MagicControl/EcRegister.h"
#include "MagicControl/PortableSettings.h"
#include "TestBoardFixture.h"
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>
namespace {std::filesystem::path testRoot;}
namespace mc {std::string executableDirectory(){return testRoot.string();}}
namespace {
void check(bool b,const char* text){if(!b)throw std::runtime_error(text);}
struct Fake final:mc::IEcBackend {
    mc::BackendInfo metadata{"windows-acpi-driver","fake",mc::EcTransport::WindowsAcpiDriver,65535,true,false};
    std::array<std::uint8_t,65536> ram{};int writes{},failAt{-1},priorityCalls{},priorityMmioWrites{};bool failLock{},priorityWritable{true},ownerBusy{};mc::BatteryAgingAccess agingAccess{mc::BatteryAgingAccess::ReadWrite};std::string host="test-host";
    std::vector<mc::BatteryAgingField> agingWrites;
    Fake(){ram[0x454]=82;ram[0x456]=72;ram[0x740]=26;ram[mc::ec::kRamTableCapability]=mc::ec::kRamTableSupportedBit;
        ram[0x314]=0x5a;ram[0x315]=0x52;ram[0x402]=0;ram[0x403]=25;ram[0x4a6]=44;
        ram[0x9c9]=0x23;ram[0x9ca]=0x7a;ram[0x726]=0x80;ram[0x72d]=0x26;ram[0x72e]=0x10;}
    const mc::BackendInfo& info()const noexcept override{return metadata;}
    std::uint8_t read(std::uint16_t a)override{return ram[a];}
    void write(std::uint16_t a,std::uint8_t v)override{if(a==0x7cc)++priorityMmioWrites;ram[a]=v;}
    std::optional<mc::UsbPowerPriorityStatus> readUsbPriorityStatus()override{return mc::UsbPowerPriorityStatus{ram[0x7cc],priorityWritable,"unsupported fixture firmware"};}
    void setUsbPowerPriority(bool performance)override{++priorityCalls;ram[0x7cc]=std::uint8_t((ram[0x7cc]&0x7f)|(performance?0x80:0));}
    void lockTransaction()override{if(failLock){failLock=false;throw std::runtime_error("injected gate failure");}}
    mc::BatteryAgingAccess batteryAgingAccess(const mc::BoardDescriptor& board)const noexcept override{
        const auto id=mc::deviceProfile(board).id;
        return id=="wu-jie-15-xpro"||id=="mechrevo-gm6ir0c"?agingAccess:mc::BatteryAgingAccess::Unavailable;
    }
    void checkBatteryAgingWriteAccess()override{if(ownerBusy)throw std::runtime_error("foreign OEM owner");}
    std::optional<mc::BatteryAgingReading> readBatteryAging()override{
        const auto be=[&](int a){return std::uint16_t((ram[a]<<8)|ram[a+1]);};
        const auto date=be(0x7f2);const auto effective=date?date:be(0x314);
        const int month=((ram[0x72e]>>4)*10+(ram[0x72e]&15));
        const int year=2000+(ram[0x72d]>>4)*10+(ram[0x72d]&15);
        const auto age=std::uint8_t((year-(1980+(effective>>9)))*12+month-((effective>>5)&15));
        mc::BatteryAgingReading r{host,be(0x314),6400,44,date,be(0x9c9),age,age,ram[0x726],ram[0x3a1],ram[0x72d],ram[0x72e]};
        r.serviceBusy=(r.service&7)!=0;r.savePending=(r.pending&8)!=0;r.preservedContext=r.service&0x80;return r;
    }
    void writeBatteryAgingField(mc::BatteryAgingField field,std::uint8_t v)override{
        std::uint16_t a{};
        switch(field){
        case mc::BatteryAgingField::AlternativeDateHigh:a=0x7f2;break;
        case mc::BatteryAgingField::AlternativeDateLow:a=0x7f3;break;
        case mc::BatteryAgingField::ExposureHigh:a=0x9c9;break;
        case mc::BatteryAgingField::ExposureLow:a=0x9ca;break;
        case mc::BatteryAgingField::Save:a=0x726;break;
        default:throw std::runtime_error("invalid aging field");
        }
        check(std::filesystem::exists(mc::portableSettingsPath("battery-aging-override.state"))&&std::filesystem::exists(mc::portableSettingsPath("battery-aging-original.state")),"both backups must exist before hardware writes");
        ++writes;agingWrites.push_back(field);if(a==failAt){failAt=-1;throw std::runtime_error("injected byte failure");}
        if(a==0x726){check(v==1,"save mask only");return;}
        check(a==0x7f2||a==0x7f3||a==0x9c9||a==0x9ca,"write escaped aging allowlist");ram[a]=v;
    }
};
}
int main(){
    const auto* configured=std::getenv("MC_BATTERY_AGING_TEST_ROOT");
    auto tempBase=configured&&*configured?std::filesystem::path(configured):std::filesystem::temp_directory_path();
    if(tempBase.filename().empty())tempBase=tempBase.parent_path();
    tempBase=std::filesystem::weakly_canonical(tempBase);
    testRoot=tempBase/("magiccontrol-aging-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    if(std::filesystem::weakly_canonical(testRoot.parent_path())!=tempBase||!testRoot.filename().string().starts_with("magiccontrol-aging-")||std::filesystem::exists(testRoot))return 2;
    try{
        check(mc::decodeSbsBatteryDate(0x5a,0x79)=="2025-03-25","shared SBS decoder handles the saved GM6 date");
        check(mc::decodeSbsBatteryDate(0,0).empty()&&mc::decodeSbsBatteryDate(0xff,0xff).empty()&&
            mc::decodeSbsBatteryDate(0x5a,0x60).empty(),"unavailable or invalid SBS dates are not fabricated");
        std::array<std::uint8_t,4> dateBytes{0x5a,0x79,0x5a,0x79};std::size_t dateIndex{};
        const auto stableDate=mc::readStableSbsBatteryDate([&](unsigned){return dateBytes.at(dateIndex++);});
        check(stableDate&&*stableDate=="2025-03-25"&&dateIndex==4,"date needs two matching byte pairs");
        unsigned changing{};const auto unstableDate=mc::readStableSbsBatteryDate([&](unsigned){return std::uint8_t(changing++);});
        check(!unstableDate&&changing==12,"unstable dates are rejected after three bounded attempts");
        auto backend=std::make_unique<Fake>();auto* f=backend.get();mc::EcAccess ec(std::move(backend));
        check(ec.verifyBoard().verified,"fixture board verified");
        f->ram[0x7cc]=0xa4;ec.setUsbPowerPriority(false);
        check(ec.readUsbPriorityStatus()->raw==0x24&&f->priorityCalls==1&&f->priorityMmioWrites==0,"USB priority uses typed transport, never old MMIO writer");
        ec.setUsbPowerPriority(true);check(ec.readUsbPriorityStatus()->raw==0xa4,"USB priority preserves port flags");
        f->priorityWritable=false;bool priorityDenied=false;
        try{ec.setUsbPowerPriority(false);}catch(const std::exception&){priorityDenied=true;}
        check(priorityDenied&&f->priorityCalls==2&&f->ram[0x7cc]==0xa4,"Unsupported firmware cannot reach any USB priority writer");
        f->ram[0x3a1]=8;bool badBaseline=false;try{mc::setBatteryAgingOverride(ec,true);}catch(...){badBaseline=true;}
        check(badBaseline&&f->writes==0&&!std::filesystem::exists(mc::portableSettingsPath("battery-aging-original.state")),"busy firmware cannot be captured as a baseline");f->ram[0x3a1]=0;
        f->ram[0x72e]=0;badBaseline=false;try{mc::setBatteryAgingOverride(ec,true);}catch(...){badBaseline=true;}
        check(badBaseline&&f->writes==0&&!std::filesystem::exists(mc::portableSettingsPath("battery-aging-original.state")),"invalid clock is rejected before backup or hardware writes");f->ram[0x72e]=0x10;
        mc::setBatteryAgingOverride(ec,true);
        check(f->agingWrites==std::vector<mc::BatteryAgingField>{mc::BatteryAgingField::AlternativeDateLow,
            mc::BatteryAgingField::AlternativeDateHigh,mc::BatteryAgingField::ExposureHigh,
            mc::BatteryAgingField::ExposureLow,mc::BatteryAgingField::Save},"safe field order and save strobe preserved");
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
        // Block the actual publication lease, not the obsolete fixed .tmp
        // filename: the storage layer now uses unique temporary siblings.
        const auto statePath=mc::portableSettingsPath("battery-aging-override.state");
        const auto publication=statePath.parent_path()/"locks"/"battery-aging-override.state.publish.lock";
        const auto held=std::filesystem::path(publication.string()+".held-test");
        const bool hadLease=std::filesystem::exists(publication);
        if(hadLease)std::filesystem::rename(publication,held);
        std::filesystem::create_directories(publication);const auto preStorage=f->writes;rejected=false;
        try{mc::setBatteryAgingOverride(ec,true);}catch(...){rejected=true;}
        check(rejected&&f->writes==preStorage,"storage failure must stop before hardware writes");
        std::filesystem::remove(publication);if(hadLease)std::filesystem::rename(held,publication);
        const auto count=f->writes;f->host="another-host";rejected=false;
        try{mc::setBatteryAgingOverride(ec,true);}catch(...){rejected=true;}
        check(rejected&&f->writes==count,"foreign-host backup refused before writes");f->host="test-host";
        std::ofstream(mc::portableSettingsPath("battery-aging-override.state"))<<"corrupt";
        rejected=false;try{mc::setBatteryAgingOverride(ec,false);}catch(...){rejected=true;}
        check(rejected&&f->writes==count,"corrupt backup cannot write");
        check(f->ram[0x726]==0x80&&f->ram[0x4a6]==44&&f->ram[0x314]==0x5a&&f->ram[0x315]==0x52,"fan/cycle/manufacture preserved");
        std::filesystem::remove(mc::portableSettingsPath("battery-aging-override.state"));
        std::filesystem::remove(mc::portableSettingsPath("battery-aging-original.state"));
        // A second verified profile can use the same owner only through a
        // backend that explicitly supplies access; no real GM6 transport here.
        mc::BoardDescriptor gm6;gm6.manufacturer="MECHREVO";gm6.baseBoard="GM6IR0C";gm6.productName="JiguangX Series GM6IR0C";
        mc::tests::setTestBoard(gm6);
        auto simulated=std::make_unique<Fake>();auto* sim=simulated.get();sim->ram[0x740]=25;
        sim->ram[0x314]=0x5a;sim->ram[0x315]=0x79;sim->agingAccess=mc::BatteryAgingAccess::ReadOnly;
        mc::EcAccess second(std::move(simulated));check(second.verifyBoard().verified,"fake GM6 identity verified");
        const auto ro=mc::observeBatteryAging(second);
        check(ro.reading&&ro.realHistory==9082&&!ro.writable,"read-only backend exposes observed history without granting a writer");
        rejected=false;try{mc::setBatteryAgingOverride(second,true);}catch(...){rejected=true;}
        check(rejected&&sim->writes==0&&!std::filesystem::exists(mc::portableSettingsPath("battery-aging-original.state")),
            "read-only aging cannot create a backup or send a write");
        sim->agingAccess=mc::BatteryAgingAccess::ReadWrite;
        sim->ownerBusy=true;rejected=false;try{mc::setBatteryAgingOverride(second,true);}catch(...){rejected=true;}
        check(rejected&&sim->writes==0&&!std::filesystem::exists(mc::portableSettingsPath("battery-aging-original.state")),
            "foreign OEM owner is rejected before backup or writes");sim->ownerBusy=false;
        mc::setBatteryAgingOverride(second,true);mc::setBatteryAgingOverride(second,false);
        check(mc::observeBatteryAging(second).realHistory==9082&&sim->ram[0x314]==0x5a&&sim->ram[0x315]==0x79,
            "second fake backend uses common backup/restore and preserves the manufacture date");
        std::filesystem::remove(mc::portableSettingsPath("battery-aging-override.state"));
        std::filesystem::remove(mc::portableSettingsPath("battery-aging-original.state"));
        sim->agingAccess=mc::BatteryAgingAccess::Unavailable;const auto unavailableWrites=sim->writes;
        rejected=false;try{mc::setBatteryAgingOverride(second,true);}catch(...){rejected=true;}
        check(rejected&&sim->writes==unavailableWrites&&!second.readBatteryAging(),"unavailable backend cannot read or write aging");
        // Every registered actual board, plus Unknown: backend-specific
        // admission must never turn an identity/profile into foreign writes.
        unsigned checkedProfiles{};
        for(const auto* profile:mc::deviceProfiles()){
            if(!profile)continue;
            mc::BoardDescriptor board;board.manufacturer=profile->manufacturer;board.baseBoard=profile->baseBoard;board.productName=profile->productName;
            mc::tests::setTestBoard(board);
            auto candidate=std::make_unique<Fake>();auto* raw=candidate.get();
            if(!profile->ecBackendId.empty())raw->metadata.id=std::string(profile->ecBackendId);
            for(const auto& probe:profile->identity)raw->ram[probe.address]=probe.expected;
            mc::EcAccess candidateEc(std::move(candidate));const bool verified=candidateEc.verifyBoard().verified;
            const auto actual=mc::deviceProfile(board).id;
            const bool expected=verified&&(actual=="wu-jie-15-xpro"||actual=="mechrevo-gm6ir0c");
            check((candidateEc.batteryAgingAccess()==mc::BatteryAgingAccess::ReadWrite)==expected,"profile aging admission mismatch");
            if(!expected){bool denied=false;try{candidateEc.writeBatteryAgingField(mc::BatteryAgingField::Save,1);}catch(...){denied=true;}
                check(denied&&raw->writes==0,"foreign profile reached aging writer");}
            ++checkedProfiles;
        }
        check(checkedProfiles==mc::deviceProfiles().size()&&checkedProfiles>=10,"all registered profiles checked");
        mc::tests::setTestBoard(mc::BoardDescriptor{"Other","Unknown",{}, {}});
        auto foreign=std::make_unique<Fake>();auto* other=foreign.get();mc::EcAccess denied(std::move(foreign));
        check(!denied.verifyBoard().verified,"unknown board is not admitted");rejected=false;
        try{mc::setBatteryAgingOverride(denied,true);}catch(...){rejected=true;}
        check(rejected&&other->writes==0,"other device cannot write aging registers");mc::tests::resetTestBoard();
        const auto identity=mc::defaultLogDirectory()/"battery_id.txt";
        check(identity.lexically_relative(testRoot).native().find(std::filesystem::path("..").native())!=0,"identity history stays in isolated test directory");
        std::filesystem::create_directories(identity.parent_path());std::ofstream(identity)<<"previous startup\n";
        const auto readIdentity=[&]{std::ifstream in(identity);return std::string(std::istreambuf_iterator<char>(in),{});};
        mc::beginBatteryIdentityHistory(false);mc::preferBatteryIdentityEc();
        mc::observeBatteryIdentity("","EC fixture",true);
        check(mc::batteryIdentityHistoryNeedsEcSample()&&readIdentity()=="previous startup\n","a missed first date keeps the EC startup observation pending");
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
