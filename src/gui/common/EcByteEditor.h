#pragma once
#include "MagicControl/DeviceProfile.h"
#include <charconv>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include "MagicControl/EcByteEditor.h"
#include "MagicControl/EcHexInput.h"
#include "MagicControl/EcFieldObservation.h"
#include "Localization.h"
#include "../../protocols/uniwill/x5sp4nag/BatteryCandidates.h"
#include "../../protocols/uniwill/gm6ir0c/BatteryDecode.h"
namespace mc::gui::ui {
using mc::parseEcHex;
inline std::string ecByteFormats(std::uint8_t value){
    const int signedValue=value<128?value:static_cast<int>(value)-256;
    std::string text=std::format("HEX 0x{:02X}   DEC {}   INT8 {}     ",value,value,signedValue);
    for(int bit=7;bit>=0;--bit)text+=std::format("b{}={}{}",bit,(value>>bit)&1,bit?"   ":"");
    return text;
}
using mc::EcEditorField;
using mc::EcByteObservation;
using mc::ecEditorField;
using mc::observeEcField;
struct EcFieldText {std::string text;std::size_t activeStart{},activeLength{};};
inline EcFieldText ecFieldText(const EcByteObservation& observation){
    EcFieldText result;std::uint32_t le=0,be=0;
    for(unsigned i=0;i<observation.bytes.size();++i){const auto byte=observation.bytes[i];
        const auto row=std::format("EC[0x{:03X}] = 0x{:02X}   {}",observation.field.address+i,byte,ecByteFormats(byte));
        if(observation.field.address+i==observation.address){result.activeStart=result.text.size();result.activeLength=row.size();}
        result.text+=row+'\n';le|=static_cast<std::uint32_t>(byte)<<(8*i);be=(be<<8)|byte;}
    if(observation.bytes.size()>1)result.text+=std::format("LE{}={}   BE{}={}   [карта: {}]",observation.bytes.size()*8,le,observation.bytes.size()*8,be,observation.field.bigEndian?"BE":"LE");
    return result;
}
inline std::string ecRegisterDescription(std::string_view profile,std::uint16_t address){
    if(gmAmdProfile(profile)&&(address==0x4A2||address==0x4A3))
        return std::format("ecBt1Temperature · OEM · LE16 · byte {}/2 · SBS0.1K candidate",address-0x4A2+1);
    const auto describe=[&](const auto& fields,const auto& bits){
        std::string text;
        for(const auto& field:fields)if(address>=field.address&&address<field.address+field.width){
            text=field.name;text+=field.confirmed?" · [documented]":" · [candidate/unverified]";
            if(field.width>1)text+=std::format(" · byte {}/{} ({})",address-field.address+1,field.width,field.bigEndian?"BE":"LE");
            break;
        }
        for(const auto& bit:bits)if(bit.address==address)text+=std::format(" · b{}: {}",bit.bit,bit.name);
        return text;
    };
    if(profile=="wu-jie-15-xpro"){
        if(address==0x454)return "ecOEM_EC_VER · байт идентичности EC · только чтение";
        if(address==0x456)return "SystemID · байт идентичности платы · только чтение";
        if(address==0x740)return "ProjectID · идентификатор проекта EC · только чтение";
        if(address==0x770)return "ROM-ID · идентификатор прошивки · только чтение";
        if(address==0x741)return "AP_OEM · b0: управление приложения; b3: reactive (OEM); b5: fan abnormal (OEM). Остальные биты не расшифрованы.";
        if(address==0x7B9)return "Верхний порог заряда · b0–b6: процент (0 = 100%); b7: флаг достигнутого порога (OEM).";
        if(address==0x7D0)return "Нижний порог · b0–b6: заданный процент; физическое действие и b7 пока не подтверждены.";
        if(address==0x497)return "ISDB · b0: режим данных ACPI. b5/b7 менялись при завершении зарядки; их смысл не подтверждён.";
        if(address>=0xF00&&address<0xF60){
            constexpr const char* names[]{"CPU UpT","CPU DownT","CPU duty raw","GPU UpT","GPU DownT","GPU duty raw"};
            return std::format("RAM FAN table · {} · point {} · raw byte",names[(address-0xF00)/16],(address&15)+1);
        }
        auto text=describe(uniwill::x5sp4nag::batteryCandidates,uniwill::x5sp4nag::batteryCandidateBits);
        if(!text.empty())return text;
    }else if(gmRamFanProfile(profile)){
        if(gmAmdProfile(profile)&&(address==0x7D0||address==0x7D1))return "DBD1/DBD2 · не порог возобновления зарядки";
        auto text=describe(gm6::batteryCandidates,gm6::batteryCandidateBits);if(!text.empty())return text;
    }else if(profile=="eluktronics-hydroc16g2"){
        if(address==0x7B9)return "OEM upper threshold · low7 percent, high bit firmware-owned";
        if(address==0x7D0)return "OEM lower threshold · low7 percent; physical enforcement unverified";
        if(address==0x4A6||address==0x4A7)return "OEM battery cycle count · byte of LE16";
    }else if(profile=="huawei-hlyl-wxx9")return "Honor: HWMI/sysfs; raw EC для этого редактора не подключён.";
    return "Назначение адреса для этой платы не подтверждено.";
}
inline std::string ecEditorRangeCaption(){
    return translate("Диапазон байта: 0x00–0xFF (0–255). Поддержка прошивкой зависит от адреса.");
}
inline std::string ecKnownValues(std::string_view profile,std::uint16_t address){
    if(gmAmdProfile(profile)&&(address==0x7D0||address==0x7D1))return "DBD1/DBD2 · DSDT command1173: Arg1/Arg2 ×8 · не порог зарядки";
    const auto heading=translate("Известные значения:");
    if(l16Profile(profile)&&address==0x7C3)
        return heading+"\n"+translate("0x06 (6) — штатная платформа L16 из дампа; переключение 4/7 не подтверждено.");
    if(profile=="wu-jie-15-xpro"&&address==0x7C3)
        return heading+"\n"+translate("0x04 (4) — платформа в последовательности ограничения заряда; также меняет лимиты APU.")+"\n"+
            translate("0x07 (7) — другая платформа: штатные профили APU на батарее имеют разные лимиты. Прочие значения не расшифрованы.");
    const bool uniwill=profile=="wu-jie-15-xpro"||gmRamFanProfile(profile)||profile=="eluktronics-hydroc16g2";
    if(uniwill&&address==0x7B9)
        return heading+"\n"+translate("b0–b6: 0x00 = 100%, 0x55 = 85%, 0x5F = 95%; b7 сохраняется.")+"\n"+
            translate("Это настройка порога, а не измеренный заряд и не гарантия физической отсечки.");
    if(uniwill&&address==0x7D0)
        return heading+"\n"+translate("b0–b6: заданный процент нижнего порога; b7 сохраняется.")+"\n"+
            translate("Физическое действие нижнего порога пока не подтверждено.");
    if(profile=="wu-jie-15-xpro"&&address==0x741)
        return heading+"\n"+translate("Маски: 0x01 — управление приложения; 0x08 — reactive; 0x20 — fan abnormal (OEM).")+"\n"+
            translate("Это отдельные биты; прочие биты не расшифрованы, bit7 сохраняется.");
    return heading+" "+translate("Расшифровка значений для этого адреса и модели отсутствует.");
}
}
