#include "OfflineTest.h"
#include "src/platform/windows/BatteryEnforcementWindows.h"
#include <array>
#include <fstream>
#include <vector>

namespace fs=std::filesystem;
namespace { fs::path fixtureDirectory; }
namespace mc {
void recordIdentityPolicyRequest(std::uint8_t,std::uint8_t,std::string_view) noexcept {}
void recordIdentityPairSnapshot(std::optional<std::uint8_t>,std::optional<std::uint8_t>,std::string_view,std::string_view) noexcept {}
void recordIdentityByteEvent(std::uint16_t,std::string_view,std::optional<std::uint8_t>,std::optional<std::uint8_t>,std::optional<std::uint8_t>,std::string_view,std::string_view,std::string_view) noexcept {}
namespace windows::batteryEnforcement {
std::string machineIdentity(){return "fixture-machine";}
fs::path stateDirectory(){return fixtureDirectory;}
}
}

int main(int argc,char** argv){
    CHECK(argc==2);
    fixtureDirectory=fs::absolute(argv[1]);
    fs::create_directories(fixtureDirectory);
    const auto receipt=fixtureDirectory/"wu-jie-15-xpro-battery-romid-owner.txt";
    const auto writeReceipt=[&](const char* machine){std::ofstream out(receipt,std::ios::trunc);out<<"v2 wu-jie-15-xpro 255 4 "<<machine<<'\n';CHECK(bool(out));};
    std::error_code error;fs::remove(receipt,error);
    std::array<std::uint8_t,65536> bytes{};bytes[0x7c3]=7;bytes[0x770]=4;
    std::vector<std::uint16_t> writes;
    const auto read=[&](std::uint16_t address){return bytes[address];};
    const auto write=[&](std::uint16_t address,std::uint8_t mask,std::uint8_t value){writes.push_back(address);bytes[address]=(bytes[address]&~mask)|(value&mask);};
    using namespace mc::windows::batteryEnforcement;
    CHECK(!hasValidOwnerReadOnly(fixtureDirectory,"wu-jie-15-xpro"));
    CHECK(rejected([&]{Change c(read,write,receipt,"wu-jie-15-xpro",mc::BatteryEcPolicy::Platform7Original,7);}));
    CHECK(writes.empty());
    writeReceipt("foreign-machine");
    CHECK(!hasValidOwnerReadOnly(fixtureDirectory,"wu-jie-15-xpro"));
    CHECK(rejected([&]{Change c(read,write,receipt,"wu-jie-15-xpro",mc::BatteryEcPolicy::Platform7Original,7);}));
    CHECK(writes.empty());
    writeReceipt("fixture-machine");
    CHECK(hasValidOwnerReadOnly(fixtureDirectory,"wu-jie-15-xpro"));
    {Change c(read,write,receipt,"wu-jie-15-xpro",mc::BatteryEcPolicy::Platform7Original,7);c.apply();c.commit();}
    CHECK(bytes[0x7c3]==7&&bytes[0x770]==255);
    CHECK(writes==std::vector<std::uint16_t>{0x770});
    CHECK(!fs::exists(receipt));
    writes.clear();
    {Change c(read,write,receipt,"wu-jie-15-xpro",mc::BatteryEcPolicy::Platform7Original,7);c.apply();c.commit();}
    CHECK(writes.empty()&&bytes[0x7c3]==7&&bytes[0x770]==255);
    writes.clear();bytes[0x7c3]=4;bytes[0x770]=255;
    CHECK(rejected([&]{Change c(read,write,receipt,"wu-jie-15-xpro",mc::BatteryEcPolicy::Platform7Original,7);}));
    CHECK(writes.empty());
    bytes[0x770]=4;writeReceipt("fixture-machine");
    CHECK(rejected([&]{Change c(read,write,receipt,"wu-jie-15-xpro",mc::BatteryEcPolicy::Platform4Original);}));
    CHECK(writes.empty()&&bytes[0x7c3]==4&&bytes[0x770]==4);
    bytes[0x7c3]=7;bytes[0x770]=4;writeReceipt("fixture-machine");
    {Change c(read,write,receipt,"wu-jie-15-xpro",mc::BatteryEcPolicy::Platform7Original,7);
        bytes[0x7c3]=4;CHECK(rejected([&]{c.apply();}));}
    CHECK(writes.empty()&&bytes[0x770]==4);
    fs::remove(receipt,error);
    std::printf("Battery ROM transition: %u checks; fake EC only\n",offlineChecks);
}
