#include "MagicControl/Logging.h"
#include "MagicControl/FileStorage.h"
#include "MagicControl/PortableSettings.h"
#include "MagicControl/BoardIdentity.h"
#include "MagicControl/Version.h"
#include "MagicControl/ActionLog.h"
#include "MagicControl/OperationMessages.h"
#include "MagicControl/DeviceProfile.h"
#include <array>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <locale>
#include <mutex>
#include <sstream>
#include <string>
#include <stdexcept>
#include <vector>
#include <algorithm>
#include <set>
#include "MagicControl/Json.h"

namespace mc {
namespace {
std::string logBoardName(std::string_view raw) {
    // Keep UTF-8 names, but use one safe filename component on both OSes.
    // A prefix prevents Windows reserved device names such as CON/AUX.
    std::string name,key;
    for(const unsigned char ch:raw){
        const bool replace=ch<=0x20||ch==0x7F||std::string_view("<>:\"/\\|?*").find(static_cast<char>(ch))!=std::string_view::npos;
        const char next=replace?'_':static_cast<char>(ch);
        if(next!='_'||name.empty()||name.back()!='_')name+=next;
        if(ch>='A'&&ch<='Z')key+=static_cast<char>(ch-'A'+'a');
        else if((ch>='a'&&ch<='z')||(ch>='0'&&ch<='9'))key+=static_cast<char>(ch);
    }
    const auto first=name.find_first_not_of("_.");
    if(first==std::string::npos)return {};
    name.erase(0,first);name.erase(name.find_last_not_of("_.")+1);
    for(const auto placeholder:{"defaultstring","tobefilledbyoem","unknown","none","na","notspecified","notapplicable","baseboardproductname","systemproductname"})
        if(key==placeholder)return {};
    // Bound paths without cutting a multibyte UTF-8 character in half.
    if(name.size()>96){std::size_t end=96;while(end&&(static_cast<unsigned char>(name[end])&0xC0)==0x80)--end;name.resize(end);}
    while(!name.empty()&&(name.back()=='.'||name.back()=='_'))name.pop_back();
    return name;
}
std::string quote(std::string_view text) {
    constexpr char hex[]{"0123456789abcdef"};
    std::string result{"\""};
    for (const unsigned char ch : text) {
        if (ch == '"' || ch == '\\') { result += '\\'; result += static_cast<char>(ch); }
        else if (ch < 0x20) { result += "\\u00"; result += hex[ch >> 4]; result += hex[ch & 15]; }
        else result += static_cast<char>(ch);
    }
    return result + '"';
}
// Keep established flat event fields, but reserve envelope names centrally.
// Parse keys at depth0 only: nested provider JSON and string contents stay raw.
std::string safeEventFields(std::string_view fields){
    std::set<std::string> used{"schema","utc","elapsed_ms","seq","slot","session","channel","app_version","actual_board","actual_profile","action_id"};
    std::string out;std::size_t copied{};int depth{};
    for(std::size_t i=0;i<fields.size();++i){
        const auto c=fields[i];
        if(c=='{'||c=='['){++depth;continue;}if(c=='}'||c==']'){--depth;continue;}
        if(c!='"')continue;
        const auto start=i++;bool escaped{};
        for(;i<fields.size();++i){if(escaped){escaped=false;continue;}if(fields[i]=='\\'){escaped=true;continue;}if(fields[i]=='"')break;}
        if(i==fields.size())break;
        auto next=i+1;while(next<fields.size()&&(fields[next]==' '||fields[next]=='\t'||fields[next]=='\r'||fields[next]=='\n'))++next;
        if(depth||next==fields.size()||fields[next]!=':')continue;
        const auto raw=fields.substr(start,i-start+1);
        const auto key=mc::json::JsonReader::parse(raw).text();
        auto unique=key;
        if(used.contains(unique)){unique="payload_"+key;while(used.contains(unique))unique="payload_"+unique;}
        used.insert(unique);
        out.append(fields.substr(copied,start-copied));out+=quote(unique);copied=i+1;
    }
    out.append(fields.substr(copied));return out;
}
struct LogSink {
    std::ofstream output;
    std::filesystem::path path;
    void append(const std::string& text){
        if(!output.is_open()||!output)return;
        output<<text;output.flush(); // Whole launch, no in-session segmentation.
    }
};
struct LogState:LogSink {
    std::mutex mutex;
    std::shared_ptr<void> slotLease;
    std::string initializationError;
    LogSink battery;
    ~LogState(){output.close();battery.output.close();slotLease.reset();}
    std::chrono::steady_clock::time_point started;
    std::uint64_t sequence{};
    int slot{};
    std::string session;
    std::string board,profile;
};
LogState& state() { static LogState value; return value; }
std::string timestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto seconds = std::chrono::system_clock::to_time_t(now);
    std::tm time{};
#ifdef _WIN32
    gmtime_s(&time, &seconds);
#else
    gmtime_r(&seconds, &time);
#endif
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
    std::ostringstream text;
    text.imbue(std::locale::classic());
    text << std::put_time(&time, "%Y-%m-%dT%H:%M:%S") << '.' << std::setfill('0') << std::setw(3) << ms << 'Z';
    return text.str();
}
} // namespace
std::filesystem::path defaultLogDirectory() {
    const auto board=readBoardDescriptor(); // OS SMBIOS only; no EC or selected UI model.
    auto name=logBoardName(board.baseBoard);
    if(name.empty())name=logBoardName(board.productName);
    if(name.empty())name="Unknown";
    const auto text="logs_"+name;
    const std::u8string component(reinterpret_cast<const char8_t*>(text.data()),text.size());
    return portableSettingsPath("startup.log").parent_path()/std::filesystem::path(component);
}
namespace {
void archiveLegacyLogs(const std::filesystem::path& folder,bool allSlotsFree){
    // Only known managed names, no recursion, no overwrite. A partial failure
    // leaves each file at its original or archived path and aborts initialization.
    std::vector<std::filesystem::path> legacy;
    const auto add=[&](const std::string& name){const auto path=folder/name;if(std::filesystem::exists(path)){
        if(!std::filesystem::is_regular_file(path)||std::filesystem::is_symlink(path))throw std::runtime_error("Unsafe legacy log path");legacy.push_back(path);}};
    add("battery.jsonl");for(int part=1;part<=3;++part)add("battery.jsonl.part"+std::to_string(part));
    for(int slot=1;slot<=3;++slot)for(int part=1;part<=3;++part)add("MagicControl-"+std::to_string(slot)+".jsonl.part"+std::to_string(part));
    if(legacy.empty())return;
    if(!allSlotsFree)throw std::runtime_error("Legacy log migration postponed: a log pair is active");
    for(int slot=1;slot<=3;++slot)add("MagicControl-"+std::to_string(slot)+".jsonl");
    const auto archiveRoot=folder/"legacy-battery";if(std::filesystem::is_symlink(archiveRoot))throw std::runtime_error("Unsafe legacy archive path");std::filesystem::create_directories(archiveRoot);
    auto archive=archiveRoot/("session-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    if(!std::filesystem::create_directory(archive))throw std::runtime_error("Legacy archive already exists");
    for(const auto& path:legacy)std::filesystem::rename(path,archive/path.filename());
}
}
bool initializeLogging(bool enabled, const std::filesystem::path& directory,bool batteryEnabled) noexcept {
    setLoggingEnabled(enabled);setBatteryLoggingEnabled(batteryEnabled);
    try{
        auto& log=state();std::unique_lock lock(log.mutex);
        log.output.close();log.output.clear();log.path.clear();log.sequence=0;log.slot=0;
        log.battery.output.close();log.battery.output.clear();log.battery.path.clear();
        log.slotLease.reset();log.initializationError.clear();
        try {
            if(!anyLoggingEnabled())return true;
            const auto folder=directory.empty()?defaultLogDirectory():directory;
            std::filesystem::create_directories(folder);
            std::string error;
            const auto rotation=detail::lockStorageFile(folder/"locks"/"log-rotation.lock",true,error);
            if(!rotation)throw std::runtime_error(error);
            std::array<std::shared_ptr<void>,3> leases;
            for(int slot=1;slot<=3;++slot){
                leases[slot-1]=detail::lockStorageFile(folder/"locks"/("log-slot-"+std::to_string(slot)+".lock"),false,error);
                if(!leases[slot-1]&&!error.empty())throw std::runtime_error(error);
            }
            archiveLegacyLogs(folder,std::all_of(leases.begin(),leases.end(),[](const auto& lease){return bool(lease);}));
            auto oldest=std::filesystem::file_time_type::max();
            for(int slot=1;slot<=3;++slot){
                if(!leases[slot-1])continue; // Never truncate a pair leased by another process.
                const auto general=folder/("MagicControl-"+std::to_string(slot)+".jsonl");
                const auto battery=folder/("battery-"+std::to_string(slot)+".jsonl");
                if(!std::filesystem::exists(general)){log.slot=slot;break;}
                auto modified=std::filesystem::last_write_time(general);
                if(std::filesystem::exists(battery))modified=std::max(modified,std::filesystem::last_write_time(battery));
                if(modified<oldest){oldest=modified;log.slot=slot;}
            }
            if(!log.slot)throw std::runtime_error("All three log pairs are active; no journal was overwritten");
            log.slotLease=std::move(leases[log.slot-1]);
            log.path=folder/("MagicControl-"+std::to_string(log.slot)+".jsonl");
            log.battery.path=folder/("battery-"+std::to_string(log.slot)+".jsonl");
            log.started=std::chrono::steady_clock::now();log.session=timestamp()+"/"+std::to_string(log.started.time_since_epoch().count());
            try{const auto board=readBoardDescriptor();log.board=board.baseBoard;log.profile=deviceProfile(board).id;}catch(...){log.board="Unknown";log.profile="unknown";}
            log.output.open(log.path,std::ios::binary|std::ios::trunc);
            // Even disabled BatteryLog gets this session's metadata, never old data.
            log.battery.output.open(log.battery.path,std::ios::binary|std::ios::trunc);
            if(!log.output||!log.battery.output)throw std::runtime_error("Cannot open the leased log pair");
        }catch(const std::exception& e){log.initializationError=e.what();}
        catch(...){log.initializationError="Cannot initialize paired logs";}
        if(!log.initializationError.empty()){
            log.output.close();log.battery.output.close();log.slotLease.reset();log.path.clear();log.battery.path.clear();log.slot=0;
            return false;
        }
        lock.unlock();
        recordLog("session","\"event\":\"session_start\",\"retained_slots\":3,\"segmented\":false,\"battery_log_enabled\":"+std::string(batteryEnabled?"true":"false"));
        return true;
    }catch(...){return false;}
}

std::string loggingInitializationError(){std::lock_guard lock(state().mutex);return state().initializationError;}

std::filesystem::path currentBatteryLogPath(){std::lock_guard lock(state().mutex);return state().battery.path;}
std::filesystem::path currentLogPath() {
    std::lock_guard lock(state().mutex);
    return state().path;
}
bool snapshotCurrentLog(const std::filesystem::path& destination,std::string& error) noexcept {
    try{
        auto& log=state();std::lock_guard lock(log.mutex);
        if(log.path.empty()||!log.output.is_open()){error="Журнал отключён или не открыт";return false;}
        log.output.flush();
        if(!log.output){error="Не удалось сбросить буфер текущего журнала";return false;}
        if(std::filesystem::exists(destination)){error="Файл снимка журнала уже существует";return false;}
        std::ofstream snapshot(destination,std::ios::binary);
        if(!snapshot){error="Не удалось создать снимок журнала";return false;}
        const auto copy=[&](LogSink& sink,std::ostream& out){
            sink.output.flush();
            out<<"{\"schema\":1,\"channel\":\"session_archive\",\"event\":\"snapshot_scope\",\"app_version\":"<<quote(MAGICCONTROL_VERSION_STRING)<<",\"retained_tail\":false,\"segmented\":false,\"retained_previous_segments\":0,\"session_slot\":"<<log.slot<<"}\n";
            std::ifstream input(sink.path,std::ios::binary);out<<input.rdbuf();out.flush();
            if(!input||!out)throw std::runtime_error("Ошибка копирования текущего журнала");
        };
        copy(log,snapshot);
        if(log.battery.output.is_open()){
            const auto target=destination.parent_path()/"battery.jsonl";
            if(std::filesystem::exists(target))throw std::runtime_error("Снимок battery.jsonl уже существует");
            std::ofstream battery(target,std::ios::binary);if(!battery)throw std::runtime_error("Не удалось сохранить battery.jsonl");
            copy(log.battery,battery);
        }
        return true;
    }catch(const std::exception& e){error=e.what();return false;}catch(...){error="Не удалось сохранить журнал";return false;}
}
void recordLog(std::string_view channel, std::string_view jsonFields) noexcept {
    OperationMessages::publish(channel,jsonFields,audit::current());
    const bool rawBattery=channel=="battery_candidates"||channel=="battery_power";
    const bool powerEvent=channel=="power"&&(jsonFields.find("\"event\":\"windows_power_event\"")!=std::string_view::npos||jsonFields.find("\"event\":\"effective_power_mode\"")!=std::string_view::npos||jsonFields.find("\"event\":\"sample_error\"")!=std::string_view::npos||jsonFields.find("\"event\":\"registration_error\"")!=std::string_view::npos);
    const bool toBattery=(batteryLoggingEnabled()&&(rawBattery||channel=="battery"||powerEvent))||(channel=="session"&&anyLoggingEnabled());
    const bool toGeneral=(!rawBattery&&loggingEnabled())||(channel=="session"&&anyLoggingEnabled());
    if(!toBattery&&!toGeneral)return;
    try {
        auto& log = state();
        std::lock_guard lock(log.mutex);
        if (!log.output.is_open()) return;
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - log.started).count();
        std::ostringstream row;
        row << "{\"schema\":1,\"utc\":" << quote(timestamp()) << ",\"elapsed_ms\":" << elapsed
            << ",\"seq\":" << ++log.sequence << ",\"slot\":"<<log.slot<< ",\"session\":"<<quote(log.session)<<",\"channel\":" << quote(channel)
            <<",\"app_version\":"<<quote(MAGICCONTROL_VERSION_STRING)<<",\"actual_board\":"<<quote(log.board)<<",\"actual_profile\":"<<quote(log.profile)
            <<",\"action_id\":"<<(audit::current()?std::to_string(audit::current()):"null");
        if (!jsonFields.empty()) row << ',' << safeEventFields(jsonFields);
        row << "}\n";
        const auto text=row.str();
        if(toGeneral)log.append(text);
        if(toBattery){
            if(!log.battery.output.is_open()){
                log.battery.output.open(log.battery.path,std::ios::binary|std::ios::app);
            }
            log.battery.append(text);
        }
    } catch (...) {}
}
void logMessage(std::string_view channel, std::string_view message) noexcept {
    // recordLog owns channel selection; BatteryLog may be on with AllLogs off.
    if (!anyLoggingEnabled()&&!OperationMessages::capturing()) return;
    try { recordLog(channel, "\"message\":" + quote(message)); } catch (...) {}
}
} // namespace mc
