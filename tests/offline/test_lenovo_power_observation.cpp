#include "src/protocols/lenovo/LegionPowerObservation.h"
#include <array>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>

namespace {
using mc::LenovoPowerEcDiagnostic;
using Field=std::optional<std::uint8_t> LenovoPowerEcDiagnostic::*;
struct Case { std::uint16_t address; Field field; };
constexpr std::array cases{
    Case{0xc2a5,&LenovoPowerEcDiagnostic::c2a5}, Case{0xc29a,&LenovoPowerEcDiagnostic::c29a},
    Case{0xc364,&LenovoPowerEcDiagnostic::c364}, Case{0xc365,&LenovoPowerEcDiagnostic::c365},
    Case{0xc304,&LenovoPowerEcDiagnostic::c304}, Case{0xc305,&LenovoPowerEcDiagnostic::c305},
    Case{0xc3b6,&LenovoPowerEcDiagnostic::c3b6}, Case{0xc3b8,&LenovoPowerEcDiagnostic::c3b8},
    Case{0xc4ae,&LenovoPowerEcDiagnostic::c4ae}, Case{0xc30c,&LenovoPowerEcDiagnostic::c30c},
    Case{0xc3fe,&LenovoPowerEcDiagnostic::c3fe}, Case{0xc2c9,&LenovoPowerEcDiagnostic::c2c9},
    Case{0xc4be,&LenovoPowerEcDiagnostic::c4be}, Case{0xc4ce,&LenovoPowerEcDiagnostic::c4ce},
    Case{0xc200,&LenovoPowerEcDiagnostic::c200}, Case{0xc4c1,&LenovoPowerEcDiagnostic::c4c1},
    Case{0xc4d0,&LenovoPowerEcDiagnostic::c4d0}, Case{0xc4d1,&LenovoPowerEcDiagnostic::c4d1},
    Case{0xc2a2,&LenovoPowerEcDiagnostic::c2a2}, Case{0xc2a3,&LenovoPowerEcDiagnostic::c2a3},
    Case{0xc452,&LenovoPowerEcDiagnostic::c452}, Case{0xc453,&LenovoPowerEcDiagnostic::c453},
    Case{0xc2bb,&LenovoPowerEcDiagnostic::c2bb}, Case{0xc43e,&LenovoPowerEcDiagnostic::c43e},
    Case{0xc22f,&LenovoPowerEcDiagnostic::c22f}, Case{0xc2a7,&LenovoPowerEcDiagnostic::c2a7},
    Case{0xc2de,&LenovoPowerEcDiagnostic::c2de}, Case{0xc2df,&LenovoPowerEcDiagnostic::c2df},
    Case{0xc450,&LenovoPowerEcDiagnostic::c450}, Case{0xc451,&LenovoPowerEcDiagnostic::c451},
    Case{0xc4a3,&LenovoPowerEcDiagnostic::c4a3}, Case{0xc439,&LenovoPowerEcDiagnostic::c439},
    Case{0xc93f,&LenovoPowerEcDiagnostic::c93f}, Case{0xc930,&LenovoPowerEcDiagnostic::c930},
    Case{0xc931,&LenovoPowerEcDiagnostic::c931}, Case{0xc932,&LenovoPowerEcDiagnostic::c932},
    Case{0xc933,&LenovoPowerEcDiagnostic::c933}
};
void check(bool ok,const char* what){if(!ok)throw std::runtime_error(what);}
std::size_t index(std::uint16_t address){
    for(std::size_t i=0;i<mc::lenovo::powerEcDiagnosticAddresses.size();++i)
        if(mc::lenovo::powerEcDiagnosticAddresses[i]==address)return i;
    throw std::runtime_error("required diagnostic address missing");
}
template<class F> void rejects(F&& f){try{f();}catch(const std::runtime_error&){return;}throw std::runtime_error("unstable required state accepted");}
}

int main(){try{
    using namespace mc::lenovo;
    const auto& addresses=powerEcDiagnosticAddresses;
    check(addresses.size()==43,"diagnostic read count changed");
    for(std::size_t i=0;i<addresses.size();++i)for(std::size_t j=0;j<i;++j)
        check(addresses[i]!=addresses[j],"duplicate sampled address");
    PowerEcDiagnosticBytes first{},second{};
    for(std::size_t i=0;i<addresses.size();++i)first[i]=second[i]=static_cast<std::uint8_t>(i);
    first[index(0xc41d)]=second[index(0xc41d)]=0;
    first[index(0xc2a5)]=second[index(0xc2a5)]=0; // valid zero, not missing
    first[index(0xc2c9)]=second[index(0xc2c9)]=255; // valid raw FF
    first[index(0xc22f)]=second[index(0xc22f)]=8;
    first[index(0xc4d0)]=second[index(0xc4d0)]=0x08;
    first[index(0xc4d1)]=second[index(0xc4d1)]=0xf3;
    first[index(0xc2a2)]=second[index(0xc2a2)]=0x17;
    first[index(0xc2a3)]=second[index(0xc2a3)]=0x01;
    first[index(0xc452)]=second[index(0xc452)]=0x2c;
    first[index(0xc453)]=second[index(0xc453)]=0x01;
    first[index(0xc2bb)]=second[index(0xc2bb)]=0x0b;
    first[index(0xc43e)]=second[index(0xc43e)]=0x41;
    auto decoded=decodePowerEcDiagnostic(first,second,0);
    check(decoded.c5e0==first[index(0xc5e0)]&&decoded.c5e1==first[index(0xc5e1)]&&
          decoded.c5e3==first[index(0xc5e3)]&&decoded.c5fb==first[index(0xc5fb)]&&
          decoded.c5ff==first[index(0xc5ff)]&&decoded.c41d==0,"critical/old state decoded incorrectly");
    for(const auto& item:cases)
        check((decoded.*item.field)==first[index(item.address)],"stable byte decoded under wrong name");
    check(decoded.c2a5==0&&decoded.c2c9==255&&decoded.c22f==8,"0/FF/class8 not preserved");
    check(decoded.c4d0==0x08&&decoded.c4d1==0xf3,"negative battery-current bytes lost");
    check(decoded.c2a2==0x17&&decoded.c2a3==0x01&&decoded.c452==0x2c&&decoded.c453==0x01&&
          decoded.c2bb==0x0b&&decoded.c43e==0x41,"second adapter chain bytes lost");
    // Every unstable optional sample must independently become unknown; no
    // disagreement of neighbouring bytes may silently invalidate another one.
    for(const auto& item:cases){
        auto changed=second;changed[index(item.address)]^=0x80;
        decoded=decodePowerEcDiagnostic(first,changed,0);
        check(!(decoded.*item.field),"unstable byte accepted");
        for(const auto& other:cases)if(other.address!=item.address)
            check((decoded.*other.field)==first[index(other.address)],"stable sibling was lost");
    }
    constexpr std::array<std::uint16_t,6> critical{0xc5e0,0xc5e1,0xc5e3,0xc5fb,0xc5ff,0xc41d};
    for(const auto address:critical){auto changed=second;changed[index(address)]^=0x80;
        rejects([&]{static_cast<void>(decodePowerEcDiagnostic(first,changed,0));});}
    rejects([&]{static_cast<void>(decodePowerEcDiagnostic(first,second,1));});
    first[index(0xc41d)]=second[index(0xc41d)]=1;
    static_cast<void>(decodePowerEcDiagnostic(first,second,1));
    std::cout<<"Lenovo power EC decode: stable, unstable, zero, FF and CTMD passed; hardware accesses=0\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
