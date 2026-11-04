#pragma once
#include "KeyboardBacklight.h"
#include <utility>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mc {
inline constexpr char lightingEvidenceSectionTitle[]="Подсветка · HID firmware и зоны OEM";
enum class LightingZone {Keyboard,Lightbar,Logo,Hinge};
constexpr std::string_view lightingZoneKey(LightingZone zone){
    switch(zone){case LightingZone::Lightbar:return "lightbar";case LightingZone::Logo:return "logo";
    case LightingZone::Hinge:return "hinge";default:return "keyboard";}
}
constexpr const char* lightingZoneTitle(LightingZone zone){
    switch(zone){case LightingZone::Lightbar:return "Подсветка полосы";case LightingZone::Logo:return "Подсветка логотипа";
    case LightingZone::Hinge:return "Подсветка шарнира";default:return "Подсветка клавиатуры";}
}
// Requested configuration is deliberately separate from observed hardware.
// ITE88 reports an effect/level, not RGB or independently addressed zone state.
struct LightingConfiguration {
    int level{4},effect{1},speed{2},direction{};
    std::array<std::uint8_t,3> rgb{255,255,255};
    friend bool operator==(const LightingConfiguration&,const LightingConfiguration&)=default;
};
struct LightingZoneInfo {
    LightingZone zone{};bool writable{};
    std::optional<LightingConfiguration> configured;
    std::optional<int> observedLevel;
    std::string status;
    friend bool operator==(const LightingZoneInfo&,const LightingZoneInfo&)=default;
};
struct LightingZonesSnapshot {
    std::vector<LightingZoneInfo> zones;
    std::string status;
    const LightingZoneInfo* find(LightingZone zone)const{
        for(const auto& item:zones)if(item.zone==zone)return &item;return nullptr;
    }
    friend bool operator==(const LightingZonesSnapshot&,const LightingZonesSnapshot&)=default;
};

// Shared intent, never permission to access a model/transport/zone. Raw updates
// retain expected-state/epoch guards for restore, indications and cycle ticks.
enum class LightingWrite {Level,Timeout,Effect,Palette,CustomColor,Native,Cycle,Zone,Update,Pulse,NativeUpdate};
enum class LightingWriteOrigin {Explicit,Restore,Temporary,Cycle};
struct LightingWriteRequest {
    LightingWrite kind{LightingWrite::NativeUpdate};int value{-1};
    KeyboardColor color{};
    KeyboardCycleSettings cycle;
    LightingConfiguration configuration;
    KeyboardBacklightChange change;
    int timeout{-1};
    LightingWriteOrigin origin{LightingWriteOrigin::Explicit};
    static LightingWriteRequest singleZone(KeyboardBacklightChange value,LightingWriteOrigin origin){
        LightingWriteRequest r{LightingWrite::Update};r.change=std::move(value);r.origin=origin;return r;
    }
    static LightingWriteRequest forZone(LightingConfiguration value){LightingWriteRequest r{LightingWrite::Zone};r.configuration=std::move(value);return r;}
    static LightingWriteRequest forCycle(KeyboardCycleSettings value,LightingWriteOrigin origin=LightingWriteOrigin::Explicit){
        LightingWriteRequest r{LightingWrite::Cycle};r.cycle=std::move(value);r.origin=origin;return r;
    }
    static LightingWriteRequest nativeUpdate(int level,int seconds){LightingWriteRequest r{LightingWrite::NativeUpdate,level};r.timeout=seconds;return r;}
};
struct LightingWriteResult {
    bool success{true};std::string message;
    std::optional<KeyboardBacklightState> keyboard;
};
}
