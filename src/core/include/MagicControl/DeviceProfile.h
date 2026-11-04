#pragma once
#include "PowerSettings.h"
#include "DeviceClass.h"

#include <cstdint>
#include <array>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace mc {
struct BoardDescriptor;
enum class DeviceFeature { Fans, PowerLimits, BatteryCharge, Backlight, SmuTelemetry, LiquidCooling };
[[nodiscard]] constexpr std::string_view legacyFeatureName(DeviceFeature feature) noexcept {
    switch(feature){
    case DeviceFeature::Fans:return "fans";case DeviceFeature::PowerLimits:return "power";
    case DeviceFeature::BatteryCharge:return "battery";case DeviceFeature::Backlight:return "lighting";
    case DeviceFeature::SmuTelemetry:return "smu telemetry";case DeviceFeature::LiquidCooling:return "liquid cooling";
    }
    return {};
}
struct DeviceCapabilities {
    // Immutable profile declarations, not permission to execute an operation.
    std::span<const std::string_view> declarations{};
    [[nodiscard]] constexpr bool anyEcControl() const noexcept {
        return supports(DeviceFeature::Fans)||supports(DeviceFeature::PowerLimits)||
            supports(DeviceFeature::BatteryCharge)||supports(DeviceFeature::Backlight)||supports(DeviceFeature::LiquidCooling);
    }
    [[nodiscard]] constexpr bool supports(DeviceFeature feature) const noexcept {
        const auto name=legacyFeatureName(feature);
        if(!name.empty())for(const auto declaration:declarations)if(declaration==name)return true;
        return false;
    }
};
// Compatibility projection of legacy declarations. A declaration is not a
// transport check or permission to execute an operation. Existing hardware
// admission continues to consult the verified backend and its guards.
struct ProfileFeatureDeclaration {std::optional<DeviceFeature> feature;std::string_view name;};
[[nodiscard]] inline std::vector<ProfileFeatureDeclaration> declaredProfileFeatures(const DeviceCapabilities& caps){
    std::vector<ProfileFeatureDeclaration> out;
    constexpr DeviceFeature legacy[]{DeviceFeature::Fans,DeviceFeature::PowerLimits,DeviceFeature::BatteryCharge,
        DeviceFeature::Backlight,DeviceFeature::SmuTelemetry,DeviceFeature::LiquidCooling};
    for(const auto name:caps.declarations){
        std::optional<DeviceFeature> known;
        for(const auto feature:legacy)if(name==legacyFeatureName(feature)){known=feature;break;}
        out.push_back({known,name});
    }
    return out;
}
struct DeviceIdentityProbe {
    std::uint16_t address;
    std::string_view name;
    std::uint8_t expected;
    std::uint8_t mask{0xFF};
};
enum class RamFanProjectRule {Exact25,Exact26,NonFill};
// Descriptive model data. This does not replace runtime identity, driver,
// protocol or ownership checks and cannot authorize an operation.
struct RamFanProfileData {
    bool gmFamily{},amd{},doubleCodec{};
    RamFanProjectRule projectRule{};
    int amdSustainedMaximum{},amdPeakMaximum{};
    std::string_view modelName,settingsFile,powerDraftFile,fanCurveFile;
};
// File-format and semantic validation data, never permission to write hardware.
// Adjacent order applies only when the respective two fields have that meaning;
// live backends still check their own firmware limits and readback.
struct CustomPresetPolicy {
    std::array<int,3> maximum{100,100,100};
    int minimum{5};
    std::array<bool,2> orderedPairs{true,true};
    bool nativeCurveDraft{},legacyNativeV3{},respective{};
    std::string_view legacyFilename{};
};
enum class BatteryModeLabels { VoltageProfiles, ChargeModes };
// A reported device can be identified without having any verified control path.
// Transport IDs describe implementations, not laptop brands or CPU vendors.
struct DeviceProfile {
    std::string_view id, manufacturer, baseBoard, displayName;
    std::string_view protocolId, ecBackendId, smuBackendId;
    DeviceCapabilities capabilities;
    std::span<const DeviceIdentityProbe> identity;
    std::string_view productName{}; // Optional extra exact match for platform variants.
    // Informational/marketing alias only. Never participates in SMBIOS matching
    // or grants access to a transport, protocol or hardware capability.
    std::string_view alternativeName{};
    // UI range only, including Off=0. Runtime transport still authorizes writes.
    // Zero means no confirmed brightness range; never borrow another board's.
    int keyboardBrightnessMaximum{};
    // Explicit profile information only; Unknown does not restrict monitoring.
    DeviceClass deviceClass{DeviceClass::Unknown};
    const RamFanProfileData* ramFanData{};
    // Documented physical absence, separate from control capabilities. Empty
    // means unknown; never infer absent hardware from an unsupported writer.
    std::span<const std::string_view> absentResources{};
    std::array<int,2> fanPointCounts{16,16}; // All devices default16/16; data overrides only.
    std::span<const std::string_view> productVariants{}; // Additional EXACT SMBIOS products, never substrings.
    std::span<const PowerOptionDescriptor> powerOptions{};
    CustomPresetPolicy customPreset{};
    BatteryModeLabels batteryModeLabels{BatteryModeLabels::VoltageProfiles};
};
[[nodiscard]] inline bool profileResourceAbsent(const DeviceProfile& profile,std::string_view resource) noexcept {
    for(const auto name:profile.absentResources)if(name==resource)return true;
    return false;
}
// Exact profile ID lookup only. This is descriptive data, not a hardware or
// write-admission check; callers retain their existing verified device guards.
[[nodiscard]] const RamFanProfileData* ramFanDataForProfile(std::string_view id) noexcept;
// Exact boards sharing owner-authorized RAM fan mechanisms. Per-board identity,
// files, CPU codecs and native factory-profile admission remain independent.
[[nodiscard]] inline constexpr bool gmRamFanProfile(std::string_view id) noexcept {
    return id=="mechrevo-gm6ir0c"||id=="mechrevo-gm5hg0a"||id=="machenike-l16";
}
[[nodiscard]] inline constexpr bool hydrocProfile(std::string_view id) noexcept {return id=="eluktronics-hydroc16g2";}
// RAM fan/CPU control admission is separate from the GM battery decoder and
// SingleZone EC lighting map. HYDROC must never inherit either of those maps.
[[nodiscard]] inline constexpr bool ramFanControlProfile(std::string_view id) noexcept {
    return gmRamFanProfile(id)||hydrocProfile(id);
}
[[nodiscard]] inline constexpr bool gmPowerDoubleCodec(std::string_view id) noexcept {
    return id=="mechrevo-gm5hg0a"||id=="machenike-l16"||hydrocProfile(id);
}
[[nodiscard]] inline constexpr bool gm5Profile(std::string_view id) noexcept {return id=="mechrevo-gm5hg0a";}
[[nodiscard]] inline constexpr bool l16Profile(std::string_view id) noexcept {return id=="machenike-l16";}
[[nodiscard]] inline constexpr bool gmAmdProfile(std::string_view id) noexcept {return gm5Profile(id)||l16Profile(id);}
[[nodiscard]] inline int gmAmdPowerMaximum(std::string_view id,int channel) noexcept {
    // Exact profile data supplies the same verified model-specific bounds.
    // This is a bound only: the existing board, codec and writer guards apply.
    if(const auto* data=ramFanDataForProfile(id))return channel==2?data->amdPeakMaximum:data->amdSustainedMaximum;
    return channel==2?200:100;
}
[[nodiscard]] inline std::string_view gmModelName(std::string_view id) noexcept {
    if(const auto* data=ramFanDataForProfile(id))return data->modelName;
    return "GM6IR0C";
}
[[nodiscard]] inline constexpr bool gmRamProject(std::string_view id,unsigned project) noexcept {
    return hydrocProfile(id)?project==26:l16Profile(id)?project==26:gm5Profile(id)?project>0&&project<255:id=="mechrevo-gm6ir0c"&&project==25;
}
[[nodiscard]] inline std::string_view gmSettings(std::string_view id) noexcept {
    if(const auto* data=ramFanDataForProfile(id))return data->settingsFile;
    return "gm6ir0c-device-settings.ini";
}
[[nodiscard]] inline std::string_view gmPowerDraftFile(std::string_view id) noexcept {
    if(const auto* data=ramFanDataForProfile(id))return data->powerDraftFile;
    return "gm6ir0c-power.txt";
}
[[nodiscard]] inline std::string_view gmFanCurveFile(std::string_view id) noexcept {
    if(id=="minisforum-hpbsd")return "hpbsd-fan-profiles.csv";
    if(id=="lenovo-legion-kwcn")return "lenovo-legion-kwcn-fan-profiles.csv";
    if(const auto* data=ramFanDataForProfile(id))return data->fanCurveFile;
    return "fan-curves.csv";
}
[[nodiscard]] std::span<const DeviceProfile* const> deviceProfiles() noexcept;
[[nodiscard]] const DeviceProfile& deviceProfile(const BoardDescriptor& board) noexcept;
void identifyDevice(BoardDescriptor& board);
}
