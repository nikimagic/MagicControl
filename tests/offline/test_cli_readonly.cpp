// Real pure CLI admission; no application, platform, driver or hardware linkage.
#include "../../src/cli/CurrentCommands.h"
#include "OfflineTest.h"
#include <algorithm>
#include <iostream>
using namespace mc::cli::detail;
int main(){
    const std::vector<std::vector<std::string>> writes{
        {"power","office"},{"power","custom","10","15","20"},{"power","slot","1"},
        {"fans","default"},{"fans","max"},{"fans","fixed","5","100"},{"fans","manual","draft.csv"},
        {"battery","limit","85"},{"battery","restart","80"},{"battery","thresholds","80","95"},
        {"battery","view","native"},{"battery","protection","healthy"},{"battery","policy","7/FF"},
        {"battery","native-turbo","on"},{"battery","priority","battery"},
        {"keyboard","level","0"},{"keyboard","cycle"},{"keyboard","color","1","2","3"},
        {"lighting","set","logo","1","0","0","0","1","2","3"},
        {"keys","microphone","off"},{"keys","bluetooth","on"},
        {"gpu","power","nvidia","fixture","50"},{"gpu","reset","radeon","fixture"},
        {"dump-ec","--with-fan-probes"}
    };
    for(const auto& command:writes){
        CHECK(writer(parse(command))); // Existing --yes/dry-run classification.
        for(bool yes:{false,true})for(bool noEc:{false,true})for(std::size_t position=0;position<=command.size();++position){
            auto args=command;args.insert(args.begin()+position,"--read-only");
            if(yes)args.push_back("--yes");if(noEc)args.push_back("--no-ec");
            bool rejected=false;try{static_cast<void>(parse(args));}catch(const std::invalid_argument& e){rejected=std::string(e.what()).find("--read-only")!=std::string::npos;}
            CHECK(rejected);
        }
    }
    const std::vector<std::vector<std::string>> reads{
        {"models"},{"board"},{"status"},{"resources","all"},{"inventory"},{"fans"},{"fans","status"},
        {"power","status"},{"battery","status"},{"battery","priority","status"},
        {"keyboard","status"},{"keys","microphone"},{"lighting","zones"},{"gpu","status"},
        {"lpp","status"},{"dump-ec"},{"dump-bios"},{"--dump-bios"},{"dump-vbios"},{"--dump-vbios"},{"dump-eeprom"},{"--dump-eeprom"}
    };
    for(auto command:reads){command.push_back("--read-only");command.push_back("--yes");const auto options=parse(command);CHECK(options.readOnly&&!writer(options));}
    bool empty=false;try{parse({});}catch(const std::invalid_argument&){empty=true;}CHECK(empty);
    std::cout<<"PASS: "<<offlineChecks<<" CLI read-only admission checks; no Runtime/backend linked\n";
}
