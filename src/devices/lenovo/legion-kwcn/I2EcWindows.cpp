#include "I2EcWindows.h"
#include "ControlWindows.h"
#include "../../../protocols/lenovo/LegionWmi.h"
#include "../../../platform/windows/pawnio/LpcIoWindows.h"
#include "MagicControl/BoardIdentity.h"
#include "MagicControl/DeviceProfile.h"
#include "MagicControl/Logging.h"
#include <windows.h>
namespace mc::windows::legion {
struct FanRam::Impl{
    std::unique_ptr<PawnLpcIo> io;HANDLE mutex{};bool held{},bad{},done{};
    void park(){io->writeSuperIo(0x2e,0x10);io->writeConfigPort(0x4e,0x20);}
    void select(std::uint16_t a){if(!held||bad||done)throw std::runtime_error("Lenovo: closed I2EC session");
        io->writeSuperIo(0x2e,0x11);io->writeSuperIo(0x2f,static_cast<std::uint8_t>(a>>8));
        io->writeSuperIo(0x2e,0x10);io->writeSuperIo(0x2f,static_cast<std::uint8_t>(a));io->writeSuperIo(0x2e,0x12);}
    std::uint8_t get(std::uint16_t a){try{select(a);const auto v=io->readSuperIo(0x2f);park();return v;}catch(...){bad=true;try{park();}catch(...){}throw;}}
    void finish(){if(done)return;bool failed=bad;if(held){try{if(io)park();}catch(...){failed=true;}if(!ReleaseMutex(mutex))failed=true;held=false;}done=true;if(failed)throw std::runtime_error("Lenovo: I2EC IO/parking not confirmed");}
    ~Impl(){try{finish();}catch(const std::exception& e){logMessage("lenovo_i2ec",e.what());}if(mutex)CloseHandle(mutex);}
};
FanRam::FanRam():impl_(std::make_unique<Impl>()){
    if(!legionHostSupported())throw std::runtime_error("Lenovo: exact actual KWCN54WW profile required");
    impl_->mutex=CreateMutexW(nullptr,FALSE,L"Global\\Access_ISABUS.HTP.Method");if(!impl_->mutex)throw std::runtime_error("Lenovo: ISA mutex unavailable");
    const auto wait=WaitForSingleObject(impl_->mutex,1000);impl_->held=wait==WAIT_OBJECT_0||wait==WAIT_ABANDONED;
    if(!impl_->held||wait==WAIT_ABANDONED){impl_->bad=true;throw std::runtime_error("Lenovo: ISA bus busy/abandoned");}
    impl_->io=std::make_unique<PawnLpcIo>(false);impl_->io->selectSlot1();
    // Read-only selector transactions first. No table/flag write precedes ID.
    for(int i=0;i<2;++i)if(impl_->get(0x2000)!=0x55||impl_->get(0x2001)!=0x07)throw std::runtime_error("Lenovo: physical ITE5507 ID not confirmed");
}
FanRam::~FanRam()=default;void FanRam::finish(){impl_->finish();}
std::uint8_t FanRam::read(std::uint16_t a){if(a<0xc000||a>0xdfff)throw std::invalid_argument("Lenovo: read outside reviewed SRAM alias");return impl_->get(a);}
lenovo::FanPacket FanRam::table(){lenovo::FanPacket p;for(std::size_t i=0;i<p.size();++i)p[i]=read(static_cast<std::uint16_t>(lenovo::tableAddress+i));return p;}
void FanRam::write(std::uint16_t a,std::uint8_t v){
    if(!legionFanWritesAllowed()||read(0xc200)!=0x10)throw std::runtime_error("Lenovo: FAN transaction interrupted by sleep/exit");
    const bool row=a>=lenovo::tableAddress&&a<lenovo::tableAddress+lenovo::nativeCapacity*lenovo::rowBytes;
    const bool resetStage=a>=0xdfe4&&a<=0xdfea&&v==0;
    // An original inactive map can contain sentinels or factory indexes11/12.
    // The owner writes identity1..10 and restores only its captured byte values.
    const bool levelMap=a>=0xc5f0&&a<=0xc5f9;
    const bool reload=a==0xdff3&&((read(a)^v)&0x7f)==0&&(v&0x80);
    if(!row&&a!=lenovo::tableFlags&&a!=lenovo::tableCount&&!resetStage&&!reload&&!levelMap)throw std::invalid_argument("Lenovo: write outside normal FAN RAM allowlist");
    if(a==lenovo::tableCount&&(v<1||v>lenovo::nativeCapacity))throw std::invalid_argument("Lenovo: native FAN count outside1..13");
    if(a==lenovo::tableFlags&&((read(a)^v)&0xfc))throw std::invalid_argument("Lenovo: unrelated FAN state bits cannot change");
    try{impl_->select(a);impl_->io->writeSuperIo(0x2f,v);impl_->park();}catch(...){impl_->bad=true;try{impl_->park();}catch(...){}throw;}
    if(read(a)!=v)throw std::runtime_error("Lenovo: FAN RAM write readback mismatch");
}
}
