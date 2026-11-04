#pragma once
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mc::diagnostics {
inline constexpr std::size_t maximumFirmwareArtifactBytes=16*1024*1024;
inline constexpr std::size_t maximumFirmwareTotalBytes=256*1024*1024;
inline constexpr std::size_t maximumFirmwareRecords=8192;
inline constexpr auto firmwareWorkerTimeout=std::chrono::seconds(30);
// OS evidence only: no BIOS/EC flash readout, AML evaluation, vendor DLL or
// write command. Names are generated ASCII relative paths, never source paths.
struct FirmwareRecord {
    std::string kind,name,source,status,errorDomain,metadataJson{"{}"};
    std::optional<std::uint32_t> errorCode,attributes;
    std::vector<std::uint8_t> bytes;
    bool success{},final{};
};
using FirmwareEmitter=std::function<void(const FirmwareRecord&)>;
struct FirmwareCaptureResult {bool finished{};std::string status;std::int64_t elapsedMs{};};
// Called only inside the already-isolated read-only worker. stateOnly limits
// the second pass to the same reviewed UEFI allowlist, not a caller-supplied name.
// Do not emit a final record: the shared fixed wire server owns completion.
void collectPlatformFirmware(bool stateOnly,const FirmwareEmitter& emit);
// Separate explicit ROM action, never included in ordinary EC/OS evidence.
void collectPlatformBiosRom(const FirmwareEmitter& emit);
inline constexpr std::size_t maximumBiosRomRecords=131080;
std::string firmwareSha256(std::span<const std::uint8_t> bytes);
bool firmwareValidUtf8(std::string_view value) noexcept;
inline std::string firmwareQuote(std::string_view value){
    constexpr char hex[]="0123456789abcdef";std::string out="\"";const bool valid=firmwareValidUtf8(value);
    for(const unsigned char c:value){
        if(c=='"'||c=='\\'){out+='\\';out+=static_cast<char>(c);}
        // Invalid native byte sequences remain lossless byte escapes, rather
        // than corrupting the UTF-8 manifest. Valid Unicode stays unchanged.
        else if(c<32||(!valid&&c>=128)){out+="\\u00";out+=hex[c>>4];out+=hex[c&15];}
        else out+=static_cast<char>(c);
    }
    return out+'"';
}
}
