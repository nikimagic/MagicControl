#pragma once

#include "TelemetryGate.h"
#include <array>
#include <atomic>
#include <optional>
#include <utility>

namespace mc {
enum class GpuPresenceKind { Nvidia, Radeon, VbiosDump };

// Presentation cache for GPU-tab and VBIOS availability discovery. A
// skipped observation retains the last value; it never reports disappearance.
// This owner does not select a device or authorize a control/firmware action.
class GpuPresence {
    TelemetryGate& gate_;
    std::array<std::atomic<bool>,3> present_{};
public:
    explicit GpuPresence(TelemetryGate& gate=telemetryGate()) noexcept:gate_(gate){}
    template<class Reader> bool observe(GpuPresenceKind kind,Reader&& reader) noexcept {
        const auto index=kind==GpuPresenceKind::Nvidia?0u:kind==GpuPresenceKind::Radeon?1u:2u;
        std::optional<TelemetryGate::Observation> observation;
        try{observation=gate_.tryObservation();}catch(...){return present_[index].load(std::memory_order_relaxed);}
        if(!observation)return present_[index].load(std::memory_order_relaxed);
        bool value{};
        try{value=static_cast<bool>(std::forward<Reader>(reader)());}catch(...){}
        present_[index].store(value,std::memory_order_relaxed);
        return value;
    }
};

inline GpuPresence& gpuPresence(){static auto* owner=new GpuPresence;return *owner;}
template<class Owner,class Reader>
bool observeGpuPresence(Owner&& owner,GpuPresenceKind kind,Reader&& reader)noexcept{
    try{return std::forward<Owner>(owner)().observe(kind,std::forward<Reader>(reader));}
    catch(...){return false;}
}
}
