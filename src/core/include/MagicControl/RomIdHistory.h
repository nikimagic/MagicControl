#pragma once
#include <cstdint>
#include <array>
#include <exception>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace mc {
struct DeviceProfile;
// These addresses belong only to reviewed UWACPIDriver/Uniwill protocols.
// Installed drivers, generic EC support and preview selections grant no admission.
bool uniwillIdentityHistoryProfile(const DeviceProfile&) noexcept;
// Persistent UTF-8 JSON-lines files rom_id/platform_id/project_id in logs_<actual board>.
// Independent of AllLogs/BatteryLog; never rotate/truncate/rename/use as ownership.
void initializeRomIdHistory() noexcept;
void recordRomIdEvent(std::string_view event, std::optional<std::uint8_t> before,
    std::optional<std::uint8_t> requested, std::optional<std::uint8_t> observed,
    std::string_view result, std::string_view source, std::string_view detail = {}) noexcept;
// Existing stable reads only: first/recovered/changed values, no extra polling.
void observeRomId(std::uint8_t value, std::string_view source) noexcept;
void recordPlatformIdEvent(std::string_view event,std::optional<std::uint8_t> before,
    std::optional<std::uint8_t> requested,std::optional<std::uint8_t> observed,
    std::string_view result,std::string_view source,std::string_view detail={}) noexcept;
void recordIdentityByteEvent(std::uint16_t address,std::string_view event,std::optional<std::uint8_t> before,
    std::optional<std::uint8_t> requested,std::optional<std::uint8_t> observed,
    std::string_view result,std::string_view source,std::string_view detail={}) noexcept;
void observePlatformId(std::uint8_t value,std::string_view source) noexcept;
void observeProjectId(std::uint8_t value,std::string_view source) noexcept;
using IdentityRomBytes=std::array<std::optional<std::uint8_t>,16>;
void recordIdentitySnapshot(std::optional<std::uint8_t> platform,const IdentityRomBytes& rom,
    std::string_view event,std::string_view source,std::string_view result,std::string_view detail={},std::optional<std::uint8_t> project={}) noexcept;
void recordIdentityPolicyRequest(std::uint8_t platform,std::uint8_t rom,std::string_view source) noexcept;
// Context survives disabled main logging. Set it in the actual worker, not
// merely around dispatch. It identifies our caller, never an external actor.
class IdentityHistoryScope {
    IdentityHistoryScope* previous_{};std::string actor_,reason_,failure_;
    std::optional<std::uint8_t> platform_,rom_;std::uint64_t id_{};
    int exceptions_{};bool active_{},involved_{},failed_{};
public:
    IdentityHistoryScope(std::string_view actor,std::string_view reason,bool onlyIfUnset=false) noexcept;
    ~IdentityHistoryScope();
    IdentityHistoryScope(const IdentityHistoryScope&)=delete;
    IdentityHistoryScope& operator=(const IdentityHistoryScope&)=delete;
    std::string_view actor()const noexcept{return actor_;}
    std::string_view reason()const noexcept{return reason_;}
    std::uint64_t id()const noexcept{return id_;}
    void mark(std::optional<std::uint8_t> platform={},std::optional<std::uint8_t> rom={}) noexcept;
    void fail(std::string_view reason) noexcept;
};
// Best-effort observation only: exceptions never change apply/rollback flow.
// No write, restore or ownership admission.00/FF and service bytes stay raw.
void recordIdentityPairSnapshot(std::optional<std::uint8_t>,std::optional<std::uint8_t>,std::string_view event,std::string_view source) noexcept;
template<class Read>void recordIdentityReadSnapshot(Read&& read,std::string_view event,std::string_view source) noexcept {
    try{
        std::optional<std::uint8_t> project,platform;IdentityRomBytes rom;std::string error;
        const auto one=[&](std::uint16_t a,auto& value){try{value=read(a);return true;}catch(const std::exception& e){if(error.empty())error=e.what();}catch(...){if(error.empty())error="Unknown EC read error";}return false;};
        // Read actual740 before preferences/controller construction. Never use
        // the profile/registry ProjectID as a substitute for this observation.
        // Fail fast: a dead transport must not cause18consecutive timeouts.
        if(one(0x740,project)&&one(0x7C3,platform))for(unsigned i=0;i<rom.size();++i)if(!one(static_cast<std::uint16_t>(0x770+i),rom[i]))break;
        bool complete=project.has_value()&&platform.has_value();for(const auto& b:rom)complete=complete&&b.has_value();
        recordIdentitySnapshot(platform,rom,event,source,complete?"read_confirmed":"partial",error,project);
    }catch(...){}
}
// OS adapter: read before constructing controllers/restoring preferences.
// Only admitted Uniwill devices get files; --no-ec records unknown there.
// Other protocols and unknown devices create no identity-history files.
void recordStartupRomId(bool ecEnabled = true) noexcept;
namespace detail {
// Append one row under an OS file lock; flush before releasing it. OPEN_ALWAYS /
// O_APPEND only; never replace/truncate/remove. Errors stay diagnostic only.
bool appendHistoryRow(const std::filesystem::path&, std::string_view, std::string& error);
}
}
