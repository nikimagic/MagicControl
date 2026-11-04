#include "OfflineTest.h"
#include "MagicControl/Logging.h"
#include "MagicControl/BoardIdentity.h"
#include "MagicControl/DeviceProfile.h"
#include <fstream>
#include <string>
#include <chrono>
#include <vector>
namespace fs=std::filesystem;
// Software-only board metadata. Path discovery remains a fatal tripwire: the
// logger must use the explicit scratch directory, never a real portable folder.
namespace mc {
BoardDescriptor readBoardDescriptor(){BoardDescriptor b;b.baseBoard="Fixture";return b;}
const DeviceProfile& deviceProfile(const BoardDescriptor&) noexcept{static const DeviceProfile p{"offline-fixture","fixture","Fixture","fixture"};return p;}
std::filesystem::path portableSettingsPath(std::string_view){std::abort();}
}
std::string read(const fs::path& path){std::ifstream in(path,std::ios::binary);CHECK(in.good());return {std::istreambuf_iterator<char>(in),{}};}
void write(const fs::path& path,std::string_view data){std::ofstream out(path,std::ios::binary);out<<data;CHECK(out.good());}
std::string session(const std::string& text){const auto start=text.find("\"session\":\"");CHECK(start!=std::string::npos);const auto end=text.find('"',start+11);CHECK(end!=std::string::npos);return text.substr(start+11,end-start-11);}
int main(){
    const auto base=fs::path(MC_LOG_TEST_DIR)/("run-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));fs::create_directories(base);
    CHECK(mc::initializeLogging(true,base,true));CHECK(mc::currentLogPath().filename()=="MagicControl-1.jsonl");CHECK(mc::currentBatteryLogPath().filename()=="battery-1.jsonl");
    mc::logMessage("battery","one-percent");mc::logMessage("battery_candidates","raw-only");
    const auto general1=read(base/"MagicControl-1.jsonl"),battery1=read(base/"battery-1.jsonl");CHECK(session(general1)==session(battery1));CHECK(general1.find("one-percent")!=std::string::npos&&general1.find("raw-only")==std::string::npos);CHECK(battery1.find("raw-only")!=std::string::npos);
    CHECK(mc::initializeLogging(true,base,false));CHECK(mc::currentBatteryLogPath().filename()=="battery-2.jsonl");mc::logMessage("battery_candidates","must-not-collect");
    const auto disabled=read(base/"battery-2.jsonl");CHECK(disabled.find("\"battery_log_enabled\":false")!=std::string::npos&&disabled.find("must-not-collect")==std::string::npos);CHECK(session(disabled)==session(read(base/"MagicControl-2.jsonl")));
    CHECK(mc::initializeLogging(false,base,true));CHECK(mc::currentLogPath().filename()=="MagicControl-3.jsonl");mc::logMessage("battery_candidates","battery-only");CHECK(read(base/"battery-3.jsonl").find("battery-only")!=std::string::npos);CHECK(read(base/"MagicControl-3.jsonl").find("battery-only")==std::string::npos);
    const auto pair2=read(base/"MagicControl-2.jsonl"),pair3=read(base/"battery-3.jsonl");
    const auto now=fs::file_time_type::clock::now();for(int slot=1;slot<=3;++slot)for(const auto prefix:{"MagicControl-","battery-"})fs::last_write_time(base/(std::string(prefix)+std::to_string(slot)+".jsonl"),now-std::chrono::hours(4-slot));
    CHECK(mc::initializeLogging(true,base,true));CHECK(mc::currentLogPath().filename()=="MagicControl-1.jsonl");CHECK(session(read(base/"MagicControl-1.jsonl"))!=session(general1));CHECK(pair2==read(base/"MagicControl-2.jsonl")&&pair3==read(base/"battery-3.jsonl"));
    const std::string large(8*1024*1024+512,'x');mc::logMessage("battery_candidates",large);mc::logMessage("large-general",large);
    CHECK(fs::file_size(base/"battery-1.jsonl")>8*1024*1024&&fs::file_size(base/"MagicControl-1.jsonl")>8*1024*1024);
    unsigned rootFiles{};for(const auto& entry:fs::directory_iterator(base))if(entry.is_regular_file()){++rootFiles;CHECK(entry.path().filename().string().find(".part")==std::string::npos);}CHECK(rootFiles==6);
    const auto dump=base/"dump";fs::create_directory(dump);std::string error;CHECK(mc::snapshotCurrentLog(dump/"session.jsonl",error));CHECK(session(read(dump/"session.jsonl"))==session(read(dump/"battery.jsonl")));CHECK(read(dump/"session.jsonl").find("\"retained_tail\":false")!=std::string::npos);CHECK(!mc::snapshotCurrentLog(dump/"session.jsonl",error));
    const auto old=base/"old";fs::create_directory(old);write(old/"battery.jsonl","legacy-battery");write(old/"battery.jsonl.part1","legacy-part");write(old/"MagicControl-1.jsonl","legacy-main");
    CHECK(mc::initializeLogging(true,old,true));CHECK(!fs::exists(old/"battery.jsonl")&&!fs::exists(old/"battery.jsonl.part1"));
    unsigned archived{};for(const auto& entry:fs::recursive_directory_iterator(old/"legacy-battery"))if(entry.is_regular_file()){
        ++archived;const auto text=read(entry.path());CHECK(text=="legacy-battery"||text=="legacy-part"||text=="legacy-main");}CHECK(archived==3);
    CHECK(mc::initializeLogging(false,base/"disabled",false));CHECK(!fs::exists(base/"disabled"));
    std::printf("Paired log slots: %u checks; software-only board, explicit scratch directory, no OS inventory or hardware\n",offlineChecks);
}
