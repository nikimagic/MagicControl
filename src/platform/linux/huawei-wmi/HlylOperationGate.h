#pragma once
#include <mutex>
namespace mc::linux_platform {
// One process-local HLYL gate shared by individual backend calls and an outer
// charge-policy transaction. It does not exclude other processes or firmware.
class HlylOperationGate {
public:
    void lock(){mutex_.lock();}
    void unlock(){mutex_.unlock();}
private:
    std::recursive_mutex mutex_;
};
}
