#pragma once
#include <windows.h>
#include "MagicControl/RomIdHistory.h"
#include "MagicControl/BatteryEcPolicy.h"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace mc::windows::batteryEnforcement {
// Read-only SMBIOS type1 UUID; empty means no trustworthy machine binding.
// Shared by every admitted Uniwill battery policy, never the preview profile.
std::string machineIdentity();
// Path lookup only: no migration, directory creation or EC access.
std::filesystem::path stateDirectory();
// Owner-authorized policy22.10.2026: off=platform4/original ROM FF;
// on=platform7/RAM ROM04. Never calls Set_ROMID or writes77E/77F/NVM.
//24.10.2026: third explicit choice7/original FF on the three admitted profiles.
// Restore FF only from FF or application-owned04, then readback before removing
// ownership. No temporary4 in the7/04->7/FF transition; no universal factory claim.
// Caller supplies exact-board/owner guards and an EC transaction for each
// apply/rollback. No fan, voltage or threshold field belongs to this mechanism.
using Read=std::function<std::uint8_t(std::uint16_t)>;
using Write=std::function<void(std::uint16_t,std::uint8_t,std::uint8_t)>;
std::filesystem::path ownerPath(const std::filesystem::path& settings,const std::string& id);
inline bool validOwner(const std::filesystem::path& path,const std::string& id){
    std::ifstream in(path);std::string schema,board,machine,extra;unsigned original{},ours{};
    const auto current=machineIdentity();
    return !current.empty()&&(in>>schema>>board>>original>>ours>>machine)&&!(in>>extra)&&
        schema=="v2"&&board==id&&original==255&&ours==4&&machine==current;
}
// Match ownerPath's machine-bound lookup without migrating or writing files.
inline bool hasValidOwnerReadOnly(const std::filesystem::path& settings,const std::string& id){
    const auto primary=stateDirectory()/(id+"-battery-romid-owner.txt");
    if(std::filesystem::exists(primary))return validOwner(primary,id);
    const auto portable=settings/(id+"-battery-romid-owner.txt");
    if(validOwner(portable,id))return true;
    return settings.filename()=="settings"&&validOwner(settings.parent_path()/(id+"-battery-romid-owner.txt"),id);
}
inline bool legacyOwner(const std::filesystem::path& path,const std::string& id){
    std::ifstream in(path);std::string board,extra;unsigned original{},ours{};
    return (in>>board>>original>>ours)&&!(in>>extra)&&board==id&&original==255&&ours==4;
}
inline void requireContext(const Read& read,const std::filesystem::path& path,const std::string& id){
    const auto platform=read(0x7c3),rom=read(0x770);
    if((platform!=4&&platform!=7)||(rom!=255&&rom!=4))throw std::runtime_error("Unknown battery platform/ROM-ID; no policy write");
    if(machineIdentity().empty())throw std::runtime_error("Battery ROM-ID ownership: SMBIOS machine UUID unavailable; no policy write");
    if(std::filesystem::exists(path)&&!validOwner(path,id)&&!(rom==255&&legacyOwner(path,id)))
        throw std::runtime_error("Battery ROM-ID ownership is unbound or belongs to another laptop; no policy write");
    if(rom==4&&!validOwner(path,id))throw std::runtime_error("ROM-ID04 has no application ownership record; no overwrite");
}
class Change {
    Read read;Write write;std::filesystem::path path;std::string id;BatteryEcPolicy policy;bool created{},committed{};
    std::uint8_t originalPlatform{},originalRom{},expectedPlatform{},expectedRom{};
    struct Field{std::uint16_t address;std::uint8_t mask,before,ours;};std::vector<Field> fields;
    void snapshot(std::string_view event) noexcept {
        std::optional<std::uint8_t> platform,rom;
        try{platform=read(0x7c3);rom=read(0x770);}catch(...){}
        recordIdentityPairSnapshot(platform,rom,event,id+"/battery_policy");
    }
    struct SnapshotAfter {
        Change& change;const char* success;const char* failure;int exceptions{std::uncaught_exceptions()};
        ~SnapshotAfter(){change.snapshot(std::uncaught_exceptions()>exceptions?failure:success);}
    };
    void writeConfirmed(std::uint16_t address,std::uint8_t mask,std::uint8_t before,std::uint8_t requested,bool rollback){
        const auto source=id+(rollback?"/battery_policy_rollback":"/battery_policy_apply");
        recordIdentityByteEvent(address,rollback?"rollback_requested":"write_requested",before,requested,{},"pending",source);
        std::optional<std::uint8_t> observed;
        try{
            write(address,mask,requested);
            observed=read(address); // The existing readback, no extra EC reads.
            if(*observed!=requested)throw std::runtime_error(rollback?"Battery policy rollback readback mismatch":"EC did not confirm battery platform/ROM-ID");
        }catch(const std::exception& e){
            recordIdentityByteEvent(address,rollback?"rollback_failed":"write_failed",before,requested,observed,
                observed?"readback_mismatch":"unconfirmed",source,e.what());
            throw;
        }catch(...){
            recordIdentityByteEvent(address,rollback?"rollback_failed":"write_failed",before,requested,observed,"unconfirmed",source,"Unknown write/readback error");
            throw;
        }
        recordIdentityByteEvent(address,rollback?"rollback_confirmed":"write_confirmed",before,requested,observed,"read_confirmed",source,
            "RAM readback only; not a flash write or battery enforcement proof");
    }
    void set(std::uint16_t address,std::uint8_t value){
        if(read(0x7c3)!=expectedPlatform||read(0x770)!=expectedRom)throw std::runtime_error("Battery policy changed externally before write");
        const auto before=read(address);if(before==value)return;
        if(address==0x770&&(before!=255&&before!=4))throw std::runtime_error("Foreign ROM-ID before policy write");
        if(address==0x7c3&&(before!=4&&before!=7))throw std::runtime_error("Foreign platform before policy write");
        const auto mask=static_cast<std::uint8_t>(address==0x770?0xfb:0xff);
        fields.push_back({address,mask,before,value}); // Attempt is owned even if transport throws.
        if(address==0x770)expectedRom=value;else expectedPlatform=value;
        writeConfirmed(address,mask,before,value,false);
    }
public:
    Change(Read r,Write w,std::filesystem::path p,std::string board,BatteryEcPolicy choice,std::optional<unsigned> requiredPlatform={}):read(std::move(r)),write(std::move(w)),path(std::move(p)),id(std::move(board)),policy(choice){
        if(!batteryEcPolicy(static_cast<int>(policy)))throw std::invalid_argument("Unknown EC battery policy");
        recordIdentityPolicyRequest(batteryPolicyPlatform(policy),batteryPolicyRom(policy),id+"/battery_policy");
        if(policy==BatteryEcPolicy::Platform7Original&&!nativeEcPolicyProfile(id))throw std::runtime_error("7/FF policy is not admitted for this model");
        requireContext(read,path,id);
        originalPlatform=expectedPlatform=read(0x7c3);originalRom=expectedRom=read(0x770);
        if(id=="wu-jie-15-xpro"&&originalRom==4&&originalPlatform!=7)
            throw std::runtime_error("Wujie ROM04 release requires platform 7; no settings written");
        if(requiredPlatform&&originalPlatform!=*requiredPlatform)throw std::runtime_error("Battery platform changed before policy admission");
    }
    Change(Read r,Write w,std::filesystem::path p,std::string board,bool on):Change(std::move(r),std::move(w),std::move(p),std::move(board),on?BatteryEcPolicy::Platform7Percent:BatteryEcPolicy::Platform4Original){}
    void apply(){
        requireContext(read,path,id);
        snapshot("policy_apply_before");SnapshotAfter after{*this,"policy_apply_after","policy_apply_failed"};
        if(policy==BatteryEcPolicy::Platform7Percent&&!validOwner(path,id)){
            std::filesystem::create_directories(path.parent_path());
            auto temporary=path;temporary+=L".tmp";
            {std::ofstream out(temporary,std::ios::trunc);out<<"v2 "<<id<<" 255 4 "<<machineIdentity()<<"\n";out.flush();if(!out)throw std::runtime_error("Cannot persist original battery ROM-ID");}
            if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
                throw std::runtime_error("Cannot commit battery ROM-ID ownership record");
            created=true;
        }
        // Preserve the old two transitions. The explicit7/FF choice intentionally
        // closes the percentage admission route without touching stored thresholds.
        if(policy==BatteryEcPolicy::Platform7Percent){set(0x770,4);set(0x7c3,7);}
        else if(policy==BatteryEcPolicy::Platform4Original){set(0x7c3,4);set(0x770,255);}
        else{set(0x770,255);set(0x7c3,7);} // Direct7/04->7/FF; no temporary platform4.
    }
    void rollback(){
        if(committed)return;
        snapshot("policy_rollback_before");SnapshotAfter after{*this,"policy_rollback_after","policy_rollback_failed"};
        for(auto it=fields.rbegin();it!=fields.rend();++it){
            const auto owned=[&](std::uint16_t a,std::uint8_t initial){const auto now=read(a);if(now==initial)return true;
                for(const auto& field:fields)if(field.address==a&&now==field.ours)return true;return false;};
            if(!owned(0x7c3,originalPlatform)||!owned(0x770,originalRom))throw std::runtime_error("Foreign battery policy context; rollback refused");
            const auto now=read(it->address);
            if(now==it->before)continue;
            if(now!=it->ours)throw std::runtime_error("Foreign battery policy value; rollback refused");
            writeConfirmed(it->address,it->mask,now,it->before,true);
        }
        fields.clear();
        if(created){if(!validOwner(path,id))throw std::runtime_error("ROM-ID record changed externally; retained");std::filesystem::remove(path);created=false;}
    }
    void commit(){
        if(read(0x7c3)!=batteryPolicyPlatform(policy)||read(0x770)!=batteryPolicyRom(policy))throw std::runtime_error("Battery policy changed before commit");
        if(policy!=BatteryEcPolicy::Platform7Percent&&std::filesystem::exists(path)){
            if(!validOwner(path,id)&&!(originalRom==255&&legacyOwner(path,id)))throw std::runtime_error("ROM-ID record changed externally; retained");
            std::filesystem::remove(path);
        }
        committed=true;
        snapshot("policy_committed");
    }
};
}
