#pragma once
#include "MagicControl/ActionLog.h"
#include <algorithm>
#include "../EmbeddedRuntimeWindows.h"
#include "MagicControl/SensorDecoders.h"
#include "../../../protocols/jedec/Spd5118.h"
#include "MagicControl/Logging.h"
#include "BinaryHashWindows.h"
#include <windows.h>
#include <bcrypt.h>
#include <string>
#include <string_view>
#include <initializer_list>
#include <array>
#include <cstdint>
#include <fstream>
#include <filesystem>
#include <format>
#include <iterator>
#include <limits>
#include <optional>
#include <stdexcept>
#include <vector>

namespace mc::windows::detail {
class Module {
    using Open = HRESULT (WINAPI*)(void**);
    using Load = HRESULT (WINAPI*)(void*, const void*, std::size_t);
    using Execute = HRESULT (WINAPI*)(void*, const char*, const std::uint64_t*, std::size_t,
                                    std::uint64_t*, std::size_t, std::size_t*);
    using Close = HRESULT (WINAPI*)(void*);
    HMODULE library_{}; void* handle_{}; Execute execute_{}; Close close_{};
    std::array<bool,2> hpbsdSpdLogged_{};
    std::uint64_t call(const char* name, std::initializer_list<std::uint64_t> input, std::size_t count = 1, std::size_t index = 0) {
        const bool tracing=audit::current()!=0;audit::Scope trace("pawnio.module_call",tracing?("\"method\":"+audit::quote(name)+",\"input_hex\":"+audit::quote(audit::hex({reinterpret_cast<const std::uint8_t*>(input.begin()),input.size()*8}))):"",0,tracing);
        std::array<std::uint64_t, 5> output{}; std::size_t returned{};
        if(count>output.size()||index>=count)throw std::invalid_argument("PawnIO output size outside typed contract");
        const HRESULT result = execute_(handle_, name, input.begin(), input.size(), output.data(), count, &returned);
        if (FAILED(result) || returned < count) throw std::runtime_error(result == E_ACCESSDENIED ? "PawnIO: требуется запуск от администратора" :
            "PawnIO: "+std::string(name)+", HRESULT="+std::to_string(static_cast<unsigned long>(result))+", returned="+std::to_string(returned));
        if(tracing)trace.finish("transport_returned","\"hresult\":"+std::to_string(static_cast<unsigned long>(result))+",\"returned\":"+std::to_string(returned)+",\"output_hex\":"+audit::quote(audit::hex({reinterpret_cast<const std::uint8_t*>(output.data()),std::min(returned,output.size())*8})));return output[index];
    }
public:
    ~Module() { reset(); }
    void reset() {
        if (handle_ && close_) close_(handle_);
        handle_ = nullptr; execute_ = nullptr; close_ = nullptr;
        if (library_) FreeLibrary(library_);
        library_ = nullptr;
    }
    bool ready() const { return handle_ != nullptr; }
    template<std::size_t N>std::array<std::uint64_t,N> pchRead(const char* name) {
        static_assert(N==2||N==5);
        // Only the two signed read-only PCH module entry points are exposed.
        if((N==2&&std::string_view(name)!="ioctl_read_raw")||(N==5&&std::string_view(name)!="ioctl_identity"))
            throw std::invalid_argument("PCH read outside typed contract");
        std::array<std::uint64_t,N> output{};std::size_t returned{};
        const auto hr=execute_(handle_,name,nullptr,0,output.data(),N,&returned);
        if(FAILED(hr)||returned!=N)throw std::runtime_error("PawnIO PCH: "+std::string(name)+", HRESULT="+std::to_string(static_cast<unsigned long>(hr))+", returned="+std::to_string(returned));
        return output;
    }
    void open(const wchar_t* moduleName, std::string_view expectedHash = {}) {
        reset();
        const auto directory = embeddedRuntimeDirectory("PawnIO");
        const auto name=std::filesystem::path(moduleName).string();
        const auto failure=[&](std::string stage,std::optional<HRESULT> hr={},unsigned long win32=0){
            return std::runtime_error(std::format("PawnIO module={} stage={} HRESULT={} Win32={} expected_sha256={}",
                name,stage,hr?std::format("0x{:08X}",static_cast<std::uint32_t>(*hr)):"not-returned",win32,expectedHash));
        };
        try{
        library_ = LoadLibraryExW((directory / "PawnIOLib.dll").c_str(), nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!library_) throw failure("LoadLibraryEx PawnIOLib.dll",{},GetLastError());
        const auto open = reinterpret_cast<Open>(GetProcAddress(library_, "pawnio_open"));
        const auto load = reinterpret_cast<Load>(GetProcAddress(library_, "pawnio_load"));
        execute_ = reinterpret_cast<Execute>(GetProcAddress(library_, "pawnio_execute"));
        close_ = reinterpret_cast<Close>(GetProcAddress(library_, "pawnio_close"));
        if (!open || !load || !execute_ || !close_) throw failure("required export missing",{},GetLastError());
        const HRESULT result = open(&handle_);
        if (FAILED(result)) throw failure(result==E_ACCESSDENIED?"pawnio_open: elevation required":"pawnio_open",result);
        std::ifstream file(directory / moduleName, std::ios::binary);
        std::vector<char> blob{std::istreambuf_iterator<char>(file), {}};
        if (blob.empty() || blob.size() > 1024 * 1024) throw failure("module missing/empty/over1MiB");
        if (!expectedHash.empty()) {
            const auto actual=binarySha256(blob);
            if(actual!=expectedHash)throw failure("module SHA256 mismatch actual="+actual);
        }
        // Keep the actual module-load HRESULT. A running driver and an existing
        // signed blob do not identify which module initialization check failed.
        const auto loaded=load(handle_,blob.data(),blob.size());
        if(FAILED(loaded))throw failure("pawnio_load bytes="+std::to_string(blob.size()),loaded);
        }catch(...){reset();throw;}
    }
    void verifySmbus() {
        if ((call("ioctl_identity", {}, 3, 2) & 0xFFFFFFFF) != 0x790B1022)
            throw std::runtime_error("PawnIO: ожидался SMBus AMD 1022:790B");
    }
    void verifyIntelSmbus() {
        if((call("ioctl_identity",{},3,2)&0xffff)!=0x8086)
            throw std::runtime_error("PawnIO: ожидался SMBus Intel 8086");
    }
    bool spd5118(std::uint8_t address) {
        if(address<0x50||address>0x57)return false;
        return call("ioctl_smbus_xfer",{address,1,0,3})==0x1851;
    }
    // DDR4 SPD page 0 only; never switch EEPROM pages. TSOD shares address pins.
    bool ddr4SpdThermal(std::uint8_t address){
        if(address<0x50||address>0x57)return false;
        const auto type=call("ioctl_smbus_xfer",{address,1,2,2});
        return (type==0x0c||type==0x0e) && (call("ioctl_smbus_xfer",{address,1,14,2})&0x80);
    }
    std::optional<double> ddr4TsodTemperature(std::uint8_t spd){
        if(spd<0x50||spd>0x57)return {};
        const auto address=static_cast<std::uint8_t>(0x18+(spd-0x50));
        const auto read=[&](unsigned reg){const auto raw=static_cast<std::uint16_t>(call("ioctl_smbus_xfer",{address,1,reg,3}));return static_cast<std::uint16_t>((raw>>8)|(raw<<8));};
        const auto cap=read(0),config=read(1),vendor=read(6),device=read(7);
        if((cap&0xff00)||(config&0xf820)||(config&0x0100))return {};
        struct Id{std::uint16_t vendor,id,mask;};
        static constexpr Id ids[]={
            {0x11d4,0x0801,0xffff},{0x001f,0x8201,0xffff},{0x1114,0x2200,0xff00},
            {0x1c68,0x2200,0xff00},{0x132d,0x3300,0xff00},{0x00b3,0x2200,0xff00},
            {0x00b3,0x2900,0xff00},{0x00b3,0x3000,0xff00},{0x004d,0x3e00,0xffff},
            {0x0054,0x0200,0xfffc},{0x0054,0x0400,0xfffc},{0x0054,0x2000,0xfffc},
            {0x0054,0x2100,0xfffc},{0x0054,0x2200,0xff00},{0x0054,0x0000,0xfffe},
            {0x1131,0xa200,0xfffc},{0x1131,0xa100,0xfffc},{0x1b09,0x0800,0xffe0},
            {0x1b09,0x0a00,0xfff0},{0x1b09,0x2200,0xff00},{0x1c85,0x2200,0xff00},
            {0x104a,0x0101,0xffff},{0x104a,0x0000,0xfffe},{0x104a,0x0300,0xffff},
            {0x104a,0x2200,0xff00},{0x104a,0x0200,0xffff}};
        bool known=false;for(const auto& id:ids)if(vendor==id.vendor&&(device&id.mask)==id.id){known=true;break;}
        if(!known||((device&0xff00)==0x2200&&(cap&0x62)!=0x62))return {};
        const auto temperature=read(5);if(temperature==0xffff)return {};
        int raw=temperature&0x1fff;if(raw&0x1000)raw-=0x2000;
        if(raw<0&&!(cap&0x04))return {};
        const double celsius=raw/16.0;return celsius>=-40&&celsius<=125?std::optional<double>(celsius):std::nullopt;
    }
    void verifyPort() {
        if (call("ioctl_piix4_port_sel", {std::numeric_limits<std::uint64_t>::max()}) != 0)
            throw std::runtime_error("PawnIO: порт SMBus отличается от подтверждённого порта 0");
    }
    std::optional<double> temperature(std::uint8_t address) {
        if (address < 0x50 || address > 0x57) return {};
        const auto read = [&](std::uint8_t reg, bool word) {
            return call("ioctl_smbus_xfer", {address, 1, reg, word ? 3u : 2u});
        };
        const auto type = read(0, true), capability = read(5, false), config = read(0x1A, false);
        if (type != 0x1851 || !(capability & 2) || (capability & 0xFC) || (config & 1)) return {};
        return spd5118TemperatureCelsius(static_cast<std::uint16_t>(read(0x31,true)));
    }
    // Caller holds Access_SMBUS and admits exact HPBSD/BIOS/CPUID. No EEPROM,
    // MR26, thresholds or bus mux writes; only the observed MR11 7->0->7.
    std::optional<double> hpbsdTemperature(std::uint8_t address) {
        if(address!=0x50&&address!=0x51)throw std::invalid_argument("HPBSD: unreviewed SPD address");
        const bool verbose=!hpbsdSpdLogged_[address-0x50]||audit::current()!=0;
        const auto result=spd5118::temperatureWithPage7Recovery(
            [&](std::uint8_t reg,bool word){return call("ioctl_smbus_xfer",{address,1,reg,word?3u:2u});},
            [&](std::uint8_t page){
                if(page!=0&&page!=7)throw std::invalid_argument("HPBSD: unreviewed SPD page");
                if(verbose)logMessage("hpbsd_spd",std::format("MR11 address=0x{:02X} selector={} requested; EEPROM/config writes=0",address,page));
                const std::uint64_t input[]{address,0,0xb,2,page};std::size_t returned{};
                const auto hr=execute_(handle_,"ioctl_smbus_xfer",input,5,nullptr,0,&returned);
                if(FAILED(hr)||returned!=0)throw std::runtime_error("HPBSD: SPD page transfer failed");
                if(call("ioctl_smbus_xfer",{address,1,0xb,2})!=page)throw std::runtime_error("HPBSD: SPD page readback failed");
                if(verbose)logMessage("hpbsd_spd",std::format("MR11 address=0x{:02X} selector={} confirmed",address,page));
            });
        if(result)hpbsdSpdLogged_[address-0x50]=true;
        return result;
    }
    std::uint64_t intelMsr(std::uint32_t address) {
        switch (address) {
        case 0x1B1: case 0x1A2: case 0x601: case 0x606: case 0x610: case 0x611: case 0x613: case 0x639: return call("ioctl_read_msr", {address});
        default: throw std::invalid_argument("Intel MSR outside monitoring allowlist");
        }
    }
    // Intel Raptor Lake DDR telemetry only: no arbitrary MCHBAR access.
    std::uint32_t intelMemoryStatus(){return static_cast<std::uint32_t>(call("ioctl_read_dword",{0x5918}));}
    std::uint32_t intelMemoryBiosData(){return static_cast<std::uint32_t>(call("ioctl_read_dword",{0x5e04}));}
    // Read-only Intel package RAPL MMIO limit registers (not MSR 0x610).
    std::uint32_t intelPackageMmioLimit1(){return static_cast<std::uint32_t>(call("ioctl_read_dword",{0x59a0}));}
    std::uint32_t intelPackageMmioLimit2(){return static_cast<std::uint32_t>(call("ioctl_read_dword",{0x59a4}));}
    std::uint64_t energyUnits() { return call("ioctl_read_msr", {0xC0010299}); }
    std::uint32_t energyCounter() { return static_cast<std::uint32_t>(call("ioctl_read_msr", {0xC001029B})); }
    std::uint32_t coreEnergyCounter() { return static_cast<std::uint32_t>(call("ioctl_read_msr", {0xC001029A})); }
    std::uint32_t temperatureRegister() { return static_cast<std::uint32_t>(call("ioctl_read_smn", {0x59800})); }
    // Read-only UMC registers. The caller owns Access_PCI and CPU/type admission.
    std::uint32_t memoryStateRegister(unsigned channel){
        if(channel>1)throw std::invalid_argument("UMC channel outside reviewed range");
        return static_cast<std::uint32_t>(call("ioctl_read_smn",{0x50D6Cu+(channel<<20)}));
    }
    std::uint32_t memoryClockRegister(unsigned channel,unsigned pstate){
        if(channel>1||pstate>3)throw std::invalid_argument("UMC clock outside reviewed range");
        return static_cast<std::uint32_t>(call("ioctl_read_smn",{0x50200u+(channel<<20)+(pstate<<8)}));
    }
};

}
