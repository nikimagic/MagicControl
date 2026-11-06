#pragma once
#include "NativeDefaultFan.h"
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace mc {
struct NativeDefaultFanCacheKey {
    int mode{},powerGate{};
    friend bool operator==(const NativeDefaultFanCacheKey&,const NativeDefaultFanCacheKey&)=default;
};
// Caches only an observation. It never extends the curve's sample timestamp,
// changes a mode or grants control. The caller still samples actual PWM.
class NativeDefaultFanCache {
public:
    [[nodiscard]] bool needsRefresh(std::uint64_t now,NativeDefaultFanCacheKey key)const noexcept{
        return !checkedAt_||now<checkedAt_||now-checkedAt_>=3000||key!=key_;
    }
    void store(NativeDefaultFanReading value,std::uint64_t checkedAt,NativeDefaultFanCacheKey key){
        reading_=std::move(value);checkedAt_=checkedAt;key_=key;
    }
    void clearInactive(){reading_={NativeDefaultFanObservation::Inactive,{}};checkedAt_=0;}
    void clearUnavailable(){reading_={};checkedAt_=0;}
    [[nodiscard]] const NativeDefaultFanReading& reading()const noexcept{return reading_;}
private:
    NativeDefaultFanReading reading_;
    std::uint64_t checkedAt_{};
    NativeDefaultFanCacheKey key_{};
};

// This is only log deduplication, never a FAN observation or admission flag.
// A muted failure remains unreported until a later poll with AllLogs enabled.
// Similarly, recovery stays pending while muted so the next available sample
// can close the previously logged failure without a misleading repeat.
class NativeDefaultFanFailureLog {
public:
    template<class Emit>
    void failure(bool enabled,std::string_view reason,Emit&& emit) noexcept {
        if(!enabled||reported_)return;
        try{emit(std::string("Default FAN firmware view unavailable: ")+std::string(reason));reported_=true;}catch(...){}
    }
    template<class Emit>
    void recovered(bool enabled,Emit&& emit) noexcept {
        if(!enabled||!reported_)return;
        try{emit("Default FAN firmware view recovered");reported_=false;}catch(...){}
    }
private:
    bool reported_{};
};
}
