#include "NvflashOutput.h"
#include "NvflashPlatform.h"
#include "../../../src/gui/common/DeviceHeader.h"
#include <iostream>
#include <stdexcept>
#include <set>
#include <array>
using namespace mc::diagnostics::nvflash;
unsigned checks{};
void require(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
std::string descriptor(std::string geometry="16384Kx1S",std::string id="EF,4015",std::string part="Vendor SyntheticPart 2.7-3.6V "){
    return "NVIDIA Firmware Update Utility (Version5.867.0)\nEEPROM ID ("+id+") : "+part+geometry+", page\n";
}
template<class F>bool refuses(F f){try{f();return false;}catch(const std::exception&){return true;}}
int main(){try{
    const auto c=capacityFromCheck(descriptor());
    require(c.recognized&&c.admitted&&c.bytes==2097152,"Kx1 means bits, not KiB");
    require(capacityFromCheck(descriptor("2048Kx8")).bytes==c.bytes,"Kx8 organization");
    require(entireSizeMatches(c,2097152)&&!entireSizeMatches(c,2048000),"2000KB VBIOS is not a2MiB chip dump");
    require(!entireSizeMatches(c,0)&&!entireSizeMatches(c,4194304),"exact size admission");
    require(capacityFromCheck(descriptor("131072Kx1S")).admitted,"16MiB inclusive boundary");
    const auto large=capacityFromCheck(descriptor("262144Kx1S"));
    require(large.recognized&&!large.admitted&&large.bytes==33554432,"32MiB uncapped geometry must be refused");
    require(!capacityFromCheck("4096Kx1S\n").admitted,"geometry without a part ID");
    require(!capacityFromCheck(descriptor("16384Kx1S","EF,4015","Unknown ")).admitted,"unknown descriptor");
    for(const auto* id:{"00,0000","FF,FFFF","EF,FFFF"})require(!capacityFromCheck(descriptor("16384Kx1S",id)).admitted,"erased/unknown ID");
    for(const auto* g:{"0Kx1S","16384Kx0S","18446744073709551615Kx65535","9999999999999999999999999999Kx1S"})require(!capacityFromCheck(descriptor(g)).admitted,"invalid/overflow geometry");
    require(capacityFromCheck(descriptor()+descriptor()).admitted,"repeated identical descriptor");
    require(!capacityFromCheck(descriptor()+descriptor("32768Kx1S")).admitted,"ambiguous geometry");
    require(!capacityFromCheck(descriptor()+descriptor("16384Kx1S","C2,2015")).admitted,"ambiguous part ID");
    require(!capacityFromCheck(descriptor()+descriptor("16384Kx1S","EF,4015","Unknown ")).admitted,"valid row must not mask a later unknown part");
    require(!capacityFromCheck(descriptor()+"Limited the part size33554432 to the max of buffer size16777216").admitted,"truncation marker");
    require(readProblem("Limited the part size33554432 to the max of buffer size16777216",false).empty(),"ordinary VBIOS scope must not inherit entire-capacity admission");
    require(!capacityFromCheck(descriptor()+"ERROR: EEPROM read failed").admitted,"error despite a recognized part");
    require(!sameCapacity(c,capacityFromCheck(descriptor("32768Kx1S"))),"changed chip size");
    require(!sameCapacity(c,capacityFromCheck(descriptor("16384Kx1S","C2,2015"))),"changed chip ID");
    require(compareConfirmed("Firmware images are identical\n"),"positive raw compare");
    require(!compareConfirmed("")&&!compareConfirmed("Firmware images are different"),"silent/negative compare");
    require(!compareConfirmed("Firmware images are identical\nERROR: read failed"),"contradictory compare");
    const std::set<std::string> allowed{"--list","--index=7","--check","--save","--entire","--compare","/captures/file.rom"};
    for(const auto cmd:{Command::List,Command::Check,Command::Save,Command::SaveEntire,Command::Compare})for(const auto& arg:arguments(cmd,7,"/captures/file.rom"))require(allowed.contains(arg),"non-read option reached argv");
    require(refuses([]{arguments(static_cast<Command>(99),7,"/captures/file.rom");}),"invalid enum");
    require(refuses([]{arguments(Command::SaveEntire,999,"/captures/file.rom");}),"invalid index");
    require(refuses([]{arguments(Command::Save,0,"--protectoff");}),"option-shaped filename");
    unsigned nvidia{},integrated{},unknown{};std::set<std::string_view> families;
    for(const auto& model:mc::gui::ui::kSupportedLaptops){
        if(model.id=="noname"){++unknown;continue;}
        if(model.previewGpu.empty()){if(model.id=="wu-jie-15-xpro"||model.id=="huawei-hlyl-wxx9")++integrated;else ++unknown;continue;}
        require(model.previewGpu.starts_with("NVIDIA GeForce RTX "),"non-NVIDIA borrowed catalogue path");
        ++nvidia;families.insert(model.previewGpu);
        // The same capacity/parser/command gate for every catalogue GPU. No
        // per-model EC registers, CPU vendor or display-route assumptions.
        require(capacityFromCheck(descriptor()).admitted,"capacity gate differs by GPU catalogue row");
        const auto args=arguments(Command::SaveEntire,7,"/captures/file.rom");
        require(args.size()==4&&args[1]=="--entire"&&args[2]=="--save","entire read contract across GPU rows");
    }
    require(nvidia==14&&integrated==2&&nvidia+integrated+unknown==mc::gui::ui::kSupportedLaptops.size()&&families.size()==7,"catalogue coverage16models/14NVIDIA/7families");
    std::cout<<"PASS "<<checks<<" offline checks;14NVIDIA profiles/7GPU families. No platform/vendor/hardware code linked.\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
