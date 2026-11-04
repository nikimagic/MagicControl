#include "FanCurveStorageWindows.h"
#include "FanControlWindows.h"
#include "NativeFixedFans.h"
#include "MagicControl/CustomPresets.h"
#include "MagicControl/DeviceProfile.h"
#include <exception>
#include <system_error>

namespace mc::windows {
FanCurveLoadResult loadLocalFanCurves(std::string_view profile, bool preview,
                                      FanCurveProfiles& profiles, FanCurveStorage& storage) {
    for(const auto* p:deviceProfiles())if(p->id==profile)for(int ch=0;ch<2;++ch)profiles.setActivePointCount(ch,p->fanPointCounts[ch]);
    try { storage.path = portableSettingsPath(std::string(gmFanCurveFile(profile))); }
    catch (const std::exception& e) { storage.error=e.what(); return FanCurveLoadResult::PathError; }
    if(storage.path.empty())return FanCurveLoadResult::NoPath;
    std::error_code error;
    if(std::filesystem::exists(storage.path,error)&&!error){
        if(loadFanCurveProfiles(storage.path,profiles,storage.error)){
            for(const auto* p:deviceProfiles())if(p->id==profile)for(int ch=0;ch<2;++ch)profiles.setActivePointCount(ch,p->fanPointCounts[ch]);
            storage.loaded=true;return FanCurveLoadResult::Loaded;
        }
        return FanCurveLoadResult::Failed;
    }
    if(!preview&&l16Profile(profile)&&!error){
        // Preserve the existing L16-only default-file creation; it does not
        // apply EC tables or derive a curve from another device.
        storage.loaded=saveFanCurveProfiles(storage.path,profiles,storage.error);
        return storage.loaded?FanCurveLoadResult::L16Created:FanCurveLoadResult::L16CreateFailed;
    }
    return FanCurveLoadResult::NoFile;
}
bool saveLocalFanCurves(bool preview,const FanCurveProfiles& profiles,FanCurveStorage& storage){
    if(preview||storage.path.empty())return false;
    if(!saveFanCurveProfiles(storage.path,profiles,storage.error))return false;
    storage.loaded=true;return true;
}
void rememberFanChoice(std::string_view id,const FanWriteRequest& request,const NativeFixedFanState* actual){
    if(request.origin==FanWriteOrigin::Restore)return;
    if(request.operation!=FanResourceOperation::SaveDraft&&(!actual||!actual->curves))return;
    // The old CLI fixed-target probe is temporary control, not a curve editor.
    if(request.operation==FanResourceOperation::Manual&&request.target&&!request.profiles&&!request.curves)return;
    const DeviceProfile* device{};for(const auto* p:deviceProfiles())if(p->id==id)device=p;
    if(!device)throw std::runtime_error("FAN settings: unknown profile");
    FanCurveProfiles saved;for(int ch=0;ch<2;++ch)saved.setActivePointCount(ch,device->fanPointCounts[ch]);
    const auto path=portableSettingsPath(std::string(gmFanCurveFile(id)));std::string error;
    if(std::filesystem::exists(path)&&!loadFanCurveProfiles(path,saved,error))throw std::runtime_error(error);
    if(request.operation==FanResourceOperation::Maximum&&!std::filesystem::exists(path)){
        // First Max must retain a migrated/model or common Manual file too;
        // creating the selection file is not permission to invent new curves.
        auto source=settingsReadPath(path);
        if(!std::filesystem::exists(source))source=settingsReadPath(portableSettingsPath("fan-curves.csv"));
        if(std::filesystem::exists(source)&&!loadFanCurveProfiles(source,saved,error))throw std::runtime_error(error);
        if(!std::filesystem::exists(source)&&actual&&actual->curves)saved.curves[0]=actual->curves->draft;
    }
    const auto priorSelection=saved.selected;const bool priorRestore=saved.restoreSelected;
    // Maximum owns the selected mode only. A carried restore/UI payload is
    // not permission to replace any of the sixteen saved Manual cells.
    if(request.profiles&&request.operation!=FanResourceOperation::Maximum)saved=*request.profiles;
    if(request.operation==FanResourceOperation::SaveDraft){saved.selected=priorSelection;saved.restoreSelected=priorRestore;}
    for(int ch=0;ch<2;++ch){saved.setActivePointCount(ch,device->fanPointCounts[ch]);
        if(request.curves&&request.operation!=FanResourceOperation::Maximum)for(int i=0;i<16;++i)saved.curves[0][ch][i]={(*request.curves)[ch][i].up,(*request.curves)[ch][i].duty};
    }
    if(request.operation!=FanResourceOperation::SaveDraft){
        if(actual->mode<0||actual->mode>3||actual->needsRestore)throw std::runtime_error("FAN selection not confirmed; settings retained");
        const auto selected=actual->mode==0?FanCurvePreset::Default:actual->mode==2?FanCurvePreset::Max:actual->mode==3?FanCurvePreset::Min:FanCurvePreset::Manual;
        saved.selected.fill(selected);saved.restoreSelected=true;
        if(selected==FanCurvePreset::Manual&&!request.profiles&&!request.curves)saved.curves[0]=actual->curves->draft;
    }
    saved.ecManual=false;saved.linked=actual&&actual->sharedCurve;
    if(!saveFanCurveProfiles(path,saved,error))throw std::runtime_error("FAN command confirmed, settings not saved: "+error);
}
bool syncConfirmedCustomFanCurve(std::string_view profile,const FanController& controller,
        std::optional<std::filesystem::file_time_type>& seen,FanCurveProfiles& profiles){
    const auto library=loadCustomPresetLibrary(profile);
    const auto path=settingsReadPath(portableSettingsPath(customPresetFilename(profile)));
    if(!library.active||!std::filesystem::exists(path))return false;
    const auto stamp=std::filesystem::last_write_time(path);
    const auto actual=controller.status();
    if(!actual.ecManual||actual.busy||actual.error||seen==stamp)return false;
    seen=stamp;
    profiles.ecManual=true;
    profiles.linked=true;
    profiles.selected.fill(FanCurvePreset::Manual);
    profiles.ecManualCurve=actual.applied[0];
    return true;
}
}
