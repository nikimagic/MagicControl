#pragma once
#include <cstdint>
#include <optional>
#include <string>
namespace mc {
class EcAccess;
// Logical fields of the reviewed Uniwill aging protocol. They are not EC
// addresses: each transport must independently admit and encode the request.
enum class BatteryAgingField : std::uint8_t {
    AlternativeDateHigh, AlternativeDateLow, ExposureHigh, ExposureLow, Save
};
enum class BatteryAgingAccess : std::uint8_t { Unavailable, ReadOnly, ReadWrite };
// History units are temperature-weighted exposure points, not wall-clock hours.
// A backend must supply the normalized busy flags and preserved-context token.
struct BatteryAgingReading {
    std::string host;
    std::uint16_t manufacture{},design{},cycles{},date{},history{};
    std::uint8_t months{},appliedMonths{},service{},pending{},yearBcd{},monthBcd{};
    bool serviceBusy{},savePending{};
    std::uint64_t preservedContext{};
    friend bool operator==(const BatteryAgingReading&,const BatteryAgingReading&)=default;
};
struct BatteryAgingStatus {
    std::optional<BatteryAgingReading> reading;
    std::optional<bool> enabled;
    bool writable{};
    std::optional<std::uint64_t> realHistory;
    bool authoritative{},retainedForDisplay{};
    std::uint64_t sampledAtMs{};
    std::string error;
    friend bool operator==(const BatteryAgingStatus&,const BatteryAgingStatus&)=default;
};
// Display only. Unknown reads may retain a confirmed selection for ten seconds;
// a real mismatch/off or invalid backup is authoritative and clears it now.
// Never copy writer permission or extend the original sample's lifetime.
inline BatteryAgingStatus retainBatteryAgingDisplay(BatteryAgingStatus fresh,const BatteryAgingStatus& previous,std::uint64_t now){
    if(fresh.authoritative)return fresh;
    // A quick command may supply an old snapshot, not just an empty result.
    // Normalize it to unknown before considering the bounded previous sample.
    fresh.enabled.reset();fresh.reading.reset();fresh.realHistory.reset();
    fresh.writable=false;fresh.retainedForDisplay=false;fresh.sampledAtMs=0;
    if(!previous.enabled.has_value()||!previous.sampledAtMs||now<previous.sampledAtMs||now-previous.sampledAtMs>10000)return fresh;
    auto retained=previous;retained.writable=false;retained.authoritative=false;retained.retainedForDisplay=true;
    retained.error=fresh.error.empty()?"Последнее подтверждённое чтение EC":fresh.error;
    return retained;
}
BatteryAgingStatus observeBatteryAging(EcAccess&);
// One core operation for GUI/CLI/lifecycle. The original backup is immutable;
// A durable ledger preserves original+new exposure across raw counter resets.
void setBatteryAgingOverride(EcAccess&,bool enabled,bool lifecycle=false);
}
