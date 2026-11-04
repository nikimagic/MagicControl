#pragma once
#include "MagicControl/HardwareInventory.h"
#include <sys/stat.h>
#include <sys/vfs.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <sstream>
#include <utility>

namespace mc::linuxinventory {
namespace fs=std::filesystem;
using Clock=std::chrono::steady_clock;
struct Fd{
    int value{-1};Fd()=default;explicit Fd(int fd):value(fd){}
    Fd(const Fd&)=delete;Fd& operator=(const Fd&)=delete;
    Fd(Fd&& other)noexcept:value(std::exchange(other.value,-1)){}
    Fd& operator=(Fd&& other)noexcept{if(this!=&other){if(value>=0)::close(value);value=std::exchange(other.value,-1);}return *this;}
    ~Fd(){if(value>=0)::close(value);}
};
inline std::string display(std::string_view value){std::string out;for(const unsigned char c:value)out+=c>=32&&c<=126?static_cast<char>(c):c=='\n'||c=='\t'?' ':'?';return out;}
inline std::string hex(std::string_view value){constexpr char digits[]="0123456789abcdef";std::string out;for(const unsigned char c:value){out+=digits[c>>4];out+=digits[c&15];}return out;}
inline bool safeLeaf(std::string_view name){return !name.empty()&&name!="."&&name!=".."&&name.size()<=255&&name.find('/')==std::string_view::npos&&name.find('\0')==std::string_view::npos;}
inline std::string errnoText(int code){return "errno="+std::to_string(code)+" "+display(std::strerror(code));}
inline bool sysfs(int fd){struct statfs data{};return ::fstatfs(fd,&data)==0&&static_cast<unsigned long>(data.f_type)==0x62656572UL;}
struct LedRows{
    std::vector<InventoryRow> rows;std::stop_token cancellation;
    Clock::time_point deadline{Clock::now()+std::chrono::seconds(3)};
    std::size_t bytes{},nodes{};bool complete{true},capped{};
    explicit LedRows(std::stop_token token):cancellation(token){}
    bool stopped(){return capped||cancellation.stop_requested()||Clock::now()>=deadline;}
    void add(std::string name,std::string value){
        if(capped)return;
        if(rows.size()>=16384||bytes+name.size()+value.size()>4*1024*1024){complete=false;capped=true;rows.push_back({"LED completeness","4MiB text/16384 row budget exhausted; partial metadata"});return;}
        bytes+=name.size()+value.size();rows.push_back({std::move(name),std::move(value)});
    }
};
struct Read{std::string raw,status;int code{};bool ok{};};
inline Fd directory(int parent,const char* name,std::string& error){
    Fd fd(::openat(parent,name,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW));
    if(fd.value<0){error=errnoText(errno);return {};}
    if(!sysfs(fd.value)){error="source is not native sysfs";return {};}
    return fd;
}
inline Fd trustedPath(int root,const fs::path& path,std::string& error){
    const auto value=path.lexically_normal();const auto s=value.string();
    if(s!="/sys"&&!s.starts_with("/sys/")){error="path outside trusted /sys";return {};}
    const auto relative=value.lexically_relative("/sys");Fd current;int parent=root;unsigned count{};
    for(const auto& part:relative){
        const auto leaf=part.string();if(leaf==".")continue;
        if(!safeLeaf(leaf)||++count>64){error="unsafe/deep native sysfs path";return {};}
        auto next=directory(parent,leaf.c_str(),error);if(next.value<0)return {};current=std::move(next);parent=current.value;
    }
    if(current.value<0){const auto duplicate=::fcntl(root,F_DUPFD_CLOEXEC,0);if(duplicate<0)error=errnoText(errno);return Fd(duplicate);}
    return current;
}
inline std::optional<fs::path> linkTarget(int fd,const fs::path& base,const char* name,std::string& error){
    std::array<char,4096> target{};const auto count=::readlinkat(fd,name,target.data(),target.size());
    if(count<0){error=errnoText(errno);return {};}
    if(static_cast<std::size_t>(count)>=target.size()){error="sysfs link target exceeds4095 bytes";return {};}
    const fs::path raw(std::string(target.data(),static_cast<std::size_t>(count)));
    const auto path=(raw.is_absolute()?raw:base/raw).lexically_normal();const auto s=path.string();
    if(!s.starts_with("/sys/")){error="native link escapes /sys";return {};}
    return path;
}
inline Read readField(LedRows& out,int fd,const char* leaf,std::size_t maximum=65536){
    Read result;if(out.stopped()){result.status="loop deadline/cancel/text budget; not read";return result;}
    if(!safeLeaf(leaf)){result.status="unsafe field name";return result;}
    Fd file(::openat(fd,leaf,O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK));
    if(file.value<0){result.code=errno;result.status=errnoText(result.code);return result;}
    struct stat st{};if(::fstat(file.value,&st)){result.code=errno;result.status="fstat: "+errnoText(result.code);return result;}
    if(!S_ISREG(st.st_mode)||!sysfs(file.value)){result.status="policy rejected nonregular/non-sysfs field";return result;}
    std::array<char,4096> bytes{};
    while(result.raw.size()<maximum){
        if(out.stopped()){result.status="loop deadline/cancel; partial field";return result;}
        const auto count=::read(file.value,bytes.data(),std::min(bytes.size(),maximum-result.raw.size()));
        if(count<0){const auto code=errno;if(code==EINTR)continue;result.code=code;result.status=errnoText(code);return result;}
        if(!count){result.ok=true;result.status=std::format("configured metadata; mode={:04o} uid={} gid={} reader_euid={}",st.st_mode&07777,st.st_uid,st.st_gid,::geteuid());
            while(!result.raw.empty()&&(result.raw.back()=='\n'||result.raw.back()=='\r'))result.raw.pop_back();return result;}
        result.raw.append(bytes.data(),static_cast<std::size_t>(count));
    }
    result.status=std::to_string(maximum)+" byte field limit (EOF not confirmed); partial metadata";return result;
}
inline void recordField(LedRows& out,const char* name,const Read& value,bool required){
    if(value.ok){out.add(name,display(value.raw));out.add(std::string(name)+" status",value.status);
        if(std::any_of(value.raw.begin(),value.raw.end(),[](unsigned char c){return (c<32&&c!='\n'&&c!='\t')||c>126;}))out.add(std::string(name)+" raw hex",hex(value.raw));
    }else {out.add(name,value.status);if(required||(value.code!=ENOENT&&value.code!=ENODATA))out.complete=false;}
}
inline bool keyboardFunction(const std::string& name){
    const auto at=name.rfind(':');if(at==std::string::npos)return false;const auto function=name.substr(at+1);
    if(function=="kbd_backlight"||function.starts_with("kbd_zoned_backlight-"))return true;
    if(!function.starts_with("kbd_backlight_"))return false;const auto suffix=function.substr(14);
    return !suffix.empty()&&suffix.find_first_not_of("0123456789")==std::string::npos;
}
inline void binding(LedRows& out,int root,const fs::path& node,int fd){
    std::string error;const auto device=linkTarget(fd,node,"device",error);
    out.add("LED device parent",device?display(device->string()):"not exported; "+error);
    fs::path current=node;bool found{};
    for(unsigned depth=0;depth<8&&!out.stopped();++depth){
        auto directoryFd=trustedPath(root,current,error);if(directoryFd.value<0){out.complete=false;out.add("LED parent access",error);break;}
        if(const auto driver=linkTarget(directoryFd.value,current,"driver",error)){
            auto driverFd=trustedPath(root,*driver,error);
            if(driverFd.value<0){out.complete=false;out.add("LED driver",error);break;}
            found=true;out.add("LED driver owner",display(current.string()));out.add("LED driver canonical",display(driver->string()));
            const auto module=linkTarget(driverFd.value,*driver,"module",error);out.add("LED module",module?display(module->string()):"not exported; "+error);
            recordField(out,"LED owner uevent",readField(out,directoryFd.value,"uevent",8192),false);break;
        }
        if(current=="/sys/devices"||current=="/sys")break;current=current.parent_path();
    }
    if(!found)out.add("LED driver binding","not resolved within8 native ancestors; no driver/model inferred");
}
inline std::vector<InventoryRow> keyboardLedRows(std::stop_token cancellation){
    LedRows out(cancellation);out.add("Метод","Read-only Linux keyboard LED/multicolor metadata; no trigger changes, module installation or generic Unknown writer");
    out.add("Граница времени","3s loop budget; a single synchronous sysfs getter can exceed it; no hard deadline promised by this in-process inventory");
    const auto unavailable=[&](std::string source,std::string reason){out.complete=false;out.add(std::move(source),std::move(reason));out.add("LED completeness","complete=false; not_available/partial native inventory; no lighting capability inferred");return std::move(out.rows);};
    Fd root(::open("/sys",O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW));
    if(root.value<0)return unavailable("LED inventory","trusted sysfs unavailable; "+errnoText(errno));
    if(!sysfs(root.value))return unavailable("LED inventory","policy rejected non-sysfs /sys mount");
    std::string error;auto klass=trustedPath(root.value,"/sys/class/leds",error);
    if(klass.value<0)return unavailable("LED class",error+"; no keyboard LED support inferred");
    const int duplicate=::fcntl(klass.value,F_DUPFD_CLOEXEC,0);
    if(duplicate<0)return unavailable("LED enumeration",errnoText(errno));
    DIR* raw=::fdopendir(duplicate);if(!raw){const auto code=errno;::close(duplicate);return unavailable("LED enumeration",errnoText(code));}
    struct Close{void operator()(DIR* d)const noexcept{::closedir(d);}};std::unique_ptr<DIR,Close> dir(raw);
    std::vector<std::string> names;unsigned entries{};
    while(!out.stopped()){
        errno=0;const auto* entry=::readdir(raw);if(!entry){if(errno){out.complete=false;out.add("LED enumeration",errnoText(errno));}break;}
        const std::string name(entry->d_name);if(name=="."||name=="..")continue;
        if(++entries>4096){out.complete=false;out.add("LED enumeration","4096 directory-entry limit; partial inventory");break;}
        if(safeLeaf(name)&&keyboardFunction(name))names.push_back(name);
    }
    std::sort(names.begin(),names.end());
    for(const auto& name:names){
        if(out.stopped())break;
        if(++out.nodes>1024){out.complete=false;out.add("LED nodes","1024 keyboard-node limit; partial inventory");break;}
        out.add("Keyboard LED",display(name));out.add("Keyboard LED name hex",hex(name));
        struct stat st{};if(::fstatat(klass.value,name.c_str(),&st,AT_SYMLINK_NOFOLLOW)){out.complete=false;out.add("LED path",errnoText(errno));continue;}
        fs::path target=fs::path("/sys/class/leds")/name;
        if(S_ISLNK(st.st_mode)){
            const auto resolved=linkTarget(klass.value,"/sys/class/leds",name.c_str(),error);
            if(!resolved){out.complete=false;out.add("LED source link",error);continue;}target=*resolved;
        }else if(!S_ISDIR(st.st_mode)){out.complete=false;out.add("LED source","not a native LED directory/link");continue;}
        auto node=trustedPath(root.value,target,error);if(node.value<0){out.complete=false;out.add("LED canonical access",error);continue;}
        out.add("LED canonical",display(target.string()));binding(out,root.value,target,node.value);
        recordField(out,"max_brightness",readField(out,node.value,"max_brightness",4096),true);
        recordField(out,"brightness",readField(out,node.value,"brightness",4096),true);
        recordField(out,"brightness_hw_changed",readField(out,node.value,"brightness_hw_changed",4096),false);
        recordField(out,"trigger",readField(out,node.value,"trigger",8192),false);
        const auto channels=readField(out,node.value,"multi_index");recordField(out,"multi_index",channels,false);
        recordField(out,"multi_intensity",readField(out,node.value,"multi_intensity"),false);
        recordField(out,"multi_max_intensity",readField(out,node.value,"multi_max_intensity"),false);
        recordField(out,"delay_on",readField(out,node.value,"delay_on",4096),false);
        recordField(out,"delay_off",readField(out,node.value,"delay_off",4096),false);
        out.add("LED capability interpretation",channels.ok?"native multicolor channel order above; configured values only, not physical output/readback proof":"brightness-only or unknown colour; absence of multi_index does not prove white hardware");
        out.add("LED admission","metadata only; no generic lighting writer enabled; trigger timer is not inactivity timeout");
    }
    if(out.stopped()){out.complete=false;out.add("LED partial",cancellation.stop_requested()?"cancelled":"loop/text deadline or budget reached");}
    out.add("LED completeness",std::format("complete={} enumerated_entries={} keyboard_nodes={}; configured metadata only",out.complete,entries,out.nodes));
    return std::move(out.rows);
}
}
