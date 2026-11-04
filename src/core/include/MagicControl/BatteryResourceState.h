#pragma once
#include "MagicControl/Monitoring.h"
#include "MagicControl/ResourceCatalog.h"

namespace mc {
// The accepted battery sample is converted once. Drawing never rebuilds a
// comparison snapshot and never selects between two numeric sources.
class BatteryResourceState {
public:
    void update(const BoardDescriptor& board,const BatteryReading& battery) noexcept {
        try{MonitoringSnapshot input;input.battery=battery;state_.update(board,input);}
        catch(...){state_.clear();}
    }
    void clear() noexcept {state_.clear();}
    [[nodiscard]] ResourcePresentationView view() const noexcept {return ResourcePresentationView(state_);}
private:
    ResourceState state_;
};
}
