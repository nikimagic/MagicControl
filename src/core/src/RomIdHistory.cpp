#include "MagicControl/RomIdHistory.h"
#include "MagicControl/Logging.h"
#include "MagicControl/BoardIdentity.h"
#include "MagicControl/DeviceProfile.h"
#include "MagicControl/Version.h"
#include "MagicControl/ActionLog.h"
#include <atomic>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <locale>
#include <mutex>
#include <sstream>

namespace mc {
namespace {
struct History {
    std::mutex gate;
    bool active{};
    struct Field {const char* filename;const char* address;bool readFailed{};std::optional<std::uint8_t> last,current,pending;};
    std::array<Field,3> fields{{{"platform_id","0x7C3"},{"rom_id","0x770"},{"project_id","0x740"}}};
    std::filesystem::path directory;
    std::string profile,board,session,lastError;
    IdentityRomBytes rom;std::string romReadAt,projectReadAt;std::uint64_t sequence{};
};
History& history(){static History value;return value;}
thread_local IdentityHistoryScope* context{};
std::atomic<std::uint64_t> nextContext{1};
std::string jsonQuote(std::string_view s){
    constexpr char hex[]="0123456789ABCDEF";std::string out="\"";
    for(unsigned char c:s){if(c=='"'||c=='\\'){out+='\\';out+=static_cast<char>(c);}
        else if(c<32){out+="\\u00";out+=hex[c>>4];out+=hex[c&15];}else out+=static_cast<char>(c);}
    return out+'"';
}
std::string value(std::optional<std::uint8_t> v){
    if(!v)return "null";
    std::ostringstream out;out<<"{\"hex\":\"0x"<<std::uppercase<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(*v)
        <<"\",\"decimal\":"<<std::dec<<unsigned(*v)<<'}';return out.str();
}
std::pair<std::string,std::string> times(){
    const auto now=std::chrono::system_clock::now();const auto raw=std::chrono::system_clock::to_time_t(now);
    std::tm utc{},local{};
#ifdef _WIN32
    gmtime_s(&utc,&raw);localtime_s(&local,&raw);
#else
    gmtime_r(&raw,&utc);localtime_r(&raw,&local);
#endif
    std::ostringstream a,b;a.imbue(std::locale::classic());b.imbue(std::locale::classic());
    a<<std::put_time(&utc,"%Y-%m-%dT%H:%M:%S")<<'.'<<std::setfill('0')<<std::setw(3)
        <<std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count()%1000<<'Z';
    b<<std::put_time(&local,"%d.%m.%Y %H:%M:%S %z");return {a.str(),b.str()};
}
bool append(History& h,std::string_view event,std::optional<std::uint8_t> before,
    std::optional<std::uint8_t> requested,std::optional<std::uint8_t> observed,
    std::string_view result,std::string_view source,std::string_view detail,unsigned index=1){
    auto& field=h.fields[index];
    if(observed)field.current=observed;
    const auto [utc,local]=times();
    std::string region;bool complete=true;for(const auto& b:h.rom){if(!region.empty())region+=' ';if(b){std::ostringstream out;out<<std::uppercase<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(*b);region+=out.str();}else{region+="??";complete=false;}}
    const auto row="{\"schema\":2,\"utc\":"+jsonQuote(utc)+",\"local_time\":"+jsonQuote(local)+
        ",\"app_version\":"+jsonQuote(MAGICCONTROL_VERSION_STRING)+",\"session\":"+jsonQuote(h.session)+
        ",\"profile\":"+jsonQuote(h.profile)+",\"board\":"+jsonQuote(h.board)+",\"event\":"+jsonQuote(event)+
        ",\"sequence\":"+std::to_string(++h.sequence)+",\"action_id\":"+std::to_string(audit::capture())+
        ",\"origin\":"+jsonQuote(context?context->actor():"observation")+",\"reason\":"+jsonQuote(context?context->reason():"")+
        ",\"history_context\":"+std::to_string(context?context->id():0)+
        ",\"address\":"+jsonQuote(field.address)+",\"before\":"+value(before)+",\"requested\":"+value(requested)+
        ",\"observed\":"+value(observed)+",\"result\":"+jsonQuote(result)+",\"source\":"+jsonQuote(source)+
        ",\"platform_last_observed\":"+value(h.fields[0].current)+",\"rom_id_last_observed\":"+value(h.fields[1].current)+
        ",\"project_last_observed\":"+value(h.fields[2].current)+",\"project_read_at\":"+jsonQuote(h.projectReadAt)+
        ",\"rom_region_last_read\":"+jsonQuote(region)+",\"rom_region_read_at\":"+jsonQuote(h.romReadAt)+
        ",\"rom_region_complete\":"+(complete?"true":"false")+",\"pair_is_atomic\":false"+
        ",\"detail\":"+jsonQuote(detail)+"}\n";
    std::string error;
    if(mc::detail::appendHistoryRow(h.directory/field.filename,row,error)){h.lastError.clear();return true;}
    if(error!=h.lastError){h.lastError=error;logMessage("identity_history","History append failed: "+error);}
    return false;
}
void record(unsigned index,std::string_view event,std::optional<std::uint8_t> before,std::optional<std::uint8_t> requested,
    std::optional<std::uint8_t> observed,std::string_view result,std::string_view source,std::string_view detail){
    auto& h=history();std::lock_guard lock(h.gate);if(!h.active)return;auto& f=h.fields[index];
    if(event=="write_requested"){
        if(!before)before=f.current;
        if(context)context->mark(index==0?requested:std::nullopt,index==1?requested:std::nullopt);
    }
    // Guard reads between transaction intent and backend submission still see
    // the old value. Only a returned transport write arms readback tracking.
    if(event=="write_returned")f.pending=requested;
    if(event=="write_failed"||event=="rollback_failed"){f.current=observed;if(context)context->fail(detail);}
    if(append(h,event,before,requested,observed,result,source,detail,index)){
        if(observed){f.last=observed;f.readFailed=false;}
        else if(event=="startup_read"||event=="observation_failed"||event=="write_failed"||event=="rollback_failed")f.readFailed=true;
        if(result=="read_confirmed"||event=="write_failed"||event=="rollback_failed")f.pending.reset();
    }
}
void observe(unsigned index,std::uint8_t observed,std::string_view source){
    auto& h=history();std::lock_guard lock(h.gate);if(!h.active)return;auto& f=h.fields[index];f.current=observed;
    if(index==2)h.projectReadAt=times().first;
    if(f.last==observed&&!f.readFailed&&!f.pending)return;
    const auto event=f.pending?(*f.pending==observed?"write_readback":"write_readback_mismatch"):f.readFailed?"read_recovered":f.last?"observed_change":"first_observation";
    if(append(h,event,f.last,f.pending,observed,"read_confirmed",source,"Observed RAM; external writer is not identified",index)){
        f.last=observed;f.readFailed=false;f.pending.reset();
    }
}
}
bool uniwillIdentityHistoryProfile(const DeviceProfile& p) noexcept {
    return (p.id=="wu-jie-15-xpro"&&p.protocolId=="uniwill-x5sp4nag")||
        (p.id=="mechrevo-gm6ir0c"&&p.protocolId=="uniwill-gm6ir0c")||
        (p.id=="mechrevo-gm5hg0a"&&p.protocolId=="uniwill-gm5hg0a")||
        (p.id=="machenike-l16"&&p.protocolId=="uniwill-l16")||
        (p.id=="eluktronics-hydroc16g2"&&p.protocolId=="uniwill-hydroc16g2-diagnostics");
}
void initializeRomIdHistory() noexcept {
    try{
        auto& h=history();std::lock_guard lock(h.gate);if(h.active)return;
        const auto board=readBoardDescriptor();const auto& profile=deviceProfile(board);
        if(!uniwillIdentityHistoryProfile(profile))return;
        h.profile=profile.id;h.board=board.baseBoard;
        h.directory=defaultLogDirectory();
        h.session=times().first+"/"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        h.active=true;
        for(unsigned index=0;index<h.fields.size();++index)append(h,"startup_begin",{}, {}, {},"pending","application_start","Read-only observation before preference restoration",index);
    }catch(const std::exception& e){logMessage("rom_id",std::string("History initialization failed: ")+e.what());}catch(...){}
}
void recordRomIdEvent(std::string_view event,std::optional<std::uint8_t> before,
    std::optional<std::uint8_t> requested,std::optional<std::uint8_t> observed,
    std::string_view result,std::string_view source,std::string_view detail) noexcept {
    try{
        record(1,event,before,requested,observed,result,source,detail);
    }catch(const std::exception& e){logMessage("rom_id",std::string("History event failed: ")+e.what());}catch(...){}
}
void observeRomId(std::uint8_t observed,std::string_view source) noexcept {
    try{
        observe(1,observed,source);
    }catch(const std::exception& e){logMessage("rom_id",std::string("History observation failed: ")+e.what());}catch(...){}
}
void recordPlatformIdEvent(std::string_view event,std::optional<std::uint8_t> before,std::optional<std::uint8_t> requested,
    std::optional<std::uint8_t> observed,std::string_view result,std::string_view source,std::string_view detail)noexcept{
    try{record(0,event,before,requested,observed,result,source,detail);}catch(...){}
}
void recordIdentityByteEvent(std::uint16_t address,std::string_view event,std::optional<std::uint8_t> before,std::optional<std::uint8_t> requested,
    std::optional<std::uint8_t> observed,std::string_view result,std::string_view source,std::string_view detail)noexcept{
    if(address==0x770)recordRomIdEvent(event,before,requested,observed,result,source,detail);
    else if(address==0x7C3)recordPlatformIdEvent(event,before,requested,observed,result,source,detail);
}
void observePlatformId(std::uint8_t value,std::string_view source)noexcept{try{observe(0,value,source);}catch(...) {}}
void observeProjectId(std::uint8_t value,std::string_view source)noexcept{try{observe(2,value,source);}catch(...) {}}
void recordIdentitySnapshot(std::optional<std::uint8_t> platform,const IdentityRomBytes& rom,std::string_view event,std::string_view source,std::string_view result,std::string_view detail,std::optional<std::uint8_t> project)noexcept{
    try{
        auto& h=history();std::lock_guard lock(h.gate);if(!h.active)return;
        h.rom=rom;h.romReadAt=times().first;h.fields[0].current=platform;h.fields[1].current=rom[0];
        h.fields[2].current=project;h.projectReadAt=h.romReadAt;
        for(unsigned index=0;index<h.fields.size();++index){auto& f=h.fields[index];const auto observed=index==2?project:index==1?rom[0]:platform;
            if(append(h,event,f.last,{},observed,result,source,detail,index)){
                if(observed){f.last=observed;f.readFailed=false;}else f.readFailed=true;
            }
        }
    }catch(...){}
}
void recordIdentityPolicyRequest(std::uint8_t platform,std::uint8_t rom,std::string_view source)noexcept{
    try{
        if(context)context->mark(platform,rom);
        recordPlatformIdEvent("policy_requested",{},platform,{},"pending",source);
        recordRomIdEvent("policy_requested",{},rom,{},"pending",source);
    }catch(...){}
}
void recordIdentityPairSnapshot(std::optional<std::uint8_t> platform,std::optional<std::uint8_t> rom,std::string_view event,std::string_view source)noexcept{
    try{
        auto& h=history();std::lock_guard lock(h.gate);if(!h.active)return;
        h.fields[0].current=platform;h.fields[1].current=rom;
        // Preserve the full-region snapshot and its original acquisition time.
        // This event freshly reads only the pair; tail bytes are not re-read.
        for(unsigned i=0;i<2;++i){auto& f=h.fields[i];const auto v=i?rom:platform;
            if(append(h,event,f.last,{},v,v?"read_confirmed":"partial",source,"Fresh pair; full ROM region retained with its original read timestamp",i)){
                f.last=v;f.readFailed=!v;f.pending.reset();
            }
        }
    }catch(...){}
}
IdentityHistoryScope::IdentityHistoryScope(std::string_view actor,std::string_view reason,bool onlyIfUnset)noexcept{
    if(onlyIfUnset&&context)return;
    try{actor_=actor;reason_=reason;previous_=context;id_=nextContext.fetch_add(1);exceptions_=std::uncaught_exceptions();context=this;active_=true;}catch(...){}
}
void IdentityHistoryScope::mark(std::optional<std::uint8_t> platform,std::optional<std::uint8_t> rom)noexcept{
    involved_=true;if(platform&&!platform_)platform_=platform;if(rom&&!rom_)rom_=rom;
}
void IdentityHistoryScope::fail(std::string_view reason)noexcept{failed_=true;try{failure_=reason;}catch(...) {}}
IdentityHistoryScope::~IdentityHistoryScope(){
    if(!active_)return;
    if(involved_){
        const auto result=failed_||std::uncaught_exceptions()>exceptions_?"failed":"returned";
        // A returned action is not a manufactured readback confirmation.
        recordPlatformIdEvent("action_result",{},platform_,{},result,"history_context",failure_);
        recordRomIdEvent("action_result",{},rom_,{},result,"history_context",failure_);
    }
    context=previous_;
}
}
