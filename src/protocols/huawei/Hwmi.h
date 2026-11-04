#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <functional>
#include <stdexcept>
#include <vector>
#include <sstream>
#include <iomanip>

namespace mc::huawei {
// Command bytes documented by mainline huawei-wmi and the HLYL reference AML.
// Windows u8Input is 64 bytes; u8Output starts at status byte 0 (not offset 4).
using Command=std::array<std::uint8_t,64>;
inline Command command(std::uint16_t opcode,std::uint8_t a=0,std::uint8_t b=0){
    Command value{};value[0]=static_cast<std::uint8_t>(opcode);value[1]=static_cast<std::uint8_t>(opcode>>8);value[2]=a;value[3]=b;return value;
}
inline constexpr std::uint16_t getCharge=0x1103,setCharge=0x1003,getFn=0x0604,setFn=0x0704,getFan=0x0802,getTemperature=0x0202;
inline constexpr std::uint16_t getTurbo=0x0E04,setTurbo=0x0F04;
// Newer OEM keyboard mode API. Gate by valid getter; older HLYL may only
// expose GLIV/SLIV through its own dispatcher. Never guess that dispatch ID.
inline constexpr std::uint16_t getKeyboardMode=0x1306,setKeyboardMode=0x1406;
inline constexpr std::uint16_t getKeyboardTimeout=0x1206,setKeyboardTimeout=0x1106;
class Session {
public:
    virtual ~Session()=default;
    virtual std::vector<std::uint8_t> call(const Command&)=0;
};
std::unique_ptr<Session> openSession();
// Serializes participating MagicControl sessions, not external OEM software.
void withWmiGate(const std::function<void()>& action);
inline void success(const std::vector<std::uint8_t>& data,std::size_t needed){
    if(data.empty()||data.size()<needed||data[0]!=0){
        std::ostringstream detail;detail<<"Huawei HWMI: response size="<<data.size()<<" required="<<needed<<" status=";
        if(data.empty())detail<<"missing";else detail<<unsigned(data[0]);
        detail<<" raw="<<std::hex<<std::setfill('0');
        for(std::size_t i=0;i<data.size()&&i<32;++i)detail<<std::setw(2)<<unsigned(data[i]);
        throw std::runtime_error(detail.str());
    }
}
inline std::vector<std::uint8_t> checkedCall(Session& session,const Command& request,std::size_t needed){
    try{auto data=session.call(request);success(data,needed);return data;}
    catch(const std::exception& error){
        std::ostringstream context;context<<"HWMI opcode=0x"<<std::hex<<std::setfill('0')<<std::setw(4)
            <<(unsigned(request[0])|(unsigned(request[1])<<8))<<" args="<<std::setw(2)<<unsigned(request[2])
            <<","<<std::setw(2)<<unsigned(request[3])<<": "<<error.what();
        throw std::runtime_error(context.str());
    }
}
}
