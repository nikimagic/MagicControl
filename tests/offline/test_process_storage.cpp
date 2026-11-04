// Fixture process only. Real logger/cache/storage; all board/path/provider
// discovery is replaced, and no app, device library or hardware API is linked.
#include "MagicControl/Logging.h"
#include "MagicControl/HardwareInventory.h"
#include "MagicControl/FileStorage.h"
#include "MagicControl/PortableSettings.h"
#include "MagicControl/BoardDescriptor.h"
#include "MagicControl/DeviceProfile.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace fs=std::filesystem;
namespace {fs::path fixtureRoot;
void check(bool ok,const std::string& reason){if(!ok)throw std::runtime_error(reason);}
void ready(const std::string& name,const std::string& value){std::string error;check(mc::detail::writeStorageTextAtomically(fixtureRoot/"control"/(name+".ready"),value,error),error);}
void release(){std::string line;std::getline(std::cin,line);}
}
namespace mc {
BoardDescriptor readBoardDescriptor(){BoardDescriptor b;b.baseBoard="Fixture";return b;}
const DeviceProfile& deviceProfile(const BoardDescriptor&) noexcept{static const DeviceProfile p{"offline-fixture","fixture","Fixture","fixture"};return p;}
bool uniwillIdentityHistoryProfile(const DeviceProfile&) noexcept{return false;}
std::filesystem::path portableSettingsPath(std::string_view name){check(!fixtureRoot.empty(),"missing explicit fixture root");return fixtureRoot/fs::path(std::u8string(reinterpret_cast<const char8_t*>(name.data()),name.size()));}
std::string executableDirectory(){throw std::runtime_error("unexpected legacy/real executable discovery");}
}
int fixture(int argc,const std::string& mode,const fs::path& root,const std::string& label){try{
    check(argc==4,"usage: fixture MODE ROOT LABEL");fixtureRoot=root;
    check(fixtureRoot.is_absolute(),"fixture root must be absolute");fs::create_directories(fixtureRoot);
    if(mode=="log-hold"||mode=="log-once"){
        if(!mc::initializeLogging(true,fixtureRoot/"logs",true)){std::cerr<<mc::loggingInitializationError()<<'\n';return 4;}
        mc::logMessage("battery",label+":begin");const auto file=mc::currentLogPath().filename().string();ready(label,file);
        if(mode=="log-hold")release();mc::logMessage("battery",label+":end");return 0;
    }
    if(mode=="inventory-hold"){
        std::string error;auto lease=mc::detail::lockStorageFile(fixtureRoot/"logs_Fixture"/".locks"/"inventory.lock",true,error);
        check(bool(lease),error);ready(label,"locked");release();return 0;
    }
    if(mode=="inventory-write"){
        for(int i=0;i<80;++i){mc::HardwareInventory inv;inv.readAt=label+":"+std::to_string(i);
            inv.addSection("fixture").addRow("value",inv.readAt+std::string(32000,label.front()));std::string error;
            check(mc::InventoryCache::save(inv,error),error);}
        return 0;
    }
    if(mode=="inventory-read"){
        for(int i=0;i<200;++i){const auto inv=mc::InventoryCache::load();check(bool(inv),"cache load/parse failed");
            check(inv->sections.size()==1&&inv->sections[0].rows.size()==1,"incomplete snapshot");
            check(inv->sections[0].rows[0].value.starts_with(inv->readAt),"mixed snapshot fields");}
        return 0;
    }
    if(mode=="inventory-oversize"){
        mc::HardwareInventory inv;inv.readAt="too-large";
        inv.addSection("fixture").addRow("value",std::string(2*1024*1024,'x'));std::string error;
        check(!mc::InventoryCache::save(inv,error)&&!error.empty(),"unreadable oversized cache was published");return 0;
    }
    if(mode=="atomic-write"){
        for(int i=0;i<60;++i){std::string error;const auto text="{\"owner\":\""+label+"\",\"data\":\""+std::string(32000,label.front())+"\"}";
            check(mc::detail::writeStorageTextAtomically(fixtureRoot/"atomic.json",text,error),error);}
        return 0;
    }
    if(mode=="atomic-failure"){
        const auto target=fixtureRoot/"blocked";fs::create_directories(target);std::string error;
        check(!mc::detail::writeStorageTextAtomically(target,"must fail",error)&&!error.empty(),"replace-directory must fail");
        check(fs::is_directory(target),"failed publication changed target");return 0;
    }
    throw std::invalid_argument("unknown fixture mode");
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
#if defined(_WIN32)
int wmain(int argc,wchar_t** argv){return fixture(argc,argc>1?fs::path(argv[1]).string():"",argc>2?fs::path(argv[2]):fs::path{},argc>3?fs::path(argv[3]).string():"");}
#else
int main(int argc,char** argv){return fixture(argc,argc>1?argv[1]:"",argc>2?fs::path(argv[2]):fs::path{},argc>3?argv[3]:"");}
#endif
