#pragma once
#include <charconv>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
namespace mc::cli {
int run(const std::vector<std::string>& args);
// Pure command admission. Parsed/validated before Runtime, board discovery,
// logging, COM, driver construction or any hardware capability is acquired.
namespace detail {
struct Options {std::vector<std::string> words;bool yes{},watch{},noEc{},probes{},readOnly{};int count{},interval{1500};std::filesystem::path output;};
inline int number(std::string_view text,int minimum,int maximum){
    int value{};const auto r=std::from_chars(text.data(),text.data()+text.size(),value);
    if(r.ec!=std::errc{}||r.ptr!=text.data()+text.size()||value<minimum||value>maximum)throw std::invalid_argument("Integer outside allowed range: "+std::string(text));
    return value;
}
inline bool boolean(std::string_view text){if(text=="on"||text=="1")return true;if(text=="off"||text=="0")return false;throw std::invalid_argument("Use on/off");}
inline void arity(const Options& o,std::size_t low,std::size_t high){if(o.words.size()<low||o.words.size()>high)throw std::invalid_argument("Wrong arguments; use mctl --help");}
inline bool writer(const Options& o){const auto& w=o.words;const auto sub=w.size()>1?w[1]:"status";
    if(w[0]=="battery"&&w.size()==3&&sub=="priority"&&w[2]=="status")return false;
    if(w[0]=="fans"||w[0]=="power"||w[0]=="battery"||w[0]=="keyboard")return sub!="status";
    if(w[0]=="keys")return w.size()>2;
    if(w[0]=="lighting")return sub=="set";
    if(w[0]=="gpu")return sub!="status";
    return w[0]=="dump-ec"&&o.probes;
}
inline Options parse(const std::vector<std::string>& args){
    Options o;
    for(std::size_t i=0;i<args.size();++i){const auto& a=args[i];
        if(a=="--yes")o.yes=true;else if(a=="--watch")o.watch=true;else if(a=="--no-ec")o.noEc=true;
        else if(a=="--with-fan-probes")o.probes=true;else if(a=="--read-only")o.readOnly=true;else if(a=="--json"){}
        else if(a=="--count"||a=="--interval-ms"||a=="--output"){
            if(++i>=args.size())throw std::invalid_argument("Missing option value");
            if(a=="--count")o.count=number(args[i],1,1000000);
            else if(a=="--interval-ms")o.interval=number(args[i],250,60000);
            else o.output=std::filesystem::u8path(args[i]);
        }else if(a=="--dump-bios"&&o.words.empty())o.words.push_back("dump-bios");
        else if(a=="--dump-vbios"&&o.words.empty())o.words.push_back("dump-vbios");
        else if(a=="--dump-eeprom"&&o.words.empty())o.words.push_back("dump-eeprom");
        else if(a.starts_with("--"))throw std::invalid_argument("Unknown option: "+a);
        else o.words.push_back(a);
    }
    if(o.words.empty())throw std::invalid_argument("Missing command");
    if(o.readOnly&&(o.probes||writer(o)))throw std::invalid_argument("--read-only forbids control commands and fan probes, including with --yes");
    return o;
}
}
}
