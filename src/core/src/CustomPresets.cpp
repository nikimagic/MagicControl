#include "MagicControl/CustomPresets.h"
#include "MagicControl/CustomPresetValidation.h"
#include "MagicControl/PortableSettings.h"
#include "MagicControl/DeviceProfile.h"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <locale>
#include <stdexcept>
namespace mc {
namespace {
const DeviceProfile& profileFor(std::string_view id){for(const auto* p:deviceProfiles())if(p->id==id&&id!="unknown")return *p;throw std::runtime_error("Custom presets: unknown profile");}
CustomPreset readyPreset(std::string_view id,int slot){
    CustomPreset p;p.powerRecipe=slot+1;
    FanCurveProfiles curves;std::string error;
    const auto& device=profileFor(id);p.respective=device.customPreset.respective;for(int ch=0;ch<2;++ch)curves.setActivePointCount(ch,device.fanPointCounts[ch]);
    auto source=portableSettingsPath(std::string(gmFanCurveFile(id)));
    if(!std::filesystem::exists(settingsReadPath(source)))source=portableSettingsPath("fan-curves.csv");
    if(!loadFanCurveProfiles(source,curves,error))throw std::runtime_error(error.empty()?"Custom presets: fan-curves.csv missing":error);
    std::ifstream format(settingsReadPath(source));std::string first;std::getline(format,first);
    p.curvesReady=!device.customPreset.nativeCurveDraft||first.starts_with("Version,4");
    // Use the already validated owner curve, never another board's EC defaults.
    for(int ch=0;ch<2;++ch)for(int i=0;i<16;++i){const auto& t=device.customPreset.nativeCurveDraft&&p.curvesReady?curves.curves[0][ch]:curves.ecManualCurve;
        const int next=i<15?t[i+1].temperature:255;
        p.curves[ch][i]={t[i].temperature,next==255?255:std::max(0,next-3),t[i].duty};}
    return p;
}
}
std::string customPresetFilename(std::string_view id){const auto name=profileFor(id).customPreset.legacyFilename;return name.empty()?std::string(id)+"-custom-presets.txt":std::string(name);}
void validateCustomPreset(std::string_view id,const CustomPreset& p){
    validateCustomPresetForProfile(profileFor(id),p);
}
CustomPresetLibrary loadCustomPresetLibrary(std::string_view id){
    const auto path=settingsReadPath(portableSettingsPath(customPresetFilename(id)));CustomPresetLibrary lib;
    if(std::filesystem::exists(path)){
        if(std::filesystem::file_size(path)>16384)throw std::runtime_error("Custom presets: oversized file");
        std::ifstream in(path);in.imbue(std::locale::classic());std::string header,profile,headerLine;int version{};
        if(!std::getline(in,headerLine))throw std::runtime_error("Custom presets: missing header");
        std::istringstream fields(headerLine);fields.imbue(std::locale::classic());
        if(!(fields>>header>>version>>profile)||header!="MagicControlCustomPresets"||(version<1||version>5)||profile!=id)throw std::runtime_error("Custom presets: invalid header/profile");
        if(version>=2){int active{};if(!(fields>>lib.selected)||lib.selected<0||lib.selected>2)throw std::runtime_error("Custom presets: invalid selection");
            fields>>std::ws;if(!fields.eof()){if(!(fields>>active)||active<0||active>1)throw std::runtime_error("Custom presets: invalid active flag");lib.active=active;}}
        std::string headerExtra;if(fields>>headerExtra)throw std::runtime_error("Custom presets: trailing header data");
        for(int slot=0;slot<3;++slot){int index{},present{},split{};
            if(!(in>>index>>present)||index!=slot+1||(present!=0&&present!=1))throw std::runtime_error("Custom presets: invalid slot");
            if(!present)continue;CustomPreset p;
            if(!(in>>p.power[0]>>p.power[1]>>p.power[2]>>split)||(split!=0&&split!=1))throw std::runtime_error("Custom presets: invalid limits");p.respective=split;
            if(version>=2&&!(in>>p.powerRecipe))throw std::runtime_error("Custom presets: missing recipe");
            p.curvesReady=!profileFor(id).customPreset.nativeCurveDraft;
            if(version>=4){int ready{};if(!(in>>ready)||(ready!=0&&ready!=1))throw std::runtime_error("Custom presets: invalid curve readiness");p.curvesReady=ready;}
            for(auto& c:p.curves)for(auto& v:c)if(!(in>>v.up>>v.down>>v.duty))throw std::runtime_error("Custom presets: incomplete curves");
            if(version==3){int native{};if(!(in>>native)||(native!=0&&native!=1))throw std::runtime_error("Custom presets: invalid native kind");
                if(native){NativeFanCurves legacy;for(auto& c:legacy)for(int i=0;i<8;++i)if(!(in>>c[i].temperature>>c[i].duty))throw std::runtime_error("Custom presets: incomplete native bands");
                    if(!profileFor(id).customPreset.legacyNativeV3)throw std::runtime_error("Custom presets: unsupported legacy native format");
                    for(int ch=0;ch<2;++ch)for(int i=0;i<profileFor(id).fanPointCounts[ch];++i)p.curves[ch][i]={legacy[ch][i].temperature,i?legacy[ch][i-1].temperature:0,legacy[ch][i].duty};p.curvesReady=true;}}
            if(version>=5){std::size_t count{};if(!(in>>count)||count>32)throw std::runtime_error("Custom presets: invalid power option count");for(std::size_t i=0;i<count;++i){std::string key;int value{};if(!(in>>key>>value)||!p.powerOptions.emplace(key,value).second)throw std::runtime_error("Custom presets: incomplete/duplicate optional power settings");}}
            validateCustomPreset(id,p);lib.slots[slot]=p;
        }
        std::string extra;if(in>>extra||!in.eof())throw std::runtime_error("Custom presets: trailing data/read error");
    }
    for(int slot=0;slot<3;++slot)if(!lib.slots[slot]){auto p=readyPreset(id,slot);validateCustomPreset(id,p);lib.slots[slot]=p;}
    return lib;
}
void saveCustomPresetLibrary(std::string_view id,const CustomPresetLibrary& lib){
    if(lib.selected<0||lib.selected>2)throw std::runtime_error("Custom presets: invalid selection");
    const bool options=std::any_of(lib.slots.begin(),lib.slots.end(),[](const auto& p){return p&&!p->powerOptions.empty();});
    const bool extended=options||profileFor(id).fanPointCounts!=std::array<int,2>{16,16}||std::any_of(lib.slots.begin(),lib.slots.end(),[](const auto& p){return p&&!p->curvesReady;});
    const auto target=portableSettingsPath(customPresetFilename(id));
    if(extended&&std::filesystem::exists(target)){
        std::ifstream previous(target);std::string first;std::getline(previous,first);
        if((options&&!first.starts_with("MagicControlCustomPresets 5 "))||(!options&&!first.starts_with("MagicControlCustomPresets 4 "))){const auto backup=target.parent_path()/"legacy-backup"/(target.filename().string()+(options?".before-power-options":".before-shared-curves"));std::filesystem::create_directories(backup.parent_path());if(!std::filesystem::exists(backup))std::filesystem::copy_file(target,backup);}
    }
    std::ostringstream out;out.imbue(std::locale::classic());out<<"MagicControlCustomPresets "<<(options?5:extended?4:2)<<' '<<id<<' '<<lib.selected<<' '<<lib.active<<'\n';
    for(int slot=0;slot<3;++slot){out<<slot+1<<' '<<bool(lib.slots[slot])<<'\n';if(!lib.slots[slot])continue;
        const auto& p=*lib.slots[slot];validateCustomPreset(id,p);out<<p.power[0]<<' '<<p.power[1]<<' '<<p.power[2]<<' '<<p.respective<<' '<<p.powerRecipe;if(extended)out<<' '<<p.curvesReady;out<<'\n';
        for(const auto& c:p.curves)for(const auto& v:c)out<<v.up<<' '<<v.down<<' '<<v.duty<<'\n';
        if(options){out<<p.powerOptions.size()<<'\n';for(const auto& [key,value]:p.powerOptions)out<<key<<' '<<value<<'\n';}
        }
    std::string error;if(!savePortableText(target,out.str(),error))throw std::runtime_error("Custom presets: "+error);
}
void ensureCustomPresets(std::string_view id){auto lib=loadCustomPresetLibrary(id);saveCustomPresetLibrary(id,lib);}
void selectCustomPresetSlot(std::string_view id,int slot){auto lib=loadCustomPresetLibrary(id);lib.selected=slot;lib.active=true;saveCustomPresetLibrary(id,lib);}
void setCustomPresetActive(std::string_view id,bool active){
    // No saved library means no active Custom selection. Clearing it must not
    // load/create factory FAN drafts or fail a native CPU command for a missing CSV.
    if(!active&&!std::filesystem::exists(settingsReadPath(portableSettingsPath(customPresetFilename(id)))))return;
    auto lib=loadCustomPresetLibrary(id);if(lib.active==active)return;lib.active=active;saveCustomPresetLibrary(id,lib);
}
std::string customManualCaption(std::string_view id,bool custom,bool requireSavedActive){
    if(!custom||id.empty())return "Manual";
    try{const auto lib=loadCustomPresetLibrary(id);if(!requireSavedActive||lib.active)return "Manual\n"+std::to_string(lib.selected+1);}catch(...){}
    return "Manual";
}

}
