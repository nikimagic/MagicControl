#include "I2EcWindows.h"
#include "TelemetryWindows.h"
#include "../../../platform/windows/pawnio/LpcIoWindows.h"
#include "MagicControl/Logging.h"
#include <windows.h>
#include <array>
#include <stdexcept>
namespace mc::windows {
struct HpbsdI2EcSession::Impl {
    std::unique_ptr<PawnLpcIo> io;
    HANDLE mutex{};bool held{},bad{},finished{};
    void park(){io->writeSuperIo(0x2e,0x10);io->writeConfigPort(0x2e,0x20);}
    void select(std::uint16_t a){
        if(!held||bad||finished)throw std::runtime_error("HPBSD: closed/failed I2EC session");
        io->writeSuperIo(0x2e,0x11);io->writeSuperIo(0x2f,static_cast<std::uint8_t>(a>>8));
        io->writeSuperIo(0x2e,0x10);io->writeSuperIo(0x2f,static_cast<std::uint8_t>(a));io->writeSuperIo(0x2e,0x12);
    }
    std::uint8_t get(std::uint16_t a){try{select(a);const auto v=io->readSuperIo(0x2f);park();return v;}
        catch(...){bad=true;try{park();}catch(...){}throw;}}
    void finish(){
        if(finished)return;bool failed=bad;
        if(held){try{if(io)park();}catch(...){failed=true;}if(!ReleaseMutex(mutex))failed=true;held=false;}
        finished=true;if(failed)throw std::runtime_error("HPBSD: I2EC transaction/parking not confirmed");
    }
    ~Impl(){try{finish();}catch(const std::exception& e){logMessage("hpbsd_i2ec",e.what());}if(mutex)CloseHandle(mutex);}
};
HpbsdI2EcSession::HpbsdI2EcSession():impl_(std::make_unique<Impl>()){
    if(wcsstr(GetCommandLineW(),L"--no-ec"))throw std::runtime_error("HPBSD: EC disabled by --no-ec");
    if(!HpbsdTelemetryReader::supportedHost())throw std::runtime_error("HPBSD: exact board/BIOS1.06/AMD19h:75h required");
    impl_->mutex=CreateMutexW(nullptr,FALSE,L"Global\\Access_ISABUS.HTP.Method");
    if(!impl_->mutex)throw std::runtime_error("HPBSD: ISA mutex unavailable");
    const auto wait=WaitForSingleObject(impl_->mutex,1000);impl_->held=wait==WAIT_OBJECT_0||wait==WAIT_ABANDONED;
    if(!impl_->held)throw std::runtime_error("HPBSD: ISA bus busy");
    if(wait==WAIT_ABANDONED){impl_->bad=true;throw std::runtime_error("HPBSD: abandoned ISA transaction; access refused");}
    impl_->io=std::make_unique<PawnLpcIo>();impl_->io->selectSlot0();
    std::array<std::uint8_t,3> pnp{};
    for(int i=0;i<3;++i){pnp[i]=impl_->io->readSuperIo(static_cast<std::uint8_t>(0x20+i));impl_->io->writeConfigPort(0x2e,0x20);}
    if(pnp[0]!=0x55||pnp[1]!=0x71)throw std::runtime_error("HPBSD: PNP5571 mismatch; XRAM not accessed");
    const std::array<std::uint16_t,6> addresses{0x2000,0x2001,0x2002,0x200d,0x180c,0x1841};
    const std::array<std::uint8_t,6> expected{0x55,0x71,pnp[2],0x43,0x14,0x7f};
    for(std::size_t i=0;i<addresses.size();++i)if(impl_->get(addresses[i])!=expected[i])throw std::runtime_error("HPBSD: I2EC/controller-clock signature mismatch");
}
HpbsdI2EcSession::~HpbsdI2EcSession()=default;
void HpbsdI2EcSession::finish(){impl_->finish();}
std::uint8_t HpbsdI2EcSession::read(std::uint16_t a){
    if(a>=0x1000&&a!=0x181e&&a!=0x181f&&a!=0x1820&&a!=0x1821&&a!=0x1803&&a!=0x1804&&a!=0x1841)throw std::invalid_argument("HPBSD: read outside RAM/typed fan sensors");
    return impl_->get(a);
}
std::vector<std::uint8_t> HpbsdI2EcSession::readRamBlock(std::uint16_t a,std::size_t n){
    if(!n||n>32||a>=0x1000||n>0x1000u-a)throw std::invalid_argument("HPBSD: diagnostic RAM block outside0000–0FFF");
    std::vector<std::uint8_t> out;out.reserve(n);for(std::size_t i=0;i<n;++i)out.push_back(read(static_cast<std::uint16_t>(a+i)));return out;
}
void HpbsdI2EcSession::writeFanByte(std::uint16_t a,std::uint8_t value){
    const bool base=a>=0x310&&a<=0x322&&(a-0x310)%3==0;
    const bool slope=a>=0x8b0&&a<=0x8b6;
    const bool system=a==0x330||a==0x333;
    const bool cpuUpper=a>=0x311&&a<=0x323&&(a-0x311)%3==0;
    const bool cpuLower=a>=0x315&&a<=0x327&&(a-0x315)%3==0;
    const bool systemBoundary=a==0x331||a==0x334||a==0x335||a==0x338;
    // Transport bounds also admit exact old25/83/93 factory/lease restoration.
    // USER recipes are20..100C upstream. Their native projection retains the
    // separate hot stage; transport admits exact factory/legacy restoration.
    if(!(slope||(base&&value<=51)||(system&&value<=40)||((cpuUpper||cpuLower)&&value>=20&&value<=93)||(systemBoundary&&value>=20&&value<=83)))throw std::invalid_argument("HPBSD: write outside checked normal fan policy fields");
    try{impl_->select(a);impl_->io->writeSuperIo(0x2f,value);impl_->park();}
    catch(...){impl_->bad=true;try{impl_->park();}catch(...){}throw;}
    if(read(a)!=value)throw std::runtime_error("HPBSD: fan byte readback mismatch");
}
void HpbsdI2EcSession::selectFanFactory(std::uint8_t expected,std::uint8_t desired){
    if((expected!=0xb1&&expected!=0xb2)||(desired!=0xb1&&desired!=0xb2))throw std::invalid_argument("HPBSD: unknown factory selector");
    if(read(0x32f)!=expected)throw std::runtime_error("HPBSD: factory selector changed externally");
    if(expected==desired)return;
    try{impl_->select(0x32f);impl_->io->writeSuperIo(0x2f,desired);impl_->park();}
    catch(...){impl_->bad=true;try{impl_->park();}catch(...){}throw;}
    if(read(0x32f)!=desired)throw std::runtime_error("HPBSD: factory selector readback mismatch");
}
}
