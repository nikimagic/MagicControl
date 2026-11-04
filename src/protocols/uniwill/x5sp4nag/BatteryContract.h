#pragma once
#include <array>
#include <cstdint>
#include <stdexcept>

namespace mc::uniwill::x5sp4nag {
enum class BatteryWriteSpace { HostEc, StoredWmi };
inline bool batteryWriteAllowed(BatteryWriteSpace space,std::uint16_t address,std::uint8_t mask){
    if(space==BatteryWriteSpace::StoredWmi)return address==0x87F&&mask==0x7F;
    if(space!=BatteryWriteSpace::HostEc)return false;
    return ((address==0x7B9||address==0x7D0)&&mask==0x7F)||(address==0x7C3&&mask==0xFF)||
        (address==0x770&&mask==0xFB); // Owned FF/04 RAM policy only; never Set_ROMID.
}
inline void requireBatteryWrite(BatteryWriteSpace space,std::uint16_t address,std::uint8_t mask){
    if(!batteryWriteAllowed(space,address,mask))throw std::runtime_error("Battery write outside exact-board allowlist");
}
inline void requireBatteryRequest(int percent,bool /*platform7*/){
    // Keep 10 for previously saved settings and restore; UI now offers 60.
    if(percent!=100&&percent!=95&&percent!=85&&percent!=60&&percent!=10)throw std::invalid_argument("Unsupported charge limit");
    // Platform choice and the explicit upper threshold are independent.
}
inline bool exactBatteryIdentity(std::array<std::uint8_t,3> identity){return identity==std::array<std::uint8_t,3>{82,72,26};}
template<class Read> void requireBatteryIdentity(bool verified,Read read){
    if(!verified||read(0x454)!=82||read(0x456)!=72||read(0x740)!=26)
        throw std::runtime_error("Battery activation is supported only on verified board 82/72/26");
}
inline void requireBatteryPlatform(std::uint8_t platform,std::uint8_t rom){
    if((platform!=4&&platform!=7)||(rom!=0xFF&&rom!=4))throw std::runtime_error("Unknown EC battery enforcement platform; no settings written");
}
inline void requireStoredBatteryThreshold(std::uint8_t value){
    if((value&0x7F)>100)throw std::runtime_error("Invalid stored battery threshold; no settings written");
}
inline std::uint32_t mergeStoredBatteryWord(std::uint32_t before,std::uint8_t value,std::uint8_t mask){
    requireBatteryWrite(BatteryWriteSpace::StoredWmi,0x87F,mask);
    return (before&(0xFFFFu&~static_cast<unsigned>(mask)))|(value&mask);
}
template<class Write> decltype(auto) checkedBatteryWrite(std::uint16_t address,std::uint8_t mask,Write write){
    requireBatteryWrite(BatteryWriteSpace::HostEc,address,mask);return write();
}
// The caller supplies a fresh transaction and validates ROM/platform ownership.
// Only our masked value may be restored, never an intervening external value.
template<class Read,class Write> void restoreBatteryBits(std::uint16_t address,std::uint8_t mask,
        std::uint8_t ours,std::uint8_t original,Read read,Write write){
    requireBatteryWrite(BatteryWriteSpace::HostEc,address,mask);
    const auto current=read(address);
    if((current&mask)==(original&mask))return;
    if((current&mask)!=(ours&mask))throw std::runtime_error("Battery field changed externally; field not overwritten");
    write(address,mask,original);
}
}
