// Explicit, fixed-range read-only ROM aperture reader. No EC/flash write IOCTL,
// arbitrary AML dispatch, SPI opcode, protection toggle, OEM DLL, driver install,
// live controller, startup restore, or automatic collection is reachable here.
#include "MagicControl/BiosRomDump.h"
#include "BiosRomDumpWindows.h"
#include "../common/FirmwareEvidence.h"
#include "uniwill-acpi/ReviewedDriverWindows.h"
#include "UniwillControlOwnerWindows.h"
#include "MagicControl/Version.h"
#include <array>
#include <algorithm>
#include <chrono>
#include <format>
#include <cstring>
#include <intrin.h>
namespace mc {
namespace {
namespace reviewed=windows::reviewed_uniwill;
constexpr wchar_t biosKey[]=L"HARDWARE\\DESCRIPTION\\System\\BIOS";
constexpr char dsdtHash[]="0171f11732dcba93dfa8037a713c74040363f932daed8d5eb0011779d12d53f5";
constexpr DWORD readDwordIoctl=0x9C40A494,romBase=0xFF000000;
struct Handle{HANDLE h{};~Handle(){if(h&&h!=INVALID_HANDLE_VALUE)CloseHandle(h);}};
struct Lock{HANDLE h;explicit Lock(HANDLE v):h(v){const auto r=WaitForSingleObject(h,2000);if(r!=WAIT_OBJECT_0&&r!=WAIT_ABANDONED)throw std::runtime_error("ACPI transaction busy");}~Lock(){ReleaseMutex(h);}};
bool exactIdentity(){return reviewed::registry(HKEY_LOCAL_MACHINE,biosKey,L"SystemManufacturer")==L"MECHREVO"&&
    reviewed::registry(HKEY_LOCAL_MACHINE,biosKey,L"BaseBoardProduct")==L"WUJIE Series-X5SP4NAG"&&
    reviewed::registry(HKEY_LOCAL_MACHINE,biosKey,L"BIOSVersion")==L"N.1.20MRO15";}
std::uint64_t reportedRomBytes(){
    const DWORD size=GetSystemFirmwareTable(0x52534D42,0,nullptr,0);if(size<8||size>4*1024*1024)return 0;
    std::vector<BYTE> bytes(size);if(GetSystemFirmwareTable(0x52534D42,0,bytes.data(),size)!=size)return 0;
    for(std::size_t at=8;at+4<=bytes.size();){const auto type=bytes[at],length=bytes[at+1];if(length<4||at+length>bytes.size())return 0;
        if(type==0&&length>=10){const auto legacy=bytes[at+9];if(legacy!=255)return (std::uint64_t(legacy)+1)*65536;
            if(length<26)return 0;const unsigned extended=bytes[at+24]|(bytes[at+25]<<8),unit=extended>>14;
            return unit<=1?std::uint64_t(extended&0x3fff)*(unit?1024ull*1024*1024:1024ull*1024):0;}
        at+=length;while(at+1<bytes.size()&&(bytes[at]||bytes[at+1]))++at;if(at+1>=bytes.size())return 0;at+=2;
    }return 0;
}
bool matchingDsdt(const std::wstring& path,unsigned depth=0){
    if(depth>8)return false;reviewed::Reg key;if(RegOpenKeyExW(HKEY_LOCAL_MACHINE,path.c_str(),0,KEY_READ|KEY_WOW64_64KEY,&key.key)!=ERROR_SUCCESS)return false;
    for(DWORD n=0;n<512;++n){wchar_t name[512]{};DWORD chars=512,type{},size{};const auto r=RegEnumValueW(key.key,n,name,&chars,nullptr,&type,nullptr,&size);
        if(r==ERROR_NO_MORE_ITEMS)break;if(r!=ERROR_SUCCESS||type!=REG_BINARY||size!=70415)continue;
        std::vector<char> bytes(size);if(RegQueryValueExW(key.key,name,nullptr,&type,reinterpret_cast<BYTE*>(bytes.data()),&size)==ERROR_SUCCESS&&size==bytes.size()&&windows::detail::binarySha256(bytes)==dsdtHash)return true;
    }
    for(DWORD n=0;n<512;++n){wchar_t name[512]{};DWORD chars=512;const auto r=RegEnumKeyExW(key.key,n,name,&chars,nullptr,nullptr,nullptr,nullptr);if(r==ERROR_NO_MORE_ITEMS)break;if(r==ERROR_SUCCESS&&matchingDsdt(path+L"\\"+name,depth+1))return true;}
    return false;
}
class Reader {
    Handle driver_,mutex_;
    const diagnostics::FirmwareEmitter& emit_;
    std::uint64_t reads_{};
    static std::string rawHex(const void* data,std::size_t size){
        constexpr char digits[]="0123456789ABCDEF";std::string text;text.reserve(size*2);
        const auto* bytes=static_cast<const unsigned char*>(data);
        for(std::size_t i=0;i<size;++i){text+=digits[bytes[i]>>4];text+=digits[bytes[i]&15];}return text;
    }
public:
    std::string proof;
    explicit Reader(const diagnostics::FirmwareEmitter& emit):emit_(emit){
        if(!exactIdentity()||wcsstr(GetCommandLineW(),L"--no-ec"))throw std::runtime_error("Exact WUJIE board/BIOS admission failed");
        int cpu[4]{};__cpuid(cpu,0);std::array<char,13> vendor{};std::memcpy(vendor.data(),&cpu[1],4);std::memcpy(vendor.data()+4,&cpu[3],4);std::memcpy(vendor.data()+8,&cpu[2],4);
        if(std::strcmp(vendor.data(),"AuthenticAMD"))throw std::runtime_error("AMD CPU required");
        __cpuid(cpu,0x80000000);if(static_cast<unsigned>(cpu[0])<0x80000001)throw std::runtime_error("AMD CPUID unavailable");
        __cpuid(cpu,0x80000001);const auto eax=static_cast<unsigned>(cpu[0]);
        if(((eax>>8)&15)+((eax>>20)&255)!=26||(((eax>>4)&15)|((eax>>12)&240))!=36)throw std::runtime_error("Strix Point family/model mismatch");
        if(!matchingDsdt(L"HARDWARE\\ACPI\\DSDT"))throw std::runtime_error("Exact reviewed Wujie DSDT hash not found; no ROM read");
        const auto owner=windows::uniwillControlOwner();if(!owner.empty())throw std::runtime_error("OEM control owner active: "+owner);
        // Both saved native driver revisions were statically inspected. An
        // unknown SYS is never accepted merely because its name/signature fits.
        try{reviewed::verifyReviewedUniwillDriver(proof,"e9840521975e9073881a488f9e978a18c93aed44c57fe2382b1d513fd27126e2");}
        catch(const std::exception&){proof.clear();reviewed::verifyReviewedUniwillDriver(proof,"6e360ee4a0c42b2a4eaf46208c1592c87f8cd71aca2a6c52831a2ad356e61235");}
        mutex_.h=CreateMutexExW(nullptr,L"Global\\MagicControl.EcTransactions.v2",0,SYNCHRONIZE|MUTEX_MODIFY_STATE);
        if(!mutex_.h)throw std::runtime_error("Cannot open shared ACPI mutex");
        // IOCTL A494 has read+write handle access bits; the handle permission
        // does not authorize writes. The single wrapper below fixes A494.
        driver_.h=CreateFileW(L"\\\\.\\ACPIDriver",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
        if(driver_.h==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot open installed OEM ACPI driver");
        Lock lock(mutex_.h);identity();
    }
    DWORD read(DWORD address){
        const bool rom=address>=romBase&&address<=0xFFFFFFFC&&!(address&3);
        const bool identity=address==0xFED50454||address==0xFED50456||address==0xFED50740;
        if(!rom&&!identity)throw std::runtime_error("Read outside fixed ROM/identity allowlist");
        // ACPI_METHOD_ARGUMENT_V1_INTEGER is ULONG (4 bytes). The native driver
        // copies DataLength bytes, while its outer IOCTL reports the supplied
        // 16-byte output-buffer size. Those are different lengths. A normal
        // DWORD reply preserves all three input canaries. Retain compatibility
        // with a zero-extended 8-byte reply, but never require that extension.
        // The OEM dispatcher can also report outer success after AML failure:
        // if all 16 bytes are unchanged, a 4-byte value equal to the requested
        // address cannot be distinguished from failure. Reject this ambiguity;
        // do not silently turn it into valid ROM data or issue extra reads.
        std::array<DWORD,4> packet{address,0xA75DC391,0x1B2C3D4E,0x5F607182},output{};DWORD received{};
        const auto began=std::chrono::steady_clock::now();
        const bool ok=DeviceIoControl(driver_.h,readDwordIoctl,packet.data(),sizeof(packet),output.data(),sizeof(output),&received,nullptr)!=FALSE;
        const DWORD win32=ok?ERROR_SUCCESS:GetLastError();
        const auto elapsed=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-began).count();
        const bool tail=output[2]==packet[2]&&output[3]==packet[3];
        const bool integer32=output[1]==packet[1],extended=output[1]==0;
        const bool unchanged=output==packet;
        const char* validation=!ok?"ioctl_failed":received!=sizeof(output)?"invalid_outer_length":
            !tail||(!integer32&&!extended)?"invalid_canary":unchanged?"ambiguous_unchanged_reply":
            integer32?"accepted_uint32":"accepted_zero_extended_uint32";
        const bool accepted=ok&&received==sizeof(output)&&tail&&(integer32||extended)&&!unchanged;
        ++reads_;
        // Bounded diagnostics: initial replies, all fixed identity reads, each
        // pass's first ROM DWORD and every rejection. No extra hardware calls.
        if(reads_<=16||identity||address==romBase||!accepted){
            diagnostics::FirmwareRecord record;record.kind="bios_acpi_response";record.source="OEM ACPI MMRD";
            record.success=accepted;record.status=validation;record.errorCode=win32;
            const auto stamp=std::format("{:%FT%TZ}",std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));
            record.metadataJson="{\"schema\":1,\"event\":\"acpi_response\",\"app_version\":"+diagnostics::firmwareQuote(MAGICCONTROL_VERSION_STRING)+
                ",\"captured_utc\":"+diagnostics::firmwareQuote(stamp)+",\"read_index\":"+std::to_string(reads_)+
                ",\"operation\":\"MMRD\",\"ioctl\":\"0x9C40A494\",\"address\":"+diagnostics::firmwareQuote(std::format("0x{:08X}",address))+
                ",\"input_hex\":"+diagnostics::firmwareQuote(rawHex(packet.data(),sizeof(packet)))+
                ",\"output_buffer_hex\":"+diagnostics::firmwareQuote(rawHex(output.data(),sizeof(output)))+
                ",\"returned_output_hex\":"+diagnostics::firmwareQuote(rawHex(output.data(),std::min<std::size_t>(received,sizeof(output))))+
                ",\"output_buffer_initialized_to_zero\":true,\"bytes_returned\":"+std::to_string(received)+
                ",\"ioctl_succeeded\":"+(ok?"true":"false")+",\"win32_error\":"+std::to_string(win32)+
                ",\"elapsed_us\":"+std::to_string(elapsed)+",\"inferred_integer_bytes\":"+(accepted?(integer32?"4":"8"):"null")+
                ",\"payload_length_inferred\":true,\"accepted\":"+(accepted?"true":"false")+
                ",\"validation\":"+diagnostics::firmwareQuote(validation)+"}";
            emit_(record); // Sent before any exception, including identity admission failure.
        }
        if(!accepted)throw std::runtime_error("MMRD "+std::string(validation)+" at "+std::format("0x{:08X}",address)+"; raw reply: acpi-responses.jsonl");
        return output[0];
    }
    void identity(){if((read(0xFED50454)&255)!=82||(read(0xFED50456)&255)!=72||(read(0xFED50740)&255)!=26)throw std::runtime_error("Wujie EC identity 82/72/26 mismatch");}
    void chunk(DWORD offset,std::vector<std::uint8_t>& bytes){
        Lock lock(mutex_.h);for(unsigned n=0;n<bytes.size();n+=4){const auto value=read(romBase+offset+n);std::memcpy(bytes.data()+n,&value,4);}
    }
    void finish(){Lock lock(mutex_.h);identity();}
};
}
namespace diagnostics {
void collectPlatformBiosRom(const FirmwareEmitter& emit){
    Reader reader(emit);FirmwareRecord admission;admission.kind="bios_admission";admission.success=true;
    const auto romBytes=reportedRomBytes();
    admission.metadataJson="{\"schema\":1,\"baseboard\":\"WUJIE Series-X5SP4NAG\",\"bios\":\"N.1.20MRO15\",\"dsdt_sha256\":"+firmwareQuote(dsdtHash)+",\"driver\":"+firmwareQuote(reader.proof)+",\"ec_identity\":[82,72,26],\"smbios_reported_rom_bytes\":"+(romBytes?std::to_string(romBytes):"null")+",\"spi_chip_bytes\":null,\"full_spi_confirmed\":false,\"writes_issued\":0}";emit(admission);
    for(unsigned pass=1;pass<=2;++pass)for(DWORD offset=0;offset<biosRomWindowBytes;offset+=512){
        FirmwareRecord r;r.kind="bios_rom2";r.name=std::to_string(pass)+":"+std::to_string(offset);r.source="OEM ACPI MMRD";r.bytes.resize(512);
        reader.chunk(offset,r.bytes);r.success=true;emit(r);Sleep(1);
    }
    reader.finish();
}
}
}

namespace mc::windows {
std::optional<int> runBiosDumpHelper(const std::vector<std::string>& arguments) {
    if(arguments.size()!=2||arguments[1]!="--dump-bios")return std::nullopt;
    return createBiosRomDump().saved?0:1;
}
}
