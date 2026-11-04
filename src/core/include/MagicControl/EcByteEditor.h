#pragma once
#include <cstdint>
#include <stdexcept>
#include <string_view>
namespace mc {
inline constexpr std::uint16_t ecEditorMaximumAddress=0xFFF;
inline bool ecEditorReadOnly(std::uint16_t address){
    return address==0x454||address==0x456||address==0x740||address==0x770;
}
inline void requireEcEditorAddress(std::uint16_t address,bool writing=false){
    if(address>ecEditorMaximumAddress)throw std::invalid_argument("Адрес EC должен быть0x000–0xFFF");
    if(writing&&ecEditorReadOnly(address))throw std::runtime_error("Identity/ROM-ID: адрес доступен только для чтения");
}
inline std::uint8_t ecEditorChangeMask(std::string_view profile,std::uint16_t address,std::uint8_t expected,std::uint8_t value){
    requireEcEditorAddress(address,true);
    unsigned allowed=0;
    if(profile=="wu-jie-15-xpro"||profile=="mechrevo-gm6ir0c")allowed=0xff;
    else if(profile=="eluktronics-hydroc16g2"&&(address==0x7B9||address==0x7D0))allowed=0x7f;
    else if(profile=="machenike-l16"&&address==0x7B9)allowed=0x7f;
    if(address==0x7B9||address==0x7D0||(profile=="wu-jie-15-xpro"&&address==0x741))allowed&=0x7f;
    const unsigned changed=expected^value;
    if(!allowed||(changed&~allowed))throw std::runtime_error("Запись адреса/firmware-owned битов не разрешена этим backend");
    return static_cast<std::uint8_t>(changed);
}
}
