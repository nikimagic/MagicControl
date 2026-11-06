#pragma once

#include "TelemetryGate.h"
#include <array>
#include <cstddef>
#include <stdexcept>
#include <utility>

namespace mc {
enum class DeferredTelemetryItem {AudioBind,AudioRender,AudioCapture,TouchpadInitial,TouchpadPoll,TouchpadKeyPoll};

// Actor-owned bounded queue: it retains only the latest request of each kind,
// never callbacks or GUI pointers. submit/drain run on that actor's thread.
class DeferredTelemetryQueue {
    TelemetryGate& gate_;std::array<DeferredTelemetryItem,6> pending_{};std::size_t count_{};
    void erase(std::size_t index)noexcept{for(auto i=index+1;i<count_;++i)pending_[i-1]=pending_[i];--count_;}
    void enqueue(DeferredTelemetryItem item)noexcept{
        std::array<DeferredTelemetryItem,2> audio{};std::size_t audioCount{};
        for(std::size_t i=0;i<count_;){
            const auto current=pending_[i];
            if(current==item){erase(i);continue;}
            if(item==DeferredTelemetryItem::AudioBind&&
               (current==DeferredTelemetryItem::AudioRender||current==DeferredTelemetryItem::AudioCapture)){
                audio[audioCount++]=current;erase(i);continue;
            }
            ++i;
        }
        pending_[count_++]=item;
        for(std::size_t i=0;i<audioCount;++i)pending_[count_++]=audio[i];
    }
public:
    explicit DeferredTelemetryQueue(TelemetryGate& gate=telemetryGate())noexcept:gate_(gate){}
    template<class Work> bool submit(DeferredTelemetryItem item,Work&& work){
        if(static_cast<unsigned>(item)>static_cast<unsigned>(DeferredTelemetryItem::TouchpadKeyPoll))throw std::invalid_argument("Invalid deferred telemetry item");
        if(count_){enqueue(item);return false;}
        auto observation=gate_.tryObservation();
        if(!observation){enqueue(item);return false;}
        std::forward<Work>(work)();return true;
    }
    template<class Work> std::size_t drain(Work&& work,std::size_t limit=16){
        std::size_t count{};
        while(count_&&count<limit){
            auto observation=gate_.tryObservation();if(!observation)break;
            const auto item=pending_[0];
            try{work(item);}catch(...){break;} // Keep the failed request for a later retry.
            erase(0);++count;
        }
        return count;
    }
    [[nodiscard]] std::size_t pending()const noexcept{return count_;}
};
}
