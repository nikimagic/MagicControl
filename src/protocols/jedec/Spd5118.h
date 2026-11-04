#pragma once
#include "MagicControl/SensorDecoders.h"
#include <exception>
#include <optional>
#include <stdexcept>
#include <cstdint>
#include <string>

namespace mc::spd5118 {
struct RestoreError : std::runtime_error {using std::runtime_error::runtime_error;};
// Read is (register,word), select is an MR11-only checked transport operation.
// Admission for page7 recovery belongs to the exact device adapter, not CPU type.
// Linux spd5118_i2c_init documents zero volatile registers on nonzero pages.
template<class Read,class Select>
std::optional<double> temperatureWithPage7Recovery(Read read,Select select){
    const auto decode=[&]() -> std::optional<double> {
        if(read(0,true)!=0x1851)return {};
        const auto revision=read(2,false),vendor=read(3,true),capability=read(5,false),config=read(0x1a,false);
        const auto bank=vendor&0xff,id=(vendor>>8)&0xff;
        if((revision&0xc1)||(bank&0x7f)>9||!(id&0x7f)||(id&0x7f)==0x7f||
           !(capability&2)||(capability&0xfc)||(config&~1u)||(config&1))return {};
        return spd5118TemperatureCelsius(static_cast<std::uint16_t>(read(0x31,true)));
    };
    const auto type=read(0,true);
    if(type==0x1851)return decode();
    if(type!=0||read(3,true)!=0||read(0xb,false)!=7)return {};
    std::exception_ptr failure;std::optional<double> result;
    try{select(0);if(read(0xb,false)!=0)throw std::runtime_error("SPD5118: MR11 page0 readback failed");result=decode();}
    catch(...){failure=std::current_exception();}
    // Even a failed selector transfer can have reached the chip. Always restore.
    try{select(7);if(read(0xb,false)!=7)throw RestoreError("SPD5118: MR11 page7 readback failed");}
    catch(const std::exception& error){throw RestoreError(std::string("SPD5118: page restoration unconfirmed: ")+error.what());}
    if(failure)std::rethrow_exception(failure);
    return result;
}
}
