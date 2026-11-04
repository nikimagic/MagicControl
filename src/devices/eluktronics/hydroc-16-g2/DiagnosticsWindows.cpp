#include "DiagnosticsWindows.h"
#include "MagicControl/Monitoring.h"
#include "MagicControl/DeviceOperation.h"
#include "../../../protocols/uniwill/hydroc16g2/Battery.h"
#include <mutex>
#include "../../../protocols/uniwill/hydroc16g2/Diagnostics.h"
#include "MagicControl/BoardIdentity.h"
#include "MagicControl/DeviceProfile.h"
#include "BackendWindows.h"
#include "../../../platform/windows/uniwill-acpi/ReviewedDriverWindows.h"
#include "BinaryHashWindows.h"
#include <windows.h>
#include <setupapi.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <format>
#include <stdexcept>
namespace mc::windows {
namespace {
using reviewed_uniwill::registry;
using reviewed_uniwill::text;
void driverEvidence(std::string& notes){reviewed_uniwill::verifyReviewedUniwillDriver(notes,hydroc16g2::driverSha256);}
void identity(){const auto board=readBoardDescriptor();const auto& p=deviceProfile(board);
    if(p.id!=hydroc16g2::profileId||p.protocolId!=hydroc16g2::protocolId||board.manufacturer!="Eluktronics Inc."||board.baseBoard!="HYDROC-16"||board.productName!="HYDROC-16 G2")
        throw std::runtime_error("HYDROC: SMBIOS identity mismatch; EC diagnostic read denied");
}

}
void verifyHydrocDriver(std::string& notes){identity();driverEvidence(notes);}
void verifyHydrocControlIdentity(){
    identity();
    if(registry(HKEY_LOCAL_MACHINE,L"HARDWARE\\DESCRIPTION\\System\\BIOS",L"BIOSVersion")!=L"N.1.13ELU11")
        throw std::runtime_error("HYDROC: full control requires reviewed BIOS N.1.13ELU11");
}
HydrocDiagnosticSession::HydrocDiagnosticSession(std::string& notes){
    notes="profile=eluktronics-hydroc16g2\nmode=read-only\ntransport=UWACPIDriver, DWORD-address/padded16-byte-reply\nread_ioctl=0x9C40A488\nram_window=0x0000-0x0FFF\nwrite_ioctl_called=false\n";
    identity();notes+="bios_version="+text(registry(HKEY_LOCAL_MACHINE,L"HARDWARE\\DESCRIPTION\\System\\BIOS",L"BIOSVersion"))+"\n";
    driverEvidence(notes);backend_=createWindowsAcpiDiagnosticBackend();
    notes+="driver_hash_verified=true\nAll EC bytes are raw observations; FANs layout, battery temperature encoding and lighting type are not inferred.\n";
    // The reviewed SYS completes the WDF request even if firmware evaluation
    // failed internally. Catch an all-fill identity window before labelling an
    // all-zero bank as a valid dump; individual zero register values remain valid.
    bool allZero=true,allFF=true;
    backend_->lockTransaction();
    try{for(const auto address:{hydroc16g2::project,hydroc16g2::biosProject,hydroc16g2::ramFanCapability,hydroc16g2::lcFanCapability}){
        const auto a=backend_->read(address),b=backend_->read(address);
        notes+=std::format("probe_0x{:03X}=0x{:02X}/0x{:02X}\n",address,a,b);
        if(a!=b)throw std::runtime_error("HYDROC: capability probe unstable; retry dump");
        allZero&=a==0;allFF&=a==255;
    }}catch(...){backend_->unlockTransaction();throw;}
    backend_->unlockTransaction();
    if(allZero||allFF)throw std::runtime_error("HYDROC: identity/capability probes are all0/allFF; firmware response not established");
    notes+="capability_probe_stable=true\nvalid mask records successful transport reads; the OEM SYS does not expose every internal ACPI failure. Snapshot is not atomic.\n";
}
std::vector<std::uint8_t> HydrocDiagnosticSession::readBlock(std::uint16_t address,std::size_t count){
    identity();if(!count||address>hydroc16g2::lastRam||count>std::size_t(hydroc16g2::lastRam)+1-address)throw std::invalid_argument("HYDROC EC diagnostic range denied");
    backend_->lockTransaction();struct Unlock {IEcBackend& b;~Unlock(){b.unlockTransaction();}} unlock{*backend_};
    return backend_->readBlock(address,count);
}
void readHydrocBatteryTelemetry(BatteryReading& result,bool singlePhysicalBattery){
    if(!singlePhysicalBattery||result.present!=true||result.deviceId.empty()||
        result.deviceId=="Windows/PowerStatus/aggregate"||wcsstr(GetCommandLineW(),L"--no-ec")||
        deviceProfile(readBoardDescriptor()).id!=hydroc16g2::profileId||deviceOperations().passiveEcReadsPaused())return;
    static std::mutex gate;static std::unique_ptr<IEcBackend> reader;
    std::lock_guard lock(gate);
    try{
        identity();if(deviceOperations().passiveEcReadsPaused())return;
        if(!reader){std::string proof;verifyHydrocDriver(proof);reader=createWindowsAcpiDiagnosticBackend();}
        reader->lockTransaction();struct Unlock{IEcBackend& b;~Unlock(){b.unlockTransaction();}} unlock{*reader};
        const auto stable=[&](std::uint16_t address){
            const auto a=reader->read(address),b=reader->read(address);
            if(a!=b)throw std::runtime_error("HYDROC battery telemetry: unstable byte");return a;
        };
        const auto guard=[&]{
            if(deviceOperations().passiveEcReadsPaused()||stable(hydroc16g2::project)!=26||!(stable(hydroc16g2::ramFanCapability)&0x40))
                throw std::runtime_error("HYDROC battery telemetry: identity/capability/pause guard");
        };
        guard();std::optional<std::uint16_t> cycles;
        for(int i=0;i<3;++i){
            const auto a=reader->readBlock(hydroc16g2::batteryCycles,2),b=reader->readBlock(hydroc16g2::batteryCycles,2);
            if(a.size()==2&&a==b){const auto value=std::uint16_t(a[0]|(std::uint16_t(a[1])<<8));if(value!=0xFFFF)cycles=value;break;}
        }
        guard();
        result.powerStatus+="; HYDROC EC cycle supplement raw4A6="+(cycles?std::to_string(*cycles):"unavailable")+
            " Windows_original_cycles="+(result.cycles?std::to_string(*result.cycles):"unavailable")+" source=exact_OEM_BatteryInfo read_only=true health_source=Windows";
        // Health/charge power/port stay with their actual OS providers. The GM6
        // capacity/ISDB map has no independently recovered HYDROC AML proof.
        if(!result.cycles&&cycles){result.cycles=*cycles;result.source+=" · HYDROC EC · cycles";}
    }catch(const std::exception& e){reader.reset();result.powerStatus+=std::string("; HYDROC EC cycle supplement unavailable (OS values retained): ")+e.what();}
}

}
