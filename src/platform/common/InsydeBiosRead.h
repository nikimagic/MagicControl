#pragma once
// Additional OEM READ backend. No caller-supplied switches and no firmware input.
#include "BiosImageValidation.h"
#include <array>
#include <string>
#include <string_view>
#include <utility>
#include <functional>
#include <limits>
#include <optional>

namespace mc::diagnostics::insyde {
// Owner01.11.2026: model/BIOS version is descriptive, never the Insyde gate.
// Candidate detection does not attest IHISI compatibility: the pinned reader
// must accept the native interface and both returned images must validate.
inline bool candidate(std::string_view cpu,std::string_view vendor,bool h2oAcpi){
    std::string normalized;for(unsigned char c:vendor)if(std::isalnum(c))normalized+=static_cast<char>(std::toupper(c));
    return (cpu=="AuthenticAMD"||cpu=="GenuineIntel")&&(h2oAcpi||normalized.starts_with("INSYDE"));
}
inline bool acpiEvidence(std::span<const char> table){
    if(table.size()<36||bios::word(table,4)!=table.size()||std::string_view(table.data(),4)!="UEFI")return false;
    unsigned sum{};for(unsigned char c:table)sum+=c;
    return !(sum&255)&&(std::string_view(table.data()+16,8)=="H2O BIOS"||std::string_view(table.data()+10,6)=="INSYDE");
}
inline std::array<std::string,2> readArguments(std::string output){
    if(output.empty()||output.front()=='-'||output.front()=='/'||output.find_first_of("\r\n\0",0,3)!=std::string::npos)throw std::runtime_error("Invalid Insyde output path");
    // Exact OEM parser: filename before-g. No-s: its OEM handler can bypass-g
    // and changes flash-completion/override settings. This API is READ only.
    return {std::move(output),"-g"};
}
// OEM INI maps successful silent reads to3010. This only admits validation;
// no exit code alone proves a full ROM and no reboot is requested.
inline bool readExitAccepted(unsigned long code){return code==0||code==3010;}
struct Region {std::uint64_t offset{},bytes{};std::uint32_t attributes{};std::string hash;};
struct FlashMap {std::uint64_t offset{},base{},bytes{};unsigned revision{};std::vector<Region> regions;};
struct Validation {bios::Shape shape;FlashMap map;std::uint64_t romBytes{};unsigned protectedHashes{},pspRoots{};bool smbiosSize{};};
using Hash=std::function<std::string(std::span<const char>)>;
inline std::uint64_t qword(std::span<const char> b,std::size_t at){return bios::word(b,at)|(std::uint64_t(bios::word(b,at+4))<<32);}
inline bool romSize(std::uint64_t n){return n>=1024*1024&&n<=64ull*1024*1024&&!(n&(n-1));}
inline std::uint32_t fletcher(std::span<const char> b){
    if(b.size()%2)throw std::runtime_error("AMD directory checksum length");
    std::uint32_t lo=65535,hi=65535;
    for(std::size_t first=0;first<b.size();first+=718){
        const auto end=std::min(b.size(),first+718);
        for(auto at=first;at<end;at+=2){lo+=static_cast<unsigned char>(b[at])|(unsigned(static_cast<unsigned char>(b[at+1]))<<8);hi+=lo;}
        lo=(lo&65535)+(lo>>16);hi=(hi&65535)+(hi>>16);
    }
    lo=(lo&65535)+(lo>>16);hi=(hi&65535)+(hi>>16);return (hi<<16)|lo;
}
inline unsigned pspEvidence(std::span<const char> image){
    const auto resolve=[&](std::uint64_t raw)->std::size_t{
        if(raw<image.size())return static_cast<std::size_t>(raw);
        if(raw>=0x100000000ull-image.size()&&raw<0x100000000ull)return static_cast<std::size_t>(raw-(0x100000000ull-image.size()));
        throw std::runtime_error("Insyde: PSP root outside captured ROM");
    };
    const auto directory=[&](std::uint64_t raw){
        const auto at=resolve(raw);
        if(at>image.size()||image.size()-at<16||bios::word(image,at)!=0x50535024)
            throw std::runtime_error("Insyde: referenced AMD PSP root missing");
        const auto count=bios::word(image,at+8);const auto bytes=16ull+16ull*count;
        if(!count||count>512||bytes>image.size()-at||fletcher(image.subspan(at+8,static_cast<std::size_t>(bytes-8)))!=bios::word(image,at+4))
            throw std::runtime_error("Insyde: AMD PSP directory bounds/checksum invalid");
    };
    for(std::size_t at=0;at+48<=image.size();at+=16){
        if(bios::word(image,at)!=0x55aa55aa)continue;
        auto raw=bios::word(image,at+20);if(!raw||raw==0xffffffff)raw=bios::word(image,at+16);
        if(!raw||raw==0xffffffff)continue;
        const auto root=resolve(raw);if(image.size()-root<16)continue;
        const auto magic=bios::word(image,root);
        if(magic==0x50535024){directory(raw);return 1;}
        if(magic!=0x50535032)continue; // Combo directory (2PSP).
        const auto count=bios::word(image,root+8);const auto bytes=32ull+16ull*count;
        if(!count||count>512||bytes>image.size()-root||fletcher(image.subspan(root+8,static_cast<std::size_t>(bytes-8)))!=bios::word(image,root+4))
            throw std::runtime_error("Insyde: AMD PSP combo bounds/checksum invalid");
        for(unsigned i=0;i<count;++i)directory(qword(image,root+32+i*16+8));
        return count;
    }
    throw std::runtime_error("Insyde: AMD EFS/PSP evidence missing");
}
inline Validation validate(std::span<const char> image,std::uint64_t declaredBytes,std::string_view cpu,const Hash& hash={}){
    if(!romSize(image.size())||!bios::hasData(image))throw std::runtime_error("Insyde: empty/unsupported ROM size");
    if(declaredBytes&&(!romSize(declaredBytes)||declaredBytes!=image.size()))throw std::runtime_error("Insyde: captured ROM differs from SMBIOS size");
    if(cpu!="AuthenticAMD"&&cpu!="GenuineIntel")throw std::runtime_error("Insyde: unsupported CPU interface");
    std::optional<FlashMap> found;unsigned checkedHashes{};
    for(std::size_t at=0;at+28<=image.size();at+=4){
        if(bios::word(image,at)!=0x4d444648)continue;
        const auto length=bios::word(image,at+4),start=bios::word(image,at+8),entry=bios::word(image,at+12);
        // Ignore code literals; a map candidate has a bounded header/table.
        if(length<28||length>image.size()-at||start<28||start>length)continue;
        if(entry!=84||image[at+16]!=0||image[at+18]!=0||(length-start)%entry||length==start||(length-start)/entry>4096)
            throw std::runtime_error("Insyde: unsupported FlashDeviceMap format/extensions");
        unsigned checksum{};for(unsigned char c:image.subspan(at,28))checksum+=c;
        if(checksum&255)throw std::runtime_error("Insyde: FlashDeviceMap header checksum invalid");
        FlashMap map{at,qword(image,at+20),length,static_cast<unsigned char>(image[at+17]),{}};
        if(!map.base||(map.base&4095)||map.base>std::numeric_limits<std::uint64_t>::max()-image.size())
            throw std::runtime_error("Insyde: invalid ROM base address");
        // Without SMBIOS, the map must independently describe a complete
        // top-of-32bit ROM window. File length alone never selects its size.
        if(!declaredBytes&&(map.base>=0x100000000ull||0x100000000ull-map.base!=image.size()))
            throw std::runtime_error("Insyde: ROM extent not confirmed by SMBIOS or map base");
        bool selfCovered{};unsigned active{};
        for(std::size_t pos=at+start;pos<at+length;pos+=entry){
            Region r{qword(image,pos+32),qword(image,pos+40),bios::word(image,pos+48),{}};
            if(r.attributes&~3u)throw std::runtime_error("Insyde: unknown map attributes");
            constexpr char digits[]="0123456789abcdef";
            for(unsigned char c:image.subspan(pos+52,32)){r.hash+=digits[c>>4];r.hash+=digits[c&15];}
            if(!(r.attributes&2)){ // EntryIgnored does not attest a captured region.
                ++active;
                if(!r.bytes||r.offset>image.size()||r.bytes>image.size()-r.offset)
                    throw std::runtime_error("Insyde: mapped region outside captured ROM");
                if(r.offset<=at&&r.bytes>=at-r.offset+length)selfCovered=true;
                if(!(r.attributes&1)){ // Modifiable regions have non-current hashes.
                    if(!hash||hash(image.subspan(static_cast<std::size_t>(r.offset),static_cast<std::size_t>(r.bytes)))!=r.hash)
                        throw std::runtime_error("Insyde: immutable region SHA256 not confirmed");
                    ++checkedHashes;
                }
            }
            map.regions.push_back(std::move(r));
        }
        if(!active||!selfCovered)throw std::runtime_error("Insyde: map does not describe its own captured range");
        std::vector<std::pair<std::uint64_t,std::uint64_t>> ranges;
        for(const auto& r:map.regions)if(!(r.attributes&2))ranges.emplace_back(r.offset,r.offset+r.bytes);
        std::sort(ranges.begin(),ranges.end());
        for(std::size_t i=1;i<ranges.size();++i)if(ranges[i].first<ranges[i-1].second)
            throw std::runtime_error("Insyde: overlapping map regions");
        if(found)throw std::runtime_error("Insyde: multiple maps require explicit layout review");
        found=std::move(map);
    }
    if(!found)throw std::runtime_error("Insyde: FlashDeviceMap missing; whole ROM not established");
    const auto volumes=bios::firmwareVolumes(image);
    if(!volumes)throw std::runtime_error("Insyde: checked UEFI firmware volumes missing");
    Validation result{{"insyde_h2o_rom",volumes},std::move(*found),image.size(),checkedHashes,0,declaredBytes!=0};
    if(cpu=="AuthenticAMD")result.pspRoots=pspEvidence(image);
    // An Intel descriptor, if present, must prove all SPI regions as usual.
    // Otherwise this is a verified ROM map, not a claim about physical SPI.
    if(cpu=="GenuineIntel"&&bios::word(image,16)==0x0ff0a55a){
        result.shape=bios::validate(image,declaredBytes,true);
        if(!bios::allRegionsReadable(result.shape))throw std::runtime_error("Insyde: unreadable Intel SPI region");
        result.shape.kind="insyde_intel_spi";
    }
    return result;
}
}
