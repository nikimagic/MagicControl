#include "MagicControl/RomIdHistory.h"
#include "MagicControl/EcIdentityInventory.h"
#include "MagicControl/BoardIdentity.h"
#include "MagicControl/DeviceProfile.h"
#include "BackendWindows.h"
#include "uniwill-acpi/ReviewedDriverWindows.h"
#include "../../devices/eluktronics/hydroc-16-g2/DiagnosticsWindows.h"
#include <windows.h>
#include <stdexcept>
#include <vector>
#include <cstring>

namespace mc {
namespace {
bool inventoryIdentityProfile(const DeviceProfile& p){return uniwillIdentityHistoryProfile(p);}
template<class Action>void withReadOnlyIdentity(const DeviceProfile& profile,Action action){
    // Shared with the pre-restore startup observation. No controller, writer,
    // preference restore, driver installation or service start is constructed.
    std::string evidence;
    if(gm5Profile(profile.id))windows::reviewed_uniwill::verifyGm5Driver(evidence);
    if(l16Profile(profile.id))windows::reviewed_uniwill::verifyL16Driver(evidence);
    if(hydrocProfile(profile.id))windows::verifyHydrocDriver(evidence);
    auto backend=createWindowsAcpiDiagnosticBackend();
    backend->lockTransaction();struct Unlock{IEcBackend& b;~Unlock(){b.unlockTransaction();}} unlock{*backend};
    const auto stable=[&](std::uint16_t address){
        auto previous=backend->read(address);
        for(int i=0;i<4;++i){const auto current=backend->read(address);if(current==previous)return current;previous=current;}
        throw std::runtime_error("Unstable ROM-ID/board identity read");
    };
    if(profile.identity.empty())throw std::runtime_error("No reviewed EC identity probes");
    for(const auto& probe:profile.identity){
        //7C3 is observed state, not a prerequisite for this read-only operation.
        if(probe.address==0x7C3)continue;
        if((stable(probe.address)&probe.mask)!=(probe.expected&probe.mask))throw std::runtime_error("EC identity mismatch before ROM-ID read");
    }
    if(l16Profile(profile.id))windows::reviewed_uniwill::verifyL16EcIdentity(stable);
    if(gm5Profile(profile.id)&&!gmRamProject(profile.id,stable(0x740)))throw std::runtime_error("GM5 EC ProjectID is unavailable");
    action(stable);
}
std::string inventoryScope(const BoardDescriptor& board){
    constexpr DWORD rsmb=0x52534d42; // GetSystemFirmwareTable('RSMB'), OS cache.
    const auto size=GetSystemFirmwareTable(rsmb,0,nullptr,0);
    std::string scope;
    if(size>=8&&size<=4*1024*1024){
        std::vector<std::uint8_t> data(size);
        const auto count=GetSystemFirmwareTable(rsmb,0,data.data(),size);
        if(count>=8&&count<=size){std::uint32_t length{};std::memcpy(&length,data.data()+4,4);
            if(length<=count-8)scope=ecIdentitySmbiosScope(std::span(data).subspan(8,length));}
    }
    if(scope.empty()){
        wchar_t guid[128]{};DWORD bytes=sizeof(guid);
        if(RegGetValueW(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Microsoft\\Cryptography",L"MachineGuid",RRF_RT_REG_SZ|RRF_SUBKEY_WOW6464KEY,nullptr,guid,&bytes)==ERROR_SUCCESS){
            scope="windows-install:";for(const auto c:guid){if(!c)break;if(c<128)scope+=static_cast<char>(c);}
        }
    }
    if(scope.empty())return {};
    return board.manufacturer+"|"+board.baseBoard+"|"+board.productName+"|"+scope;
}
}
EcIdentityInventory readPlatformInventoryEcIdentity(std::stop_token token){
    EcIdentityInventory result;result.source="windows-acpi-driver-readonly";
    try{
        const auto board=readBoardDescriptor();const auto& profile=deviceProfile(board);result.scope=inventoryScope(board);
        if(!inventoryIdentityProfile(profile)){result.error="Для фактической модели нет подтверждённого EC RAM reader";return result;}
        if(token.stop_requested()){result.error="Чтение отменено";return result;}
        withReadOnlyIdentity(profile,[&](const auto& stable){
            const auto read=[&](std::uint16_t address,std::optional<std::uint8_t>& slot){
                if(token.stop_requested()){result.error="Чтение отменено";return;}
                try{slot=stable(address);}catch(const std::exception& e){if(result.error.empty())result.error=e.what();}
            };
            read(0x740,result.project);
            read(0x7C3,result.platform);
            for(unsigned i=0;i<result.rom.size();++i)read(static_cast<std::uint16_t>(0x770+i),result.rom[i]);
        });
    }catch(const std::exception& e){result.error=e.what();}
    catch(...){result.error="Неизвестная ошибка EC RAM reader";}
    return result;
}
void recordStartupRomId(bool ecEnabled) noexcept {
    try{
        const auto board=readBoardDescriptor();const auto& profile=deviceProfile(board);
        if(!inventoryIdentityProfile(profile))return;
        IdentityHistoryScope origin("startup","before_automatic_apply");
        initializeRomIdHistory();
        if(!ecEnabled){recordIdentitySnapshot({}, {},"startup_read","--no-ec","disabled","No EC access requested");return;}
        withReadOnlyIdentity(profile,[&](const auto& stable){
            recordIdentityReadSnapshot(stable,"startup_read","windows-acpi-driver-readonly/before_automatic_apply");
        });
    }catch(const std::exception& e){recordIdentitySnapshot({}, {},"startup_read","windows-acpi-driver-readonly","unavailable",e.what());}
    catch(...){recordIdentitySnapshot({}, {},"startup_read","windows","unavailable","Unknown read error");}
}
}
