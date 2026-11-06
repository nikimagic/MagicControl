#include "MagicControl/ResourceControl.h"
#include "DiagnosticDumpInternal.h"
#include "DisplayEvidence.h"
#include "MagicControl/Json.h" // Reuse the existing bounded strict JSON reader.
#include "MagicControl/BoardIdentity.h"
#include "MagicControl/DeviceProfile.h"
#include "MagicControl/HardwareInventory.h"
#include "MagicControl/LightingZones.h"
#include "MagicControl/LiquidCooling.h"
#include "MagicControl/Logging.h"
#include "MagicControl/ActionLog.h"
#include "MagicControl/Monitoring.h"
#include "MagicControl/NvidiaGpu.h"
#include "MagicControl/IntelGpu.h"
#include "MagicControl/PortableSettings.h"
#include "MagicControl/CustomPresets.h"
#include "MagicControl/Version.h"
#include "MagicControl/DeviceOperation.h"
#include "MagicControl/Errors.h"
#include "MagicControl/FileStorage.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <fstream>
#include <iomanip>
#include <locale>
#include <sstream>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <future>
#include <map>
namespace mc {
namespace {
// Same INFO transcript/audit binding on both OSes: no UI worker, extra hardware
// polling or dependence on AllLogs. Display failure cannot change dump results.
void diagnosticStage(std::string_view stage,std::string_view message)noexcept{
    try{recordLog("diagnostic_dump","\"event\":\"stage\",\"stage\":"+audit::quote(stage)+",\"message\":"+audit::quote(message));}catch(...){}
}
std::string utc(){return std::format("{:%Y-%m-%dT%H:%M:%SZ}",std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));}
std::string pathText(const std::filesystem::path& p){const auto s=p.u8string();return {reinterpret_cast<const char*>(s.data()),s.size()};}
std::string quote(std::string_view s){
    std::string out="\"";for(unsigned char c:s){
        if(c=='"'||c=='\\'){out+='\\';out+=static_cast<char>(c);}
        else if(c<32)out+=std::format("\\u{:04x}",c);else out+=static_cast<char>(c);
    }return out+'"';
}
std::string lightingEvidenceJson(const HardwareInventory& inventory,std::string_view profile){
    bool summary{},complete{},invalid{};std::size_t count{};std::string records;
    for(const auto& section:inventory.sections)if(section.title==lightingEvidenceSectionTitle)for(const auto& row:section.rows){
        std::string data;
        try{const auto parsed=mc::json::JsonReader::parse(row.value);
            if(!std::holds_alternative<mc::json::Json::Object>(parsed.value))throw std::runtime_error("Not an evidence object");
            data=row.value;
            if(row.name=="summary"){summary=true;complete=parsed.at("complete").flag();}
        }catch(...){invalid=true;data="{\"status\":\"partial_or_error\",\"detail\":"+quote(row.value)+"}";}
        if(count++)records+=',';records+="{\"kind\":"+quote(row.name)+",\"data\":"+data+"}";
    }
    return "{\"schema\":1,\"app_version\":"+quote(MAGICCONTROL_VERSION_STRING)+",\"actual_profile\":"+quote(profile)+",\"captured_utc\":"+quote(utc())+
        ",\"status\":"+quote(!count?"not_collected_or_backend_unavailable":summary&&complete&&!invalid?"complete":"partial")+
        ",\"collection_complete\":"+(summary&&complete&&!invalid?"true":"false")+",\"physical_zones_confirmed\":false,\"ec_access_by_this_getter\":false,\"lighting_effect_writes\":false,\"record_count\":"+
        std::to_string(count)+",\"records\":["+records+"]}\n";
}
template<class T>std::string number(std::optional<T> v){
    if(!v||!std::isfinite(static_cast<double>(*v)))return "null";
    std::ostringstream s;s.imbue(std::locale::classic());s<<std::setprecision(12)<<*v;return s.str();
}
void textFile(const std::filesystem::path& p,std::string_view text){
    std::ofstream f(p,std::ios::binary|std::ios::trunc);f.write(text.data(),static_cast<std::streamsize>(text.size()));f.close();
    if(!f)throw std::runtime_error("Не удалось сохранить "+pathText(p));
}
std::string dependencySnapshotJson(){
    const auto snapshot=diagnostics::platformDependencies();
    std::string json="{\"utc\":"+quote(utc())+",\"pawnio_applicable\":"+(snapshot.pawnIoApplicable?"true":"false")+
        ",\"status\":"+quote(snapshot.status)+",\"error\":"+quote(snapshot.error)+",\"driver\":";
    // A failed SCM query stays null; a confirmed missing service is installed=false.
    if(snapshot.pawnIo){const auto& s=*snapshot.pawnIo;
        json+=std::format("{{\"installed\":{},\"running\":{},\"service_state\":{},\"win32_exit_code\":{},\"service_exit_code\":{},\"service_type\":{},\"start_type\":{},\"image_path\":{},\"resolved_image_path\":{},\"file_version\":{},\"version_status\":{},\"loaded_version\":{},\"loaded_version_raw\":{},\"loaded_version_status\":{},\"driver_version\":{},\"version_source\":{},\"file_version_source\":\"installed SYS VERSIONINFO\",\"bundled_setup_version\":\"2.2.0\"}}",
            s.installed,s.running,s.serviceState,s.win32ExitCode,s.serviceExitCode,s.serviceType,s.startType,quote(pathText(std::filesystem::path(s.binaryPath))),
            quote(pathText(std::filesystem::path(s.resolvedBinaryPath))),s.fileVersion.empty()?"null":quote(s.fileVersion),quote(s.versionStatus),
            s.loadedVersion.empty()?"null":quote(s.loadedVersion),number(s.loadedVersionRaw),quote(s.loadedVersionStatus),
            s.version().empty()?"null":quote(s.version()),quote(s.versionSource()));
    }else json+="null";
    json+=",\"runtime_storage\":"+quote(snapshot.pawnIoRuntimeStorage)+",\"runtime_directory\":"+
        (snapshot.pawnIoApplicable?quote(pathText(snapshot.pawnIoRuntimeDirectory)):"null")+",\"runtime_files\":[";
    for(std::size_t i=0;i<snapshot.pawnIoRuntimeFiles.size();++i){const auto& file=snapshot.pawnIoRuntimeFiles[i];if(i)json+=',';
        json+="{\"name\":"+quote(file.name)+",\"present\":"+(file.present?(*file.present?"true":"false"):"null")+
            ",\"bytes\":"+number(file.bytes)+",\"sha256\":"+quote(file.sha256)+",\"error\":"+quote(file.error)+"}";
    }
    return json+"],\"runtime_check\":\"Embedded resource presence, size and SHA256; no extraction or driver start. Providers verify and pin cache bytes before loading\"}";
}
std::string dependencyDocument(const std::string& before,const std::string& after="null"){
    return "{\"schema\":1,\"probe\":\"Read-only SCM/file metadata and loaded driver version IOCTL; no service start, module load/execute or hardware IO\",\"before\":"+before+",\"after\":"+after+"}\n";
}
void binaryFile(const std::filesystem::path& p,const std::vector<std::uint8_t>& bytes){
    // A later probe or power loss must not leave the completed raw capture
    // only in the stream/cache. Reuse the shared flushed atomic publisher.
    std::string error;
    if(!detail::writeStorageTextAtomically(p,{reinterpret_cast<const char*>(bytes.data()),bytes.size()},error))
        throw std::runtime_error("Не удалось сохранить "+pathText(p)+": "+error);
}
std::string captureFirmwareEvidence(const std::filesystem::path& directory,bool stateOnly,std::stop_token token,const std::shared_ptr<TelemetryGate::DiagnosticCollection>& collection){
    const auto started=utc();const auto board=readBoardDescriptor();std::vector<std::string> rows;
    diagnostics::FirmwareCaptureResult result;std::size_t payloadBytes{},metadataBytes{},failed{};bool owned{};
    const auto q=[](std::string_view text){return diagnostics::firmwareQuote(text);};
    auto checkpoint=std::chrono::steady_clock::now();
    const auto manifest=[&]{
        std::string text="{\"schema\":1,\"read_only\":true,\"is_bios_or_ec_flash_image\":false,\"snapshot_atomic\":false,\"state_only\":"+std::string(stateOnly?"true":"false")+
            ",\"started_utc\":"+q(started)+",\"updated_utc\":"+q(utc())+",\"actual_profile\":"+q(deviceProfile(board).id)+
            ",\"manufacturer\":"+q(board.manufacturer)+",\"baseboard\":"+q(board.baseBoard)+",\"product\":"+q(board.productName)+
            std::format(",\"capture_finished\":{},\"cancelled\":{},\"elapsed_ms\":{},\"payload_bytes\":{},\"unsuccessful_sources\":{},",result.finished,token.stop_requested(),result.elapsedMs,payloadBytes,failed)+
            "\"status\":"+q(result.status)+",\"limitations\":\"OS ACPI/SMBIOS and reviewed UEFI variables only; source availability/validation is per record. Windows API returns only first duplicate signature; registry cache and Linux tables are non-atomic boot/runtime evidence. Empty payloads are metadata-only, not padded files. JSONL is the incremental journal; manifest is checkpointed at most once per second and on completion. No AML execution, firmware writes or flash readout.\",\"records\":[";
        for(std::size_t i=0;i<rows.size();++i){if(i)text+=',';text+=rows[i];}return text+"]}\n";
    };
    try{
        if(!std::filesystem::create_directory(directory))throw std::runtime_error("Firmware evidence directory already exists");owned=true;
        result.status="Collecting read-only OS firmware evidence";textFile(directory/"manifest.json",manifest());
        std::ofstream journal(directory/"records.jsonl",std::ios::binary|std::ios::trunc);
        if(!journal)throw std::runtime_error("Cannot save firmware evidence journal");
        auto worker=diagnostics::createFastDiagnosticWorker(collection);
        result=worker->firmware(stateOnly,[&](const diagnostics::FirmwareRecord& record){
            if(record.kind.size()>64||record.name.size()>180||record.source.size()>65536||record.status.size()>16384||record.metadataJson.size()>1024*1024)
                throw std::runtime_error("Firmware metadata budget exceeded");
            std::string digest,hashError;bool saved{};
            if(!record.name.empty()&&!record.bytes.empty()){
                // Source registry/sysfs names are metadata, never output paths.
                // The fixed child can emit only a generated leaf in three lanes.
                const std::filesystem::path relative(record.name);auto it=relative.begin();
                if(relative.is_absolute()||it==relative.end()||record.name.find('\\')!=std::string::npos)
                    throw std::runtime_error("Invalid firmware artifact path");
                const auto lane=it->string();if(lane!="acpi"&&lane!="smbios"&&lane!="uefi")throw std::runtime_error("Unknown firmware artifact lane");
                if(++it==relative.end())throw std::runtime_error("Firmware artifact leaf is missing");const auto leaf=it->string();
                if(++it!=relative.end()||leaf=="."||leaf==".."||leaf.empty()||
                    leaf.find_first_of("<>:\"|?*")!=std::string::npos||
                    std::any_of(leaf.begin(),leaf.end(),[](unsigned char c){return c<32||c>126;}))
                    throw std::runtime_error("Invalid firmware artifact leaf");
                const auto folder=directory/lane;
                if(!std::filesystem::exists(folder))std::filesystem::create_directory(folder);
                if(std::filesystem::is_symlink(std::filesystem::symlink_status(folder)))throw std::runtime_error("Firmware artifact lane is a symlink");
                const auto path=directory/relative;
                if(std::filesystem::exists(std::filesystem::symlink_status(path)))throw std::runtime_error("Duplicate firmware artifact path");
                try{digest=diagnostics::firmwareSha256(record.bytes);}catch(const std::exception& e){hashError=e.what();}
                binaryFile(path,record.bytes);saved=true;
            }else if(!record.bytes.empty())throw std::runtime_error("Firmware payload has no generated filename");
            std::string metadata=record.metadataJson.empty()?"{}":record.metadataJson,metadataError;
            try{
                if(!diagnostics::firmwareValidUtf8(metadata))throw std::runtime_error("Invalid UTF-8 metadata");
                const auto parsed=mc::json::JsonReader::parse(metadata);
                if(!std::holds_alternative<mc::json::Json::Object>(parsed.value))throw std::runtime_error("Firmware metadata is not a JSON object");
            }catch(const std::exception& e){metadataError=e.what();metadata="null";}
            const bool textValid=diagnostics::firmwareValidUtf8(record.kind)&&diagnostics::firmwareValidUtf8(record.name)&&
                diagnostics::firmwareValidUtf8(record.source)&&diagnostics::firmwareValidUtf8(record.status)&&diagnostics::firmwareValidUtf8(record.errorDomain);
            const bool accepted=record.success&&metadataError.empty()&&textValid;
            payloadBytes+=record.bytes.size();if(!accepted)++failed;
            const auto row="{\"received_utc\":"+q(utc())+",\"kind\":"+q(record.kind)+",\"file\":"+q(record.name)+
                ",\"source\":"+q(record.source)+",\"status\":"+q(record.status)+",\"error_domain\":"+q(record.errorDomain)+
                ",\"error_code\":"+number(record.errorCode)+",\"attributes\":"+number(record.attributes)+
                std::format(",\"success\":{},\"source_success\":{},\"text_utf8_valid\":{},\"file_saved\":{},\"bytes\":{},\"empty_payload_metadata_only\":{},",accepted,record.success,textValid,saved,record.bytes.size(),record.bytes.empty())+
                "\"sha256\":"+(digest.empty()?"null":q(digest))+",\"hash_error\":"+q(hashError)+",\"metadata\":"+metadata+
                ",\"metadata_validation_error\":"+q(metadataError)+",\"metadata_quarantined_raw\":"+(metadataError.empty()?"null":q(record.metadataJson))+"}";
            metadataBytes+=row.size();if(metadataBytes>16*1024*1024)throw std::runtime_error("Firmware manifest budget exceeded");
            rows.push_back(row);journal<<row<<'\n';journal.flush();if(!journal)throw std::runtime_error("Cannot flush firmware evidence journal");
            if(std::chrono::steady_clock::now()-checkpoint>=std::chrono::seconds(1)){
                textFile(directory/"manifest.json",manifest());checkpoint=std::chrono::steady_clock::now();
            }
        },token,diagnostics::firmwareWorkerTimeout);
        textFile(directory/"manifest.json",manifest());
    }catch(const std::exception& e){result.finished=false;result.status=e.what();
        if(owned)try{textFile(directory/"manifest.json",manifest());}catch(...){}
    }
    return std::format("{}: finished={}, records={}, unsuccessful={}, elapsed={}ms; {}",pathText(directory.filename()),result.finished,rows.size(),failed,result.elapsedMs,result.status);
}
std::string batteryPolicyEvidence(const diagnostics::EcDump& ec,std::string_view profile){
    std::string json="{\"schema\":1,\"source\":\"initial EC RAM before telemetry/probes\",\"actual_profile\":"+diagnostics::firmwareQuote(profile)+
        ",\"read_only\":true,\"percentage_enforcement_verified\":false,\"restart_execution_verified\":false,";
    if(profile!="wu-jie-15-xpro"&&!gmRamFanProfile(profile)&&profile!="eluktronics-hydroc16g2")
        return json+"\"status\":\"Uniwill candidate map not applicable to this actual board; see OEM interface notes and battery telemetry\"}\n";
    json+="\"status\":\"Raw candidate fields only; voltage profiles and stored percentages are not proof of cutoff\",\"restart_role\":"+
        quote(gmAmdProfile(profile)?"DBD1_not_charge_restart":"candidate_unverified")+",\"registers\":[";bool first=true;
    for(const auto address:{0x740u,0x742u,0x770u,0x78Eu,0x7A6u,0x7B9u,0x7C3u,0x7D0u}){
        if(!first)json+=',';first=false;const bool valid=address>=ec.base&&address-ec.base<ec.valid.size()&&address-ec.base<ec.bytes.size()&&ec.valid[address-ec.base];
        json+=std::format("{{\"address\":{},\"valid\":{},\"raw\":{}}}",address,valid,valid?std::to_string(ec.bytes[address-ec.base]):"null");
    }
    return json+"]}\n";
}
std::string keyboardEvidence(const diagnostics::EcDump& ec,std::string_view profile){
    const auto at=[&](unsigned address)->std::optional<int>{
        if(address<ec.base||address-ec.base>=ec.bytes.size()||address-ec.base>=ec.valid.size()||!ec.valid[address-ec.base])return {};
        return ec.bytes[address-ec.base];};
    std::string json="{\"schema\":1,\"captured_utc\":"+quote(utc())+",\"actual_profile\":"+quote(profile)+",\"read_only\":true,\"physical_response_verified\":false,";
    if(!ec.keyboardJson.empty())return json+"\"typed_context\":"+ec.keyboardJson+"}\n";
    if(profile!="wu-jie-15-xpro"&&!gmRamFanProfile(profile))
        return json+"\"status\":"+quote(ec.keyboardStatus.empty()?"No admitted EC RGB map; see native HID/LED inventory and its completeness/errors":ec.keyboardStatus)+"}\n";
    json+="\"source\":\"initial EC block snapshot; fields non-atomic\",\"selected_level\":"+(at(0x78c)?std::to_string((*at(0x78c)>>5)&7):"null")+
        ",\"power_disabled\":"+(at(0x78c)?std::string((*at(0x78c)&2)?"true":"false"):"null")+",\"registers\":[";
    bool first=true;for(const auto address:{0x740u,0x741u,0x766u,0x767u,0x769u,0x76au,0x76bu,0x76cu,0x76du,0x76eu,0x78cu,0x78eu,0x7c5u}){
        if(!first)json+=',';first=false;json+=std::format("{{\"address\":{},\"valid\":{},\"raw\":{}}}",address,at(address).has_value(),number(at(address)));}
    return json+"],\"note\":\"Selected brightness, idle power-disable, saved preference and optical output are distinct\"}\n";
}
std::string sensorsJson(const std::vector<SensorReading>& values);
std::string gpuReadingsJson(const std::vector<GpuReading>& values,std::string_view source="NVIDIA NVML"){
    std::string json="{\"schema\":1,\"source\":"+quote(source)+",\"vbios_image\":false,\"units\":{\"temperature\":\"C\",\"clock\":\"MHz\",\"power\":\"mW\",\"memory\":\"bytes\"},\"devices\":[";
    for(std::size_t i=0;i<values.size();++i){const auto& r=values[i];if(i)json+=',';
        json+=std::format("{{\"name\":{},\"uuid\":{},\"driver\":{},\"vbios_version\":{},\"available\":{},\"power_control_reported\":{},\"status\":{},",
            quote(r.name),quote(r.uuid),quote(r.driver),quote(r.vbios),r.available,r.powerControl,quote(r.status));
        json+=std::format("\"gpu_type\":{},\"telemetry_source\":{},\"fan_rpm\":{},\"hotspot_celsius\":{},\"power_setting_unit\":{},\"power_setting\":{},\"power_setting_min\":{},\"power_setting_max\":{},\"power_setting_default\":{},",
            quote(r.type==GpuType::Integrated?"integrated":r.type==GpuType::Discrete?"discrete":"unknown"),quote(r.source),number(r.fanRpm),number(r.hotspotCelsius),quote(r.powerUnit==GpuPowerUnit::Percent?"percent":"watts"),number(r.powerValue),number(r.powerMinimum),number(r.powerMaximum),number(r.powerDefault));
        json+=std::format("\"temperature_c\":{},\"gpu_usage_percent\":{},\"memory_usage_percent\":{},\"graphics_mhz\":{},\"memory_mhz\":{},\"pstate\":{},",
            number(r.temperature),number(r.utilization),number(r.memoryUtilization),number(r.graphicsMhz),number(r.memoryMhz),number(r.pstate));
        json+=std::format("\"pci\":{},\"memory_temperature_c\":{},\"memory_junction_c\":{},\"power_source\":{},",quote(r.pci),number(r.memoryCelsius),number(r.memoryJunctionCelsius),quote(r.powerSource));
        json+="\"temperature_source\":"+quote(r.temperatureSource)+",";
        json+="\"physical_id\":"+quote(r.physicalId)+",\"os_adapter_id\":"+quote(r.osAdapterId)+",\"identity_source\":"+quote(r.identitySource)+",\"identity_status\":"+quote(r.identityStatus)+
            ",\"pci_location\":{\"domain\":"+number(r.location.domain)+",\"bus\":"+number(r.location.bus)+",\"device\":"+number(r.location.device)+",\"function\":"+number(r.location.function)+",\"source\":"+quote(r.location.source)+"},\"power_domain\":"+quote(gpuPowerDomainName(r.powerDomain))+",";
        json+=std::format("\"power_mw\":{},\"limit_mw\":{},\"enforced_mw\":{},\"default_mw\":{},\"minimum_mw\":{},\"maximum_mw\":{},\"total_bytes\":{},\"used_bytes\":{},\"free_bytes\":{},",
            number(r.powerMw),number(r.limitMw),number(r.enforcedMw),number(r.defaultMw),number(r.minimumMw),number(r.maximumMw),number(r.totalBytes),number(r.usedBytes),number(r.freeBytes));
        json+="\"fan_count\":"+number(r.fanCount)+",\"fan_status\":"+quote(r.fanStatus)+",\"fans\":[";
        for(std::size_t f=0;f<r.fans.size();++f){if(f)json+=',';const auto& fan=r.fans[f];
            json+=std::format("{{\"index\":{},\"percent\":{},\"target_percent\":{},\"policy\":{},\"status\":{}}}",fan.index,number(fan.percent),number(fan.target),number(fan.policy),quote(fan.status));}
        json+="],\"provider_identities\":[";for(std::size_t p=0;p<r.providerIdentities.size();++p){if(p)json+=',';const auto& identity=r.providerIdentities[p];json+="{\"handle\":"+quote(identity.handle)+",\"source\":"+quote(identity.source)+",\"status\":"+quote(identity.status)+"}";}
        json+="],\"additional_sensors\":"+sensorsJson(r.additionalSensors)+",\"api_status\":[";for(std::size_t a=0;a<r.apiStatus.size();++a){if(a)json+=',';json+=quote(r.apiStatus[a]);}json+="]}";
    }return json+"]}\n";
}
std::string graphicsJson(const std::vector<GraphicsAdapter>& adapters){
    std::string json="{\"schema\":1,\"selection\":\"all adapters, not only primary\",\"workload_created\":false,\"adapters\":[";
    for(std::size_t i=0;i<adapters.size();++i){if(i)json+=',';const auto& a=adapters[i];
        json+=std::format("{{\"name\":{},\"id\":{},\"pci\":{},\"vendor_id\":{},\"device_id\":{},\"subsystem_id\":{},\"revision\":{},\"driver\":{},\"type\":{},\"primary\":{},\"primary_source\":{},\"software\":{},\"dedicated_memory_bytes\":{},\"shared_memory_bytes\":{},\"current_process_local_usage_bytes\":{},\"current_process_local_budget_bytes\":{},\"status\":{},\"intel_telemetry\":{}",
            quote(a.name),quote(a.id),quote(a.pci),a.vendorId,a.deviceId,a.subsystemId,a.revision,quote(a.driver),quote(a.type==GpuType::Integrated?"integrated":a.type==GpuType::Discrete?"discrete":"unknown"),a.primaryKnown?std::string(a.primary?"true":"false"):std::string("null"),quote(a.primarySource),a.software,number(a.dedicatedBytes),number(a.sharedBytes),number(a.processLocalUsageBytes),number(a.processLocalBudgetBytes),quote(a.status),
            quote(a.vendorId==0x8086?"see intel.json / sensors.jsonl; each Intel driver API reports availability":"not applicable"));
        // Extend this raw OS adapter record; do not collapse logical/provider
        // observations in the diagnostic files themselves.
        json+=",\"primary_known\":"+std::string(a.primaryKnown?"true":"false")+",\"boot_vga\":"+(a.bootVga?std::string(*a.bootVga?"true":"false"):std::string("null"))+",\"physical_id\":"+quote(a.physicalId)+",\"identity_source\":"+quote(a.identitySource)+",\"identity_status\":"+quote(a.identityStatus)+
            ",\"pci_location\":{\"domain\":"+number(a.location.domain)+",\"bus\":"+number(a.location.bus)+",\"device\":"+number(a.location.device)+",\"function\":"+number(a.location.function)+",\"source\":"+quote(a.location.source)+"}}";
    }return json+"]}\n";
}
std::string fanProbeJson(const FanMaximumResult& r){
    std::string json=std::format("{{\"source\":{},\"status\":{},\"attempted\":{},\"command_applied\":{},\"response_observed\":{},\"settled\":{},\"restored\":{},\"physical_rpm_channels\":[\"CPU EC\",\"GPU EC\"],\"before_rpm\":[{},{}],\"observed_peak_rpm\":[{},{}],\"samples\":[",
        quote(r.source),quote(r.status),r.attempted,r.commandApplied,r.responseObserved,r.settled,r.restored,number(r.beforeRpm[0]),number(r.beforeRpm[1]),number(r.peakRpm[0]),number(r.peakRpm[1]));
    for(std::size_t i=0;i<r.samples.size();++i){if(i)json+=',';const auto& s=r.samples[i];json+=std::format("{{\"elapsed_ms\":{},\"rpm\":[{},{}],\"source_percent\":[{},{}]}}",s.elapsedMs,number(s.rpm[0]),number(s.rpm[1]),number(s.percent[0]),number(s.percent[1]));}
    return json+"]}\n";
}
std::string ecTimingJson(const diagnostics::EcDump& dump){
    std::string json=std::format("{{\"schema\":1,\"elapsed_ms\":{},\"complete\":{},\"local_passive_ec_polling_paused\":{},\"atomic_snapshot\":false,\"timing_unit\":\"microseconds\",\"attempts\":[",dump.elapsedMs,dump.complete,dump.passivePollingPaused);
    for(std::size_t i=0;i<dump.readAttempts.size();++i){
        if(i)json+=',';const auto& a=dump.readAttempts[i];const auto& t=a.transport;
        json+=std::format("{{\"address\":{},\"count\":{},\"elapsed_us\":{},\"outcome\":{},\"transport_timing\":",a.address,a.count,a.elapsedUs,quote(a.outcome));
        if(t.detailed)json+=std::format("{{\"process_wait_us\":{},\"transport_wait_us\":{},\"io_us\":{},\"bytes_read\":{},\"max_byte_us\":{},\"max_byte_address\":{}}}",t.processWaitUs,t.transportWaitUs,t.ioUs,t.bytesRead,t.maxByteUs,(t.bytesRead||t.ioUs)?std::to_string(t.maxByteAddress):"null");
        else json+="null";
        json+='}';
    }
    return json+"]}\n";
}
std::string sensorsJson(const std::vector<SensorReading>& values){
    std::string json="[";
    for(std::size_t i=0;i<values.size();++i){if(i)json+=',';const auto& r=values[i];
        json+=std::format("{{\"device\":{},\"metric\":{},\"unit\":{},\"source\":{},\"status\":{},\"value\":{},\"raw\":{},\"sampled_at_monotonic_ms\":{}}}",
            quote(r.device),quote(r.metric),quote(r.unit),quote(r.source),quote(r.status),number(r.value),number(r.raw),r.sampledAtMs);
    }return json+"]";
}
std::string batteryReadingJson(const BatteryReading& b){
    return std::format("{{\"device_id\":{},\"model\":{},\"source\":{},\"online\":{},\"power_status\":{},\"present\":{},\"dynamic_stale\":{},\"percent\":{},\"watts\":{},\"volts\":{},\"milliamps\":{},\"celsius\":{},\"temperature_raw\":{},\"temperature_source\":{},\"temperature_status\":{},\"cycles\":{},\"health_percent\":{}}}",
        quote(b.deviceId),quote(b.model),quote(b.source),b.onlineKnown?(b.online?"true":"false"):"null",quote(b.powerStatus),b.present?(*b.present?"true":"false"):"null",b.dynamicStale,
        number(b.percent),number(b.watts),number(b.volts),number(b.milliamps),number(b.celsius),number(b.temperatureRaw),quote(b.temperatureSource),quote(b.temperatureStatus),number(b.cycles),number(b.healthPercent));
}
std::string batteryDocument(const MonitoringSnapshot& snapshot){
    auto json=batteryReadingJson(snapshot.battery);json.pop_back();
    json+=",\"health_stale\":"+std::string(snapshot.battery.healthStale?"true":"false")+",\"health_sampled_at_monotonic_ms\":"+std::to_string(snapshot.battery.healthAtMs)+",\"health_source\":"+quote(snapshot.battery.healthSource);
    const char* flow=snapshot.battery.flow==BatteryFlow::Charging?"charging":snapshot.battery.flow==BatteryFlow::Discharging?"discharging":snapshot.battery.flow==BatteryFlow::Idle?"idle":"unknown";
    json+=",\"flow\":"+quote(flow);
    json+=",\"primary_device_id\":"+quote(snapshot.battery.deviceId)+",\"devices\":[";
    for(std::size_t i=0;i<snapshot.batteries.size();++i){if(i)json+=',';json+=batteryReadingJson(snapshot.batteries[i]);}
    return json+"]}\n";
}
std::vector<SensorReading> monitoringSensors(const MonitoringSnapshot& s){
    auto rows=s.additionalSensors;
    const auto add=[&](std::string device,std::string metric,std::string unit,std::string source,std::optional<double> value,std::string status="",std::optional<double> raw={}){
        rows.push_back({std::move(device),std::move(metric),std::move(unit),std::move(source),status.empty()?(value?"ok":"not reported"):status,value,raw,s.processorSampledAtMs});
    };
    for(std::size_t i=0;i<2;++i){const auto& p=s.processors[i];const auto device=(i?"Dashboard GPU / ":"CPU / ")+s.processorModels[i];
        add(device,"temperature","C",p.temperatureSource,p.celsius,s.processorStatus);
        add(device,"driver_temperature","C","Driver API · independent channel",p.driverCelsius);
        add(device,"Tctl_temperature","C","PawnIO · Tctl",p.tctlCelsius);
        add(device,"usage","percent",p.usageSource,p.usagePercent);
        add(device,"frequency","MHz",p.clockSource,p.clockMhz);
        add(device,"power","W",p.powerSource,p.powerWatts);
        add(device,"fan_speed","RPM",p.fanSource,p.fanRpm);
        add(device,"fan_duty","percent",p.fanSource,p.fanPwm);
        for(const auto* item:{&s.memory[i],&s.disks[i]}){
            const auto type=item==&s.memory[i]?"RAM":"Disk";
            add(std::format("{} slot{} / {} / {}",type,i,item->model,item->serial),"temperature","C",item->source,item->celsius,item->status);
        }
    }
    const auto battery=[&](const BatteryReading& b){
        const auto status=b.flow==BatteryFlow::Charging?"charging":b.flow==BatteryFlow::Discharging?"discharging":b.flow==BatteryFlow::Idle?"idle":"flow unknown";
        const auto id="Battery / "+b.deviceId+" / "+b.model;
        add(id,"temperature","C",b.temperatureSource,b.celsius,b.temperatureStatus,b.temperatureRaw?std::optional<double>(*b.temperatureRaw):std::nullopt);
        add(id,"charge","percent",b.source,b.percent,status);add(id,"power","W",b.source,b.watts,status);
        add(id,"voltage","V",b.source,b.volts);add(id,"current_magnitude","mA",b.source,b.milliamps,status);
        add(id,"cycles","count",b.source,b.cycles?std::optional<double>(*b.cycles):std::nullopt);add(id,"health","percent",b.source,b.healthPercent);
        add(id,"online","boolean",b.source,b.onlineKnown?std::optional<double>(b.online?1.:0.):std::nullopt,b.powerStatus.empty()?(b.onlineKnown?"fresh AC observation":"AC state unknown"):b.powerStatus);
    };
    if(s.batteries.empty())battery(s.battery);else for(const auto& b:s.batteries)battery(b);
    add("CPU","TjMax","C","Intel DTS",s.cpuTemperatureTargetCelsius);
    add("APU","package_power","W",s.apuPackagePowerSource,s.apuPackagePowerWatts,s.processorStatus);
    if(s.processorVendors[0]==ProcessorVendor::Intel)
        add("CPU","cores_power","W",s.cpuCorePowerSource,s.cpuCorePowerWatts,s.processorStatus);
    add("SoC","temperature","C",s.socSource,s.socCelsius);
    add("NPU","usage","percent",s.npuUsageSource,s.npuUsagePercent);
    add("NPU","frequency","MHz",s.npuClockSource,s.npuClockMhz);
    add("NPU","power","W",s.npuPowerSource,s.npuPowerWatts,s.processorStatus);
    add("System","power_candidate","W",s.systemPowerSource,s.systemPowerStale?std::nullopt:s.systemPowerWatts,s.systemPowerStale?"stale/unavailable; suppressed":"experimental EC raw=W assumption");
    add("System","consumption_estimate","W",s.systemPowerSource,s.systemPowerStale?std::nullopt:s.systemConsumptionWatts,"estimate; EC candidate minus positive charging power");
    return rows;
}
void gpuSensors(std::vector<SensorReading>& rows,const std::vector<GpuReading>& devices){
    for(const auto& gpu:devices){
        const auto add=[&](std::string metric,std::string unit,auto value){
            rows.push_back({gpu.uuid+" / "+gpu.name,std::move(metric),std::move(unit),gpu.source,value?"ok":gpu.status,value?std::optional<double>(*value):std::nullopt,{},0});
        };
        add("temperature","C",gpu.temperature);add("hotspot_temperature","C",gpu.hotspotCelsius);
        add("memory_temperature","C",gpu.memoryCelsius);add("memory_junction_temperature","C",gpu.memoryJunctionCelsius);
        add("usage","percent",gpu.utilization);add("memory_usage","percent",gpu.memoryUtilization);
        add("frequency","MHz",gpu.graphicsMhz);add("memory_frequency","MHz",gpu.memoryMhz);
        rows.push_back({gpu.uuid+" / "+gpu.name,"power/"+std::string(gpuPowerDomainName(gpu.powerDomain)),"mW",gpu.powerSource.empty()?gpu.source:gpu.powerSource,gpu.powerMw?"ok; explicit power domain, not summed":gpu.status,gpu.powerMw?std::optional<double>(*gpu.powerMw):std::nullopt,{},0});
        add("fan_speed","RPM",gpu.fanRpm);
        add("vram_used","bytes",gpu.usedBytes);add("vram_total","bytes",gpu.totalBytes);
        for(const auto& fan:gpu.fans)add("fan"+std::to_string(fan.index)+"/speed","percent",fan.percent);
        rows.insert(rows.end(),gpu.additionalSensors.begin(),gpu.additionalSensors.end());
    }
}
// One recorder for every model. Slow optional WMI providers run separately so
// they cannot create gaps in the fast samples taken during fan probes. Neither
// reader takes hardwareGate: page closing must always be able to restore fans.
class SensorRecorder {
    std::ofstream file_;
    std::mutex fileGate_,waitGate_;
    std::condition_variable wake_;
    std::atomic<int> phase_{0};
    std::atomic<bool> finishing_{false};
    std::atomic<bool> drainingProviders_{false};
    std::stop_token ownerCancellation_;
    std::atomic<unsigned> samples_{0};
    std::atomic<unsigned> fastSamples_{0};
    std::atomic<long long> firstFastMs_{-1},lastFastMs_{-1};
    std::atomic<long long> firstCompletedFastMs_{-1},lastCompletedFastMs_{-1};
    std::atomic<bool> fastExited_{false};
    std::atomic<bool> baselineProviderComplete_{false};
    std::atomic<unsigned> failedProviderPasses_{0};
    std::atomic<unsigned> completedProviderPasses_{0};
    std::optional<diagnostics::FastDiagnosticFrame> latestFrame_;
    std::optional<diagnostics::FastDiagnosticFrame> lastUsefulFrame_;
    static constexpr long long minimumTelemetryMs=60000;
    std::string error_;
    std::filesystem::path summaryPath_;
    std::filesystem::path samplingPath_;
    struct Statistic {SensorReading id;double minimum{},maximum{},first{},last{};unsigned count{};};
    std::map<std::string,Statistic> statistics_;
    std::chrono::steady_clock::time_point started_{std::chrono::steady_clock::now()};
    mc::ResourceActor fast_,slow_;
    const char* phase()const{
        switch(phase_.load()){case 1:return "ec_snapshot";case 2:return "ec_max";case 3:return "ec_restored";case 4:return "gpu_snapshot";case 5:return "nvml_max";case 6:return "after";case 7:return "firmware_table_export";default:return "before";}
    }
    void append(std::string_view channel,const std::vector<SensorReading>& rows,std::string_view gpu="",std::string_view capturedPhase="",long long readMs=0,std::string_view battery=""){
        const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started_).count();
        const auto now=std::chrono::floor<std::chrono::milliseconds>(std::chrono::system_clock::now());
        std::string line=std::format("{{\"schema\":1,\"utc\":{},\"elapsed_ms\":{},\"phase_at_start\":{},\"phase_at_end\":{},\"channel\":{},\"read_duration_ms\":{},\"readings\":{}",
            quote(std::format("{:%Y-%m-%dT%H:%M:%S}Z",now)),elapsed,quote(capturedPhase),quote(phase()),quote(channel),readMs,sensorsJson(rows));
        if(!gpu.empty())line+=",\"gpus\":"+std::string(gpu);
        if(!battery.empty()){
            while(!battery.empty()&&(battery.back()=='\n'||battery.back()=='\r'||battery.back()==' '||battery.back()=='\t'))battery.remove_suffix(1);
            line+=",\"battery\":"+std::string(battery);
        }
        line+="}\n";
        std::lock_guard lock(fileGate_);file_<<line;file_.flush();if(!file_)error_="sensors.jsonl: write failed";else{
            ++samples_;
            if(channel=="fast"){if(firstFastMs_.load()<0)firstFastMs_=elapsed;lastFastMs_=elapsed;++fastSamples_;}
        }
        for(const auto& r:rows)if(r.value&&std::isfinite(*r.value)){
            auto& stat=statistics_[r.device+'\0'+r.metric+'\0'+r.unit+'\0'+r.source];
            if(!stat.count){stat.id=r;stat.minimum=stat.maximum=stat.first=*r.value;}
            stat.minimum=std::min(stat.minimum,*r.value);stat.maximum=std::max(stat.maximum,*r.value);stat.last=*r.value;++stat.count;
        }
    }
    void samplingWindow(std::string_view status){
        const auto first=firstFastMs_.load(),last=lastFastMs_.load();
        const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started_).count();
        const auto completedFirst=firstCompletedFastMs_.load(),completedLast=lastCompletedFastMs_.load();
        textFile(samplingPath_,std::format("{{\"schema\":1,\"utc\":{},\"status\":{},\"requested_minimum_ms\":{},\"nominal_fast_interval_ms\":1000,\"recorder_elapsed_ms\":{},\"first_fast_elapsed_ms\":{},\"last_fast_elapsed_ms\":{},\"observed_fast_span_ms\":{},\"completed_provider_span_ms\":{},\"completed_provider_passes\":{},\"failed_partial_provider_passes\":{},\"fast_samples\":{},\"all_records\":{},\"minimum_met\":{},\"cancelled\":{}}}\n",
            quote(utc()),quote(status),minimumTelemetryMs,elapsed,first<0?"null":std::to_string(first),last<0?"null":std::to_string(last),
            first<0||last<0?"null":std::to_string(last-first),completedFirst<0||completedLast<0?"null":std::to_string(completedLast-completedFirst),completedProviderPasses_.load(),failedProviderPasses_.load(),fastSamples_.load(),samples_.load(),minimumWindowComplete(),ownerCancellation_.stop_requested()));
    }
public:
    SensorRecorder(const std::filesystem::path& path,EcAccess* ec,std::stop_token cancellation,std::shared_ptr<TelemetryGate::DiagnosticCollection> collection):file_(path,std::ios::binary),ownerCancellation_(cancellation),summaryPath_(path.parent_path()/"sensor-summary.json"),samplingPath_(path.parent_path()/"sampling-window.json"){
        if(!file_)throw std::runtime_error("Не удалось создать sensors.jsonl");
        std::promise<void> first;auto ready=first.get_future();
        // No driver/library is opened on this thread. Both stop sources are
        // connected before the first bounded IPC request, including baseline.
        fast_=mc::ResourceActor([this,first=std::move(first),collection=std::move(collection)](std::stop_token stop)mutable{
            bool firstSample=true;
            try{
                std::stop_source providers;
                std::stop_callback threadStop(stop,[&]{providers.request_stop();wake_.notify_all();});
                std::stop_callback userStop(ownerCancellation_,[&]{providers.request_stop();wake_.notify_all();});
                auto worker=diagnostics::createFastDiagnosticWorker(collection);
                while(!stop.stop_requested()){
                    const auto begin=std::chrono::steady_clock::now();const std::string capturedPhase=phase();
                    const bool finalSample=finishing_.load();
                    auto frame=worker->read(providers.get_token(),std::chrono::seconds(10));
                    if(!frame.complete)++failedProviderPasses_;else ++completedProviderPasses_;
                    {std::lock_guard lock(fileGate_);latestFrame_=frame;
                        // A later hung provider must not erase an earlier full
                        // snapshot. Every exported file retains its capture UTC.
                        if(frame.complete||!lastUsefulFrame_||(!lastUsefulFrame_->complete&&frame.readings.size()>lastUsefulFrame_->readings.size()))lastUsefulFrame_=frame;}
                    append("fast",frame.readings,frame.gpuJson,capturedPhase,frame.readDurationMs,frame.batteryJson);
                    if(frame.complete){const auto savedAt=lastFastMs_.load();if(firstCompletedFastMs_.load()<0)firstCompletedFastMs_=savedAt;lastCompletedFastMs_=savedAt;}
                    if(firstSample){baselineProviderComplete_=frame.complete;first.set_value();firstSample=false;}
                    if(finalSample)break;
                    std::unique_lock lock(waitGate_);wake_.wait_until(lock,begin+std::chrono::seconds(1),[&]{return stop.stop_requested()||finishing_.load();});
                }
            }catch(const std::exception& e){std::lock_guard lock(fileGate_);error_=e.what();}
            // Cancellation before the first pass must wake construction too.
            if(firstSample)try{first.set_value();}catch(...){}
            fastExited_=true;wake_.notify_all();
        });
        static_cast<void>(ec); // Access belongs to the command owner, never the killable reader.
        if(ready.wait_for(std::chrono::seconds(12))!=std::future_status::ready){
            fast_.request_stop();std::lock_guard lock(fileGate_);error_="Initial read-only provider IPC deadline exceeded; probes disabled";
        }
        samplingWindow("collecting");
        slow_=mc::ResourceActor([this](std::stop_token stop){
            // A normal finish drains the current provider pass. Stopping it
            // immediately used to discard a still-starting CPU WMI query.
            // Explicit user cancellation/destruction still interrupts queries.
            std::stop_source queries;
            std::stop_callback ownStop(stop,[&]{queries.request_stop();});
            std::stop_callback userStop(ownerCancellation_,[&]{queries.request_stop();});
            try{diagnostics::PlatformSensors platform;while(!queries.stop_requested()){
                const auto begin=std::chrono::steady_clock::now();const std::string capturedPhase=phase();
                auto rows=platform.read(true,queries.get_token());if(!rows.empty())append("optional_providers",rows,"",capturedPhase,std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-begin).count());
                if(drainingProviders_.load())break;
                std::unique_lock lock(waitGate_);wake_.wait_for(lock,std::chrono::seconds(5),[&]{return queries.stop_requested()||drainingProviders_.load();});
                if(drainingProviders_.load())break;
            }}catch(const std::exception& e){append("optional_providers",{{"OS providers","provider_status","","MagicControl",e.what(),{},{},0}});}
        });
    }
    void setPhase(int phase){phase_=phase;}
    bool baselineProviderComplete()const noexcept{return baselineProviderComplete_.load();}
    diagnostics::FastDiagnosticFrame snapshot(){std::lock_guard lock(fileGate_);return latestFrame_.value_or(diagnostics::FastDiagnosticFrame{});}
    diagnostics::FastDiagnosticFrame providerSnapshot(){std::lock_guard lock(fileGate_);return lastUsefulFrame_.value_or(diagnostics::FastDiagnosticFrame{});}
    bool minimumWindowComplete()const{
        const auto first=firstCompletedFastMs_.load(),last=lastCompletedFastMs_.load();
        return first>=0&&last>=first&&last-first>=minimumTelemetryMs&&completedProviderPasses_.load()>=2;
    }
    std::string finish(){
        phase_=6;
        // Count a full minute from the first SAVED fast sample, not from driver
        // initialization or EC capture. Probes/inventory already consume this
        // window; a fast ReadOnly package waits out the remaining observation.
        std::stop_callback cancelled(ownerCancellation_,[this]{fast_.request_stop();slow_.request_stop();wake_.notify_all();});
        const auto baselineMs=firstCompletedFastMs_.load()>=0?firstCompletedFastMs_.load():firstFastMs_.load();
        if(baselineMs>=0){
            const auto deadline=started_+std::chrono::milliseconds(baselineMs+minimumTelemetryMs);
            while(!ownerCancellation_.stop_requested()&&!fastExited_.load()&&std::chrono::steady_clock::now()<deadline){
                samplingWindow("collecting");
                logMessage("diagnostic_dump",std::format("telemetry minimum=60000ms elapsed_since_first_ms={} fast_samples={}",
                    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started_).count()-baselineMs,fastSamples_.load()));
                diagnosticStage("telemetry_window",std::format("Наблюдение датчиков: прошло {} с; минимум 60 с.",
                    (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started_).count()-baselineMs)/1000));
                std::unique_lock wait(waitGate_);
                wake_.wait_until(wait,std::min(deadline,std::chrono::steady_clock::now()+std::chrono::seconds(10)),[&]{return ownerCancellation_.stop_requested()||fastExited_.load();});
            }
        }
        phase_=6;drainingProviders_=true;wake_.notify_all();
        diagnosticStage("telemetry_finish","Завершение опроса датчиков и сохранение измерений.");
        if(slow_.joinable())slow_.join();
        finishing_=true;wake_.notify_all();
        if(fast_.joinable())fast_.join();
        samplingWindow(ownerCancellation_.stop_requested()?"cancelled":minimumWindowComplete()?"complete":"incomplete");
        std::lock_guard lock(fileGate_);
        std::string summary="{\"schema\":1,\"scope\":\"observed in this dump; not hardware maximum\",\"channels\":[";bool first=true;
        for(const auto& [key,s]:statistics_){if(!first)summary+=',';first=false;
            summary+=std::format("{{\"device\":{},\"metric\":{},\"unit\":{},\"source\":{},\"count\":{},\"first\":{},\"last\":{},\"minimum\":{},\"maximum\":{}}}",
                quote(s.id.device),quote(s.id.metric),quote(s.id.unit),quote(s.id.source),s.count,s.first,s.last,s.minimum,s.maximum);
        }
        textFile(summaryPath_,summary+"]}\n");
        const auto firstSaved=firstFastMs_.load(),lastSaved=lastFastMs_.load();
        return std::format("sensors.jsonl: {} записей, fast={}; период fast около1с. Окно после первого fast: {}мс, минимум60000мс {}. {}\nRead-only fast providers: deadline10с на проход; failed/partial passes={}; IPC worker persistent, прогресс/ошибки сохранены.\nПараметры/отмена/полнота: sampling-window.json. Минимумы/максимумы: sensor-summary.json (только наблюдения этого сбора).\n",
            samples_.load(),fastSamples_.load(),firstSaved<0||lastSaved<0?0:lastSaved-firstSaved,minimumWindowComplete()?"выполнен":"НЕ выполнен",error_,failedProviderPasses_.load());
    }
    ~SensorRecorder(){fast_.request_stop();slow_.request_stop();wake_.notify_all();if(fast_.joinable())fast_.join();if(slow_.joinable())slow_.join();}
};
}
namespace diagnostics {
struct FastDiagnosticSource::Impl{
    std::unique_ptr<ReadOnlyDiagnosticHardware> hardware=createReadOnlyDiagnosticHardware();
    MonitoringReader monitoring{hardware?hardware->access():nullptr,true};
    PlatformSensors platform;NvidiaGpu nvidia;RadeonGpu radeon;IntelGpu intel;
    FastDiagnosticFrame read(const std::function<void(const FastDiagnosticFrame&)>& progress){
        const auto started=std::chrono::steady_clock::now();FastDiagnosticFrame frame;frame.utc=utc();
        const auto failure=[&](const char* provider,const std::exception& e){frame.readings.push_back({provider,"provider_status","","MagicControl · isolated read-only worker",e.what(),{},{},batteryMonotonicMs()});};
        const auto publish=[&](const char* next){if(progress){frame.readDurationMs=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count();frame.status=std::string("partial provider pass; pending=")+next;progress(frame);}};
        publish("MonitoringReader");
        try{const auto sample=monitoring.read();frame.readings=monitoringSensors(sample);frame.batteryJson=batteryDocument(sample);}
        catch(const std::exception& e){failure("MonitoringReader",e);}
        publish("OS sensors");
        try{auto rows=platform.read(false,{});frame.readings.insert(frame.readings.end(),rows.begin(),rows.end());}
        catch(const std::exception& e){failure("OS sensors",e);}
        publish("read-only EC fans");
        try{if(hardware){const auto fans=hardware->fans();
            for(unsigned ch=0;ch<2;++ch){const std::string name=ch?"GPU fan / read-only EC":"CPU fan / read-only EC";
                frame.readings.push_back({name,"speed","RPM","Typed read-only getter",fans.rpm[ch]?"ok":"not reported",fans.rpm[ch]?std::optional<double>(*fans.rpm[ch]):std::nullopt,{},batteryMonotonicMs()});
                frame.readings.push_back({name,"duty","percent","Typed read-only getter",fans.percent[ch]?"ok":"not reported",fans.percent[ch]?std::optional<double>(*fans.percent[ch]):std::nullopt,{},batteryMonotonicMs()});}
        }}catch(const std::exception& e){failure("Read-only EC fans",e);}
        publish("graphics enumeration");
        std::vector<GraphicsAdapter> adapters;
        try{adapters=graphicsAdapters();frame.graphicsJson=graphicsJson(adapters);}catch(const std::exception& e){failure("Graphics enumeration",e);}
        publish("NVIDIA");
        std::vector<GpuReading> nv,amd,intelRows;
        const auto present=[&](unsigned vendor){return std::any_of(adapters.begin(),adapters.end(),[&](const auto& a){return a.vendorId==vendor&&!a.software;});};
        try{if(present(0x10de))nv=nvidia.read(true);}catch(const std::exception& e){failure("NVIDIA",e);}
        gpuSensors(frame.readings,nv);frame.nvidiaJson=gpuReadingsJson(nv);for(const auto& gpu:nv)frame.nvidia.push_back({gpu.name,gpu.uuid,gpu.available});publish("Radeon");
        try{if(present(0x1002))amd=radeon.read(true);}catch(const std::exception& e){failure("Radeon",e);}
        gpuSensors(frame.readings,amd);frame.radeonJson=gpuReadingsJson(amd,"AMD ADLX / ADL / amdgpu · all AMD adapters");publish("Intel GPU");
        try{if(present(0x8086))intelRows=intel.read();}catch(const std::exception& e){failure("Intel GPU",e);}
        gpuSensors(frame.readings,intelRows);frame.intelJson=gpuReadingsJson(intelRows,"Intel IGCL / Linux i915/xe");
        frame.gpuJson="{\"nvidia\":"+frame.nvidiaJson+",\"amd\":"+frame.radeonJson+",\"intel\":"+frame.intelJson+"}";
        frame.gpuJson.erase(std::remove(frame.gpuJson.begin(),frame.gpuJson.end(),'\n'),frame.gpuJson.end());
        frame.complete=true;frame.status="Completed read-only provider pass; individual API availability/errors retained";
        frame.readDurationMs=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count();return frame;
    }
};
FastDiagnosticSource::FastDiagnosticSource():impl_(std::make_unique<Impl>()){}
FastDiagnosticSource::~FastDiagnosticSource()=default;
FastDiagnosticFrame FastDiagnosticSource::read(const std::function<void(const FastDiagnosticFrame&)>& progress){return impl_->read(progress);}
void captureEcBlocks(EcDump& dump,const BlockReader& reader,std::stop_token token,std::size_t bytes){
    if(dump.base>0xffff||!bytes||bytes>0x10000-dump.base)throw std::invalid_argument("EC snapshot window exceeds16-bit transport");
    dump.bytes.assign(bytes,0);dump.valid.assign(bytes,0);
    dump.readAttempts.clear();
    dump.errors.clear();dump.complete=false;dump.busyWaitCount=0;dump.blockRetryCount=0;dump.maxBlockReadMs=0;
    const auto started=std::chrono::steady_clock::now();
    // Bound each lock hold and deliberately give queued telemetry/control
    // readers a scheduling window. recursive_mutex::unlock + yield alone
    // allowed this worker to immediately reacquire the gate for many blocks.
    constexpr std::size_t blockBytes=16;
    const auto deadline=started+std::chrono::seconds(180);
    auto nextNotice=started+std::chrono::seconds(2);
    std::size_t validBytes{};
    diagnosticStage("ec_read",std::format("Чтение EC: {} / {} байт.",validBytes,dump.bytes.size()));
    const auto elapsed=[&]{dump.elapsedMs=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count();};
    std::mutex waitGate;std::condition_variable_any wake;
    const auto wait=[&](std::chrono::milliseconds delay){
        std::unique_lock lock(waitGate);
        wake.wait_until(lock,token,std::min(deadline,std::chrono::steady_clock::now()+delay),[]{return false;});
    };
    for(std::size_t offset=0;offset<dump.bytes.size();offset+=blockBytes){
        const auto count=std::min(blockBytes,dump.bytes.size()-offset);
        unsigned failures{};std::string lastReason;
        for(;;){
            if(std::chrono::steady_clock::now()>=nextNotice){
                elapsed();diagnosticStage("ec_wait",std::format("Чтение EC: {} / {} байт; прошло {} с.",validBytes,dump.bytes.size(),dump.elapsedMs/1000));
                nextNotice=std::chrono::steady_clock::now()+std::chrono::seconds(2);
            }
            if(token.stop_requested()||std::chrono::steady_clock::now()>=deadline){
                dump.status=token.stop_requested()?"Отменено: сохранён частичный снимок":"Истекло180с: сохранён частичный снимок";
                if(!token.stop_requested())dump.errors.push_back({dump.base+static_cast<std::uint32_t>(offset),static_cast<std::uint32_t>(count),lastReason.empty()?"Истёк общий бюджет медленного чтения EC":"Истекло время ожидания/повторов: "+lastReason});
                elapsed();return;
            }
            try{
                const auto readStarted=std::chrono::steady_clock::now();
                EcReadAttempt attempt;attempt.address=dump.base+static_cast<std::uint32_t>(offset);attempt.count=static_cast<std::uint32_t>(count);
                const auto read=[&]{
                    const auto finish=[&]{
                        attempt.elapsedUs=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-readStarted).count());
                        dump.readAttempts.push_back(attempt);
                    };
                    try{
                        auto data=reader(static_cast<std::uint16_t>(dump.base+offset),count,&attempt.transport);
                        attempt.outcome=data.size()==count?"ok":"short_read";finish();return data;
                    }catch(const std::exception& e){attempt.outcome=e.what();finish();throw;}
                };
                const auto data=read();
                const auto readMs=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-readStarted).count());
                dump.maxBlockReadMs=std::max(dump.maxBlockReadMs,readMs);
                if(data.size()!=count)throw std::runtime_error("Неполный ответ EC");
                std::copy(data.begin(),data.end(),dump.bytes.begin()+offset);
                std::fill_n(dump.valid.begin()+offset,count,std::uint8_t{1});
                validBytes+=count;
                if(validBytes%256==0||validBytes==dump.bytes.size()){
                    elapsed();logMessage("diagnostic_dump",std::format("EC progress valid_bytes={} total_bytes={} next_address=0x{:04X} elapsed_ms={} max_block_ms={} pause_waits={}",validBytes,dump.bytes.size(),dump.base+offset+count,dump.elapsedMs,dump.maxBlockReadMs,dump.busyWaitCount));
                    diagnosticStage("ec_read",std::format("Чтение EC: {} / {} байт.",validBytes,dump.bytes.size()));
                }
                break;
            }catch(const EcObservationBusy& e){
                // Background read-only state polling uses Transaction too.
                // Its observation pause is not a fatal error or a sleep event.
                ++dump.busyWaitCount;lastReason=e.what();wait(std::chrono::milliseconds(20));
            }catch(const BoardNotVerified& e){
                dump.errors.push_back({dump.base+static_cast<std::uint32_t>(offset),static_cast<std::uint32_t>(count),e.what()});
                dump.status="Чтение остановлено: проверка платы не пройдена";elapsed();return;
            }catch(const AddressNotReachable& e){
                dump.errors.push_back({dump.base+static_cast<std::uint32_t>(offset),static_cast<std::uint32_t>(count),e.what()});
                dump.status="Чтение остановлено: адрес вне доступного транспорта";elapsed();return;
            }catch(const BackendUnavailable& e){
                dump.errors.push_back({dump.base+static_cast<std::uint32_t>(offset),static_cast<std::uint32_t>(count),e.what()});
                dump.status="Чтение остановлено: транспорт недоступен";elapsed();return;
            }catch(const std::exception& e){
                lastReason=e.what();
                if(++failures>=3){
                    dump.errors.push_back({dump.base+static_cast<std::uint32_t>(offset),static_cast<std::uint32_t>(count),"Три попытки: "+lastReason});break;
                }
                ++dump.blockRetryCount;
                logMessage("diagnostic_dump",std::format("EC retry address=0x{:04X} attempt={} reason={}",dump.base+offset,failures+1,lastReason));
                wait(std::chrono::milliseconds(50));
            }
        }
        // No long transaction or ObservationPause: online cooling keeps running.
        wait(std::chrono::milliseconds(2));
    }
    dump.complete=std::all_of(dump.valid.begin(),dump.valid.end(),[](auto value){return value==1;});
    dump.status=dump.complete?(dump.base==0&&dump.bytes.size()==4096?std::string("Полный снимок EC RAM0x000–0xFFF"):std::format("Полный снимок EC RAM0x{:04X}–0x{:04X}",dump.base,dump.base+dump.bytes.size()-1)):"Сохранён частичный снимок: отдельные блоки не прочитаны после повторов";
    elapsed();
}
}
DiagnosticDumpResult createDiagnosticDump(std::shared_ptr<EcAccess> access,std::stop_token token,MaximumFanProbe maximumProbe,std::shared_ptr<std::mutex> hardwareGate,DiagnosticDumpMode mode,FirmwareFanDefault firmwareDefault,std::string displaySnapshot,std::filesystem::path outputRoot){
    auto collection=telemetryGate().tryDiagnosticCollection();
    if(!collection){DiagnosticDumpResult result;result.message="EC diagnostic unavailable while a firmware capture is active";return result;}
    diagnosticStage("prepare","Подготовка диагностического пакета EC.");
    // Restriction belongs to the collector used by GUI/CLI. In0.1.441 the
    // Lenovo8KiB read completed, then an active Max transaction preceded the
    // reported shutdown. Do not repeat writes merely to acquire diagnostics.
    const auto requestedMode=mode;
    mode=effectiveDiagnosticDumpMode(deviceProfile(readBoardDescriptor()).id,mode);
    if(mode!=requestedMode)diagnosticStage("read_only_policy","Дамп EC: только чтение; активные пробы отключены для этой модели.");
    DiagnosticDumpResult result;std::string report,dependenciesBefore;
    const std::stop_callback cancelFanRestore(token,[mode]{
        if(mode==DiagnosticDumpMode::WithFanProbes)invalidateDiagnosticFanSessions();
    });
    diagnostics::FirmwareDefaultResult baseline;
    bool restoreHandled{};
    const auto restorePrevious=[&]{
        if(restoreHandled)return;
        restoreHandled=true;
        diagnosticStage("fan_restore",mode==DiagnosticDumpMode::ReadOnly?"Проверка итогового состояния вентиляторов.":"Проверка и восстановление прежнего режима вентиляторов.");
        FanDiagnosticRestore restored;
        if(mode==DiagnosticDumpMode::ReadOnly)restored.status="ReadOnly: режим FANs не изменялся";
        else if(token.stop_requested())restored.status="Сбор отменён: прежний Manual/online не запускается";
        else if(!baseline.confirmed||result.hardwareWarning)restored.status="Возврат прежнего FANs пропущен: Default/cleanup не подтверждён";
        else if(!baseline.session.restore){
            restored.status="Нет подтверждённого собственного режима для возврата";
            if(!baseline.session.previousMode.empty()&&baseline.session.previousMode!="Default"){
                result.hardwareWarning=true;
                result.hardwareWarningMessage="Режим FANs до сбора неизвестен; возврат к нему пропущен. После проб подтверждён Default.";
            }
        }
        else{
            try{
                std::lock_guard hardwareLock(*hardwareGate);
                restored=baseline.session.restore();
            }catch(const std::exception& e){restored.attempted=true;restored.status=e.what();}
             catch(...){restored.attempted=true;restored.status="Неизвестная ошибка возврата FANs";}
            if(!restored.confirmed&&!restored.superseded)result.hardwareWarning=true;
        }
        report+="\nВозврат прежнего FANs ("+baseline.session.previousMode+"): "+restored.status+"\n";
        logMessage("diagnostic_dump",std::format("FAN restore previous={} attempted={} confirmed={} superseded={}: {}",baseline.session.previousMode,restored.attempted,restored.confirmed,restored.superseded,restored.status));
        if(!result.directory.empty())try{
            textFile(result.directory/"fans-restore-state.json",std::format("{{\"schema\":1,\"previous_mode\":{},\"attempted\":{},\"confirmed\":{},\"superseded\":{},\"cancelled\":{},\"status\":{},\"physical_execution_measured\":false}}\n",quote(baseline.session.previousMode),restored.attempted,restored.confirmed,restored.superseded,token.stop_requested(),quote(restored.status)));
        }catch(const std::exception& e){report+="Не удалось сохранить fans-restore-state.json: "+std::string(e.what())+"\n";}
    };
    try{
        const auto started=utc();const auto board=readBoardDescriptor();const auto& profile=deviceProfile(board);
        // Explicit active diagnostics use only the actual model's typed adapter.
        // The narrow HYDROC factory adapter does not enable ordinary FANs/RGB.
        auto root=std::move(outputRoot);if(root.empty()){root=currentLogPath().parent_path();if(root.empty())root=defaultLogDirectory();root/="ec-dumps";}
        std::filesystem::create_directories(root);
        const auto name=std::format("dump-{:%Y%m%d-%H%M%S}",std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));
        for(unsigned suffix=0;suffix<1000;++suffix){auto candidate=root/(name+"-"+std::to_string(suffix));
            if(std::filesystem::create_directory(candidate)){result.directory=std::move(candidate);break;}}
        if(result.directory.empty())throw std::runtime_error("Не удалось создать отдельную папку дампа");
        report="MagicControl · диагностический дамп\nВерсия: "+std::string(MAGICCONTROL_VERSION_STRING)+"\nНачало UTC: "+started+
            "\nПроизводитель: "+board.manufacturer+"\nМодель: "+board.productName+"\nПлата: "+board.baseBoard+"\nПрофиль: "+std::string(profile.id)+"\n";
        report+="\nЭто снимок состояния EC RAM/диагностики, не образ BIOS или VBIOS для прошивки.\n"
                "Чтение по блокам; EC/ОС могут менять значения во время сбора. Снимок не атомарный.\n"
                "Подсветка, пороги батареи, мощность, напряжения, частоты и прошивки не меняются.\n";
        report+=mode==DiagnosticDumpMode::ReadOnly?"Режим: только диагностика; Default и пробы вентиляторов отключены.\n":"Порядок: FANs → Default; EC и RAM-таблицы; телеметрия/логи; Max EC/NVML на30с с возвратом в Default. После сбора возвращается прежний подтверждённый режим FANs, если его не заменила новая команда или переход питания.\n";
        textFile(result.directory/"report.txt",report+"\nСбор выполняется.\n");
        if(hydrocProfile(profile.id))textFile(result.directory/"lighting-hid.json",lightingEvidenceJson({},profile.id));
        if(hydrocProfile(profile.id))textFile(result.directory/"liquid-cooling.json","{\"app_version\":"+quote(MAGICCONTROL_VERSION_STRING)+",\"captured_utc\":"+quote(utc())+",\"state\":"+liquidCoolingDiagnosticsJson()+"}\n");
        textFile(result.directory/"comment.txt","Комментарий владельца к снимку:\nЧто произошло / что нажимали:\nПосле запуска, сна или гибернации:\nПитание: батарея / зарядка, мощность блока и порт:\nДополнительные наблюдения:\n");
        logMessage("diagnostic_dump","begin: "+pathText(result.directory));
        // Save immediately: even an interrupted EC/inventory collection retains
        // the initial dependency evidence independently of rotating session logs.
        diagnosticStage("dependencies_before","Проверка драйверов и встроенных компонентов.");
        dependenciesBefore=dependencySnapshotJson();
        textFile(result.directory/"dependencies.json",dependencyDocument(dependenciesBefore));
        diagnosticStage("displays","Сбор сведений о дисплеях.");
        try{if(displaySnapshot.empty())displaySnapshot=diagnostics::capturePlatformDisplays(token);}
        catch(const std::exception& error){diagnostics::DisplayEvidence failed;failed.source="platform display getter";failed.status="Display capture failed";
            failed.issues.push_back({"enumeration","capturePlatformDisplays",error.what(),"exception",{}});displaySnapshot=diagnostics::displayEvidenceJson(failed);}
        textFile(result.directory/"displays.json",displaySnapshot);
        report+="Мониторы: displays.json — текущий режим/частота/масштаб ОС с источниками и отдельными причинами unknown; preferred EDID не подставляется.\n";
        report+="Зависимости: dependencies.json — PawnIO SCM, путь и версия SYS-файла, отдельный ответ загруженного драйвера IOCTL_PIO_VERSION до/после сбора, наличие runtime. Отсутствие VERSIONINFO не мешает запросу загруженной версии; версия DLL/установщика её не заменяет. Установка не равна успешному чтению датчиков.\n";
        if(!hardwareGate)hardwareGate=std::make_shared<std::mutex>();
        diagnosticStage("fan_default",mode==DiagnosticDumpMode::ReadOnly?"Режим чтения: вентиляторы не переключаются.":"Подготовка вентиляторов: Default и проверка состояния.");
        {
            std::lock_guard hardwareLock(*hardwareGate);
            if(mode==DiagnosticDumpMode::ReadOnly)baseline.status="Пропущено: режим только чтения";
            else baseline=diagnostics::preparePlatformFanDefault(access.get(),firmwareDefault,token);
        }
        result.hardwareWarning=baseline.attempted&&!baseline.confirmed;
        textFile(result.directory/"fans-default-state.json",std::format("{{\"schema\":1,\"utc\":{},\"attempted\":{},\"confirmed\":{},\"status\":{},\"previous_mode\":{},\"restore_available\":{}}}\n",quote(utc()),baseline.attempted,baseline.confirmed,quote(baseline.status),quote(baseline.session.previousMode),static_cast<bool>(baseline.session.restore)));
        report+="\nПодготовка Default: "+baseline.status+"\n";
        logMessage("diagnostic_dump","Default before EC snapshot: "+baseline.status);
        // No SensorRecorder yet: preserve the firmware baseline before any
        // diagnostic telemetry/probe workers are started. Normal GUI getters
        // may still run; the block snapshot is explicitly non-atomic.
        const auto captureSnapshot=[&](bool defaultConfirmed){
            // A separate scheduling hint, never ObservationPause: the dump
            // itself must still read, and active cooling remains independent.
            const DeviceOperationGate::PassiveEcReadPause quiet(deviceOperations(),defaultConfirmed);
            auto snapshot=diagnostics::capturePlatformEc(access.get(),token);
            snapshot.passivePollingPaused=defaultConfirmed;return snapshot;
        };
        diagnosticStage("ec_before","Чтение исходного снимка памяти EC.");
        auto ec=captureSnapshot(baseline.confirmed);result.ecComplete=ec.complete;
        if(baseline.confirmed){
            const auto observed=diagnostics::platformSnapshotFanDefault(ec);
            if(!observed.value_or(false)){
                baseline.confirmed=false;result.hardwareWarning=true;
                baseline.status=observed?"Флаги исходного EC-снимка уже не Default; Max пропущен":"Флаги Default в исходном EC-снимке не прочитаны; Max пропущен";
                report+="Проверка исходного снимка: "+baseline.status+"\n";
                logMessage("diagnostic_dump",baseline.status);
            }
        }
        // Include the post-capture confirmation too: a command readback alone
        // does not prove that no other writer changed the baseline meanwhile.
        textFile(result.directory/"fans-default-state.json",std::format("{{\"schema\":1,\"utc\":{},\"attempted\":{},\"confirmed\":{},\"status\":{},\"previous_mode\":{},\"restore_available\":{}}}\n",quote(utc()),baseline.attempted,baseline.confirmed,quote(baseline.status),quote(baseline.session.previousMode),static_cast<bool>(baseline.session.restore)));
        diagnosticStage("ec_save",ec.complete?"Память EC прочитана; сохранение снимка и таблиц вентиляторов.":"EC прочитан не полностью; сохранение доступных данных и ошибок.");
        // Commit the raw bytes/valid mask first, before derived table/WMI/ACPI
        // formatting. A failure here aborts collection before any Max probe.
        if(!ec.bytes.empty()){
            binaryFile(result.directory/"ec.bin",ec.bytes);binaryFile(result.directory/"ec-valid.bin",ec.valid);
            recordLog("diagnostic_dump",std::format("\"event\":\"ec_snapshot_saved\",\"bytes\":{},\"valid_bytes\":{},\"base\":{},\"flushed_atomic\":true",ec.bytes.size(),std::count(ec.valid.begin(),ec.valid.end(),1),ec.base));
        }
        textFile(result.directory/"fan-tables-default.json",diagnostics::platformFanTables(ec,baseline.confirmed));
        if(profile.id=="eluktronics-hydroc16g2")textFile(result.directory/"ec-protocol-candidates.json",diagnostics::platformFanTables(ec,false));
        report+="Таблицы: fan-tables-default.json — исходные RAM-байты/valid и отдельный OEM reference. Неактивная RAM не выдаётся за внутреннюю таблицу прошивки.\n";
        report+="\nEC источник: "+ec.source+"\nEC результат: "+ec.status+"\n";
        report+=ec.interfaceNotes;
        if(!ec.interfaceNotes.empty())textFile(result.directory/"ec-interface.txt",ec.interfaceNotes);
        for(const auto& [name,json]:ec.metadataJson){
            if(name.empty()||name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-")!=std::string::npos||!name.ends_with(".json"))throw std::runtime_error("Invalid diagnostic metadata filename");
            textFile(result.directory/name,json);
        }
        if(!ec.bytes.empty())report+=std::format("Ожиданий временной паузы EC: {}; повторов чтения: {}; время EC: {}мс.\n",ec.busyWaitCount,ec.blockRetryCount,ec.elapsedMs);
        if(!ec.bytes.empty())report+=std::format("Блок EC:16байт, уступка доступа между блоками:2мс, бюджет:180с; максимальная длительность чтения блока с ожиданием доступа: {}мс.\n",ec.maxBlockReadMs);
        textFile(result.directory/"ec-read-timing.json",ecTimingJson(ec));
        report+="Время каждого чтения: ec-read-timing.json; ожидание mutex и I/O разделены только там, где транспорт их измеряет. Пауза необязательных EC-getter применяется только после подтверждённого Default; активное охлаждение не приостанавливается.\n";
        if(!ec.bytes.empty()){
            std::string csv="Address,Value,Valid\r\n";
            for(std::size_t i=0;i<ec.bytes.size();++i)csv+=std::format("0x{:04X},{},{}\r\n",i+ec.base,ec.valid[i]?std::format("0x{:02X}",ec.bytes[i]):"",ec.valid[i]?1:0);
            textFile(result.directory/"ec.csv",csv);
            report+=std::format("Прочитано: {} / {} байт.\n",std::count(ec.valid.begin(),ec.valid.end(),1),ec.bytes.size());
            report+="ec-valid.bin:1=прочитано,0=не прочитано. Нули в непрочитанных позициях ec.bin — заполнители, не данные EC.\n";
        }
        if(ec.writesBefore&&ec.writesAfter)report+=std::format("Счётчик записей общего EC: {} → {} (включает независимый online-контроллер).\n",*ec.writesBefore,*ec.writesAfter);
        for(const auto& error:ec.errors)report+=std::format("Ошибка EC0x{:04X},{}байт: {}\n",error.address,error.count,error.message);
        textFile(result.directory/"battery-policy-initial.json",batteryPolicyEvidence(ec,profile.id));
        textFile(result.directory/"keyboard-initial.json",keyboardEvidence(ec,profile.id));
        textFile(result.directory/"report.txt",report+"\nИсходный EC-снимок сохранён; последующие стадии ещё выполняются.\n");
        diagnosticStage("firmware_before","Сбор таблиц ACPI и сведений прошивки.");
        report+="\nFirmware before: "+captureFirmwareEvidence(result.directory/"firmware-before",false,token,collection)+"\n";
        diagnosticStage("telemetry_baseline","Запуск наблюдения датчиков; получение первых показаний.");
        SensorRecorder sensors(result.directory/"sensors.jsonl",access.get(),token,collection);
        const bool telemetryBaseline=sensors.baselineProviderComplete();
        report+="\nТелеметрия после исходного EC-снимка: sensors.jsonl; независимые источники, null/ошибки, временные метки и фазы проб. Показания не атомарные.\n";
        diagnostics::FirmwareFactoryResult factory;
        sensors.setPhase(7);
        diagnosticStage("factory_tables","Проверка доступности заводских таблиц вентиляторов.");
        {
            std::lock_guard hardwareLock(*hardwareGate);
            if(mode==DiagnosticDumpMode::ReadOnly)factory.status="Пропущено: режим только чтения";
            else if(!baseline.confirmed)factory.status="Пропущено: исходный Default не подтверждён";
            else if(!telemetryBaseline)factory.status="Пропущено: начальный read-only provider pass не завершён";
            else if(token.stop_requested())factory.status="Отменено до factory запроса";
            else factory=diagnostics::capturePlatformFactoryTables(token);
        }
        if(factory.json.empty())factory.json=std::format("{{\"schema\":1,\"attempted\":{},\"complete\":{},\"restored\":{},\"status\":{}}}\n",factory.attempted,factory.complete,factory.restored,quote(factory.status));
        textFile(result.directory/"fan-tables-firmware.json",factory.json);
        report+="Factory mailbox: fan-tables-firmware.json — "+factory.status+"\nИсходный ec.bin/fan-tables-default.json сохранён до mailbox; mirror не активируется и не replay-ится.\n";
        result.hardwareWarning=result.hardwareWarning||(factory.attempted&&!factory.restored);
        FanMaximumResult ecMaximum;ecMaximum.status="Проба отменена до запуска";
        sensors.setPhase(2);
        {
            // Closing a page cancels then takes this gate, waiting only for
            // hardware cleanup, never for a slow inventory/WMI query.
            std::lock_guard hardwareLock(*hardwareGate);
            if(mode==DiagnosticDumpMode::ReadOnly)ecMaximum.status="Пропущено: диагностика без аппаратных проб";
            else if(!baseline.confirmed)ecMaximum.status="Пропущено: Default до дампа не подтверждён";
            else if(result.hardwareWarning)ecMaximum.status="Пропущено: возврат управления после factory запроса не подтверждён";
            else if(!telemetryBaseline)ecMaximum.status="Пропущено: начальный read-only provider pass не завершён; см.sensors.jsonl";
            else if(!token.stop_requested()){
                diagnosticStage("max_ec","Проба Max EC: измерение оборотов до 30 с, затем возврат в Default.");
                ecMaximum=diagnostics::capturePlatformMaximum(access.get(),maximumProbe,token);
            }
        }
        result.hardwareWarning=result.hardwareWarning||(ecMaximum.attempted&&!ecMaximum.restored);
        sensors.setPhase(3);
        textFile(result.directory/"fans-ec-max.json",fanProbeJson(ecMaximum));
        report+="\nПроба Max EC: "+ecMaximum.status+"\n";
        if(!ecMaximum.attempted)diagnosticStage("max_ec_skipped","Проба Max EC пропущена.");
        if(ecMaximum.attempted){report+=std::format("Наблюдаемый пик CPU/GPU RPM: {} / {}. Возврат: {}.\n",number(ecMaximum.peakRpm[0]),number(ecMaximum.peakRpm[1]),ecMaximum.restored?"подтверждён":"НЕ ПОДТВЕРЖДЁН");
            diagnosticStage("ec_after","Проба Max EC завершена; чтение контрольного снимка EC.");
            auto after=captureSnapshot(ecMaximum.restored);textFile(result.directory/"ec-after-read-timing.json",ecTimingJson(after));if(!after.bytes.empty()){binaryFile(result.directory/"ec-after.bin",after.bytes);binaryFile(result.directory/"ec-after-valid.bin",after.valid);}
            report+="EC после пробы: "+after.status+"\n";
        }
        if(!token.stop_requested()){
            diagnosticStage("inventory","Сбор сведений об устройстве и версиях драйверов.");
            const auto inventory=collectDiagnosticInventory(token);
            textFile(result.directory/"inventory.json",toJson(inventory));
            if(hydrocProfile(profile.id)){
                textFile(result.directory/"lighting-hid.json",lightingEvidenceJson(inventory,profile.id));
                report+="Подсветка HYDROC: lighting-hid.json — HID VID/PID/Usage/FeatureLength, сырые ответы firmware80H, ForceBase/ForceLogo/ForceHinge и дополнительные OEM flags в32/64битных ветках. Getter отправляет только фиксированный80H; цвета/яркость/flash/EC не меняет. Ошибки, занятость OEM и partial сохраняются; физическое наличие всех световых модулей не выводится из одного имени HID.\n";
            }
            report+="\nСведения о ноутбуке: inventory.json; время снимка: "+inventory.readAt+"\n";
        }
        sensors.setPhase(4);
        // Use the persistent bounded provider snapshot, never repeat blocking
        // display-driver/Storage/Battery reads on the command-owner thread.
        const auto probeFrame=sensors.snapshot();
        if(!token.stop_requested()&&!probeFrame.nvidia.empty()){
            NvidiaGpu reader;const auto& readings=probeFrame.nvidia;
            FanMaximumResult nvmlMaximum;nvmlMaximum.source="NVIDIA NVML";
            sensors.setPhase(5);
            {
                std::lock_guard hardwareLock(*hardwareGate);
                // User requested the RTX4060 of GM6IR0C. Never fan-test a
                // preview/eGPU/another unknown board by a guessed GPU index.
                if(mode==DiagnosticDumpMode::ReadOnly)nvmlMaximum.status="Пропущено: диагностика без аппаратных проб";
                else if(!baseline.confirmed)nvmlMaximum.status="Пропущено: Default до дампа не подтверждён";
                else if(!telemetryBaseline||!probeFrame.complete)nvmlMaximum.status="Пропущено: read-only telemetry baseline/pass incomplete";
                else if(token.stop_requested())nvmlMaximum.status="Отменено до пробы";
                else if(result.hardwareWarning)nvmlMaximum.status="Пропущено: возврат EC после первой пробы не подтверждён";
                else if(profile.id!="mechrevo-gm6ir0c"||readings.size()!=1||!readings[0].available||readings[0].name!="NVIDIA GeForce RTX 4060 Laptop GPU")
                    nvmlMaximum.status="Проба разрешена только для единственной RTX4060 Laptop на реальном GM6IR0C";
                else{auto operation=deviceOperations().tryBegin();
                    if(!operation)nvmlMaximum.status="Управление занято другой операцией";
                    else{
                        diagnosticStage("max_nvml","Проба Max NVIDIA: измерение оборотов до 30 с, затем восстановление.");
                        nvmlMaximum=reader.measureMaximumFans(readings[0].uuid,[&]{return diagnostics::readPlatformFans(access.get());},token);
                    }
                }
            }
            result.hardwareWarning=result.hardwareWarning||(nvmlMaximum.attempted&&!nvmlMaximum.restored);
            sensors.setPhase(6);
            textFile(result.directory/"fans-nvml-max.json",fanProbeJson(nvmlMaximum));
            report+="Проба NVML100%: "+nvmlMaximum.status+"\nПроценты NVML — индексы NVML, без предполагаемого соответствия каналам EC CPU/GPU.\n";
        }else report+="NVIDIA: не обнаружена или сбор отменён; NVML не запускался.\n";
        diagnosticStage("telemetry_wait","Наблюдение датчиков: сбор не менее 60 с после первого сохранённого показания.");
        report+=sensors.finish();
        diagnosticStage("keyboard_after","Сбор итогового состояния клавиатуры.");
        try{const auto context=diagnostics::capturePlatformKeyboardContext(token);
            textFile(result.directory/"keyboard-after.json",std::string("{\"schema\":1,\"captured_utc\":")+quote(utc())+",\"read_only\":true,\"context\":"+
                (context.empty()?"null,\"status\":\"No additional admitted typed context; see initial EC and native inventory\"":context)+"}\n");
        }catch(const std::exception& e){textFile(result.directory/"keyboard-after.json","{\"schema\":1,\"captured_utc\":"+quote(utc())+",\"error\":"+quote(e.what())+"}\n");}
        diagnosticStage("firmware_after","Сбор итогового состояния прошивки.");
        report+="Firmware after: "+captureFirmwareEvidence(result.directory/"firmware-after",true,token,collection)+"\n";
        const auto snapshot=sensors.providerSnapshot();
        const auto saveProvider=[&](const char* name,const std::string& json){
            if(json.empty())textFile(result.directory/name,"{\"schema\":1,\"status\":\"No completed bounded provider result; see sensors.jsonl\"}\n");
            else if(json.front()=='{')textFile(result.directory/name,"{\"snapshot_utc\":"+quote(snapshot.utc)+",\"provider_pass_complete\":"+(snapshot.complete?"true":"false")+","+json.substr(1));
            else throw std::runtime_error("Invalid internal provider JSON snapshot");
        };
        saveProvider("graphics.json",snapshot.graphicsJson);saveProvider("nvidia.json",snapshot.nvidiaJson);
        saveProvider("radeon.json",snapshot.radeonJson);saveProvider("intel.json",snapshot.intelJson);saveProvider("battery.json",snapshot.batteryJson);
        report+="Vendor/battery snapshots: последний завершённый bounded read-only provider pass либо наиболее полный partial; snapshot_utc и provider_pass_complete в каждом JSON, последующие ошибки остаются в sensors.jsonl. Все adapters/батареи, отдельные IDs и API errors; primary iGPU не заменяет dGPU.\n";
        const bool telemetryComplete=sensors.minimumWindowComplete();
        diagnosticStage("dependencies_after","Сохранение итоговых сведений о драйверах.");
        textFile(result.directory/"dependencies.json",dependencyDocument(dependenciesBefore,dependencySnapshotJson()));
        // Preserve the original collection set and add the actual board's
        // filenames from the same traits used by the runtime controllers.
        // Never copy a portable tree or enumerate arbitrary personal files.
        std::vector<std::string> settingsFiles{"app-settings.ini","device-settings.ini","fan-curves.csv","gm6ir0c-fans.txt","gm6ir0c-power.txt","keyboard-cycle.ini","gm6ir0c-device-settings.ini","hydroc16g2-device-settings.ini"};
        const auto settingsProfile=deviceProfile(readBoardDescriptor()).id;
        if(!settingsProfile.empty()){const auto file=customPresetFilename(settingsProfile);if(std::find(settingsFiles.begin(),settingsFiles.end(),file)==settingsFiles.end())settingsFiles.push_back(file);}
        if(settingsProfile=="mechrevo-gm6ir0c"){
            settingsFiles.push_back("gm6ir0c-configuration.txt");
        }
        if(ramFanControlProfile(settingsProfile))for(const auto file:{gmSettings(settingsProfile),gmPowerDraftFile(settingsProfile),gmFanCurveFile(settingsProfile)}){
            if(std::find(settingsFiles.begin(),settingsFiles.end(),file)==settingsFiles.end())settingsFiles.emplace_back(file);
        }
        if(hydrocProfile(settingsProfile))for(const auto zone:{"keyboard","lightbar","logo","hinge"})settingsFiles.push_back(std::string("hydroc16g2-lighting-")+zone+".ini");
        std::string settingsManifest="{\"schema\":1,\"actual_profile\":"+quote(settingsProfile)+",\"source_read_only\":true,\"files\":[";
        diagnosticStage("settings","Копирование файлов настроек в диагностический пакет.");
        bool firstSetting=true;
        for(const auto& file:settingsFiles){
            std::string status="not_present",error;std::optional<std::uintmax_t> bytes;
            if(token.stop_requested())status="cancelled";
            else try{const auto source=settingsReadPath(portableSettingsPath(file));
                if(std::filesystem::exists(source)){
                    bytes=std::filesystem::file_size(source);
                    if(*bytes>1024*1024){status="size_limit";report+=file+": пропущен, размер больше1МиБ\n";}
                    else{
                        std::filesystem::create_directories(result.directory/"settings");
                        std::filesystem::copy_file(source,result.directory/"settings"/file);status="copied";
                    }
                }
            }catch(const std::exception& e){status="error";error=e.what();report+=file+": "+error+"\n";}
            if(!firstSetting)settingsManifest+=',';firstSetting=false;
            settingsManifest+="{\"name\":"+quote(file)+",\"status\":"+quote(status)+",\"bytes\":"+
                (bytes?std::to_string(*bytes):"null")+",\"error\":"+quote(error)+"}";
        }
        textFile(result.directory/"settings-manifest.json",settingsManifest+"]}\n");
        result.cancelled=token.stop_requested();
        // All passive evidence and probes are complete. Restore only through
        // the original typed owner; completion OSD comes after its readback.
        restorePrevious();
        diagnosticStage("save","Сохранение журнала и итогового отчёта.");
        logMessage("diagnostic_dump",std::format("finished: ec_complete={} cancelled={} folder={}",result.ecComplete,result.cancelled,pathText(result.directory)));
        std::string logError;
        report+=snapshotCurrentLog(result.directory/"session.jsonl",logError)?"Журнал текущего запуска: session.jsonl; battery.jsonl при включённом BatteryLog\n":"Журнал: "+logError+"\n";
        report+="Окончание UTC: "+utc()+"\nОтменено: "+(result.cancelled?"да":"нет")+"\n";
        textFile(result.directory/"report.txt",report);result.saved=true;
        result.message=(result.cancelled?"Сохранён частичный пакет после отмены: ":result.ecComplete?"Дамп и диагностика сохранены: ":"Диагностика сохранена; EC неполный/недоступен, см.report.txt: ")+pathText(result.directory);
        if(!result.cancelled&&!telemetryComplete)result.message="Диагностика сохранена, минутное окно датчиков НЕ завершено; см.sampling-window.json: "+pathText(result.directory);
    }catch(const std::exception& e){
        // A storage/provider failure must not silently strand a successfully
        // prepared session in Default. The same safety/cancel checks apply.
        try{restorePrevious();}catch(const std::exception& restore){result.hardwareWarning=true;report+="\nFAN restore reporting failed: "+std::string(restore.what())+"\n";}
        result.message="Не удалось завершить дамп: "+std::string(e.what());
        if(!result.directory.empty())try{textFile(result.directory/"report.txt",report+"\nОШИБКА: "+e.what()+"\n");}catch(...){}
        if(!result.directory.empty()&&!dependenciesBefore.empty())try{textFile(result.directory/"dependencies.json",dependencyDocument(dependenciesBefore,dependencySnapshotJson()));}catch(...){}
        logMessage("diagnostic_dump",result.message);
    }
    if(result.hardwareWarning&&result.hardwareWarningMessage.empty())
        result.hardwareWarningMessage="Возврат управления вентиляторами не подтверждён. Проверьте FANs.";
    return result;
}
}
