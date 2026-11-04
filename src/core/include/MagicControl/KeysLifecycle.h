#pragma once
#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <functional>
#include <memory>
#include <cstdint>

namespace mc {
// One KEYs schema for every board, including unknown and inventory-only devices.
enum class KeyAction { Windows, Osd, Fn, Usb, Ac, Copilot, Wifi, Bluetooth, Camera, Touchpad, Microphone };
inline constexpr std::size_t kKeyCount=11;
static_assert(static_cast<std::size_t>(KeyAction::Microphone)+1==kKeyCount);
struct KeysPreferences {
    std::array<std::optional<bool>,kKeyCount> values{};
    std::optional<bool>& operator[](KeyAction key){return values[static_cast<std::size_t>(key)];}
    const std::optional<bool>& operator[](KeyAction key)const{return values[static_cast<std::size_t>(key)];}
};
[[nodiscard]] constexpr bool storedKeyPreference(KeyAction key) noexcept {
    return static_cast<std::size_t>(key)<kKeyCount;
}
// Bluetooth is OS-owned during startup/resume; only explicit commands change it.
[[nodiscard]] constexpr bool restoreKeyPreferenceOnLifecycle(KeyAction key) noexcept {
    return storedKeyPreference(key)&&key!=KeyAction::Bluetooth&&key!=KeyAction::Osd;
}
std::filesystem::path keysPreferencesPath(std::string_view actualProfile={});
bool loadKeysPreferences(const std::filesystem::path&,KeysPreferences&,std::string& error);
// Read legacy choices once without seeding intent from live/reset hardware.
void migrateKeysPreferences(std::string_view actualProfile);
bool rememberKeyPreference(KeyAction key,bool enabled,std::string& error);
struct KeyObservation {
    std::optional<bool> enabled;
    bool supported{},available{};
    std::string detail;
    // A merged effective state may hide a latched native setting. Its adapter
    // may request one idempotent reconciliation during lifecycle restoration.
    bool reconcile{};
};
// OS adapters supply fresh typed reads/setters. This worker never discovers or
// guesses an EC protocol and never depends on FAN/APU/battery restore success.
class KeysLifecycle {
public:
    using Reader=std::function<KeyObservation(KeyAction)>;
    using Setter=std::function<void(KeyAction,bool)>;
    using Observer=std::function<void(KeyAction)>;
    KeysLifecycle(std::string actualProfile,Reader read,Setter set,Observer synchronizeObserved={});
    ~KeysLifecycle();
    void request(std::string reason,std::function<void()> prepareRadio={});
    // Coalesced current-state notification. Uses the same worker/admission,
    // never replays a saved value over a fresh external OS change.
    void observe(KeyAction);
    void suspend();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
