#pragma once
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include "NativeFanCurve.h"
#include "PowerSettings.h"
namespace mc {
struct CustomFanPoint {int up{},down{},duty{};friend bool operator==(const CustomFanPoint&,const CustomFanPoint&)=default;};
using CustomFanCurve=std::array<CustomFanPoint,16>;
using CustomFanCurves=std::array<CustomFanCurve,2>;
struct CustomPreset {
    std::array<int,3> power{};CustomFanCurves curves{};bool respective{};
    // 0 explicit values; 1/2/3 resolve this model's Office/Gaming/Turbo source.
    int powerRecipe{};
    // Legacy device-only drafts are imported into curves exactly once. The
    // public working model is ALWAYS the same16-cell pair for every device.
    bool curvesReady{true};
    PowerOptions powerOptions; // Same preset, optional GPU/thermal/time settings.
};
struct CustomPresetLibrary {std::array<std::optional<CustomPreset>,3> slots;int selected{};bool active{};};
std::string customPresetFilename(std::string_view profile);
CustomPresetLibrary loadCustomPresetLibrary(std::string_view profile);
void saveCustomPresetLibrary(std::string_view profile,const CustomPresetLibrary&);
void ensureCustomPresets(std::string_view profile);
void selectCustomPresetSlot(std::string_view profile,int slot);
void setCustomPresetActive(std::string_view profile,bool active);
std::string customManualCaption(std::string_view profile,bool custom,bool requireSavedActive=true);
void validateCustomPreset(std::string_view profile,const CustomPreset&);
inline constexpr std::array<const char*,3> customPresetNames{"Quiet","Balanced","Performance"};
}
