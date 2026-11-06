#pragma once
#include <span>
#include <vector>
#include <stdexcept>
#include <cstdint>
namespace mc::diagnostics::vbios {
struct Image {std::size_t offset{},bytes{};unsigned vendor{},device{},revision{},codeType{},checksum{};};
struct Shape {std::vector<Image> images;std::size_t trailingBytes{},prefixBytes{};unsigned ifrVersion{};};
inline Shape validate(std::span<const std::uint8_t> data,unsigned device){
    if(data.size()<512||data.size()>64*1024*1024)throw std::runtime_error("VBIOS: empty, truncated or oversized image");
    const auto word=[&](std::size_t at)->unsigned{if(at+2>data.size())throw std::runtime_error("VBIOS: truncated word");return data[at]|(unsigned(data[at+1])<<8);};
    const auto dword=[&](std::size_t at)->std::uint32_t{if(at>data.size()||data.size()-at<4)throw std::runtime_error("VBIOS: truncated IFR field");return std::uint32_t(data[at])|(std::uint32_t(data[at+1])<<8)|(std::uint32_t(data[at+2])<<16)|(std::uint32_t(data[at+3])<<24);};
    Shape out;std::size_t at{};
    if(data[0]=='N'&&data[1]=='V'&&data[2]=='G'&&data[3]=='I'){
        // NVIDIA Init-from-ROM header: Linux nova-core VBIOS format documentation.
        const auto fixed1=dword(4),fixed2=dword(8);
        out.ifrVersion=(fixed1>>8)&0xff;
        if(out.ifrVersion==1||out.ifrVersion==2){
            const auto fixedSize=(fixed1>>16)&0x7fff;
            if(fixedSize<12)throw std::runtime_error("VBIOS: invalid IFR fixed header size");
            at=dword(std::size_t(fixedSize)+4);
        }else if(out.ifrVersion==3){
            const auto total=fixed2&0xfffff;
            if(total<12)throw std::runtime_error("VBIOS: invalid IFR total header size");
            const auto directory=std::size_t(dword(total))+4096;
            if(dword(directory)!=0x44524652)throw std::runtime_error("VBIOS: invalid IFR ROM directory");
            at=dword(directory+8);
        }else throw std::runtime_error("VBIOS: unsupported IFR version");
        if((at&3)||at<12||at>=data.size())throw std::runtime_error("VBIOS: invalid IFR PCI ROM offset");
        out.prefixBytes=at;
    }
    for(unsigned count=0;count<64;++count){
        if(at+0x1a>data.size()||data[at]!=0x55||data[at+1]!=0xaa)throw std::runtime_error("VBIOS: invalid 55AA PCI ROM header");
        const auto pcir=at+word(at+0x18);
        if(pcir<at+0x1a||pcir+0x18>data.size()||data[pcir]!='P'||data[pcir+1]!='C'||data[pcir+2]!='I'||data[pcir+3]!='R')throw std::runtime_error("VBIOS: invalid PCIR pointer/signature");
        const auto length=std::size_t(word(pcir+0x10))*512;
        if(!length||length>data.size()-at||pcir+word(pcir+0xa)>at+length||word(pcir+0xa)<0x18)throw std::runtime_error("VBIOS: invalid PCI image length");
        const unsigned vendor=word(pcir+4),imageDevice=word(pcir+6),revision=data[pcir+0xc],code=data[pcir+0x14];
        bool deviceMatches=imageDevice==device;
        if(revision>=3&&word(pcir+8)){
            const auto list=pcir+word(pcir+8);
            if(list<pcir+word(pcir+0xa)||list+2>at+length)throw std::runtime_error("VBIOS: invalid PCI device list");
            bool terminated{};
            for(auto p=list;p+2<=at+length;p+=2){const auto id=word(p);if(!id){terminated=true;break;}if(id==device)deviceMatches=true;}
            if(!terminated)throw std::runtime_error("VBIOS: unterminated PCI device list");
        }
        // Some NVIDIA IFR containers carry a generic 0000:0000 EFI GOP after
        // the identified legacy image. Never admit it as a standalone image.
        const bool genericEfi=out.ifrVersion&&code==3&&vendor==0&&imageDevice==0&&!out.images.empty();
        if((!genericEfi&&(vendor!=0x10de||!deviceMatches))||(code!=0&&code!=3))throw std::runtime_error("VBIOS: PCI vendor/device/code type does not match the selected NVIDIA");
        unsigned sum{};for(std::size_t i=at;i<at+length;++i)sum=(sum+data[i])&255;
        // PCI legacy checksum is specified; the EFI image has its own PE rules.
        // Record EFI sum without applying a legacy-only checksum assumption.
        if(code==0&&sum)throw std::runtime_error("VBIOS: invalid legacy PCI checksum");
        out.images.push_back({at,length,vendor,imageDevice,revision,code,sum});at+=length;
        if(data[pcir+0x15]&0x80){out.trailingBytes=data.size()-at;return out;}
    }
    throw std::runtime_error("VBIOS: unterminated PCI ROM chain");
}
}
