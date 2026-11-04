#include "I2EcWindows.h"
#include "../../../platform/windows/pawnio/LpcIoWindows.h"
#include "MagicControl/BoardIdentity.h"
#include "MagicControl/DeviceProfile.h"
#include "MagicControl/Logging.h"
#include "MagicControl/Errors.h"
#include <windows.h>
#include <stdexcept>
#include <string>
namespace mc::windows {
struct HlylI2EcSession::Impl {
    PawnLpcIo lpc;HANDLE mutex{};bool locked{},entered{},poisoned{};
    bool release(bool final=false) noexcept {
        bool ok=true;
        if(entered)try{lpc.writeConfigPort(0x2e,2);lpc.writeConfigPort(0x2f,2);entered=false;}
        catch(...){ok=false;logMessage("huawei_i2ec","PNPCFG exit failed; state unknown");}
        // A checked finish failure retains ownership until destruction can
        // attempt the exit once more. Never let a caller continue target writes
        // after an uncertain configuration-port exit.
        if(locked&&(!entered||final)){
            if(ReleaseMutex(mutex))locked=false;
            else{ok=false;logMessage("huawei_i2ec","ISA mutex release failed; Win32="+std::to_string(GetLastError()));}
        }
        if(!ok)poisoned=true;
        return ok;
    }
    ~Impl(){release(true);if(mutex)CloseHandle(mutex);}
    void acquire(DWORD timeout){
        if(poisoned)throw BackendUnavailable("I2EC: previous configuration exit failed","Session stopped; no further address/target access");
        const auto wait=WaitForSingleObject(mutex,timeout);locked=wait==WAIT_OBJECT_0||wait==WAIT_ABANDONED;
        if(!locked)throw EcObservationBusy("I2EC: доступ к Super-I/O занят");
        lpc.selectSlot0();entered=true;
        for(const auto byte:{0x87,0x01,0x55,0x55})lpc.writeConfigPort(0x2e,static_cast<std::uint8_t>(byte));
    }
    void select(std::uint16_t address){
        if(poisoned)throw BackendUnavailable("I2EC: previous configuration exit failed","Session stopped; no further address/target access");
        if(!locked||!entered)throw std::logic_error("I2EC: сессия PNPCFG не захвачена");
        lpc.writeSuperIo(0x2e,0x11);lpc.writeSuperIo(0x2f,static_cast<std::uint8_t>(address>>8));
        lpc.writeSuperIo(0x2e,0x10);lpc.writeSuperIo(0x2f,static_cast<std::uint8_t>(address));lpc.writeSuperIo(0x2e,0x12);
    }
};
HlylI2EcSession::HlylI2EcSession(){
    if(deviceProfile(readBoardDescriptor()).id!="huawei-hlyl-wxx9")throw std::runtime_error("I2EC: требуется HLYL-WXX9-PCB");
    wchar_t bios[128]{};DWORD size=sizeof(bios);
    if(RegGetValueW(HKEY_LOCAL_MACHINE,L"HARDWARE\\DESCRIPTION\\System\\BIOS",L"BIOSVersion",RRF_RT_REG_SZ,nullptr,bios,&size)!=ERROR_SUCCESS||std::wstring(bios)!=L"1.09")
        throw std::runtime_error("I2EC: поддерживается только проверенный BIOS1.09");
    impl_=std::make_unique<Impl>();
    impl_->mutex=CreateMutexW(nullptr,FALSE,L"Global\\Access_ISABUS.HTP.Method");if(!impl_->mutex)throw std::runtime_error("I2EC: mutex недоступен");
    try{impl_->acquire(1000);verifyAccess();}
    catch(const std::exception& error){
        const std::string reason=error.what();
        if(!impl_->release())throw HlylI2EcFinishError(reason+"; I2EC constructor cleanup/PNPCFG exit not confirmed");
        throw;
    }
}
void HlylI2EcSession::verifyAccess(){
    if(impl_->lpc.readSuperIo(0x20)!=0x89||impl_->lpc.readSuperIo(0x21)!=0x87||read(0x2000)!=0x89||read(0x2001)!=0x87)
        throw BoardNotVerified("I2EC: IT8987 ID/canary не совпадает");
    const auto control=read(0x200d)&3;if(control!=2&&control!=3)throw BackendUnavailable("I2EC: интерфейс отключён","Проверить штатное состояние BIOS; режим I2EC автоматически не меняется");writable=control==3;
}
HlylI2EcSession::~HlylI2EcSession()=default;
void HlylI2EcSession::finish(){
    const bool failedBefore=impl_->poisoned;
    if(!impl_->release()||failedBefore)throw HlylI2EcFinishError("I2EC: выход из PNPCFG/освобождение ISA не подтверждены; состояние интерфейса неизвестно; дальнейшие записи отменены");
}
std::uint8_t HlylI2EcSession::read(std::uint16_t address){impl_->select(address);return impl_->lpc.readSuperIo(0x2f);}
std::vector<std::uint8_t> HlylI2EcSession::readDiagnosticBlock(std::uint16_t address,std::size_t count){
    if(!count||count>16||address>=0x1000||count>0x1000u-address)
        throw AddressNotReachable(address,0x0fff,"HLYL I2EC diagnostic RAM / block<=16 bytes");
    try{
        if(!impl_->locked)impl_->acquire(0);
        verifyAccess();
        std::vector<std::uint8_t> bytes;bytes.reserve(count);
        // Only the address latch is written; I2EC_DATA is read, never written.
        for(std::size_t i=0;i<count;++i)bytes.push_back(read(static_cast<std::uint16_t>(address+i)));
        verifyAccess();
        if(!impl_->release())throw BackendUnavailable("I2EC: выход из PNPCFG не подтверждён","Чтение остановлено; проверить транспорт PawnIO");
        return bytes;
    }catch(...){impl_->release();throw;}
}
void HlylI2EcSession::writeBacklight(std::uint16_t address,std::uint8_t value,bool nativePwmReadback){
    if(!writable||(address!=0x02f4&&address!=0x03fc&&address!=0x1804))throw std::runtime_error("I2EC: запись вне backlight allowlist");
    if(nativePwmReadback&&address!=0x1804)throw std::runtime_error("I2EC: native PWM readback is limited to keyboard duty");
    // Only03FC[7:6] belongs to brightness. Firmware may update neighbouring
    // bits after the snapshot; preserve their current value, including rollback.
    if(address==0x03fc)value=static_cast<std::uint8_t>((read(address)&0x3f)|(value&0xc0));
    impl_->select(address);impl_->lpc.writeSuperIo(0x2f,value);
    const auto mask=address==0x03fc?0xc0:0xff;
    const auto actual=read(address);
    // Firmware may fade duty before this immediate read completes. The only
    // opting-in caller verifies its complete guarded selected/actual snapshot;
    // this is not acceptance of arbitrary duty or a new writable address.
    if(((actual^value)&mask)&&!nativePwmReadback)throw std::runtime_error("I2EC: подсветка не подтверждена чтением");
}
void HlylI2EcSession::writeFanRuntime(std::uint16_t address,std::uint8_t value){
    if(!writable)throw std::runtime_error("I2EC: интерфейс только для чтения");
    if(address==0x042c){if((read(address)^value)&0xfd)throw std::runtime_error("I2EC: разрешено менять только fan manual bit1");}
    else if(address==0x0532||address==0x053c){if(value>read(0x1801))throw std::runtime_error("I2EC: target превышает PWM period");}
    else throw std::runtime_error("I2EC: запись вне fan allowlist");
    impl_->select(address);impl_->lpc.writeSuperIo(0x2f,value);
    if(read(address)!=value)throw std::runtime_error("I2EC: fan runtime не подтверждён чтением");
}
}
