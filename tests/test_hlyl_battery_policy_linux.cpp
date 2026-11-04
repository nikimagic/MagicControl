#include "MagicControl/LaptopControl.h"
#include "MagicControl/LaptopChargeResource.h"
#include "MagicControl/LaptopLightingResource.h"
#include "MagicControl/ControlAudit.h"
#include "../src/platform/linux/SettingsServicesLinux.h"
#include "../src/platform/linux/huawei-wmi/HlylOperationGate.h"
#include <stdexcept>
#include <string>
#include <vector>
#include <mutex>
#include <future>
#include <chrono>

namespace {
struct FakeControl final:mc::LaptopControl {
    mc::LaptopControlSnapshot snapshot;std::vector<std::string> calls;bool failWrite{};std::string_view policyId{"huawei-hlyl-wxx9"};
    std::shared_ptr<mc::linux_platform::HlylOperationGate> gate;
    explicit FakeControl(std::shared_ptr<mc::linux_platform::HlylOperationGate> shared=std::make_shared<mc::linux_platform::HlylOperationGate>()):gate(std::move(shared)){}
    std::string_view chargePolicyId() const noexcept override{return policyId;}
    void withChargePolicyTransaction(const std::function<void()>& action) override {std::lock_guard lock(*gate);action();}
    mc::LaptopControlSnapshot read() override {std::lock_guard lock(*gate);calls.push_back("read");return snapshot;}
    void setChargeThresholds(int start,int end) override {
        std::lock_guard lock(*gate);
        calls.push_back("write:"+std::to_string(start)+":"+std::to_string(end));
        if(failWrite)throw std::runtime_error("write failed");
        snapshot.chargeStart=start;snapshot.chargeEnd=end;
    }
    void setFnLock(bool) override {throw std::logic_error("unexpected Fn");}
    void setKeyboardLevel(int value) override {calls.push_back("level:"+std::to_string(value));}
    void setKeyboardTimeout(int value) override {calls.push_back("timeout:"+std::to_string(value));}
};
void check(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
}
int main(){
    const auto apply=[&](mc::LaptopControl& control,int upper,int restart,const std::function<int(int)>& select,const std::function<void(int)>& remember){
        return mc::executeBatteryResource(control,mc::BatteryWriteRequest::native(upper,restart),{select,remember,[]{},{}});
    };
    {
        FakeControl direct;
        mc::executeBatteryResource(direct,mc::BatteryWriteRequest::pair(0,100),{});
        check(direct.calls==std::vector<std::string>{"write:0:100"},"CLI pair route preserves both explicit values and does not add a policy read");
        direct.calls.clear();direct.failWrite=true;bool rejected=false;
        try{mc::executeBatteryResource(direct,mc::BatteryWriteRequest::pair(90,95),{});}catch(const std::runtime_error&){rejected=true;}
        check(rejected&&direct.calls==std::vector<std::string>{"write:90:95"},"charge resource propagates backend failures without retry");
    }
    FakeControl unknown;unknown.policyId="other";
    check(!mc::nativeBatteryPolicySupported(&unknown)&&!mc::nativeBatteryPolicySupported(nullptr),"only exact implemented backend selects native policy");

    auto typed=std::make_unique<FakeControl>();typed->policyId="huawei-hlyl-wxx9";
    typed->snapshot.chargeWritable=true;typed->snapshot.chargeEnd=95;auto* rawTyped=typed.get();
    auto audited=mc::auditedLaptopControl(std::move(typed));
    check(audited&&audited->chargePolicyId()=="huawei-hlyl-wxx9","audit wrapper preserves typed backend identity");
    int transactionCalls=0;audited->withChargePolicyTransaction([&]{++transactionCalls;});
    check(transactionCalls==1,"audit wrapper forwards charge transaction");
    apply(*audited,85,-1,[](int){return 80;},[](int){throw std::logic_error("unexpected save");});
    check(rawTyped->calls==std::vector<std::string>{"read","write:80:85"},"audited transaction permits nested backend calls");
    mc::LaptopControlSnapshot availability;availability.chargeWritable=true;
    check(!mc::linux_platform::batteryControlsAvailable(nullptr,availability),"missing backend hides battery actions");
    FakeControl other;other.policyId="unknown";
    check(!mc::linux_platform::batteryControlsAvailable(&other,availability),"unknown backend hides battery actions despite writable observation");
    {
        using namespace mc::linux_platform;
        SettingsWorkerRequest request;request.upper=85;request.restart=80;
        bool rejected=false;try{mc::executeBatteryResource(other,mc::BatteryWriteRequest::native(request.upper,request.restart));}catch(const std::runtime_error&){rejected=true;}
        check(rejected&&other.calls.empty(),"resource route does not let unknown backends borrow Honor charge policy");
        request={};request.keyboardLevel=2;request.keyboardTimeout=30;
        const auto lighting=mc::executeLightingResource(other,mc::LightingWriteRequest::nativeUpdate(request.keyboardLevel,request.keyboardTimeout));
        check(lighting.success&&other.calls==std::vector<std::string>{"level:2","timeout:30"},
            "combined lighting route preserves original setter order");
        other.calls.clear();request={};request.keyboardTimeout=15;
        check(mc::executeLightingResource(other,mc::LightingWriteRequest::nativeUpdate(request.keyboardLevel,request.keyboardTimeout)).success&&other.calls==std::vector<std::string>{"timeout:15"},
            "timeout-only route does not change brightness");
        other.calls.clear();request={};request.keyboardLevel=1;
        check(mc::executeLightingResource(other,mc::LightingWriteRequest::nativeUpdate(request.keyboardLevel,request.keyboardTimeout)).success&&other.calls==std::vector<std::string>{"level:1"},
            "brightness-only route does not change timeout");
        other.calls.clear();request={};rejected=false;
        try{mc::executeBatteryResource(other,mc::BatteryWriteRequest::native(request.upper,request.restart));}catch(const std::exception&){rejected=true;}
        check(rejected&&other.calls.empty(),"empty battery request cannot dispatch a write");
        rejected=false;try{mc::executeLightingResource(other,{static_cast<mc::LightingWrite>(42),1});}
        catch(const std::invalid_argument&){rejected=true;}
        check(rejected&&other.calls.empty(),"unknown typed lighting command cannot reach a backend");
    }
    check(mc::linux_platform::batteryControlsAvailable(audited.get(),availability),"Honor policy with writable observation is shown");
    availability.chargeWritable=false;
    check(!mc::linux_platform::batteryControlsAvailable(audited.get(),availability),"Honor policy alone never enables battery actions");
    {
        struct Unsupported final:mc::LaptopControl {int reads{};mc::LaptopControlSnapshot read() override{++reads;return {};}
            void setChargeThresholds(int,int) override{throw std::logic_error("unexpected write");}
            void setFnLock(bool) override{}} unsupported;
        bool rejected=false;try{apply(unsupported,95,90,[](int v){return v;},[](int){});}
        catch(const std::runtime_error&){rejected=true;}
        check(rejected&&unsupported.reads==0,"backend without transaction support cannot enter charge policy");
    }
    FakeControl control;control.snapshot.chargeWritable=true;control.snapshot.chargeEnd=95;
    int selected=80,selectionCalls=0,saved=-1;
    const auto select=[&](int){++selectionCalls;return selected;};
    const auto remember=[&](int value){saved=value;};
    auto message=apply(control,95,90,select,remember);
    check(!message.empty()&&control.calls==std::vector<std::string>{"read","write:90:95"}&&selectionCalls==0&&saved==90,"explicit restart ordering");
    control.calls.clear();saved=-1;message=apply(control,85,-1,select,remember);
    check(!message.empty()&&control.calls==std::vector<std::string>{"read","write:80:85"}&&selectionCalls==1&&saved==-1,"upper limit uses saved restart");
    control.calls.clear();message=apply(control,10,-1,select,remember);
    check(!message.empty()&&control.calls==std::vector<std::string>{"read","write:5:10"}&&selectionCalls==1&&saved==5,"legacy ten percent behavior");
    control.calls.clear();saved=-1;selected=70;message=apply(control,60,-1,select,remember);
    check(!message.empty()&&control.calls==std::vector<std::string>{"read","write:55:60"}&&saved==-1,"sixty percent restart cap");
    control.calls.clear();selected=85;bool failed=false;
    try{apply(control,85,-1,select,remember);}catch(const std::runtime_error&){failed=true;}
    check(failed&&control.calls==std::vector<std::string>{"read"},"invalid threshold must not write");
    control.calls.clear();control.snapshot.chargeWritable=false;failed=false;
    try{apply(control,95,90,select,remember);}catch(const std::runtime_error&){failed=true;}
    check(failed&&control.calls==std::vector<std::string>{"read"},"unwritable battery must not write");
    control.calls.clear();control.snapshot.chargeWritable=true;control.failWrite=true;saved=-1;failed=false;
    try{apply(control,95,90,select,remember);}catch(const std::runtime_error&){failed=true;}
    check(failed&&saved==-1&&control.calls==std::vector<std::string>{"read","write:90:95"},"failed backend write must not save");
    control.calls.clear();control.failWrite=false;failed=false;
    try{apply(control,95,90,select,[](int){throw std::runtime_error("save failed");});}
    catch(const std::runtime_error&){failed=true;}
    check(failed&&control.calls==std::vector<std::string>{"read","write:90:95"},"failed save reports error after attempted write");
    control.calls.clear();apply(control,95,90,select,remember);
    check(saved==90&&control.calls==std::vector<std::string>{"read","write:90:95"},"transaction releases lock after failure");
    {
        // Competing typed operations share the same process-local lock. Hold
        // the first transaction between its read and decision, then prove the
        // second setter cannot complete until that transaction has written.
        FakeControl shared;shared.snapshot.chargeWritable=true;shared.snapshot.chargeEnd=95;
        std::promise<void> decisionEntered,releaseDecision,competitorStarted;
        auto entered=decisionEntered.get_future();auto release=releaseDecision.get_future().share();
        auto started=competitorStarted.get_future();
        auto first=std::async(std::launch::async,[&]{return apply(shared,95,-1,
            [&](int){decisionEntered.set_value();release.wait();return 90;},[](int){});});
        const bool reached=entered.wait_for(std::chrono::seconds(5))==std::future_status::ready;
        if(!reached){releaseDecision.set_value();first.wait();check(false,"charge transaction did not reach decision");}
        auto second=std::async(std::launch::async,[&]{competitorStarted.set_value();shared.setChargeThresholds(75,80);});
        const bool began=started.wait_for(std::chrono::seconds(5))==std::future_status::ready;
        const bool blocked=second.wait_for(std::chrono::milliseconds(100))==std::future_status::timeout;
        releaseDecision.set_value();first.get();second.get();
        check(began&&blocked&&shared.calls==std::vector<std::string>{"read","write:90:95","write:75:80"}&&
            shared.snapshot.chargeEnd==80,"charge transaction prevents in-process interleaving");
    }
    {
        auto sharedGate=std::make_shared<mc::linux_platform::HlylOperationGate>();
        FakeControl first(sharedGate),second(sharedGate);
        std::promise<void> held,release,attempted;auto heldFuture=held.get_future();
        auto releaseFuture=release.get_future().share();auto attemptedFuture=attempted.get_future();
        auto owner=std::async(std::launch::async,[&]{first.withChargePolicyTransaction([&]{held.set_value();releaseFuture.wait();});});
        const bool ready=heldFuture.wait_for(std::chrono::seconds(5))==std::future_status::ready;
        if(!ready){release.set_value();owner.wait();check(false,"shared gate not acquired");}
        auto competing=std::async(std::launch::async,[&]{attempted.set_value();return second.read();});
        const bool started=attemptedFuture.wait_for(std::chrono::seconds(5))==std::future_status::ready;
        const bool blocked=competing.wait_for(std::chrono::milliseconds(100))==std::future_status::timeout;
        release.set_value();owner.get();competing.get();
        check(started&&blocked&&second.calls==std::vector<std::string>{"read"},"shared gate serializes separate backend instances");
    }
}
