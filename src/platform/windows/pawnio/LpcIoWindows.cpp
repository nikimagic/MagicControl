#include "MagicControl/ActionLog.h"
#include "LpcIoWindows.h"
#include "../EmbeddedRuntimeWindows.h"
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <vector>
#include <string>
#include <stdexcept>

namespace mc::windows {
namespace {
struct File {
    HANDLE handle{INVALID_HANDLE_VALUE};
    ~File(){if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);}
    std::vector<unsigned char> pinned(const std::filesystem::path& path,const std::string& expected){
        handle=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(handle==INVALID_HANDLE_VALUE)throw std::runtime_error("I2EC: отсутствует закреплённый PawnIO runtime");
        LARGE_INTEGER size{};if(!GetFileSizeEx(handle,&size)||size.QuadPart<32||size.QuadPart>1024*1024)throw std::runtime_error("I2EC: неверный размер runtime");
        std::vector<unsigned char> data(static_cast<size_t>(size.QuadPart));DWORD read{};
        if(!ReadFile(handle,data.data(),static_cast<DWORD>(data.size()),&read,nullptr)||read!=data.size())throw std::runtime_error("I2EC: runtime не прочитан");
        BCRYPT_ALG_HANDLE algorithm{};
        if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw std::runtime_error("I2EC: SHA256 недоступен");
        std::array<unsigned char,32> digest{};
        const auto result=BCryptHash(algorithm,nullptr,0,data.data(),static_cast<ULONG>(data.size()),digest.data(),static_cast<ULONG>(digest.size()));
        BCryptCloseAlgorithmProvider(algorithm,0);
        if(result<0)throw std::runtime_error("I2EC: ошибка SHA256");
        constexpr char hex[]="0123456789ABCDEF";std::string actual;
        for(const auto byte:digest){actual+=hex[byte>>4];actual+=hex[byte&15];}
        if(actual!=expected)throw std::runtime_error("I2EC: SHA256 PawnIO runtime не совпадает");
        return data;
    }
};
}
struct PawnLpcIo::Impl {
    using Open=HRESULT(WINAPI*)(void**);
    using Load=HRESULT(WINAPI*)(void*,const void*,size_t);
    using Execute=HRESULT(WINAPI*)(void*,const char*,const std::uint64_t*,size_t,std::uint64_t*,size_t,size_t*);
    using Close=HRESULT(WINAPI*)(void*);
    File dllFile,moduleFile;HMODULE library{};void* module{};Execute execute{};Close close{};std::uint16_t configBase{0x2e};bool traceOperations{true};
    ~Impl(){if(module&&close)close(module);if(library)FreeLibrary(library);}
    void prepare(){
        const auto folder=embeddedRuntimeDirectory("PawnIO");
        // FileShare.Read keeps each pinned file from being replaced between
        // hash verification and loading; the signed modules are unmodified.
        dllFile.pinned(folder/"PawnIOLib.dll","D71F62627D66983BB9F5B1C269F27BCD8C1B8A46E794377A6330F84C198F4443");
        const auto blob=moduleFile.pinned(folder/"LpcIO.bin","B3896A1CAB0D808FCA31FE2EBCAE045D59DAC690DA87B17C858BB8DA357EB45E");
        library=LoadLibraryExW((folder/"PawnIOLib.dll").c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if(!library)throw std::runtime_error("I2EC: PawnIOLib.dll не загружена");
        const auto open=reinterpret_cast<Open>(GetProcAddress(library,"pawnio_open"));
        const auto load=reinterpret_cast<Load>(GetProcAddress(library,"pawnio_load"));
        execute=reinterpret_cast<Execute>(GetProcAddress(library,"pawnio_execute"));close=reinterpret_cast<Close>(GetProcAddress(library,"pawnio_close"));
        if(!open||!load||!execute||!close||FAILED(open(&module)))throw std::runtime_error("I2EC: PawnIO недоступен; нужны установленные драйвер и права администратора");
        if(FAILED(load(module,blob.data(),blob.size())))throw std::runtime_error("I2EC: подписанный модуль LpcIO не загружен");
    }
    std::uint64_t call(const char* name,std::initializer_list<std::uint64_t> input,bool output=false){
        const bool tracing=traceOperations&&audit::current()!=0;audit::Scope trace("pawnio.lpc",tracing?("\"method\":"+audit::quote(name)+",\"input_hex\":"+audit::quote(audit::hex({reinterpret_cast<const std::uint8_t*>(input.begin()),input.size()*8}))):"",0,tracing);
        std::uint64_t value{};size_t count{};
        if(FAILED(execute(module,name,input.begin(),input.size(),output?&value:nullptr,output?1:0,&count))||count!=(output?1u:0u))
            {audit::event("lpc_transport_failed","\"method\":"+audit::quote(name));throw std::runtime_error(std::string("I2EC: PawnIO ")+name+" отклонён");}
        if(tracing)trace.finish("transport_returned","\"value\":"+(output?std::to_string(value):"null")+",\"returned\":"+std::to_string(count));return value;
    }
};
PawnLpcIo::PawnLpcIo(bool traceOperations):impl_(std::make_unique<Impl>()){impl_->traceOperations=traceOperations;impl_->prepare();}
PawnLpcIo::~PawnLpcIo()=default;
void PawnLpcIo::selectSlot0(){impl_->call("ioctl_select_slot",{0});impl_->configBase=0x2e;}
void PawnLpcIo::selectSlot1(){impl_->call("ioctl_select_slot",{1});impl_->configBase=0x4e;}
std::uint8_t PawnLpcIo::readSuperIo(std::uint8_t index){return static_cast<std::uint8_t>(impl_->call("ioctl_superio_inb",{index},true));}
void PawnLpcIo::writeSuperIo(std::uint8_t index,std::uint8_t value){impl_->call("ioctl_superio_outb",{index,value});}
void PawnLpcIo::writeConfigPort(std::uint16_t port,std::uint8_t value){
    if(port!=impl_->configBase&&port!=impl_->configBase+1)throw std::invalid_argument("I2EC: порт вне выбранного PNPCFG slot");
    impl_->call("ioctl_pio_outb",{port,value});
}
}
