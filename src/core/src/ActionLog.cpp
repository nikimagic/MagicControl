#include "MagicControl/ActionLog.h"
#include "MagicControl/Json.h"
#include "MagicControl/Logging.h"
#include <atomic>
#include <bit>
#include <exception>
#include <map>
#include <mutex>
namespace mc::audit {
namespace {
std::atomic<Id> next{1};thread_local Id active{},lastInput{};

// Only measurements and observation timestamps are sampled. Control values,
// status/error fields and availability changes remain immediate.
bool measurement(std::string_view key){
    return key=="load"||key=="watts"||key=="power_mw"||key=="celsius"||
        key=="cpu_celsius"||key=="battery_celsius"||key=="fan_rpm"||key=="fan_pwm"||
        key=="usage_percent"||key=="clock"||key=="clock_mhz"||key=="graphics_mhz"||key=="memory_mhz"||
        key=="sampled_at_ms"||key=="sampled_at_monotonic_ms"||key.ends_with("_sampled_at_monotonic_ms")||key=="elapsed_seconds";
}
void appendSignature(std::string& out,const json::Json& node,bool measured=false,bool nativeFans=false){
    if(std::holds_alternative<std::nullptr_t>(node.value)){out+='n';return;}
    if(const auto* v=std::get_if<bool>(&node.value)){out+=*v?'t':'f';return;}
    if(const auto* v=std::get_if<double>(&node.value)){
        out+='d';if(!measured)out+=std::to_string(std::bit_cast<std::uint64_t>(*v));out+=';';return;
    }
    if(const auto* v=std::get_if<std::string>(&node.value)){out+='s';out+=std::to_string(v->size());out+=':';out+=*v;return;}
    if(const auto* v=std::get_if<json::Json::Array>(&node.value)){
        out+='[';for(const auto& item:*v)appendSignature(out,item,measured,nativeFans);out+=']';return;
    }
    out+='{';for(const auto& [key,value]:std::get<json::Json::Object>(node.value)){
        out+=std::to_string(key.size());out+=':';out+=key;
        appendSignature(out,value,measurement(key)||(nativeFans&&(key=="temperature"||key=="request"||key=="actual")),nativeFans||key=="native_default_fans");
    }out+='}';
}
bool sampledSource(std::string_view source){
    return source=="FANs.presentation"||source=="SETs.presentation"||source=="GPU.page.presentation"||
        source=="typed_laptop_control"||source=="typed_laptop_telemetry"||source.starts_with("NVML.");
}
}
std::string quote(std::string_view value){
    constexpr char digits[]="0123456789abcdef";std::string out="\"";
    for(unsigned char c:value){if(c=='"'||c=='\\'){out+='\\';out+=static_cast<char>(c);}
        else if(c<32){out+="\\u00";out+=digits[c>>4];out+=digits[c&15];}else out+=static_cast<char>(c);}
    return out+'"';
}
std::string hex(std::span<const std::uint8_t> value){constexpr char digits[]="0123456789ABCDEF";std::string out;out.reserve(value.size()*2);for(auto b:value){out+=digits[b>>4];out+=digits[b&15];}return out;}
Id current()noexcept{return active;}
TaskContext::TaskContext()noexcept:previous_(active),input_(lastInput){active=0;lastInput=0;}
TaskContext::~TaskContext(){active=previous_;lastInput=input_;}
Id capture()noexcept{return active?active:lastInput;}
void clearInput(Id id)noexcept{if(lastInput==id)lastInput=0;}
void event(std::string_view name,std::string_view fields)noexcept{try{
    recordLog("action","\"event\":"+quote(name)+(fields.empty()?"":","+std::string(fields)));
}catch(...) {}}
Id input(std::string_view source,std::string_view name,std::string_view fields)noexcept{
    if(!loggingEnabled())return 0;
    const auto prior=active;const auto id=next.fetch_add(1);lastInput=id;active=id;
    try{recordLog("ui","\"event\":"+quote(name)+",\"source\":"+quote(source)+",\"parent_action_id\":"+(prior?std::to_string(prior):"null")+(fields.empty()?"":","+std::string(fields)));}catch(...){}
    active=prior;return id;
}
Scope::Scope(std::string_view command,std::string_view fields,Id parent,bool enabled)noexcept{
    if(!enabled||!loggingEnabled())return;try{command_=command;previous_=active;id_=parent?parent:capture()?capture():next.fetch_add(1);active=id_;enabled_=true;
        start_=std::chrono::steady_clock::now();exceptions_=std::uncaught_exceptions();event("command_requested","\"command\":"+quote(command_)+(fields.empty()?"":","+std::string(fields)));
    }catch(...){if(enabled_)active=previous_;enabled_=false;}
}
void related(Id id,std::string_view name,std::string_view fields)noexcept{const auto prior=active;active=id;event(name,fields);active=prior;}
void state(std::string_view source,std::string_view json,const void* observer)noexcept{if(!loggingEnabled())return;try{
    struct Previous{std::string full,signature,emittedFull;std::chrono::steady_clock::time_point emitted{};std::uint64_t observerId{};bool seen{};};
    static std::mutex gate;static std::map<std::pair<std::string,std::uintptr_t>,Previous> previous;
    static std::uint64_t nextObserverId=1;
    const bool sampled=sampledSource(source);std::string signature;
    if(sampled){try{appendSignature(signature,mc::json::JsonReader::parse(json));}catch(...){signature=json;}}
    else signature=json;
    const auto now=std::chrono::steady_clock::now();const bool inAction=current()!=0;
    bool changed{},emit{},periodic{};std::uint64_t observerId{};
    {std::lock_guard lock(gate);auto& old=previous[{std::string(source),reinterpret_cast<std::uintptr_t>(observer)}];
        if(observer&&!old.observerId)old.observerId=nextObserverId++;
        observerId=old.observerId;
        changed=!old.seen||(sampled?old.emittedFull!=json:old.full!=json);
        const bool material=!old.seen||old.signature!=signature;
        periodic=sampled&&changed&&!material&&!inAction&&now-old.emitted>=std::chrono::seconds(10);
        emit=material||inAction||periodic;
        old.full=json;old.signature=std::move(signature);old.seen=true;
        if(emit){old.emitted=now;old.emittedFull=json;}
    }
    if(emit)event("state_observed","\"source\":"+quote(source)+",\"changed\":"+(changed?"true":"false")+
        (observerId?",\"observer_id\":"+std::to_string(observerId):"")+
        (periodic?",\"reason\":\"periodic_10s\"":"")+",\"state\":"+std::string(json));
}catch(...) {}}
void Scope::finish(std::string_view outcome,std::string_view fields)noexcept{
    if(!enabled_||finished_)return;finished_=true;try{
        const auto elapsed=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start_).count();
        event("command_result","\"command\":"+quote(command_)+",\"outcome\":"+quote(outcome)+",\"elapsed_us\":"+std::to_string(elapsed)+
            ",\"physical_response_measured\":false"+(fields.empty()?"":","+std::string(fields)));
    }catch(...){}
}
void Scope::fail(std::string_view reason)noexcept{try{finish("failed","\"error\":"+quote(reason));}catch(...) {}}
Scope::~Scope(){if(!enabled_)return;if(!finished_)finish(std::uncaught_exceptions()>exceptions_?"exception_escaped":command_.starts_with("ui.")?"ui_callback_returned":"returned_unconfirmed");active=previous_;if(!previous_)clearInput(id_);}
}
