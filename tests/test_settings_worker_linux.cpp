#include "../src/platform/linux/SettingsWorkerLinux.h"
#include "../src/platform/linux/SettingsFailureLinux.h"
#include "../src/platform/linux/SettingsObservationEpoch.h"
#include "MagicControl/ResourceControl.h"
#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <vector>

int main(){
    using namespace mc::linux_platform;
    const auto check=[](bool ok,const char* text){if(!ok)throw std::runtime_error(text);};
    {
        mc::LaptopDisplayCache cache;mc::LaptopControlSnapshot confirmed;
        confirmed.connected=true;confirmed.chargeWritable=true;confirmed.keyboardWritable=true;confirmed.chargeStart=80;confirmed.chargeEnd=95;
        auto laptop=cache.update(confirmed);
        mc::BatteryReading battery,os; battery.percent=87;battery.healthPercent=90;os.percent=86;
        mc::EcBatteryReading ec;ec.coherent=true;ec.battery.percent=87;
        invalidateSettingsAfterWorkerFailure(cache,laptop,battery,os,ec);
        check(!laptop.connected&&!laptop.chargeWritable&&!laptop.keyboardWritable&&laptop.retainedForDisplay&&
            laptop.chargeStart==80&&laptop.chargeEnd==95,"failed worker retains values without restoring device permissions");
        check(battery.dynamicStale&&battery.healthStale&&battery.percent==87&&os.dynamicStale&&!os.healthStale&&os.percent==86,
            "failed worker retains battery data and marks health stale only if present");
        check(!ec.coherent&&ec.retainedForDisplay&&ec.battery.dynamicStale&&ec.battery.percent==87,
            "failed worker retains coherent EC values as stale");
        ec={};invalidateSettingsAfterWorkerFailure(cache,laptop,battery,os,ec);
        check(!ec.coherent&&!ec.retainedForDisplay&&!ec.battery.dynamicStale,"absent EC observation is not fabricated as retained");
        laptop=cache.update(confirmed);
        check(laptop.connected&&laptop.chargeWritable&&laptop.keyboardWritable,"fresh confirmed observation restores actual capabilities");
    }
    std::vector<std::string> calls;
    bool failBattery=false,failCollect=false,match=true,lightingOk=true,throwLighting=false,throwReadback=false;
    SettingsWorkerActions actions{
        [&]{calls.push_back("battery");if(failBattery)throw std::runtime_error("battery failed");return std::string("battery ok");},
        [&]{calls.push_back("lighting");if(throwLighting)throw std::runtime_error("lighting exception");return SettingsWorkerResult{lightingOk,"lighting result"};},
        [&]{calls.push_back("collect");if(failCollect)throw std::runtime_error("collect failed");},
        [&]{calls.push_back("readback");if(throwReadback)throw std::runtime_error("readback exception");return match;}};
    SettingsWorkerRequest both{85,-1,2,-1,true};
    auto result=executeSettingsWorkerLinux(both,actions);
    check(result.success&&result.message=="lighting result"&&calls==std::vector<std::string>{"battery","lighting","collect","readback"},"operation order");
    calls.clear();failBattery=true;result=executeSettingsWorkerLinux(both,actions);
    check(!result.success&&result.message=="battery failed"&&calls==std::vector<std::string>{"battery","lighting","collect"},"battery failure remains visible after lighting succeeds");
    calls.clear();lightingOk=false;result=executeSettingsWorkerLinux(both,actions);
    check(!result.success&&result.message=="battery failed; lighting result"&&calls==std::vector<std::string>{"battery","lighting","collect"},"both failures remain visible");
    calls.clear();lightingOk=true;throwLighting=true;failCollect=true;result=executeSettingsWorkerLinux(both,actions);
    check(!result.success&&result.message=="battery failed; lighting exception; collect failed"&&calls==std::vector<std::string>{"battery","lighting","collect"},"all stage exceptions remain visible");
    throwLighting=false;failCollect=false;
    lightingOk=true;
    calls.clear();failBattery=false;failCollect=true;result=executeSettingsWorkerLinux(both,actions);
    check(!result.success&&result.message=="collect failed"&&calls==std::vector<std::string>{"battery","lighting","collect"},"collection failure");
    calls.clear();failCollect=false;match=false;result=executeSettingsWorkerLinux(both,actions);
    check(!result.success&&calls==std::vector<std::string>{"battery","lighting","collect","readback"},"unconfirmed readback");
    calls.clear();both.hasBackend=false;result=executeSettingsWorkerLinux(both,actions);
    check(!result.success&&calls==std::vector<std::string>{"collect"},"missing backend refuses writes");
    calls.clear();result=executeSettingsWorkerLinux({},actions);
    check(!result.success&&calls==std::vector<std::string>{"collect"},"poll remains read-only");
    calls.clear();match=true;lightingOk=false;both.hasBackend=true;result=executeSettingsWorkerLinux(both,actions);
    check(!result.success&&calls==std::vector<std::string>{"battery","lighting","collect"},"lighting failure");
    calls.clear();lightingOk=true;throwLighting=true;result=executeSettingsWorkerLinux(both,actions);
    check(!result.success&&result.message=="lighting exception"&&calls==std::vector<std::string>{"battery","lighting","collect"},"lighting exception still collects");
    calls.clear();throwLighting=false;throwReadback=true;result=executeSettingsWorkerLinux(both,actions);
    check(!result.success&&result.message=="readback exception"&&calls==std::vector<std::string>{"battery","lighting","collect","readback"},"readback exception returns failure");
    {
        int failingStage=0;std::vector<int> sequence;
        SettingsWorkerActions unusual{
            [&]()->std::string{sequence.push_back(1);if(failingStage==1)throw 42;return "battery";},
            [&]()->SettingsWorkerResult{sequence.push_back(2);if(failingStage==2)throw 42;return {true,"lighting"};},
            [&]{sequence.push_back(3);if(failingStage==3)throw 42;},
            [&]()->bool{sequence.push_back(4);if(failingStage==4)throw 42;return true;}};
        const SettingsWorkerRequest request{85,-1,2,-1,true};
        const char* messages[]{"","операции батареи","операции подсветки","чтения состояния","подтверждения состояния"};
        for(failingStage=1;failingStage<=4;++failingStage){sequence.clear();
            const auto outcome=executeSettingsWorkerLinux(request,unusual);
            check(!outcome.success&&outcome.message.find(messages[failingStage])!=std::string::npos,
                "non-standard stage failure remains visible");
            check(sequence==(failingStage==4?std::vector<int>{1,2,3,4}:std::vector<int>{1,2,3}),
                "non-standard stage failure retains ordering and avoids false readback confirmation");
        }
    }
    SettingsObservationEpoch epoch;const auto beforeSleep=epoch.capture();
    check(epoch.current(beforeSleep),"initial observation is current");
    epoch.transition(true);check(!epoch.current(beforeSleep)&&epoch.asleep(),"sleep invalidates in-flight observation");
    const auto asleep=epoch.capture();epoch.transition(true);
    check(epoch.capture()==asleep&&!epoch.current(asleep),"duplicate sleep is idempotent");
    epoch.transition(false);check(!epoch.current(beforeSleep)&&!epoch.current(asleep)&&epoch.current(epoch.capture()),"resume requires a fresh observation");
    {
        mc::ResourceWorker coordinator;std::promise<void> started,release;
        auto startedFuture=started.get_future();auto releaseFuture=release.get_future();std::atomic<int> actions{};
        coordinator.launch([&]{started.set_value();releaseFuture.wait();++actions;},[]{});
        check(coordinator.busy(),"first resource job is admitted");
        const bool startedInTime=startedFuture.wait_for(std::chrono::seconds(5))==std::future_status::ready;
        bool refused=false;try{coordinator.launch([]{},[]{});}catch(const std::logic_error&){refused=true;}release.set_value();coordinator.join();
        check(startedInTime&&refused&&actions==1&&!coordinator.busy(),"resource lane rejects overlapping action and joins completion");
        check(coordinator.takeCompletion(),"joined result can be consumed");
        coordinator.launch([&]{++actions;},[]{});coordinator.join();coordinator.takeCompletion();
        check(actions==2&&!coordinator.busy(),"second action and completion succeed");
    }
    {
        std::atomic<bool> finished{};
        {mc::ResourceWorker coordinator;
            coordinator.launch([&]{finished=true;},[]{});}
        check(finished.load(),"destructor joins worker before state is destroyed");
    }
    {
        bool fail=true;std::atomic<int> actions{};
        mc::ResourceWorker coordinator([&](std::function<void()> work)->std::thread{
            if(fail){fail=false;throw 42;}return std::thread(std::move(work));});
        bool threw=false;try{coordinator.launch([&]{++actions;},[]{});}catch(...){threw=true;}
        check(threw&&!coordinator.busy()&&actions==0,"failed thread creation releases busy without action");
        coordinator.launch([&]{++actions;},[]{});coordinator.join();coordinator.takeCompletion();
        check(actions==1&&!coordinator.busy(),"retry after thread creation failure completes");
    }
    std::cout<<"Linux SETs worker checks passed\n";
}
