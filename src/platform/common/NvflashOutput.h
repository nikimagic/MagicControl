#pragma once
#include <string>
#include <string_view>
#include <regex>
#include <cstdint>
#include <limits>
#include <cctype>
namespace mc::diagnostics::nvflash {
inline constexpr std::uint64_t maximumEntireBytes=16*1024*1024;
inline std::string normalized(std::string_view text){std::string out;for(unsigned char c:text)if(!std::isspace(c))out+=static_cast<char>(std::tolower(c));return out;}
inline std::string readProblem(std::string text,bool entireRequired=true){
    for(auto& c:text)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for(const auto* marker:{"limited the part size","read failed","reading failed","error:","error!","failed","unable ","cannot ","mismatch","not identical","images are different","contents differ","skip reading","skipping region","skipping data","truncat","unsupported eeprom","unknown eeprom","unrecognized eeprom","romin","override"})
        if((entireRequired||std::string_view(marker)!="limited the part size")&&text.find(marker)!=std::string::npos)return marker;
    return {};
}
struct Capacity {
    bool recognized{},admitted{};
    std::uint64_t bytes{},kUnits{},width{};
    unsigned manufacturer{},device{};
    std::string part,descriptor,reason;
};
// NVFlash5.867's FUN140483cd0/FUN140480c60 print the recognized descriptor
// as EEPROM ID (byte,word) : maker part voltage Kxwidth[S], page/byte.
// FUN1401ad830 computes K * width *128 BYTES, then caps to16MiB. Parse the
// uncapped printed geometry; never derive capacity from the saved ROM length.
inline Capacity capacityFromCheck(const std::string& raw){
    Capacity out;
    const auto reject=[](std::string reason){Capacity c;c.reason=std::move(reason);return c;};
    if(const auto p=readProblem(raw);!p.empty()){out.reason="NVFlash check reported "+p;return out;}
    const std::regex row(R"(EEPROM\s+ID\s*\(\s*([0-9a-f]{1,2})\s*,\s*([0-9a-f]{1,4})\s*\)\s*:\s*([^\r\n]+))",std::regex::icase);
    const std::regex geometry(R"(([0-9]+)\s*K\s*x\s*([0-9]+)(?:[a-zA-Z])?(?=\s|,|$))",std::regex::icase);
    bool found{};
    try{for(std::sregex_iterator i(raw.begin(),raw.end(),row),end;i!=end;++i){
        Capacity value;value.manufacturer=static_cast<unsigned>(std::stoul((*i)[1].str(),nullptr,16));value.device=static_cast<unsigned>(std::stoul((*i)[2].str(),nullptr,16));value.descriptor=(*i)[3].str();
        std::smatch g;if(!std::regex_search(value.descriptor,g,geometry)||normalized(value.descriptor).find("unknown")!=std::string::npos)return reject("EEPROM descriptor/geometry is unknown");
        value.part=value.descriptor.substr(0,static_cast<std::size_t>(g.position()));
        if(!value.manufacturer||value.manufacturer==255||!value.device||value.device==65535||normalized(value.part).empty())return reject("EEPROM part identity is incomplete");
        value.kUnits=std::stoull(g[1].str());value.width=std::stoull(g[2].str());
        if(!value.kUnits||!value.width||value.width>65535||value.kUnits>std::numeric_limits<std::uint64_t>::max()/128/value.width)return reject("Invalid EEPROM geometry");
        value.bytes=value.kUnits*value.width*128;
        if(value.bytes<512||value.bytes%512)return reject("Invalid EEPROM byte capacity");
        if(found&&(out.manufacturer!=value.manufacturer||out.device!=value.device||out.bytes!=value.bytes||out.kUnits!=value.kUnits||out.width!=value.width||normalized(out.part)!=normalized(value.part)))return reject("Ambiguous EEPROM identities/geometries");
        value.recognized=true;value.admitted=value.bytes<=maximumEntireBytes;value.reason=value.admitted?"Known EEPROM ID and uncapped Kx geometry":"EEPROM exceeds NVFlash5.86716MiB read limit";out=std::move(value);found=true;
    }}catch(const std::exception&){return reject("Cannot parse EEPROM geometry");}
    // Restricted status-register access alone does not invalidate a recognized
    // EEPROM ID/geometry. Refuse only when geometry is actually absent/invalid.
    if(!found)out.reason=normalized(raw).find("restrictedorunsupported")!=std::string::npos?
        "NVFlash status register is restricted or unsupported and no EEPROM descriptor was returned":
        "NVFlash check did not provide a recognized EEPROM descriptor";
    return out;
}
inline bool sameCapacity(const Capacity& a,const Capacity& b){return a.admitted&&b.admitted&&a.manufacturer==b.manufacturer&&a.device==b.device&&a.bytes==b.bytes&&a.kUnits==b.kUnits&&a.width==b.width&&normalized(a.part)==normalized(b.part);}
inline bool entireSizeMatches(const Capacity& c,std::uint64_t bytes){return c.admitted&&bytes==c.bytes;}
inline bool compareConfirmed(const std::string& raw){return readProblem(raw).empty()&&normalized(raw).find("firmwareimagesareidentical")!=std::string::npos;}
}
