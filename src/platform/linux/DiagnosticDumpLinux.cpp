#include "../common/DiagnosticDumpInternal.h"
#include "../common/DisplayEvidence.h"
#include "MagicControl/LaptopControl.h"
#include <format>
#include <filesystem>
#include <fstream>
#include <cerrno>
namespace mc::diagnostics {
std::string capturePlatformDisplays(std::stop_token token){
    DisplayEvidence out;out.source="Linux sysfs DRM connector metadata (non-GUI fallback)";
    out.status="Current compositor mode/refresh/scaling unavailable in this process; sysfs supported modes are not current modes";
    out.cancelled=token.stop_requested();if(out.cancelled){out.status="Display fallback cancelled before enumeration";return displayEvidenceJson(out);}
    out.issues.push_back({"enumeration/current_mode/user_scale","Linux platform","No compositor/XRandR reader in the platform worker; GUI callers can supply a GTK main-thread display snapshot",{}, {}});
    const auto read=[&](const std::filesystem::path& path)->std::optional<std::string>{
        errno=0;std::ifstream input(path);if(!input)return {};
        std::string text;std::getline(input,text);if(text.size()>4096)text.resize(4096);return text;
    };
    std::error_code error;unsigned count{};
    for(std::filesystem::directory_iterator it("/sys/class/drm",error),end;!error&&it!=end;it.increment(error)){
        if(token.stop_requested()){out.cancelled=true;break;}if(++count>512){out.issues.push_back({"connector_scan","sysfs","Connector safety limit reached",{}, {}});break;}
        const auto status=read(it->path()/"status");if(!status)continue;
        DisplayReading row;row.id=row.name=it->path().filename().string();row.source=out.source;
        row.connected=*status=="connected"?std::optional<bool>(true):*status=="disconnected"?std::optional<bool>(false):std::nullopt;
        row.status="Connector evidence only; active/current desktop state not inferred";
        const auto enabled=read(it->path()/"enabled");const auto firstSupported=read(it->path()/"modes");
        row.metadataJson="{\"connectorStatus\":"+firmwareQuote(*status)+",\"encoderEnabledRaw\":"+(enabled?firmwareQuote(*enabled):"null")+
            ",\"firstSupportedModeNotCurrent\":"+(firstSupported?firmwareQuote(*firstSupported):"null")+"}";
        row.issues.push_back({"active/primary/pixelWidth/pixelHeight/refreshHz/scalePercent/effectiveDpi","sysfs DRM","Connector status/encoder/mode-list do not establish compositor current physical mode, refresh, primary or user scale",{}, {}});
        out.displays.push_back(std::move(row));
    }
    if(error)out.issues.push_back({"connector_scan","/sys/class/drm",error.message(),"errno",static_cast<std::uint32_t>(error.value())});
    out.cancelled=out.cancelled||token.stop_requested();
    out.metadataJson="{\"guiSnapshotProvided\":false,\"edidPreferredUsedAsCurrent\":false}";
    return displayEvidenceJson(out);
}
DependencySnapshot platformDependencies(){
    DependencySnapshot snapshot;snapshot.status="PawnIO is a Windows driver; not applicable on Linux";return snapshot;
}
std::string capturePlatformKeyboardContext(std::stop_token token){
    if(token.stop_requested())return {};
    auto typed=createLaptopControl();if(!typed)return {};
    const auto state=typed->read();
    return std::format("{{\"schema\":1,\"read_only\":true,\"source\":\"native sysfs; only real attributes\",\"selected_level\":{},\"timeout_seconds\":{},\"brightness_writable\":{},\"timeout_writable\":{},\"physical_response_verified\":false}}\n",
        state.keyboardLevel?std::to_string(*state.keyboardLevel):"null",state.keyboardTimeoutSeconds?std::to_string(*state.keyboardTimeoutSeconds):"null",state.keyboardWritable,state.keyboardTimeoutWritable);
}
FirmwareDefaultResult preparePlatformFanDefault(EcAccess*,const FirmwareFanDefault&,std::stop_token){
    return {false,false,"Linux: подтверждённый EC Default adapter не подключён; только пассивная диагностика",{}};
}
std::string platformFanTables(const EcDump&,bool){return "{\"schema\":1,\"default_command_verified\":false,\"firmware_internal_table_exported\":false,\"status\":\"Linux EC fan-table transport unavailable\"}\n";}
std::optional<bool> platformSnapshotFanDefault(const EcDump&){return {};}
FirmwareFactoryResult capturePlatformFactoryTables(std::stop_token){
    return {false,false,true,{},"Linux: подтверждённый factory mailbox transport не подключён"};
}
EcDump capturePlatformEc(EcAccess*,std::stop_token){
    EcDump dump;dump.source="Linux";
    // ec_sys only exposes256bytes, while our EC RAM spans0x000–0xFFF.
    // Never present that small window as a complete model-specific dump.
    dump.status="Полный EC RAM транспорт Linux пока не подключён. Сохранены системные сведения и доступная NVIDIA NVML диагностика";
    try{if(auto typed=createLaptopControl()){const auto s=typed->read();dump.interfaceNotes=std::format("OEM/sysfs: {}\nСостояние: {}\nПодсветка level={} writable={}\nПороги батареи start={} end={} writable={}\n",s.source,s.status,s.keyboardLevel.value_or(-1),s.keyboardWritable,s.chargeStart.value_or(-1),s.chargeEnd.value_or(-1),s.chargeWritable);}}
    catch(const std::exception& e){dump.interfaceNotes=e.what();}
    return dump;
}
FanMaximumResult capturePlatformMaximum(EcAccess*,const MaximumFanProbe&,std::stop_token){
    FanMaximumResult result;result.source="Linux EC";result.status="Проба Max EC Linux не подключена";return result;
}
FanMaximumSample readPlatformFans(EcAccess*){return {};}
}
