#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
namespace mc {
class EcAccess;
using KeyboardColor=std::array<std::uint8_t,3>;
enum class SingleZoneModel {Wujie,Gm6};
// Observed native EC timer, never a timeout setting or permission to write.
struct KeyboardIdleState {
    std::uint16_t elapsed{},limit{};
    std::uint8_t flags{},powerState{},workingLevel{},platform{},acLevel{},dcLevel{};
    bool onAC{},hostPowerDisabled{};
    std::uint64_t sampledAtMs{};
    friend bool operator==(const KeyboardIdleState&,const KeyboardIdleState&)=default;
};
std::optional<KeyboardIdleState> readKeyboardIdle(EcAccess&);
struct KeyboardBacklightState {
    SingleZoneModel model{};bool supported{};
    std::uint8_t keyboard{},effect{},breathing{};KeyboardColor color{};
    std::optional<std::uint8_t> pulseRaw; // Full observed7C5; bit7 is FAN Respective, not a lighting flag.
    bool retainedForDisplay{};
    friend bool operator==(const KeyboardBacklightState&,const KeyboardBacklightState&)=default;
};
struct KeyboardBacklightChange {
    std::optional<int> level,effect,pulse; // effect0 mono,1 breathing,2 native rainbow
    std::optional<KeyboardColor> color;
    // Native idle blank is independent from selected brightness bits.
    std::optional<bool> powerDisabled;
    bool factoryColor{};
    // Cycle ownership covers RGB/effects, never physical brightness hotkeys.
    std::optional<KeyboardBacklightState> expected;
    bool preserveEffectsWhenBlanked{}; // Exact temporary snapshot restore, never the user's OFF button.
};
KeyboardBacklightState readKeyboardBacklight(EcAccess&);
KeyboardBacklightState applyKeyboardBacklight(EcAccess&,const KeyboardBacklightChange&);
const std::array<KeyboardColor,8>& keyboardColorPalette(SingleZoneModel);
KeyboardColor keyboardColorLevels(SingleZoneModel,const KeyboardColor& uiRgb);
// Shared lifecycle of the one physical keyboard. Callers own DeviceOperation;
// a successful temporary effect/restore publishes its fresh final state so an
// already-running cycle can continue without mistaking our write for takeover.
// An unfinished scope stops that cycle; it never resumes an unknown snapshot.
class KeyboardLightingPause {
public:
    explicit KeyboardLightingPause(std::string reason);
    ~KeyboardLightingPause();
    KeyboardLightingPause(const KeyboardLightingPause&)=delete;
    KeyboardLightingPause& operator=(const KeyboardLightingPause&)=delete;
    void complete(const KeyboardBacklightState& actual);
    // Proven pre-write transient retry only: retain resumePending and do not
    // claim a final state, but do not turn a deferred read into ownership loss.
    void defer();
private:
    std::uint64_t powerEpoch_{};bool complete_{};
};
// Suspend has no hardware writes. Resume leaves cycles paused until a fresh,
// admitted restore (or explicit cycle start) completes. No startup autoplay.
void setKeyboardLightingSuspended(bool suspended);
struct KeyboardCycleSettings {
    // Exact OEM SingleZone indices1,5,9,13,21,25, in UI RGB units.
    std::array<KeyboardColor,6> colors{{{255,0,0},{255,102,0},{255,204,0},{0,255,0},{0,0,255},{255,0,255}}};
    int intervalSeconds{10};
    friend bool operator==(const KeyboardCycleSettings&,const KeyboardCycleSettings&)=default;
};
bool loadKeyboardCycleSettings(KeyboardCycleSettings&,std::string& error);
bool saveKeyboardCycleSettings(const KeyboardCycleSettings&,std::string& error);
struct KeyboardCycleStatus {bool active{};unsigned index{};std::string message;bool paused{};friend bool operator==(const KeyboardCycleStatus&,const KeyboardCycleStatus&)=default;};
// Common host timer. Typed callbacks preserve each model's gate/OEM guards.
// No startup apply. Stop/exit leaves the last static colour, never replays an
// old hardware snapshot. An external RGB/effect change stops the cycle.
class KeyboardColorCycle {
public:
    using Reader=std::function<KeyboardBacklightState()>;
    using Writer=std::function<KeyboardBacklightState(const KeyboardBacklightChange&)>;
    KeyboardColorCycle(Reader,Writer);~KeyboardColorCycle();
    void start(const KeyboardCycleSettings&);void stop();
    KeyboardCycleStatus status() const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
}
