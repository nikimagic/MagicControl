#pragma once
#include "MagicControl/Monitoring.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>

namespace mc::lenovo {
// Each alias is a candidate EC SRAM address. Collect twice under the existing
// read-only I2EC lease; this does not make the overall snapshot atomic.
inline constexpr std::array<std::uint16_t,43> powerEcDiagnosticAddresses{
    0xc5e0,0xc5e1,0xc5e3,0xc5fb,0xc5ff,0xc41d,0xc4a3,0xc439,0xc22f,0xc2a7,
    0xc2de,0xc2df,0xc450,0xc451,0xc93f,0xc930,0xc931,0xc932,0xc933,0xc2a5,
    0xc29a,0xc364,0xc365,0xc304,0xc305,0xc3b6,0xc3b8,0xc4ae,0xc30c,0xc3fe,
    0xc4be,0xc4ce,0xc200,0xc2c9,0xc4c1,0xc4d0,0xc4d1,
    0xc2a2,0xc2a3,0xc452,0xc453,0xc2bb,0xc43e};
using PowerEcDiagnosticBytes=std::array<std::uint8_t,powerEcDiagnosticAddresses.size()>;

inline LenovoPowerEcDiagnostic decodePowerEcDiagnostic(
    const PowerEcDiagnosticBytes& first,const PowerEcDiagnosticBytes& second,int actualCustom){
    // Preserve the existing strict admission for mode/power-selection bytes.
    if(!std::equal(first.begin(),first.begin()+6,second.begin()))
        throw std::runtime_error("EC power-selection bytes changed during read");
    if((second[5]&1)!=(actualCustom&1))
        throw std::runtime_error("Custom mode changed during read");
    LenovoPowerEcDiagnostic value;
    value.c5e0=second[0];value.c5e1=second[1];value.c5e3=second[2];
    value.c5fb=second[3];value.c5ff=second[4];value.c41d=second[5];
    const auto stable=[&](std::size_t i)->std::optional<std::uint8_t>{
        return first[i]==second[i]?std::optional<std::uint8_t>(second[i]):std::nullopt;
    };
    value.c4a3=stable(6);value.c439=stable(7);value.c22f=stable(8);value.c2a7=stable(9);
    value.c2de=stable(10);value.c2df=stable(11);value.c450=stable(12);value.c451=stable(13);value.c93f=stable(14);
    value.c930=stable(15);value.c931=stable(16);value.c932=stable(17);value.c933=stable(18);
    value.c2a5=stable(19);value.c29a=stable(20);value.c364=stable(21);value.c365=stable(22);
    value.c304=stable(23);value.c305=stable(24);value.c3b6=stable(25);value.c3b8=stable(26);
    value.c4ae=stable(27);value.c30c=stable(28);value.c3fe=stable(29);value.c4be=stable(30);
    value.c4ce=stable(31);value.c200=stable(32);value.c2c9=stable(33);value.c4c1=stable(34);
    value.c4d0=stable(35);value.c4d1=stable(36);
    value.c2a2=stable(37);value.c2a3=stable(38);value.c452=stable(39);value.c453=stable(40);
    value.c2bb=stable(41);value.c43e=stable(42);
    value.status="I2EC alias candidate; each reported byte twice-read stable; not atomic; not hardware verified";
    return value;
}
}
