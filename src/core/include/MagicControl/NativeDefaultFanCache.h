#pragma once
#include "NativeDefaultFan.h"
#include <cstdint>
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
}
