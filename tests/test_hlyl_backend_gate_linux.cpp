#include "MagicControl/LaptopControl.h"
#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <stdexcept>

namespace mc::linux_platform {std::unique_ptr<LaptopControl> createHlylLockTestControl();}
namespace {void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}}
int main(){
    using namespace std::chrono_literals;
    auto first=mc::linux_platform::createHlylLockTestControl();
    auto second=mc::linux_platform::createHlylLockTestControl();
    check(first&&second&&first->chargePolicyId()=="huawei-hlyl-wxx9"&&second->chargePolicyId()=="huawei-hlyl-wxx9",
        "test controllers must expose the real typed policy");
    std::promise<void> held,release,attempted;auto ready=held.get_future();
    auto released=release.get_future().share();auto started=attempted.get_future();std::atomic<bool> nestedSafe{};
    auto owner=std::async(std::launch::async,[&]{first->withChargePolicyTransaction([&]{
        const auto observation=first->read();nestedSafe=!observation.connected&&!observation.chargeWritable;
        held.set_value();released.wait();});});
    const bool acquired=ready.wait_for(5s)==std::future_status::ready;
    if(!acquired){release.set_value();owner.wait();check(false,"nested real backend lock did not complete");}
    auto other=std::async(std::launch::async,[&]{attempted.set_value();try{second->setChargeThresholds(80,85);}
        catch(const std::runtime_error&){return true;}return false;});
    const bool began=started.wait_for(5s)==std::future_status::ready;
    const bool blocked=other.wait_for(100ms)==std::future_status::timeout;
    release.set_value();owner.get();const bool rejected=other.get();
    check(acquired&&began&&blocked&&nestedSafe&&rejected,"real backend instances must share the gate and refuse hardware");
    bool failed=false;try{first->withChargePolicyTransaction([&]{first->setChargeThresholds(80,85);});}
    catch(const std::runtime_error&){failed=true;}
    check(failed&&!second->read().chargeWritable,"exception must release the real backend gate");
}
