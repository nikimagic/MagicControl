#pragma once
#include "MagicControl/PortableSettings.h"
#include "MagicControl/FanResourceControl.h"
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace mc::windows {
class FanController;
struct NativeFixedFanState;
enum class FanCurveLoadResult { NoPath, NoFile, Loaded, Failed, L16Created, L16CreateFailed, PathError };
struct FanCurveStorage {
    std::filesystem::path path;
    std::string error;
    bool loaded{};
};
// Persistence only: no curve conversion, EC access, or change of fan mode.
FanCurveLoadResult loadLocalFanCurves(std::string_view profile, bool preview,
                                      FanCurveProfiles& profiles, FanCurveStorage& storage);
bool saveLocalFanCurves(bool preview, const FanCurveProfiles& profiles, FanCurveStorage& storage);
void rememberFanChoice(std::string_view,const FanWriteRequest&,const NativeFixedFanState* =nullptr);
// Pull only an already applied EC Manual table after the active Custom file
// changes. This preserves the old exact table copy; no curves are generated.
bool syncConfirmedCustomFanCurve(std::string_view profile,const FanController&,
    std::optional<std::filesystem::file_time_type>& seen,FanCurveProfiles&);
}
