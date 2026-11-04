#include "RamFanFactory.h"
#include <algorithm>
#include <chrono>
#include <format>
#include <initializer_list>
#include <stdexcept>
#include <thread>
#include <utility>

namespace mc::uniwill {
namespace {
constexpr std::array fields{std::pair{std::uint16_t{0x7C6},4u},std::pair{std::uint16_t{0x726},128u},
    std::pair{std::uint16_t{0x727},64u},std::pair{std::uint16_t{0x751},64u}};
std::uint8_t stable(const RamFanFactoryIo& io,std::uint16_t a){
    auto old=io.read(a);for(int i=0;i<3;++i){const auto v=io.read(a);if(old==v)return v;old=v;}
    throw std::runtime_error(std::format("Factory: unstable EC 0x{:03X}",a));
}
void admission(const RamFanFactoryIo& io,std::uint8_t project){
    io.guard();
    if(stable(io,0x740)!=project||!(stable(io,0x78E)&0x40))throw std::runtime_error("Factory: ProjectID/RAM1.5 mismatch");
    for(const auto [a,mask]:fields)if(stable(io,a)&mask)throw std::runtime_error("Factory: Default not confirmed; active RAM/custom/boost refused");
    // Exact LiquidCooling.SetECSupport(_Connected), 7C7.1. The capability
    // 7C8.5 and persisted HWOC level 7C7.2–4 are NOT connection indicators.
    if((stable(io,0x7C8)&0x20)&&(stable(io,0x7C7)&2))throw std::runtime_error("Factory: native LC connection owns fan policy");
}
void readRaw(const RamFanFactoryIo& io,RamFanFactoryRaw& raw,std::stop_token token={}){
    for(std::size_t i=0;i<raw.bytes.size();++i){
        if(token.stop_requested())throw std::runtime_error("Factory: cancelled during raw snapshot");
        raw.bytes[i]=io.read(static_cast<std::uint16_t>(0xF00+i));raw.valid[i]=1;
    }
}
bool plausible(const RamFanFactoryRaw& raw){
    if(!std::all_of(raw.valid.begin(),raw.valid.end(),[](auto v){return v==1;}))return false;
    for(unsigned base:{0u,48u}){
        bool realPoint{};unsigned previous{};
        for(unsigned i=0;i<16;++i){
            const unsigned up=raw.bytes[base+i];
            if(up!=255&&up>125)return false;
            if(i&&up<previous)return false;
            previous=up;realPoint|=up>0&&up<125;
            // OEM GetEcFanTable skips Down[0]; it is not a temperature point.
            const auto down=raw.bytes[base+16+i];if(i&&down!=255&&down>125)return false;
            // F5D–F5F are also command/ack bytes. Preserve them verbatim rather
            // than pretending they are ordinary final GPU duty slots.
            if(base==0||i<13)if(raw.bytes[base+32+i]>200)return false;
        }
        if(!realPoint)return false;
    }
    return true;
}
std::string quote(const std::string& s){
    std::string out="\"";for(unsigned char c:s){if(c=='"'||c=='\\'){out+='\\';out+=static_cast<char>(c);}
        else if(c<32)out+=std::format("\\u{:04X}",c);else out+=static_cast<char>(c);}return out+'"';
}
std::string rawJson(const RamFanFactoryRaw& r){
    std::string s="{\"bytes\":[";for(unsigned i=0;i<96;++i){if(i)s+=',';s+=r.valid[i]?std::to_string(r.bytes[i]):"null";}
    s+="],\"valid\":[";for(unsigned i=0;i<96;++i){if(i)s+=',';s+=std::to_string(r.valid[i]);}return s+"]}";
}
void serialize(RamFanFactoryResult& r){
    const auto allRead=[](const auto& raw){return std::all_of(raw.valid.begin(),raw.valid.end(),[](auto v){return v==1;});};
    const auto changed=allRead(r.rawBefore)&&allRead(r.rawAfter)?(r.rawBefore.bytes!=r.rawAfter.bytes?"true":"false"):"null";
    r.json=std::format("{{\"schema\":1,\"source\":\"OEM RAMFan1.5 factory mailbox; active diagnostic request\",\"firmware_image\":false,\"attempted\":{},\"complete\":{},\"restored\":{},\"cancelled\":{},\"status\":{},\"payload_replayed\":false,\"ram_mirror_left_inactive\":{},\"expected_project\":{},\"default_confirmed_before\":{},\"default_confirmed_after\":{},\"app_presence_mask\":1,\"app_raw_before\":{},\"app_raw_after\":{},\"encoding\":\"F00/F10/F20 CPU, F30/F40/F50 GPU; Up logical index shifted by1, Down0 reserved, raw duty /2; F5D-F5F overlap mailbox and are preserved as raw, not ordinary terminal duty points\",\"raw_before\":{},\"raw_after\":{},\"raw_changed\":{},\"modes\":[",
        r.attempted,r.complete,r.restored,r.cancelled,quote(r.status),r.attempted&&r.restored,
        r.expectedProject,r.defaultBefore,r.defaultAfter,r.appBefore?std::to_string(*r.appBefore):"null",r.appAfter?std::to_string(*r.appAfter):"null",
        rawJson(r.rawBefore),rawJson(r.rawAfter),changed);
    for(unsigned i=0;i<3;++i){if(i)r.json+=',';const auto& s=r.samples[i];
        r.json+=std::format("{{\"mode\":{},\"name\":{},\"ready\":{},\"table_valid\":{},\"waits\":{},\"status\":{},\"raw\":{}}}",s.mode,quote(s.name),s.ready,s.valid,s.waits,quote(s.status),rawJson(s.raw));}
    r.json+="]}\n";
}
RamFanFactoryResult freshResult(){
    RamFanFactoryResult r;r.samples[0].mode=2;r.samples[0].name="Gaming";
    r.samples[1].mode=3;r.samples[1].name="Office";r.samples[2].mode=1;r.samples[2].name="Turbo";
    for(auto& sample:r.samples)sample.status="not requested";
    return r;
}
}
RamFanFactoryResult refusedRamFanFactory(std::string status,bool cancelled){
    auto r=freshResult();r.status=std::move(status);r.cancelled=cancelled;serialize(r);return r;
}
RamFanFactoryResult exportRamFanFactory(const RamFanFactoryIo& io,std::uint8_t project,bool explicitWithFanProbes,std::stop_token token){
    auto r=freshResult();r.expectedProject=project;
    bool appObserved{},appTouched{};std::uint8_t originalApp{};
    try{
        if(!explicitWithFanProbes)throw std::runtime_error("Factory: skipped; explicit WithFanProbes required");
        if(project!=25&&project!=26)throw std::runtime_error("Factory: unreviewed project refused");
        if(token.stop_requested())throw std::runtime_error("Factory: cancelled before request");
        if(!io.read||!io.writeCommand||!io.guard)throw std::runtime_error("Factory: incomplete typed transport");
        admission(io,project);r.defaultBefore=true;r.appBefore=stable(io,0x741);originalApp=*r.appBefore&1;appObserved=true;
        readRaw(io,r.rawBefore,token);
        if(stable(io,0xF5D)==0xFD||stable(io,0xF5E)==0xC9)throw std::runtime_error("Factory: previous mailbox request still pending");
        admission(io,project);
        if(token.stop_requested())throw std::runtime_error("Factory: cancelled before APP release");
        r.attempted=true;appTouched=true;
        const auto app=stable(io,0x741);io.writeCommand(0x741,static_cast<std::uint8_t>(app&~1u));
        if(stable(io,0x741)&1)throw std::runtime_error("Factory: APP presence release not confirmed");
        for(auto& sample:r.samples){
            admission(io,project);
            if(token.stop_requested())throw std::runtime_error("Factory: cancelled before mode request");
            if(stable(io,0x741)&1)throw std::runtime_error("Factory: APP ownership changed");
            sample.status="request sent; awaiting both mailbox markers";
            io.writeCommand(0xF5F,static_cast<std::uint8_t>(sample.mode));
            io.writeCommand(0xF5D,0xFD);io.writeCommand(0xF5E,0xC9);
            for(unsigned wait=0;wait<10;++wait){
                // Keep the OEM500ms cadence, but allow cancellation to reach
                // cleanup promptly; cleanup never uses the cancelled token.
                for(int part=0;part<10&&!token.stop_requested();++part)std::this_thread::sleep_for(std::chrono::milliseconds(50));
                if(token.stop_requested())throw std::runtime_error("Factory: cancelled while waiting for firmware");
                sample.waits=wait+1;admission(io,project);
                if(stable(io,0x741)&1)throw std::runtime_error("Factory: APP ownership changed while waiting");
                if(io.read(0xF5D)!=0xFD&&io.read(0xF5E)!=0xC9){sample.ready=true;break;}
            }
            if(!sample.ready){sample.status="timeout: both mailbox markers did not acknowledge";throw std::runtime_error("Factory: "+sample.name+" timeout");}
            readRaw(io,sample.raw,token);RamFanFactoryRaw second;readRaw(io,second,token);
            admission(io,project);
            if(stable(io,0x741)&1)throw std::runtime_error("Factory: APP ownership changed during table capture");
            const auto acknowledged=[](const auto& raw){return raw.valid[0x5D]&&raw.valid[0x5E]&&raw.bytes[0x5D]!=0xFD&&raw.bytes[0x5E]!=0xC9;};
            const bool ackStillValid=acknowledged(sample.raw)&&acknowledged(second)&&io.read(0xF5D)!=0xFD&&io.read(0xF5E)!=0xC9;
            sample.valid=ackStillValid&&sample.raw.bytes==second.bytes&&plausible(sample.raw);
            sample.status=sample.valid?"two equal raw snapshots; conservative RAM1.5 plausibility checks passed":"ack received, but unstable/invalid raw table";
            if(!sample.valid)throw std::runtime_error("Factory: "+sample.name+" invalid table; not labelled as factory data");
        }
        r.complete=true;r.status="Three firmware mailbox tables exported; mirror is left inactive, not a flash image";
    }catch(const std::exception& e){r.status=e.what();}
    r.cancelled=token.stop_requested();
    if(appObserved){
        try{
            admission(io,project);
            const auto current=stable(io,0x741);
            if(appTouched&&(current&1)==0&&originalApp){
                io.writeCommand(0x741,static_cast<std::uint8_t>(current|1));
            }else if(appTouched&&(current&1)!=originalApp)throw std::runtime_error("APP owned mask changed externally; no blind restore");
            r.appAfter=stable(io,0x741);
            if((*r.appAfter&1)!=originalApp)throw std::runtime_error("APP presence restoration not confirmed");
            admission(io,project);r.defaultAfter=true;r.restored=true;
        }catch(const std::exception& e){r.status+="; cleanup NOT confirmed: "+std::string(e.what());}
        // Observation only. Never rewrite the previous (possibly inactive)
        // payload after a successful export, timeout, invalid reply or cancel.
        try{readRaw(io,r.rawAfter);}catch(const std::exception& e){r.status+="; final raw observation failed: "+std::string(e.what());}
    }
    serialize(r);return r;
}
}
