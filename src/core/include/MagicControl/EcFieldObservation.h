#pragma once
#include "DeviceProfile.h"
#include "EcByteEditor.h"
#include "../../../protocols/uniwill/x5sp4nag/BatteryCandidates.h"
#include "../../../protocols/uniwill/gm6ir0c/BatteryDecode.h"
#include <cstdint>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>
namespace mc {
struct EcEditorField {std::uint16_t address{};unsigned width{1};bool bigEndian{};};
inline EcEditorField ecEditorField(std::string_view profile,std::uint16_t address){
    if(gmAmdProfile(profile)&&(address==0x4A2||address==0x4A3))return {0x4A2,2,false};
    const auto find=[&](const auto& fields){for(const auto& f:fields)if(address>=f.address&&address<f.address+f.width)return EcEditorField{f.address,f.width,f.bigEndian};return EcEditorField{address,1,false};};
    if(profile=="wu-jie-15-xpro")return find(uniwill::x5sp4nag::batteryCandidates);
    if(gmRamFanProfile(profile))return find(gm6::batteryCandidates);
    if(profile=="eluktronics-hydroc16g2"&&(address==0x4A6||address==0x4A7))return {0x4A6,2,false};
    return {address,1,false};
}
struct EcByteObservation {std::uint16_t address{};std::uint8_t value{};EcEditorField field;std::vector<std::uint8_t> bytes;};
template<class Read> EcByteObservation observeEcField(std::string_view profile,std::uint16_t address,Read read){
    const auto field=ecEditorField(profile,address);
    if(!field.width||field.width>4||field.address+field.width-1>ecEditorMaximumAddress)throw std::runtime_error("Поле выходит за пределы EC-окна");
    for(int attempt=0;attempt<3;++attempt){
        auto bytes=read(field.address,field.width);if(bytes.size()!=field.width)throw std::runtime_error("Неполное чтение EC");
        if(field.width==1||bytes==read(field.address,field.width))return {address,bytes[address-field.address],field,std::move(bytes)};
    }
    throw std::runtime_error("Многобайтное поле EC меняется; согласованный снимок не получен");
}
}
