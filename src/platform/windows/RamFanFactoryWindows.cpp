#include "RamFanFactoryWindows.h"
#include "BackendWindows.h"
#include "UniwillControlOwnerWindows.h"
#include "../../devices/eluktronics/hydroc-16-g2/DiagnosticsWindows.h"
#include "MagicControl/BoardIdentity.h"
#include "MagicControl/DeviceProfile.h"
#include "MagicControl/Logging.h"
#include <windows.h>
#include <array>
#include <format>
#include <stdexcept>

namespace mc::windows {
namespace {
struct Transaction{IEcBackend& ec;explicit Transaction(IEcBackend& b):ec(b){ec.lockTransaction();}~Transaction(){ec.unlockTransaction();}};
std::uint8_t stable(IEcBackend& ec,std::uint16_t a){
    auto old=ec.read(a);for(int i=0;i<3;++i){const auto v=ec.read(a);if(v==old)return v;old=v;}
    throw std::runtime_error("HYDROC diagnostic: unstable EC field");
}
void guard(IEcBackend& ec){
    if(wcsstr(GetCommandLineW(),L"--no-ec"))throw std::runtime_error("HYDROC diagnostic disabled by --no-ec");
    std::string proof;verifyHydrocDriver(proof); // Exact SMBIOS + installed pinned SYS.
    if(stable(ec,0x740)!=26||!(stable(ec,0x78E)&0x40))throw std::runtime_error("HYDROC diagnostic: ProjectID/RAM1.5 mismatch");
    const auto owner=uniwillControlOwner();if(!owner.empty())throw std::runtime_error("HYDROC diagnostic owned by OEM: "+owner);
    if((stable(ec,0x7C8)&0x20)&&(stable(ec,0x7C7)&2))throw std::runtime_error("HYDROC diagnostic refused: native LC connected");
}
std::unique_ptr<IEcBackend> open(){
    if(wcsstr(GetCommandLineW(),L"--no-ec"))throw std::runtime_error("HYDROC diagnostic disabled by --no-ec");
    std::string proof;verifyHydrocDriver(proof);logMessage("factory_tables",proof);
    return createWindowsAcpiFactoryBackend();
}
}
HydrocDiagnosticDefaultResult prepareHydrocDiagnosticDefault(bool explicitWithFanProbes,std::stop_token token){
    HydrocDiagnosticDefaultResult r;
    try{
        if(!explicitWithFanProbes){r.status="HYDROC Default skipped: explicit WithFanProbes required";return r;}
        if(token.stop_requested()){r.status="HYDROC Default cancelled before access";return r;}
        auto ec=open();Transaction transaction(*ec);guard(*ec);
        if(token.stop_requested()){r.status="HYDROC Default cancelled before any write";return r;}
        // Release commit first. Never clear/replay payload or alter LC, APP,
        // respective, CPU PL or the whole-system751 mode during this baseline.
        constexpr std::array fields{std::pair{std::uint16_t{0x7C6},4u},std::pair{std::uint16_t{0x726},128u},
            std::pair{std::uint16_t{0x727},64u},std::pair{std::uint16_t{0x751},64u}};
        r.attempted=true;
        for(const auto [address,mask]:fields){guard(*ec);const auto before=stable(*ec,address);
            if(before&mask)ec->write(address,static_cast<std::uint8_t>(before&~mask));
            if(stable(*ec,address)&mask)throw std::runtime_error("HYDROC diagnostic Default readback failed");
        }
        for(const auto [address,mask]:fields)if(stable(*ec,address)&mask)throw std::runtime_error("HYDROC diagnostic Default changed during confirmation");
        r.confirmed=true;r.status="HYDROC Default: commit/custom/indicator/boost masks released; RAM payload, mode, PL and APP unchanged";
    }catch(const std::exception& e){r.status=e.what();}
    logMessage("factory_tables",std::format("HYDROC baseline attempted={} confirmed={} status={}",r.attempted,r.confirmed,r.status));return r;
}
uniwill::RamFanFactoryResult exportHydrocFactoryTables(bool explicitWithFanProbes,std::stop_token token){
    if(!explicitWithFanProbes)return uniwill::exportRamFanFactory({},26,false,token);
    if(token.stop_requested())return uniwill::refusedRamFanFactory("HYDROC factory cancelled before access",true);
    try{
        auto ec=open();Transaction transaction(*ec);guard(*ec);
        uniwill::RamFanFactoryIo io{
            [&](auto address){return ec->read(address);},
            [&](auto address,auto value){ec->write(address,value);},
            [&]{guard(*ec);}};
        auto result=uniwill::exportRamFanFactory(io,26,true,token);
        logMessage("factory_tables",std::format("HYDROC attempted={} complete={} restored={} status={}",result.attempted,result.complete,result.restored,result.status));return result;
    }catch(const std::exception& e){
        // Let the common serializer represent refused admission without I/O or
        // a fabricated valid/all-zero payload, also for direct typed callers.
        return uniwill::refusedRamFanFactory(e.what(),token.stop_requested());
    }
}
}
