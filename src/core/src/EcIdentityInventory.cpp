#include "MagicControl/EcIdentityInventory.h"
#include "MagicControl/ActionLog.h"
#include "MagicControl/Logging.h"
#include "MagicControl/RomIdHistory.h"
#include "MagicControl/BoardIdentity.h"
#include "MagicControl/DeviceProfile.h"
#include <algorithm>
#include <ctime>
#include <format>
#include <sstream>
namespace mc {
namespace {
bool identityRow(const InventoryRow& row){return row.name==kEcProjectCaption||row.name==kEcPlatformCaption||row.name==kEcRomCaption;}
bool identitySectionApplicable(const InventorySection& section){
    BoardDescriptor board;
    for(const auto& row:section.rows){
        if(row.name=="Производитель")board.manufacturer=row.value;
        else if(row.name=="Материнская плата"||row.name=="Системная плата")board.baseBoard=row.value;
        else if(row.name=="Модель из прошивки"||row.name=="Модель")board.productName=row.value;
    }
    return uniwillIdentityHistoryProfile(deviceProfile(board));
}
std::string hex(std::optional<std::uint8_t> value){return value?std::format("{:02X}",*value):"??";}
std::string rom(const EcIdentityInventory& s){std::string out;for(auto value:s.rom){if(!out.empty())out+=' ';out+=hex(value);}return out;}
std::optional<std::uint8_t> byte(std::string_view text){
    if(text.size()!=2)return {};
    const auto digit=[](char c)->int{if(c>='0'&&c<='9')return c-'0';if(c>='A'&&c<='F')return c-'A'+10;return -1;};
    const auto a=digit(text[0]),b=digit(text[1]);if(a<0||b<0)return {};return static_cast<std::uint8_t>(a*16+b);
}
std::optional<EcIdentityInventory> cached(const HardwareInventory& inventory){
    for(const auto& section:inventory.sections)if(section.title==kEcIdentityInventoryMetadata){
        EcIdentityInventory s;std::string version,raw;
        for(const auto& row:section.rows){
            if(row.name=="schema")version=row.value;else if(row.name=="scope")s.scope=row.value;
            else if(row.name=="read_at")s.readAt=row.value;else if(row.name=="platform")s.platform=byte(row.value);
            else if(row.name=="project")s.project=byte(row.value);
            else if(row.name=="rom")raw=row.value;else if(row.name=="source")s.source=row.value;
        }
        if((version!="1"&&version!="2")||s.scope.empty()||s.scope.size()>512||s.readAt.empty()||s.readAt.size()>64)return {};
        std::istringstream input(raw);std::string value;
        for(auto& slot:s.rom){if(!(input>>value))return {};slot=byte(value);}
        if(input>>value)return {};
        // Old caches did not collect740. Retain their real platform/ROM bytes,
        // while leaving project unknown; never fill it from a device profile.
        if(s.platform&&std::all_of(s.rom.begin(),s.rom.end(),[](auto v){return v.has_value();}))return s;return {};
    }return {};
}
std::string status(const EcIdentityInventory& s){
    if(s.attemptAt.empty())return {};
    if(s.retained)return " · сохранено "+s.readAt+"; чтение "+s.attemptAt+" недоступно";
    if(!s.complete())return " · неполный снимок "+s.attemptAt;
    return {};
}
void insertRows(std::vector<InventoryRow>& rows,const EcIdentityInventory& current){
    std::erase_if(rows,[](const auto& row){return row.name==kEcProjectCaption||row.name==kEcPlatformCaption||row.name==kEcRomCaption;});
    auto where=std::find_if(rows.begin(),rows.end(),[](const auto& row){return row.name=="Версия SMBIOS";});
    if(where==rows.end()){rows.push_back({"Версия SMBIOS",kInventoryUnknown});where=std::prev(rows.end());}
    const auto at=std::size_t(where-rows.begin())+1;
    const auto platform=current.platform?"0x"+hex(current.platform)+" ("+std::to_string(*current.platform)+")":"??";
    const auto project=current.project?"0x"+hex(current.project)+" ("+std::to_string(*current.project)+")":"??";
    rows.insert(rows.begin()+at,{{kEcProjectCaption,project+status(current)},{kEcPlatformCaption,platform+status(current)},{kEcRomCaption,rom(current)+status(current)}});
}
}
void ensureInventoryEcIdentityRows(InventorySection& section){
    // Pure projection of collected SMBIOS facts, including old caches.
    // No GUI-time OS/EC probing and no fields for unrelated protocols.
    if(!identitySectionApplicable(section)){std::erase_if(section.rows,identityRow);return;}
    const auto has=[&](const char* name){return std::any_of(section.rows.begin(),section.rows.end(),[&](const auto& row){return row.name==name;});};
    if(!has(kEcPlatformCaption)||!has(kEcRomCaption))insertRows(section.rows,EcIdentityInventory{});
    else if(!has(kEcProjectCaption)){
        auto where=std::find_if(section.rows.begin(),section.rows.end(),[](const auto& row){return row.name==kEcPlatformCaption;});
        section.rows.insert(where,{kEcProjectCaption,"??"}); // Upgrade display only; preserve old successful bytes.
    }
}
bool EcIdentityInventory::complete()const noexcept{return project&&platform&&std::all_of(rom.begin(),rom.end(),[](auto v){return v.has_value();});}
std::string ecIdentityTimestamp(){
    const auto now=std::time(nullptr);std::tm local{};
#ifdef _WIN32
    localtime_s(&local,&now);
#else
    localtime_r(&now,&local);
#endif
    char text[32]{};std::strftime(text,sizeof(text),"%d.%m.%Y %H:%M:%S",&local);return text;
}
std::string ecIdentitySmbiosScope(std::span<const std::uint8_t> table){
    for(std::size_t at=0;at+4<=table.size();){
        const auto type=table[at],length=table[at+1];if(length<4||length>table.size()-at)return {};
        if(type==1&&length>=25){
            const auto uuid=table.subspan(at+8,16);
            if(std::all_of(uuid.begin(),uuid.end(),[](auto b){return b==0;})||std::all_of(uuid.begin(),uuid.end(),[](auto b){return b==255;}))return {};
            std::string out="smbios:";for(auto b:uuid)out+=hex(b);return out;
        }
        if(type==127)return {};at+=length;
        while(at+1<table.size()&&(table[at]||table[at+1]))++at;
        if(at+1>=table.size())return {};at+=2;
    }return {};
}
void appendInventoryEcIdentity(HardwareInventory& inventory,std::stop_token token){
    if(token.stop_requested()||inventory.sections.empty())return;
    if(!uniwillIdentityHistoryProfile(deviceProfile(readBoardDescriptor()))){
        for(auto& section:inventory.sections)std::erase_if(section.rows,identityRow);
        std::erase_if(inventory.sections,[](const auto& s){return s.title==kEcIdentityInventoryMetadata;});
        return;
    }
    IdentityHistoryScope origin("user","inventory_collection",true);
    auto current=readPlatformInventoryEcIdentity(token);
    current.attemptAt=ecIdentityTimestamp();current.readAt=current.attemptAt;
    const auto fresh=current;
    recordIdentitySnapshot(fresh.platform,fresh.rom,"info_read",fresh.source,fresh.complete()?"read_confirmed":"partial",fresh.error,fresh.project);
    if(!current.complete()&&!current.scope.empty())if(const auto old=InventoryCache::load())if(auto prior=cached(*old);prior&&prior->scope==current.scope){
        prior->attemptAt=current.attemptAt;prior->error=current.error;prior->retained=true;current=std::move(*prior);
    }
    insertRows(inventory.sections.front().rows,current);
    // Existing version3 inventory JSON persists explicit typed metadata as a
    // diagnostic section. The shared INFO projection hides only this metadata.
    std::erase_if(inventory.sections,[](const auto& s){return s.title==kEcIdentityInventoryMetadata;});
    auto& meta=inventory.addSection(kEcIdentityInventoryMetadata);
    meta.rows={{"schema","2"},{"scope",current.scope},{"read_at",current.readAt},{"attempt_at",current.attemptAt},{"project",hex(current.project)},
        {"platform",hex(current.platform)},{"rom",rom(current)},{"source",current.source},{"error",current.error},
        {"retained",current.retained?"true":"false"}};
    recordLog("info_ec_identity","\"project\":"+(fresh.project?std::to_string(*fresh.project):"null")+",\"platform\":"+(fresh.platform?std::to_string(*fresh.platform):"null")+
        ",\"rom_770_77f\":"+audit::quote(rom(fresh))+",\"complete\":"+(fresh.complete()?"true":"false")+
        ",\"attempt_at\":"+audit::quote(current.attemptAt)+",\"display_snapshot_at\":"+audit::quote(current.readAt)+
        ",\"retained\":"+(current.retained?"true":"false")+",\"source\":"+audit::quote(fresh.source)+",\"error\":"+audit::quote(fresh.error));
}
}
