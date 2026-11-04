#include "MagicControl/RomIdHistory.h"
#include "MagicControl/EcIdentityInventory.h"
#include "MagicControl/BoardIdentity.h"
#include "MagicControl/DeviceProfile.h"
#include <fstream>
namespace mc {
EcIdentityInventory readPlatformInventoryEcIdentity(std::stop_token){
    EcIdentityInventory result;result.source="linux-unavailable";
    result.error="Чтение EC RAM Linux-транспортом пока не реализовано";
    // OS metadata only. Never substitute ec_sys's256byte window for EC770..77F.
    std::ifstream input("/sys/firmware/dmi/tables/DMI",std::ios::binary);
    if(input){std::vector<std::uint8_t> table;char block[4096];
        while(input&&table.size()<4*1024*1024){input.read(block,sizeof(block));table.insert(table.end(),block,block+input.gcount());}
        if(!input.bad()&&input.eof())result.scope=ecIdentitySmbiosScope(table);}
    return result;
}
void recordStartupRomId(bool ecEnabled) noexcept {
    try{if(!uniwillIdentityHistoryProfile(deviceProfile(readBoardDescriptor())))return;}catch(...){return;}
    IdentityHistoryScope origin("startup","before_automatic_apply");
    initializeRomIdHistory();
    recordIdentitySnapshot({}, {},"startup_read","linux",ecEnabled?"unsupported":"disabled",
        ecEnabled?"No reviewed Linux transport for RAM ROM-ID 0x770; no EC access":"No EC access requested");
}
}
