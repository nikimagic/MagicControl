#pragma once
#include <array>
#include <optional>
#include <cstdint>
#include "Localization.h"
#include "DisplayFormat.h"
#include "MagicControl/BatteryEcPolicy.h"
#include "MagicControl/PortableSettings.h"
#include "MagicControl/BatteryProtection.h"
#include "MagicControl/BatteryUiPolicy.h"
#include "MagicControl/Monitoring.h"
#include "MagicControl/DeviceProfile.h"
#include "MagicControl/RomIdHistory.h"
#include "../../protocols/uniwill/x5sp4nag/BatteryCandidates.h"
namespace mc::gui::ui {
// Owner request31.10.2026: remove USB-C, raw platform/ROM buttons and the
// Logger card from SETs on both OS. Presentation only: preserve saved battery
// policies, primary EC/OS summaries and independent file logging.
inline constexpr bool showUsbPriorityControls=false;
inline constexpr bool showEcPolicyControls=false;
inline constexpr bool showEcLoggerCard=false;
// One Wujie reference card on every OS/profile, including model preview.
// Native voltage-protection modes0/1/2 are NOT these percentage thresholds.
// Availability/selection require a backend that implements actual percentages.
inline constexpr std::array<const char*,4> batteryThresholdNames{"100%","95%","85%","60%"};
using mc::batteryThresholdValues;
using mc::batteryThresholdCount;
// The power-policy command must never share the fourth percentage's ID.
using mc::batteryAutomaticIndex;
using mc::BatteryCardView;
// Context keys preserve OEM mode names in EVERY language. In particular Healthy
// is a battery mode here, not the inventory health status translated as Исправен.
// Both OS adapters retain these source keys during language changes.
inline constexpr std::array<const char*,3> batteryVoltageNames{
    "@battery-mode:Full Capacity","@battery-mode:Balanced","@battery-mode:Healthy"};
inline constexpr std::array<const char*,3> lenovoChargeModeNames{"Normal","Conservation","Rapid"};
inline const auto& batteryModeNames(BatteryModeLabels style)noexcept{return style==BatteryModeLabels::ChargeModes?lenovoChargeModeNames:batteryVoltageNames;}
// Inventory/unknown profiles have a local view, without borrowing another
// laptop's settings file. Reuse the core resolver instead of another ID list.
using mc::batteryCardPreferencesPath;
using mc::initialBatteryCardView;
inline constexpr const char* batteryRestartCaption="Возобновить заряд ниже";
inline constexpr const char* combinedBatteryAgingCaption="Обход старения";
inline constexpr std::array<const char*,3> ecPolicyButtonNames{"4/FF","7/04","7/FF"};
inline constexpr int ecPolicyButtonCount(std::string_view profile) noexcept {return nativeEcPolicyProfile(profile)?3:2;}
inline int ecPolicySelection(std::optional<std::uint8_t> platform,std::optional<std::uint8_t> rom){
    if(!platform||!rom)return -1;
    const auto policy=observedBatteryEcPolicy(*platform,*rom);return policy?static_cast<int>(*policy):-1;
}
inline std::string ecPolicyCaption(std::optional<std::uint8_t> platform,std::optional<std::uint8_t> rom){
    const char* digits="0123456789ABCDEF";
    std::string romText="—";
    if(rom)romText=std::string{digits[*rom>>4],digits[*rom&15]};
    return formatMessage("Платформа {platform} · ROM {rom}",{{"platform",platform?std::to_string(*platform):"—"},{"rom",romText}});
}
// Raw EC identity, not inferred model/factory defaults. These technical field
// names remain English in every locale; missing bytes stay unknown.
inline std::string batteryIdentityCaption(std::optional<std::uint8_t> project,std::optional<std::uint8_t> platform,std::optional<std::uint8_t> rom){
    const char* digits="0123456789ABCDEF";
    const auto romText=rom?std::string{digits[*rom>>4],digits[*rom&15]}:std::string("—");
    return "Project ID "+(project?std::to_string(*project):"—")+" · Platform ID "+
        (platform?std::to_string(*platform):"—")+" · ROM ID "+romText;
}
// This caption describes the Uniwill register map, not a generic EC identity.
inline bool showBatteryEcIdentity(const DeviceProfile& profile)noexcept{return uniwillIdentityHistoryProfile(profile);}
// One catalogue drives reader, logging and row count: no forgotten candidates.
inline constexpr int primaryBatteryRows=14; // Seven rows per source, including EC exposure history.
inline constexpr int osBatteryDetailRows=27;
inline constexpr int ecBatteryDetailRows = 29 + static_cast<int>(std::size(uniwill::x5sp4nag::batteryCandidates)+std::size(uniwill::x5sp4nag::batteryCandidateBits));
inline const auto& batteryPolicyNames() noexcept {
    return batteryThresholdNames;
}
inline std::string batteryVoltageTargets(std::optional<int> base,std::optional<int> target){
    if(!base||!target)return translate("Зарядное напряжение: данные не прочитаны");
    return formatMessage("BMS {base} → цель EC {target} В",
        {{"base",displayNumber(*base/1000.0,2)},{"target",displayNumber(*target/1000.0,2)}});
}
// The EC field is a candidate adapter rating, never measured input power or a
// connector identity. A retained EC value must not be shown as current.
inline std::optional<std::string> lenovoAdapterPowerSummary(std::string_view profile,bool preview,const BatteryReading& os,const EcBatteryReading& ec){
    if(profile!="lenovo-legion-kwcn")return {};
    if(preview)return translate("БП EC · мощность не определена");
    if(!os.onlineKnown||!os.online||os.dynamicStale)return {};
    const auto now=batteryMonotonicMs();
    const auto sampled=ec.adapterCandidateAtMs;
    const bool fresh=sampled&&now>=sampled&&now-sampled<=15000;
    if(ec.profileId==profile&&ec.coherent&&!ec.retainedForDisplay&&!ec.battery.dynamicStale&&fresh&&ec.adapterWattCandidate){
        const auto power=std::to_string(*ec.adapterWattCandidate);
        return formatMessage("БП EC ≈ {power} Вт · вход не определён",{{"power",power}});
    }
    return translate("БП EC · мощность не определена");
}
}
