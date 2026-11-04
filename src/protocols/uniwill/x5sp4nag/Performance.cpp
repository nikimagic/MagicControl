#include "MagicControl/Performance.h"
#include "MagicControl/PortableSettings.h"
#include "MagicControl/EcAccess.h"
#include "MagicControl/EcRegister.h"
#include "MagicControl/DeviceProfile.h"
#include "PowerState.h"
#include <exception>
#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

namespace mc {
// POWER CONTRACT: POWER_MODES.md next to this source is mandatory before edits.
// SETs owns APU profile mask 0xB0 in 0x751, NEVER Fan Boost 0x40 or fan gates/tables.
// Override order on verified board 82/72/26, PM 0x5D0009:
//   0x783 (PL1) -> STAPM; 0x784 (PL2) -> Slow; 0x785 (PL4) -> Fast.
// This is NOT the usual display order STAPM/Fast/Slow. Zero returns firmware policy.
// 0x785 direct raw=W was observed only with 0x727.7 clear; do not toggle that byte.
namespace {
constexpr std::array<std::uint16_t, 3> overrides{ec::kPowerLimitSustained, ec::kPowerLimitShort, ec::kPowerLimitPeak};
// OEM preset triples (PL-style EC values), read-only here: Office 734..736,
// Balanced 730..732, Turbo 7A7..7A9. NOT calibrated SMU STAPM/Fast/Slow triples.
constexpr std::array<std::uint16_t, 3> tables{0x734, 0x730, 0x7A7};
constexpr std::array<std::uint8_t, 3> codes{ec::kPerformanceOffice, ec::kPerformanceBalanced, ec::kPerformanceTurbo};
std::optional<int> limit(std::uint8_t value) { return value > 0 && value<=200 ? std::optional<int>(value) : std::nullopt; }
EcPowerLimits readLimits(EcAccess& access, std::uint16_t first) {
    return {limit(access.readStable(first)), limit(access.readStable(first + 1)), limit(access.readStable(first + 2))};
}
}
EcPerformanceState readEcPerformance(EcAccess& access) {
    // Presets, requested overrides and a separate Wujie accepted-limit cache.
    // None is measured consumption or independent SMU acknowledgement.
    const EcAccess::Transaction transaction(access);
    const auto mode=access.readStable(ec::kPerformanceMode);
    std::array<EcPowerLimits,3> presets;
    for(std::size_t i=0;i<tables.size();++i)presets[i]=readLimits(access,tables[i]);
    std::array<std::uint8_t,3> custom;
    for(std::size_t i=0;i<overrides.size();++i)custom[i]=access.readStable(overrides[i]);
    auto state=x5sp4nag::powerState(mode,presets,custom);
    // Do not transplant46B to GM6/Honor/other devices: it has another meaning.
    if(!access.boardVerified()||deviceProfile(access.verifyBoard().board).id!="wu-jie-15-xpro")return state;
    try{
        if(access.readStable(ec::kBoardEcVersion)!=82||access.readStable(ec::kBoardSystemId)!=72||
           access.readStable(ec::kBoardProjectId)!=26)throw std::runtime_error("Wujie identity changed");
        const auto read=[&]{return std::array<std::uint8_t,3>{access.readStable(ec::kAcceptedStapm),
            access.readStable(ec::kAcceptedSlow),access.readStable(ec::kAcceptedFast)};};
        auto before=read();
        for(int attempt=0;attempt<3;++attempt){const auto after=read();
            if(before==after){
                if(std::any_of(after.begin(),after.end(),[](auto v){return v==0||v==255;}))
                    throw std::runtime_error("EC accepted limits unavailable (0/FF)");
                state.accepted={after[0],after[1],after[2]};state.acceptedValid=true;
                state.acceptedStatus="EC 0x46A/0x46B/0x46F · STAPM/Slow/Fast";return state;
            }
            before=after;
        }
        throw std::runtime_error("EC accepted limits changing; awaiting coherent snapshot");
    }catch(const std::exception& error){state.acceptedStatus=error.what();}
    return state;
}
PerformanceChange selectEcPerformance(EcAccess& access, int preset) {
    if (preset < 0 || preset >= static_cast<int>(codes.size())) throw std::invalid_argument("Неизвестный пресет производительности.");
    access.ensureCapability(DeviceFeature::PowerLimits, "uniwill-x5sp4nag");
    const EcAccess::Transaction transaction(access);
    if(!access.boardVerified()||access.readStable(ec::kBoardEcVersion)!=82||
       access.readStable(ec::kBoardSystemId)!=72||access.readStable(ec::kBoardProjectId)!=26)
        throw std::runtime_error("Native CPU policy requires verified WUJIE82/72/26.");
    const auto before = access.readStable(ec::kPerformanceMode);
    // SETs owns 0x751 mask 0xB0 only. Preserve Fan Boost bit 6 (0x40),
    // fan ownership and every fan table. No fan reactivation after a profile click.
    constexpr std::uint8_t profileMask = ec::kPerformancePresetMask | ec::kPerformanceBit5;
    const auto desired = static_cast<std::uint8_t>((before & ~profileMask) | codes[preset]);
    std::array<std::uint8_t, 3> previous{};
    for (std::size_t i = 0; i < overrides.size(); ++i) previous[i] = access.readStable(overrides[i]);
    std::vector<std::size_t> touched;
    bool modeAttempted{};
    try {
        // Selecting a stock profile removes APU overrides only. This is NOT
        // fan-table cleanup and must never call restoreNativeFanMode/reapply fans.
        for (std::size_t i = 0; i < overrides.size(); ++i) if (previous[i]) {
            touched.push_back(i);
            access.writeVerified(overrides[i], 0);
        }
        if (access.readStable(ec::kPerformanceMode) != before)
            throw std::runtime_error("Режим EC изменился во время применения; повторите выбор.");
        modeAttempted = true;
        if (before != desired) static_cast<void>(access.updateBitsVerified(ec::kPerformanceMode, profileMask, codes[preset]));
        const auto after = access.readStable(ec::kPerformanceMode);
        if (after != desired) throw std::runtime_error("EC не подтвердил пресет производительности.");
        for(const auto address:overrides)if(access.readStable(address)!=0)
            throw std::runtime_error("EC не подтвердил снятие всех CPU overrides.");
        return {before, after};
    } catch (...) {
        const auto failure = std::current_exception();
        std::string rollbackErrors;
        const auto restore = [&](std::uint16_t address, std::uint8_t expected, std::uint8_t original) {
            try {
                const auto actual = access.readStable(address);
                if (actual == expected && actual != original) access.writeVerified(address, original);
                else if (actual != original) throw std::runtime_error("настройка изменена извне");
            } catch (const std::exception& error) { rollbackErrors += " " + std::to_string(address) + ": " + error.what(); }
        };
        if (modeAttempted) restore(ec::kPerformanceMode, desired, before);
        for (auto it = touched.rbegin(); it != touched.rend(); ++it) restore(overrides[*it], 0, previous[*it]);
        if (!rollbackErrors.empty()) throw std::runtime_error("Пресет не применён; возврат настроек не подтверждён:" + rollbackErrors);
        std::rethrow_exception(failure);
    }
}
namespace {
PerformanceChange applyPowerOverrides(EcAccess& access, const std::array<int, 3>& watts,bool automatic,bool custom=false) {
    if(automatic&&watts!=std::array<int,3>{})throw std::invalid_argument("Auto requires zero overrides");
    if(!automatic)
    for (const int value : watts)
        if (value < 5 || value > kApuManualMaximum) throw std::invalid_argument("Лимиты мощности: допустимо 5–255 Вт.");
    const auto selected=custom?selectedCustomBase("wu-jie-15-xpro"):0;
    access.ensureCapability(DeviceFeature::PowerLimits, "uniwill-x5sp4nag");
    const EcAccess::Transaction transaction(access);
    if (!access.boardVerified() || access.readStable(ec::kBoardEcVersion) != 82 ||
        access.readStable(ec::kBoardSystemId) != 72 || access.readStable(ec::kBoardProjectId) != 26)
        throw std::runtime_error("Manual мощности доступен только для платы 82/72/26.");
    // Read only: 727 also contains fan/camera bits. Never force a scale mode.
    if (!automatic&&(access.readStable(ec::kControl727) & 0x80))
        throw std::runtime_error("Manual: масштаб PL4 ×2 пока не подтверждён; настройки не изменены.");
    std::array<std::uint8_t, 3> before{};
    for (std::size_t i = 0; i < overrides.size(); ++i) before[i] = access.readStable(overrides[i]);
    const auto modeBefore=custom?access.readStable(ec::kPerformanceMode):std::uint8_t{};
    if(custom&&(modeBefore&ec::kPerformancePresetMask)==ec::kPerformancePresetMask)
        throw std::runtime_error("Custom: неизвестная исходная CPU-политика");
    constexpr std::uint8_t cpuMask=0xB0;
    // Owner-selected Custom base: Turbo, independent of the prior profile.
    // Only CPU profile bits change; Fan Boost and FAN ownership stay untouched.
    const std::uint8_t customBase=selected?ec::kPerformanceTurbo:0;
    bool modeAttempted{};
    std::vector<std::size_t> touched;
    try {
        // OEM uses base00; this explicit application policy chooses Turbo.
        // Boost40 and RAM/custom/presence gates remain with the fan owner.
        if(custom&&access.readStable(ec::kPerformanceMode)!=modeBefore)
            throw std::runtime_error("Custom: режим изменился до применения");
        if(custom&&(modeBefore&cpuMask)!=customBase){
            modeAttempted=true;static_cast<void>(access.updateBitsVerified(ec::kPerformanceMode,cpuMask,customBase));
        }
        // Register order is STAPM, SLOW, FAST. No gate/PWM/table writes.
        for (std::size_t i = 0; i < overrides.size(); ++i) if (before[i] != watts[i] || modeAttempted) {
            touched.push_back(i);
            access.writeVerified(overrides[i], static_cast<std::uint8_t>(watts[i]));
        }
        if(automatic)for(const auto address:overrides)if(access.readStable(address)!=0)
            throw std::runtime_error("EC не подтвердил Auto: overrides не обнулены");
        const auto after=custom?access.readStable(ec::kPerformanceMode):std::uint8_t{};
        if(custom&&after!=static_cast<std::uint8_t>((modeBefore&~cpuMask)|customBase))
            throw std::runtime_error("Custom: режим изменился во время применения");
        if(custom)for(std::size_t i=0;i<overrides.size();++i)
            if(access.readStable(overrides[i])!=watts[i])throw std::runtime_error("Custom: EC не подтвердил запрос лимитов");
        return {modeBefore,after};
    } catch (...) {
        const auto failure = std::current_exception();
        std::string errors;
        for (auto it = touched.rbegin(); it != touched.rend(); ++it) {
            const auto i = *it;
            try {
                const auto actual = access.readStable(overrides[i]);
                if (actual != before[i] && actual != watts[i]) throw std::runtime_error("изменено извне");
                if (actual != before[i]) access.writeVerified(overrides[i], before[i]);
            } catch (const std::exception& error) { errors += " " + std::to_string(overrides[i]) + ": " + error.what(); }
        }
        if(modeAttempted)try{
            const auto actual=access.readStable(ec::kPerformanceMode);
            if((actual&cpuMask)==customBase)static_cast<void>(access.updateBitsVerified(ec::kPerformanceMode,cpuMask,modeBefore&cpuMask));
            else if((actual&cpuMask)!=(modeBefore&cpuMask))throw std::runtime_error("CPU-политика изменена извне");
        }catch(const std::exception& error){errors+=" mode751: "+std::string(error.what());}
        if (!errors.empty()) throw std::runtime_error("Manual: ошибка возврата мощности:" + errors);
        std::rethrow_exception(failure);
    }
}
}
void applyManualPowerLimits(EcAccess& access,const std::array<int,3>& watts){static_cast<void>(applyPowerOverrides(access,watts,false));}
PerformanceChange applyCustomPowerLimits(EcAccess& access,const std::array<int,3>& watts){return applyPowerOverrides(access,watts,false,true);}
PerformanceChange selectWujieCustomBase(EcAccess& access,int base){
    if(base!=0&&base!=1)throw std::invalid_argument("Custom Base: invalid selection");
    access.ensureCapability(DeviceFeature::PowerLimits,"uniwill-x5sp4nag");
    const EcAccess::Transaction transaction(access);
    if(!access.boardVerified()||access.readStable(ec::kBoardEcVersion)!=82||access.readStable(ec::kBoardSystemId)!=72||access.readStable(ec::kBoardProjectId)!=26)
        throw std::runtime_error("Custom Base: Wujie identity not confirmed");
    const auto before=access.readStable(ec::kPerformanceMode);constexpr std::uint8_t mask=0xB0;
    if((before&mask)!=0&&(before&mask)!=0x10)throw std::runtime_error("Custom Base: active base is unknown");
    const auto desired=static_cast<std::uint8_t>(base?0x10:0);
    try{
        if((before&mask)!=desired)static_cast<void>(access.updateBitsVerified(ec::kPerformanceMode,mask,desired));
        const auto after=access.readStable(ec::kPerformanceMode);
        if(after!=static_cast<std::uint8_t>((before&~mask)|desired))throw std::runtime_error("Custom Base: EC readback mismatch");
        return {before,after};
    }catch(const std::exception& failure){
        const std::string reason=failure.what();
        try{
            const auto actual=access.readStable(ec::kPerformanceMode);
            if((actual&mask)==desired&&(actual&mask)!=(before&mask))static_cast<void>(access.updateBitsVerified(ec::kPerformanceMode,mask,before&mask));
            else if((actual&mask)!=(before&mask))throw std::runtime_error("CPU base changed externally");
        }catch(const std::exception& rollback){throw std::runtime_error(reason+"; Custom Base rollback unconfirmed: "+rollback.what());}
        throw std::runtime_error(reason+"; Custom Base restored");
    }
}
void resetManualPowerLimits(EcAccess& access){static_cast<void>(applyPowerOverrides(access,{0,0,0},true));}
} // namespace mc
