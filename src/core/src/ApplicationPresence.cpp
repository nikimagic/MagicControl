#include "MagicControl/ApplicationPresence.h"
#include "MagicControl/ActionLog.h"
#include "MagicControl/EcAccess.h"
#include "MagicControl/EcRegister.h"
#include "MagicControl/Errors.h"
#include "MagicControl/FanCurve.h"
#include "MagicControl/KeysLifecycle.h"
#include <array>
#include <exception>
#include <stdexcept>

namespace mc {
namespace {
void admit(EcAccess& ec){
    if(deviceProfile(ec.verifyBoard().board).id!="wu-jie-15-xpro"||
       ec.readStable(ec::kBoardEcVersion)!=82||ec.readStable(ec::kBoardSystemId)!=72||
       ec.readStable(ec::kBoardProjectId)!=26)
        throw BoardNotVerified("APP Default: exact Wujie identity not confirmed");
}
constexpr std::array<std::pair<std::uint16_t,std::uint8_t>,6> fields{{
    {ec::kRamTableCommit,ec::kRamTableCommitBit},
    {ec::kCustomMode,ec::kCustomModeBit},
    {ec::kControl727,ec::kControl727FanBit},
    {ec::kPerformanceMode,ec::kPerformanceBoostBit},
    {0x728,4},
    {ec::kAppPresence,ec::kAppPresenceBit}
}};
}
ApplicationPresenceState readApplicationPresence(EcAccess& ec){
    const EcAccess::Transaction transaction(ec);admit(ec);
    ApplicationPresenceState s;
    s.present=(ec.readStable(ec::kAppPresence)&ec::kAppPresenceBit)!=0;
    s.ramFans=(ec.readStable(ec::kRamTableCommit)&ec::kRamTableCommitBit)!=0;
    s.customFans=(ec.readStable(ec::kCustomMode)&ec::kCustomModeBit)!=0;
    s.fanOverride=(ec.readStable(ec::kControl727)&ec::kControl727FanBit)!=0;
    s.fanBoost=(ec.readStable(ec::kPerformanceMode)&ec::kPerformanceBoostBit)!=0;
    s.copilot=(ec.readStable(0x728)&4)!=0;
    return s;
}
void resetApplicationControl(EcAccess& ec,const std::function<void()>& disableCopilot,const std::function<void()>& checkState){
    audit::Scope trace("application.default");
    const EcAccess::Transaction transaction(ec);admit(ec);ec.ensureWritable();
    if(!disableCopilot)throw std::invalid_argument("Default: copilot-lock resource is not bound");
    const auto check=[&]{if(checkState)checkState();};check();
    const auto before=readApplicationPresence(ec);
    audit::event("app_consumers_before","\"app\":"+std::to_string(before.present)+
        ",\"ram_fans\":"+std::to_string(before.ramFans)+",\"custom_fans\":"+std::to_string(before.customFans)+
        ",\"fan_override\":"+std::to_string(before.fanOverride)+",\"boost\":"+std::to_string(before.fanBoost)+
        ",\"copilot\":"+std::to_string(before.copilot));
    std::array<std::uint8_t,fields.size()> saved{};
    for(std::size_t i=0;i<fields.size();++i)saved[i]=ec.readStable(fields[i].first)&fields[i].second;
    const auto power=[&]{return std::array<std::uint8_t,4>{
        static_cast<std::uint8_t>(ec.readStable(ec::kPerformanceMode)&0xb0),
        ec.readStable(ec::kPowerLimitSustained),ec.readStable(ec::kPowerLimitShort),ec.readStable(ec::kPowerLimitPeak)};};
    const auto powerBefore=power();
    try{
        check();restoreNativeFanMode(ec);
        check();disableCopilot(); // Reuse the one admitted copilot-lock resource.
        check();
        if(readApplicationPresence(ec).hasConsumers())
            throw std::runtime_error("Default: APP consumer reset was not confirmed");
        // Owner instruction30October2026: APP is released only after EVERY
        // admitted consumer is cleared. Preserve every other bit of741.
        static_cast<void>(ec.updateBitsVerified(ec::kAppPresence,ec::kAppPresenceBit,0));
        check();const auto after=readApplicationPresence(ec);
        if(after.present||after.hasConsumers())throw std::runtime_error("Default: APP release was not confirmed");
        if(power()!=powerBefore)throw std::runtime_error("Default: CPU policy/limits changed during APP release; result not confirmed");
        // KeysLifecycle reloads this dedicated preference under DeviceOperation.
        // Persist after readback so restart/resume cannot re-enable Copilot.
        std::string error;
        if(!rememberKeyPreference(KeyAction::Copilot,false,error))
            throw std::runtime_error("Default: Copilot Off could not be saved: "+error);
        trace.finish("readback_confirmed","\"app\":0,\"consumers\":0,\"copilot_saved\":false,\"power_preserved\":true,\"power_writes\":0,\"table_writes\":0");
    }catch(...){
        const auto failure=std::current_exception();
        // Keep a suspend/epoch interruption typed for the owning worker. Never
        // turn it into a generic failure that would trigger writes after sleep.
        // Raw masked rollback also preserves dormant728.2 with original APP=0.
        for(std::size_t i=0;i<fields.size();++i){check();
            try{static_cast<void>(ec.updateBitsVerified(fields[i].first,fields[i].second,saved[i]));}
            catch(const std::exception& error){throw std::runtime_error(std::string("Default: rollback not confirmed: ")+error.what());}}
        std::rethrow_exception(failure);
    }
}
}
