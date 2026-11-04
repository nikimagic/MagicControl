// Manual diagnostic, not a CTest or product command. All operations are reads
// through the existing EC backend; no GUI startup, profile or limit setter.
#include "MagicControl/EcAccess.h"
#include "MagicControl/IEcBackend.h"
#include "MagicControl/Performance.h"
#include "MagicControl/ControlAudit.h"
#include "MagicControl/DeviceProfile.h"
#include "MagicControl/Logging.h"
#include <windows.h>
#include <iostream>
#include <array>
int main(){
    mc::initializeLogging(false);
    try{
        mc::EcAccess ec(mc::createDefaultBackend());const auto& board=ec.verifyBoard();
        if(!board.verified||mc::deviceProfile(board.board).id!="wu-jie-15-xpro")return 2;
        constexpr std::array<unsigned,29> named{0x751,0x726,0x727,0x741,0x7c5,0x7c6,0x7c3,0x770,0x490,0x49f,
            0x783,0x784,0x785,0x8c0,0x8c1,0x8c3,0x8c4,0x8c5,0x8c6,0x86b,0x86c,0x86e,0x9dd,0x7cd,0x7ce,
            0x4ab,0xa38,0xa39,0xa3a};
        for(int sample=0;sample<3;++sample){
            const auto power=mc::readEcPerformance(ec);if(!power.acceptedValid)return 3;
            std::cout<<"{\"sample\":"<<sample<<",\"at_ms\":"<<GetTickCount64()<<",\"power\":"<<mc::audit::ecPower(power)<<",\"raw\":{";
            bool first=true;const auto add=[&](unsigned address){if(!first)std::cout<<',';first=false;
                std::cout<<'\"'<<address<<"\":"<<unsigned(ec.readStable(static_cast<std::uint16_t>(address)));};
            {const mc::EcAccess::Transaction transaction(ec);
                for(auto address:named)add(address);
                for(auto base:{0x870,0x878,0x888})for(int i=0;i<8;++i)add(base+i);
            }
            std::cout<<"},\"fan_tables\":[";const auto table=ec.readBlock(0xf00,96);
            for(std::size_t i=0;i<table.size();++i){if(i)std::cout<<',';std::cout<<unsigned(table[i]);}
            std::cout<<"],\"ec_writes\":"<<ec.writeCount()<<"}\n"<<std::flush;
            if(sample<2)Sleep(1000);
        }
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
