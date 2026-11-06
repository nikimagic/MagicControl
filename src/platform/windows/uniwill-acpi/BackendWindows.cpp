#include "MagicControl/ActionLog.h"
#include "BackendWindows.h"
#include "../LightingOwnershipWindows.h"
#include "../UniwillControlOwnerWindows.h"
#include "ReviewedDriverWindows.h"
#include "../../../protocols/uniwill/BatteryProtection.h"
#include "../../../protocols/uniwill/hydroc16g2/Battery.h"
#include "../../../protocols/uniwill/gm6ir0c/BatteryAgingGuard.h"

#include <cstddef>
#include <optional>
#include <string>
#include <stdexcept>
#include <vector>

#include "MagicControl/Errors.h"
#include "MagicControl/Types.h"

#include <windows.h>
#include <sddl.h>

namespace mc {
namespace {

/// The driver's device object, as installed by the OEM Control Center.
constexpr const char* kDevicePath = "\\\\.\\ACPIDriver";

/// Matches ACPIDriverDll!ReadEC / WriteEC from the C# reference implementation.
constexpr DWORD kIoctlRead = 0x9C40A488;
constexpr DWORD kIoctlWrite = 0x9C40A48C;

/// The driver copies into a 16-byte output buffer and reads only the first
/// 8 bytes of input. The OEM DLL passed a 4 MiB buffer for both, which made the
/// driver's input-length loop run four million times per EC request; sending the
/// real 8-byte packet is dramatically cheaper.
constexpr DWORD kInputSize = 8;
constexpr DWORD kOutputSize = 16;

/// Addresses above this cannot be expressed in the driver's packet.
constexpr std::uint16_t kMaxAddress = 0xFFFF;

/// Window's driver IOCTL for reading an embedded controller byte.
struct EcPacket {
    std::uint32_t address;
    std::uint32_t value;
};
static_assert(sizeof(EcPacket) == kInputSize, "the driver expects an 8-byte packet");

std::string describeWin32Error(DWORD code) {
    LPWSTR message = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&message), 0, nullptr);

    std::string text;
    if (length != 0 && message != nullptr) {
        const int needed = WideCharToMultiByte(CP_UTF8, 0, message, -1, nullptr, 0, nullptr, nullptr);
        if (needed > 1) {
            text.resize(static_cast<std::size_t>(needed));
            WideCharToMultiByte(CP_UTF8, 0, message, -1, text.data(), needed, nullptr, nullptr);
            text.pop_back();
        }
        // The system text ends with a newline that only adds noise in logs.
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) {
            text.pop_back();
        }
    }
    LocalFree(message);

    if (text.empty()) {
        text = "Win32 error " + toHex(code);
    }
    return text;
}

/// Explains the two failure modes a user can actually act on.
std::string hintForError(DWORD code) {
    switch (code) {
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
            return "The ACPI driver is not installed. Install the MECHREVO Control Center, or "
                   "place UWACPIDriver.sys plus its .inf next to this program and install it as "
                   "an administrator.";
        case ERROR_ACCESS_DENIED:
            return "Access denied. Run MagicControl as administrator: the EC driver only accepts "
                   "requests from an elevated process.";
        case ERROR_SHARING_VIOLATION:
            return "The driver is busy. Another copy of MagicControl or the OEM Control Center is "
                   "holding the device.";
        default:
            return "Verify that the ACPI driver is loaded and that this process is elevated.";
    }
}

}  // namespace

class WindowsAcpiDriverBackend final : public IEcBackend {
public:
    explicit WindowsAcpiDriverBackend(bool diagnosticOnly=false,bool batteryOnly=false,bool factoryOnly=false,bool thresholdsOnly=false) : diagnosticOnly_(diagnosticOnly),batteryOnly_(batteryOnly),factoryOnly_(factoryOnly),thresholdsOnly_(thresholdsOnly) {
        info_.id = "windows-acpi-driver";
        info_.displayName = "UWACPIDriver (Windows ACPI driver)";
        info_.transport = EcTransport::WindowsAcpiDriver;
        info_.maxAddress = kMaxAddress;
        info_.writable = true;
        info_.requiresElevation = true;
        if(diagnosticOnly_){info_.id="windows-acpi-driver-readonly";info_.displayName="UWACPIDriver · diagnostic read-only";info_.maxAddress=0x0FFF;info_.writable=false;}
        if(batteryOnly_){info_.id="windows-acpi-battery-policy";info_.displayName="UWACPIDriver · native battery policy only";info_.writable=true;}
        if(factoryOnly_){info_.id="windows-acpi-factory-diagnostic";info_.displayName="UWACPIDriver · explicit factory diagnostic only";info_.writable=true;}

        if(thresholdsOnly_){info_.id="windows-acpi-battery-threshold-trial";info_.displayName="UWACPIDriver · battery threshold trial only";info_.writable=true;}

        device_ = CreateFileW(L"\\\\.\\ACPIDriver", GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0,
                              nullptr);
        if (device_ == INVALID_HANDLE_VALUE) {
            const DWORD error = GetLastError();
            device_ = INVALID_HANDLE_VALUE;
            const auto message=std::string("Could not open ")+kDevicePath+": "+describeWin32Error(error);
            if(error==ERROR_NOT_READY||error==ERROR_DEVICE_NOT_CONNECTED||error==ERROR_BUSY||error==ERROR_SEM_TIMEOUT)
                throw WindowsEcNotReady(message,hintForError(error),error);
            throw BackendUnavailable(message,hintForError(error));
        }
        // One physical EC across console/RDP/service sessions. Grant only wait
        // and release to authenticated users; administrative owners retain GA.
        PSECURITY_DESCRIPTOR descriptor{};
        if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:(A;;GA;;;SY)(A;;GA;;;BA)(A;;0x00100001;;;AU)",SDDL_REVISION_1,&descriptor,nullptr)) {
            CloseHandle(device_);device_=INVALID_HANDLE_VALUE;
            throw BackendUnavailable("Could not create global EC security descriptor","EC access disabled");
        }
        SECURITY_ATTRIBUTES security{sizeof(security),descriptor,FALSE};
        transactionMutex_ = CreateMutexExW(&security,L"Global\\MagicControl.EcTransactions.v2",0,SYNCHRONIZE|MUTEX_MODIFY_STATE);
        LocalFree(descriptor);
        if (!transactionMutex_) {
            CloseHandle(device_);
            device_ = INVALID_HANDLE_VALUE;
            throw BackendUnavailable("Could not create the EC transaction gate", "Close other MagicControl processes and retry.");
        }
    }

    ~WindowsAcpiDriverBackend() override {
        if (transactionMutex_) CloseHandle(transactionMutex_);
        if (device_ != INVALID_HANDLE_VALUE) {
            CloseHandle(device_);
        }
    }

    [[nodiscard]] const BackendInfo& info() const noexcept override { return info_; }

    [[nodiscard]] std::uint8_t read(std::uint16_t address) override {
        return exchange(address, std::nullopt);
    }

    void write(std::uint16_t address, std::uint8_t value) override {
        if(thresholdsOnly_){
            // Explicit owner policy22.10.2026. Scope remains battery-only:
            // platform4/7 and ROM-ID FF/04 RAM, no save marker or other ROM byte.
            if(address==0x7c3||address==0x770){
                lockTransaction();struct Unlock{WindowsAcpiDriverBackend& b;~Unlock(){b.unlockTransaction();}} unlock{*this};
                const auto before=read(address);
                if(address==0x7c3){if((before!=4&&before!=7)||(value!=4&&value!=7))throw std::runtime_error("Battery platform value denied");}
                else{
                    if((before!=255&&before!=4)||(value!=255&&value!=4))throw std::runtime_error("Battery ROM-ID value denied");
                    value=static_cast<std::uint8_t>((before&4)|(value&0xfb));
                }
                static_cast<void>(exchange(address,value));return;
            }
            const bool upper=address==hydroc16g2::batteryUpper;
            if(!upper&&address!=hydroc16g2::batteryRestart)throw std::runtime_error("Battery threshold backend: address denied");
            const auto low=value&hydroc16g2::batteryThresholdMask;
            // Baseline0 is permitted for an owned rollback, never an automatic
            // disable operation. Public trial accepts only100/95/85 + restart80.
            if(low>(upper?100:95))throw std::runtime_error("Battery threshold backend: value denied");
            lockTransaction();struct Unlock{WindowsAcpiDriverBackend& b;~Unlock(){b.unlockTransaction();}} unlock{*this};
            const auto current=read(address);
            value=static_cast<std::uint8_t>((current&~hydroc16g2::batteryThresholdMask)|low);
            static_cast<void>(exchange(address,value));return;
        }
        if(factoryOnly_){
            std::uint8_t mask{};
            switch(address){
            case 0x741:mask=1;break;
            case 0x7C6:mask=4;break;
            case 0x726:mask=128;break;
            case 0x727:case 0x751:mask=64;break;
            case 0xF5D:if(value!=0xFD)throw std::runtime_error("Factory mailbox marker denied");break;
            case 0xF5E:if(value!=0xC9)throw std::runtime_error("Factory mailbox marker denied");break;
            case 0xF5F:if(value<1||value>3)throw std::runtime_error("Factory mailbox mode denied");break;
            default:throw std::runtime_error("Factory EC backend: address denied");
            }
            if(mask){
                // Default may release these four fields; it must never activate
                // a payload or boost. APP presence alone can be restored.
                if(address!=0x741&&(value&mask))throw std::runtime_error("Factory EC backend: activation denied");
                const auto current=read(address);
                value=static_cast<std::uint8_t>((current&~mask)|(value&mask));
            }
        }
        if(batteryOnly_){
            if(address!=uniwill::batteryProtection||(value&uniwill::batteryProtectionMask)==uniwill::batteryProtectionMask)throw std::runtime_error("Battery EC backend: policy/address denied");
            // Fresh masked RMW at the last transport boundary. Adjacent fields
            // belong to firmware/other functions, never to the battery API.
            const auto current=read(address);
            value=static_cast<std::uint8_t>((current&~uniwill::batteryProtectionMask)|(value&uniwill::batteryProtectionMask));
        }
        static_cast<void>(exchange(address, value));
    }

    void lockTransaction() override {
        const DWORD result = WaitForSingleObject(transactionMutex_, 5000);
        if(result==WAIT_TIMEOUT)
            throw EcObservationBusy("Another MagicControl process is busy with the EC; retry the command.");
        if(result!=WAIT_OBJECT_0&&result!=WAIT_ABANDONED)
            throw std::runtime_error("EC transaction gate failed: "+describeWin32Error(GetLastError()));
    }
    void unlockTransaction() noexcept override { ReleaseMutex(transactionMutex_); }
    void checkKeyboardWriter() const override {
        if(diagnosticOnly_||batteryOnly_||factoryOnly_||thresholdsOnly_)throw std::runtime_error("Restricted EC transport has no lighting writer");
        windows::ensureLightingWritable();
    }
    std::optional<UsbPowerPriorityStatus> readUsbPriorityStatus() override {
        if(diagnosticOnly_||batteryOnly_||factoryOnly_||thresholdsOnly_)return {};
        wujieI2ecAdmission();
        const auto a=uniwillI2ecByte(0x7cc),b=uniwillI2ecByte(0x7cc);
        if(a==0xff||a!=b)throw std::runtime_error("USB-C priority: unstable I2EC status");
        // N.1.20MRO15: bank2 A3F6 -> A3A3 -> 98F8 calls D8F6;
        // 9934..9937 forces7CC.7 whenever Type-C and AC are present.
        // Native AC test also returnedA4 after24. This legacy TUXEDO bit
        // is NOT an admitted persistent Battery/Performance mode here.
        return UsbPowerPriorityStatus{b,false,
            "Приоритет USB-C недоступен в этой прошивке: EC возвращает бит 0x7CC.7 при подключённом USB-C."};
    }
    BatteryAgingAccess batteryAgingAccess(const BoardDescriptor& board) const noexcept override {
        const auto id=deviceProfile(board).id;
        return !diagnosticOnly_&&!batteryOnly_&&!factoryOnly_&&!thresholdsOnly_&&
            ((board.baseBoard=="WUJIE Series-X5SP4NAG"&&id=="wu-jie-15-xpro")||
             (board.manufacturer=="MECHREVO"&&board.baseBoard=="GM6IR0C"&&
              board.productName=="JiguangX Series GM6IR0C"&&id=="mechrevo-gm6ir0c"))?
            BatteryAgingAccess::ReadWrite:BatteryAgingAccess::Unavailable;
    }
    void checkBatteryAgingWriteAccess() override {agingI2ecAdmission(true);}
    std::optional<BatteryAgingReading> readBatteryAging() override {
        if(diagnosticOnly_||batteryOnly_||factoryOnly_||thresholdsOnly_)return {};
        agingI2ecAdmission(false);
        const auto sample=[&]{
            const auto be=[&](std::uint16_t a){return static_cast<std::uint16_t>((uniwillI2ecByte(a)<<8)|uniwillI2ecByte(a+1));};
            const auto le=[&](std::uint16_t a){const auto low=uniwillI2ecByte(a);return static_cast<std::uint16_t>(low|(uniwillI2ecByte(a+1)<<8));};
            BatteryAgingReading reading{uniwillI2ecHost_,be(0x314),le(0x402),le(0x4a6),be(0x7f2),be(0x9c9),
                uniwillI2ecByte(0x36c),uniwillI2ecByte(0x39d),uniwillI2ecByte(0x726),uniwillI2ecByte(0x3a1),uniwillI2ecByte(0x72d),uniwillI2ecByte(0x72e)};
            reading.serviceBusy=(reading.service&7)!=0;reading.savePending=(reading.pending&8)!=0;
            reading.preservedContext=reading.service&0x80;return reading;
        };
        for(int i=0;i<3;++i){const auto a=sample(),b=sample();if(a==b){
            if(gm6Aging_){
                // The ordinary host window returns FF across 0800–0BFF.
                // These two internal dividers are reset below60 by the same
                // EC1.12 routine; all-FF cannot become a false baseline.
                const auto low=uniwillI2ecByte(0x9c7),high=uniwillI2ecByte(0x9c8);
                if(!gm6::agingExposureDividers(low,high))throw std::runtime_error("GM6 aging: internal exposure context unavailable");
            }
            return b;
        }}
        throw std::runtime_error("Battery aging: unstable EC snapshot");
    }
    std::optional<std::array<std::uint8_t,4>> readNativeFanFirmwareSelectors() override {
        if(diagnosticOnly_||batteryOnly_||factoryOnly_||thresholdsOnly_||
           deviceProfile(readBoardDescriptor()).id!="mechrevo-gm6ir0c")return {};
        agingI2ecAdmission(false,false);
        const auto sample=[&]{return std::array<std::uint8_t,4>{uniwillI2ecByte(0x8e6),uniwillI2ecByte(0x8e7),uniwillI2ecByte(0x8b8),uniwillI2ecByte(0x8eb)};};
        for(int i=0;i<3;++i){const auto a=sample(),b=sample();if(a==b)return b;}
        throw std::runtime_error("GM6 FAN: unstable firmware selectors");
    }
    void writeBatteryAgingField(BatteryAgingField field,std::uint8_t value) override {
        if(diagnosticOnly_||batteryOnly_||factoryOnly_||thresholdsOnly_)throw std::runtime_error("Restricted transport has no aging writer");
        agingI2ecAdmission(true);
        std::uint16_t address{};
        switch(field){
        case BatteryAgingField::AlternativeDateHigh:address=0x7f2;break;
        case BatteryAgingField::AlternativeDateLow:address=0x7f3;break;
        case BatteryAgingField::ExposureHigh:address=0x9c9;break;
        case BatteryAgingField::ExposureLow:address=0x9ca;break;
        case BatteryAgingField::Save:address=0x726;break;
        default:throw std::runtime_error("Battery aging field denied");
        }
        if(address==0x9c9||address==0x9ca){
            uniwillI2ecPort(0x681,address>>8);uniwillI2ecPort(0x682,address&255);
            // RIOP writes683, then reads684; its return is discarded.
            uniwillI2ecPort(0x683,value);
            if(uniwillI2ecByte(address)!=value)throw std::runtime_error("Battery history write not confirmed");
        }else if(address==0x7f2||address==0x7f3){
            exchange(address,value);
            if(uniwillI2ecByte(address)!=value)throw std::runtime_error("Battery age date write not confirmed");
        }else if(address==0x726&&value==1){
            // bit7 is owned by FANs. Only the native save strobe may change.
            const auto before=uniwillI2ecByte(address);
            if(before&7)throw std::runtime_error("Battery service request busy");
            exchange(address,static_cast<std::uint8_t>(before|1));
            if((uniwillI2ecByte(address)&0xfe)!=(before&0xfe))throw std::runtime_error("Battery service context changed");
        }else throw std::runtime_error("Battery aging address/value denied");
    }

private:
    std::uint8_t uniwillI2ecPort(std::uint16_t port,std::uint8_t value){
        if(port<0x681||port>0x683)throw std::runtime_error("I2EC port denied");
        const std::uint32_t input[]{port,value};std::byte output[16]{};DWORD n{};
        if(!DeviceIoControl(device_,0x9c40a4c8,const_cast<std::uint32_t*>(input),8,output,16,&n,nullptr)||!n||n>16)
            throw std::runtime_error("Wujie I2EC RIOP failed");
        return std::to_integer<std::uint8_t>(output[0]);
    }
    std::uint8_t uniwillI2ecByte(std::uint16_t address){uniwillI2ecPort(0x681,address>>8);return uniwillI2ecPort(0x682,address&255);}
    void agingI2ecAdmission(bool writing,bool requireBattery=true){
        if(deviceProfile(readBoardDescriptor()).id!="mechrevo-gm6ir0c"){wujieI2ecAdmission();return;}
        using namespace windows::reviewed_uniwill;
        const auto board=readBoardDescriptor();
        if(board.manufacturer!="MECHREVO"||board.baseBoard!="GM6IR0C"||board.productName!="JiguangX Series GM6IR0C"||
           registry(HKEY_LOCAL_MACHINE,L"HARDWARE\\DESCRIPTION\\System\\BIOS",L"BIOSVersion")!=L"N.1.05MRO09")
            throw std::runtime_error("GM6 aging: exact board/BIOS required");
        if(!gm6Aging_||writing){
            std::string notes;verifyReviewedUniwillDriver(notes,"6e360ee4a0c42b2a4eaf46208c1592c87f8cd71aca2a6c52831a2ad356e61235");
        }
        if(requireBattery&&uniwillI2ecHost_.empty()){
            uniwillI2ecHost_=text(registry(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Microsoft\\Cryptography",L"MachineGuid"));
            if(uniwillI2ecHost_.empty())throw std::runtime_error("GM6 aging: host identity unavailable");
        }
        if(writing){const auto owner=windows::uniwillControlOwner();if(!owner.empty())throw std::runtime_error("GM6 aging: OEM owns EC: "+owner);}
        // First compare ordinary ECRR against exact EC version/project; only
        // then use the two-argument RIOP selector path. No WIOP is used.
        const auto capability=exchange(0x78e,{});
        const auto hostReady=requireBattery?exchange(0x490,{}):std::uint8_t{0};
        if(!gm6::agingEcIdentity(exchange(0x454,{}),exchange(0x740,{}),capability,hostReady,requireBattery))
            throw std::runtime_error("GM6 aging: EC identity or battery readiness mismatch");
        if(!gm6::agingSku18(exchange(0x74c,{}),exchange(0x7ca,{})))
            throw std::runtime_error("GM6 aging: firmware SKU not confirmed");
        // EC1.12 publishes applied aging/CV only with AC present. Do not start
        // a write that cannot be confirmed and rolled back on battery power.
        if(writing&&requireBattery&&!gm6::agingExternalPower(hostReady))throw std::runtime_error("GM6 aging: подключите питание для подтверждения и отката");
        const auto internalCapability=uniwillI2ecByte(0x78e);
        const auto internalReady=requireBattery?uniwillI2ecByte(0x490):std::uint8_t{0};
        const auto internalVersion=uniwillI2ecByte(0x454),internalProject=uniwillI2ecByte(0x740);
        if(!gm6::agingEcIdentity(internalVersion,internalProject,internalCapability,internalReady,requireBattery))
            throw std::runtime_error("GM6 I2EC identity rejected: version454=0x"+toHex(internalVersion)+
                " project740=0x"+toHex(internalProject)+" capability78E=0x"+toHex(internalCapability)+
                (requireBattery?" ready490=0x"+toHex(internalReady):" ready490=not-read"));
        // Preserve the identity short-circuit: an unconfirmed private window
        // never reaches the configuration reads or grants access to a writer.
        const auto baseHigh=uniwillI2ecByte(0x2014),baseLow=uniwillI2ecByte(0x2015),enabled=uniwillI2ecByte(0x2012);
        if(!gm6::agingI2ecConfiguration(baseHigh,baseLow,enabled))
            throw std::runtime_error("GM6 I2EC configuration rejected: base2014=0x"+toHex(baseHigh)+
                " base2015=0x"+toHex(baseLow)+" enable2012=0x"+toHex(enabled));
        if(requireBattery){
            const auto sample=[&]{return std::array<std::uint8_t,3>{uniwillI2ecByte(0x8e6),uniwillI2ecByte(0x8e7),uniwillI2ecByte(0x8b8)};};
            const auto first=sample(),second=sample();
            if(first!=second||!gm6::agingPrivateWindow(first[0],first[1],first[2]))
                throw std::runtime_error("GM6 aging: private I2EC window not confirmed");
        }
        gm6Aging_=true;
    }
    void wujieI2ecAdmission(){
        if(!wujieReviewed_){
            using namespace windows::reviewed_uniwill;
            const auto board=readBoardDescriptor();
            if(board.baseBoard!="WUJIE Series-X5SP4NAG"||deviceProfile(board).id!="wu-jie-15-xpro"||
               registry(HKEY_LOCAL_MACHINE,L"HARDWARE\\DESCRIPTION\\System\\BIOS",L"BIOSVersion")!=L"N.1.20MRO15")
                throw std::runtime_error("Wujie I2EC: exact BIOS required");
            std::string notes;verifyReviewedUniwillDriver(notes,"6e360ee4a0c42b2a4eaf46208c1592c87f8cd71aca2a6c52831a2ad356e61235");
            uniwillI2ecHost_=text(registry(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Microsoft\\Cryptography",L"MachineGuid"));
            if(uniwillI2ecHost_.empty())throw std::runtime_error("Wujie I2EC: host identity unavailable");
            wujieReviewed_=true;
        }
        if(exchange(0x454,{})!=82||exchange(0x456,{})!=72||exchange(0x740,{})!=26)
            throw std::runtime_error("Wujie I2EC: EC identity mismatch");
        if(uniwillI2ecByte(0x454)!=82||uniwillI2ecByte(0x456)!=72||uniwillI2ecByte(0x740)!=26||
           uniwillI2ecByte(0x2014)!=6||uniwillI2ecByte(0x2015)!=0x80||!(uniwillI2ecByte(0x2012)&1)||(uniwillI2ecByte(0x200d)&3)!=3)
            throw std::runtime_error("Wujie I2EC: identity/configuration mismatch");
        if((uniwillI2ecByte(0x490)&6)!=6)throw std::runtime_error("Wujie I2EC: battery not ready");
    }
    bool wujieReviewed_{},gm6Aging_{};std::string uniwillI2ecHost_;
    /// Issues one IOCTL. Read and write share the packet layout, so one function
    /// covers both and the two code paths cannot drift apart.
    std::uint8_t exchange(std::uint16_t address, std::optional<std::uint8_t> value) {
        const bool isWrite = value.has_value();
        const bool tracing=isWrite||audit::current()!=0;
        audit::Scope trace("uniwill.acpi_exchange",tracing?("\"address\":"+std::to_string(address)+",\"write\":"+(isWrite?"true":"false")+",\"requested\":"+(value?std::to_string(*value):"null")):"",0,tracing);
        if(diagnosticOnly_&&((isWrite&&!batteryOnly_&&!factoryOnly_&&!thresholdsOnly_)||address>0x0FFF))throw std::runtime_error("Diagnostic EC backend: write/address denied");

        EcPacket packet{};
        packet.address = address;
        packet.value = value.value_or(0);

        std::byte output[kOutputSize] = {};
        DWORD returned = 0;

        // Preserve8/16 for existing controllers. HYDROC's DefaultTool passes a
        // DWORD address; retain16 output bytes because the SYS copies the ACPI
        // integer's reported width without comparing the caller's buffer size.
        const DWORD inputSize=diagnosticOnly_&&!isWrite?4:kInputSize,outputSize=kOutputSize;
        const BOOL ok = DeviceIoControl(device_, isWrite ? kIoctlWrite : kIoctlRead, &packet,
                                        inputSize, output, outputSize, &returned, nullptr);
        const DWORD callError=ok?ERROR_SUCCESS:GetLastError();
        if(tracing)audit::event("acpi_transport_response","\"ioctl\":"+std::to_string(isWrite?kIoctlWrite:kIoctlRead)+",\"input_hex\":"+audit::quote(audit::hex({reinterpret_cast<const std::uint8_t*>(&packet),inputSize}))+",\"bytes_returned\":"+std::to_string(returned)+",\"output_hex\":"+audit::quote(audit::hex({reinterpret_cast<const std::uint8_t*>(output),std::min<DWORD>(returned,outputSize)}))+",\"driver_call_succeeded\":"+(ok?"true":"false")+",\"win32_error\":"+std::to_string(callError)+",\"physical_response_measured\":false");
        if (!ok) {
            const DWORD error = callError;
            trace.fail("DeviceIoControl Win32="+std::to_string(error));
            throw EcIoError(address,
                            "EC 0x" + toHex(address) + ": driver " +
                                (isWrite ? "write" : "read") + " failed: " +
                                describeWin32Error(error),
                            error);
        }

        if (!isWrite && (returned == 0 || returned > outputSize)) {
            throw EcIoError(address, "EC 0x" + toHex(address) +
                                         ": driver returned an invalid reply length (" +
                                         std::to_string(returned) + " bytes)");
        }

        if(tracing)trace.finish("transport_returned");
        if (isWrite) {
            return *value;
        }
        return std::to_integer<std::uint8_t>(output[0]);
    }

    BackendInfo info_{};
    HANDLE device_{INVALID_HANDLE_VALUE};
    HANDLE transactionMutex_{};
    bool diagnosticOnly_{};
    bool batteryOnly_{};
    bool factoryOnly_{};
    bool thresholdsOnly_{};
};

std::unique_ptr<IEcBackend> createWindowsAcpiDriverBackend() {
    return std::make_unique<WindowsAcpiDriverBackend>();
}
std::unique_ptr<IEcBackend> createWindowsAcpiDiagnosticBackend(){return std::make_unique<WindowsAcpiDriverBackend>(true);}
std::unique_ptr<IEcBackend> createWindowsAcpiBatteryBackend(){return std::make_unique<WindowsAcpiDriverBackend>(true,true);}
std::unique_ptr<IEcBackend> createWindowsAcpiBatteryThresholdBackend(){return std::make_unique<WindowsAcpiDriverBackend>(true,false,false,true);}
std::unique_ptr<IEcBackend> createWindowsAcpiFactoryBackend(){return std::make_unique<WindowsAcpiDriverBackend>(true,false,true);}

}  // namespace mc
