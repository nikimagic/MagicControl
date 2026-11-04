#pragma once
#include "MagicControl/BoardIdentity.h"
#include "MagicControl/DeviceProfile.h"
#include "MagicControl/ResourceControl.h"
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace mc {
inline bool overclockObservationSupported(const BoardDescriptor& board){
    return board.manufacturer=="MECHREVO"&&board.baseBoard=="WUJIE Series-X5SP4NAG"&&deviceProfile(board).id=="wu-jie-15-xpro";
}
struct OverclockFirmware {
    bool readable{};std::string source,status;
    std::optional<std::uint8_t> project,version,cpuSupport,memorySupport,memorySwitch,recovery,menuSupport,menuSwitch;
    std::optional<std::uint32_t> frequency,frequencyMinimum,frequencyMaximum,voltage,voltageMinimum,voltageMaximum;
};
// The reviewed Wujie 5.57 managed layout is 180+ bytes. These are raw FwVars
// and capability flags, not measurements or permission for firmware writes.
inline OverclockFirmware decodeWujieOverclock(std::span<const std::uint8_t> bytes){
    OverclockFirmware result;
    if(bytes.size()<180){result.status="Переменная слишком короткая; формат не подтверждён";return result;}
    if(bytes[6]!=26){result.status="Project ID в переменной не соответствует Вуджи";return result;}
    const auto word=[&](std::size_t i){return std::uint32_t(bytes[i])|(std::uint32_t(bytes[i+1])<<8)|(std::uint32_t(bytes[i+2])<<16)|(std::uint32_t(bytes[i+3])<<24);};
    result.readable=true;result.project=bytes[6];result.version=bytes[0x43];result.cpuSupport=bytes[0x42];
    result.memorySupport=bytes[0x60];result.memorySwitch=bytes[0x33];result.recovery=bytes[0x5c];
    result.menuSupport=bytes[0x6e];result.menuSwitch=bytes[0x6f];
    result.frequency=word(0x44);result.frequencyMaximum=word(0x48);result.frequencyMinimum=word(0x4c);
    result.voltage=word(0x50);result.voltageMaximum=word(0x54);result.voltageMinimum=word(0x58);
    result.status="UEFI: прочитано; аппаратное применение не подтверждено";return result;
}
OverclockFirmware readOverclockFirmware(const BoardDescriptor& actual);
using OverclockRows=std::vector<std::pair<std::string,std::string>>;
OverclockRows overclockRows(const OverclockFirmware&);
// Same process-owned observation lane on both OS; the UI cannot supply a
// firmware address, write request, or backend lambda.
class OverclockOperations {
public:
    ~OverclockOperations(){worker_.join();}
    bool refresh(std::function<void()> ready);
    bool take();
    const OverclockFirmware& snapshot()const noexcept{return current_;}
    bool busy()const noexcept{return worker_.busy();}
private:
    ResourceWorker worker_;OverclockFirmware current_{},pending_{};
};
}
