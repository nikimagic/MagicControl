#pragma once
#include "ActionLog.h"
#include "LaptopControl.h"
#include "KeyboardBacklight.h"
#include "LightingZones.h"
#include "Monitoring.h"
#include "ResourceCatalog.h"
#include "Performance.h"
#include <type_traits>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <locale>
namespace mc::audit {
inline std::string metric(std::optional<double> value,int precision=0,bool trimZero=false){
    if(!value||!std::isfinite(*value))return "—";
    std::ostringstream out;out.imbue(std::locale::classic());out<<std::fixed<<std::setprecision(precision)<<*value;
    auto text=out.str();if(trimZero&&text.ends_with(".0"))text.resize(text.size()-2);return text;
}
// Counts only; this reports accepted resource state, not a shadow comparison.
inline std::string resourceStateSummary(const ResourceState& state){
    if(!state.current())return "{\"available\":false}";
    return "{\"available\":true,\"schema\":"+std::to_string(ResourceCatalog::schemaVersion)+
        ",\"resources\":"+std::to_string(state.current()->resources.size())+"}";
}
template<class T>std::string optional(const std::optional<T>& v){if(!v)return "null";if constexpr(std::is_floating_point_v<T>){if(!std::isfinite(*v))return "null";}if constexpr(std::is_same_v<T,bool>)return *v?"true":"false";else return std::to_string(*v);}
inline std::string ecPower(const EcPerformanceState& s){
    const auto limits=[](const EcPowerLimits& p){return "{\"stapm\":"+optional(p.sustained)+",\"slow\":"+optional(p.shortBoost)+",\"fast\":"+optional(p.peak)+"}";};
    return "{\"overrides\":"+limits(s.overrides)+",\"accepted_valid\":"+(s.acceptedValid?"true":"false")+
        ",\"accepted\":"+limits(s.acceptedValid?s.accepted:EcPowerLimits{})+",\"source\":"+quote(s.acceptedStatus)+"}";
}
inline std::string rgb(const std::array<std::uint8_t,3>& c){return "["+std::to_string(c[0])+","+std::to_string(c[1])+","+std::to_string(c[2])+"]";}
inline std::string keyboardIdle(const std::optional<KeyboardIdleState>& s){
    if(!s)return "null";
    return "{\"elapsed_seconds\":"+std::to_string(s->elapsed)+",\"limit_seconds\":"+std::to_string(s->limit)+
        ",\"raw_741\":"+std::to_string(s->flags)+",\"platform\":"+std::to_string(s->platform)+
        ",\"power_state\":"+std::to_string(s->powerState)+",\"on_ac\":"+(s->onAC?"true":"false")+
        ",\"working_level\":"+std::to_string(s->workingLevel)+",\"ac_level\":"+std::to_string(s->acLevel)+
        ",\"dc_level\":"+std::to_string(s->dcLevel)+",\"host_power_disabled\":"+(s->hostPowerDisabled?"true":"false")+"}";
}
inline std::string keyboard(const KeyboardBacklightState& s,int rawPulse=-1){if(rawPulse<0&&s.pulseRaw)rawPulse=*s.pulseRaw;return "{\"supported\":"+std::string(s.supported?"true":"false")+
    ",\"model\":"+quote(s.model==SingleZoneModel::Gm6?"GM6_SingleZone":"Wujie_SingleZone")+
    ",\"raw_valid\":"+(s.supported&&!s.retainedForDisplay?"true":"false")+",\"retained_for_display\":"+(s.retainedForDisplay?"true":"false")+",\"raw_78C\":"+std::to_string(s.keyboard)+",\"raw_767\":"+std::to_string(s.effect)+",\"raw_7C5\":"+(rawPulse<0?"null":std::to_string(rawPulse))+",\"pulse_7C5_low3\":"+std::to_string(s.breathing)+
    ",\"raw_rgb_769_76A_76B\":"+rgb(s.color)+",\"level\":"+std::to_string((s.keyboard>>5)&7)+",\"power_disabled\":"+((s.keyboard&2)?"true":"false")+
    ",\"rainbow\":"+((s.effect&128)?"true":"false")+",\"physical_response_measured\":false}";}
inline std::string typedPower(const LaptopControlSnapshot& s){
    if(!s.customCpuPowerLimits&&s.customPowerOptions.empty())return {};
    std::string out=",\"custom_cpu_limits\":";
    if(s.customCpuPowerLimits){const auto& p=*s.customCpuPowerLimits;out+='['+std::to_string(p[0])+','+std::to_string(p[1])+','+std::to_string(p[2])+']';}else out+="null";
    out+=",\"power_options\":{";bool first=true;for(const auto& p:s.customPowerOptions){if(!first)out+=',';first=false;out+=quote(p.key)+':'+optional(p.actual);}return out+'}';
}
inline std::string laptop(const LaptopControlSnapshot& s){return "{\"connected\":"+std::string(s.connected?"true":"false")+",\"retained_for_display\":"+(s.retainedForDisplay?"true":"false")+
    ",\"source\":"+quote(s.source)+",\"status\":"+quote(s.status)+",\"charge_start\":"+optional(s.chargeStart)+",\"charge_end\":"+optional(s.chargeEnd)+
    ",\"charge_writable\":"+(s.chargeWritable?"true":"false")+",\"charge_mode\":"+optional(s.chargeMode)+",\"charge_mode_writable\":"+(s.chargeModeWritable?"true":"false")+",\"fn_lock\":"+optional(s.fnLock)+",\"fn_writable\":"+(s.fnWritable?"true":"false")+
    ",\"native_turbo\":"+optional(s.nativeTurbo)+",\"turbo_writable\":"+(s.turboWritable?"true":"false")+
    ",\"stock_cpu_profile\":"+optional(s.stockCpuProfile)+",\"stock_cpu_profile_writable\":"+(s.stockCpuProfileWritable?"true":"false")+
    ",\"custom_cpu_power_writable\":"+(s.customCpuPowerWritable?"true":"false")+",\"custom_cpu_power_active\":"+optional(s.customCpuPowerActive)+
    ",\"keyboard_level\":"+optional(s.keyboardLevel)+",\"keyboard_timeout_seconds\":"+optional(s.keyboardTimeoutSeconds)+
    ",\"keyboard_actual_duty\":"+optional(s.keyboardActualDuty)+",\"keyboard_pwm_period\":"+optional(s.keyboardPwmPeriod)+
    ",\"keyboard_phase\":"+quote(s.keyboardPhase)+",\"keyboard_status\":"+quote(s.keyboardStatus)+
    ",\"keyboard_writable\":"+(s.keyboardWritable?"true":"false")+",\"timeout_writable\":"+(s.keyboardTimeoutWritable?"true":"false")+
    ",\"fan_rpm\":["+optional(s.fanRpm[0])+","+optional(s.fanRpm[1])+"],\"cpu_celsius\":"+optional(s.cpuCelsius)+",\"battery_celsius\":"+optional(s.batteryCelsius)+typedPower(s)+"}";}
inline std::string lighting(const LightingConfiguration& c){return "{\"level\":"+std::to_string(c.level)+",\"effect\":"+std::to_string(c.effect)+",\"speed\":"+std::to_string(c.speed)+",\"direction\":"+std::to_string(c.direction)+",\"rgb\":"+rgb(c.rgb)+"}";}
inline std::string presentation(const MonitoringSnapshot& s){
    // Compact observation only: do not log81curve points per channel/poll.
    std::string native="null";
    if(s.nativeDefaultFans){native="[";for(int ch=0;ch<2;++ch){if(ch)native+=',';const auto& f=s.nativeDefaultFans->channels[ch];
        native+="{\"profile\":"+quote(f.profile)+",\"temperature\":"+optional(f.temperatureCelsius)+
            ",\"temperature_source\":"+quote(f.temperatureSource)+",\"temperature_address\":"+std::to_string(f.temperatureAddress)+
            ",\"request\":"+optional(f.rpm?f.requestedRpm:f.requestedPercent)+",\"request_derived\":"+(f.requestDerived?"true":"false")+
            ",\"unit\":"+quote(f.rpm?"rpm":"percent")+",\"actual\":"+(f.rpm?optional(f.actualRpm):std::to_string(s.nativeDefaultFans->actualPercent[ch]))+",\"retained\":"+(f.retained?"true":"false")+",\"point_count\":"+std::to_string(f.points.size())+"}";}
        native+=']';}
    std::string cards="[";for(const auto& c:s.gpuCards.cards){if(cards.size()>1)cards+=',';cards+="{\"adapter_id\":"+quote(c.adapterId)+",\"name\":"+quote(c.name)+",\"load\":"+optional(c.usagePercent)+",\"load_stale\":"+(c.usageStale?"true":"false")+",\"watts\":"+optional(c.powerWatts)+",\"celsius\":"+optional(c.celsius)+",\"clock_mhz\":"+optional(c.clockMhz)+",\"power_source\":"+quote(c.powerSource)+",\"status\":"+quote(c.status)+"}";}cards+=']';
    std::string retained="[";for(const auto& f:s.retainedForDisplay){if(retained.size()>1)retained+=',';retained+="{\"field\":"+quote(f.field)+",\"sampled_at_monotonic_ms\":"+std::to_string(f.sampledAtMs)+"}";}retained+=']';
    const auto npu="{\"usage_percent\":"+optional(s.npuUsagePercent)+",\"clock_mhz\":"+optional(s.npuClockMhz)+",\"watts\":"+optional(s.npuPowerWatts)+
        ",\"usage_source\":"+quote(s.npuUsageSource)+",\"clock_source\":"+quote(s.npuClockSource)+",\"power_source\":"+quote(s.npuPowerSource)+"}";
    return "{\"selected_gpu\":"+quote(s.gpuCards.selectedAdapterId)+",\"selection_reason\":"+quote(s.gpuCards.selectionReason)+",\"gpu_cards\":"+cards+",\"npu\":"+npu+",\"retained_fields\":"+retained+",\"battery_health\":"+optional(s.battery.healthPercent)+",\"health_stale\":"+(s.battery.healthStale?"true":"false")+",\"health_sampled_at_monotonic_ms\":"+std::to_string(s.battery.healthAtMs)+",\"health_source\":"+quote(s.battery.healthSource)+",\"native_default_fans\":"+native+"}";
}
inline std::string gpuDisplay(const std::vector<GpuReading>& rows){std::string out="[";for(const auto& r:rows){if(out.size()>1)out+=',';
    std::string times="[";for(const auto& [field,at]:r.retainedFieldTimes){if(times.size()>1)times+=',';times+="{\"field\":"+quote(field)+",\"sampled_at_monotonic_ms\":"+std::to_string(at)+"}";}times+=']';
    out+="{\"id\":"+quote(r.uuid)+",\"name\":"+quote(r.name)+",\"available\":"+(r.available?"true":"false")+",\"power_control\":"+(r.powerControl?"true":"false")+",\"retained_for_display\":"+(r.retainedForDisplay?"true":"false")+",\"retained_fields\":"+times+",\"power_mw\":"+optional(r.powerMw)+",\"power_source\":"+quote(r.powerSource)+",\"load\":"+optional(r.utilization)+",\"clock\":"+optional(r.graphicsMhz)+",\"status\":"+quote(r.status)+"}";}return out+"]";}
}
namespace mc {std::unique_ptr<LaptopControl> auditedLaptopControl(std::unique_ptr<LaptopControl>);}
