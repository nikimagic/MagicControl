#pragma once
#include <functional>

namespace mc {
class EcAccess;
// Exact Wujie APP consumers. A latched Copilot request matters even when APP
// is currently absent; otherwise a later presence restore could re-enable it.
struct ApplicationPresenceState {
    bool present{},ramFans{},customFans{},fanOverride{},fanBoost{},copilot{};
    [[nodiscard]] bool hasConsumers() const noexcept {
        return ramFans||customFans||fanOverride||fanBoost||copilot;
    }
};
ApplicationPresenceState readApplicationPresence(EcAccess&);
// Explicit Default only. One core operation for GUI/CLI: return FAN gates to
// firmware, disable native Copilot, verify all consumers, release APP LAST,
// and save Copilot Off. Never change PL values, RAM curves or Custom slots.
// Internal recovery/diagnostic FAN cleanup must keep its narrower contract.
// The platform binds disableCopilot to the existing copilot-lock resource;
// core never creates a second native/OS Copilot writer.
void resetApplicationControl(EcAccess&,const std::function<void()>& disableCopilot,
                             const std::function<void()>& checkState={});
}
