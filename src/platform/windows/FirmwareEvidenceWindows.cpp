#include "../common/FirmwareEvidence.h"
#include "BinaryHashWindows.h"
#include "MagicControl/BoardIdentity.h"
#include "MagicControl/DeviceProfile.h"
#include <windows.h>
#include <tlhelp32.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <format>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace mc::diagnostics {
bool firmwareValidUtf8(std::string_view value) noexcept {
    return value.empty()||(value.size()<=2147483647u&&MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0)>0);
}
std::string firmwareSha256(std::span<const std::uint8_t> bytes){
    return windows::detail::binarySha256({reinterpret_cast<const char*>(bytes.data()),bytes.size()});
}
namespace {
using Clock=std::chrono::steady_clock;
constexpr DWORD acpiProvider=0x41435049,rsmbProvider=0x52534D42;
constexpr std::size_t maximumUefiBytes=64*1024,maximumRegistryKeys=4096;
constexpr unsigned maximumRegistryDepth=12;
constexpr wchar_t variableGuid[]=L"{9f33f85c-13ca-4fd1-9c4a-96217722c593}";
constexpr std::array<const wchar_t*,3> variableNames{L"OemMagicVariable",L"UniWillVariable",L"OemMagicDoor"};

struct RegistryKey {
    HKEY value{};
    ~RegistryKey(){if(value)RegCloseKey(value);}
};
struct ServiceHandle {SC_HANDLE value{};~ServiceHandle(){if(value)CloseServiceHandle(value);}};
struct ProcessHandle {HANDLE value{INVALID_HANDLE_VALUE};~ProcessHandle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}};
std::string utf8(const std::wstring& value){
    if(value.empty())return {};
    const auto size=WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
    if(size<=0)return "<UTF16 conversion failed>";
    std::string out(static_cast<std::size_t>(size),0);
    WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),out.data(),size,nullptr,nullptr);return out;
}
std::string printable(std::span<const std::uint8_t> bytes){
    std::string out;out.reserve(bytes.size());for(auto b:bytes)out+=b>=32&&b<=126?static_cast<char>(b):'?';return out;
}
std::string signature(std::uint32_t id){
    std::array<std::uint8_t,4> bytes{};std::memcpy(bytes.data(),&id,4);return printable(bytes);
}
std::string signature(std::span<const std::uint8_t> bytes){
    return bytes.size()>=4?printable(bytes.first(4)):"RAW_";
}
std::string fileSignature(std::string value){
    if(value.size()!=4)value="RAW_";
    // Preserve legitimate printable signatures such as ASF!. Only generated
    // filename characters are sanitized; metadata retains the original bytes.
    constexpr std::string_view forbidden="<>:\"/\\|?*";
    for(auto& c:value)if(static_cast<unsigned char>(c)<32||static_cast<unsigned char>(c)>126||forbidden.find(c)!=std::string_view::npos)c='_';
    return value;
}
bool excluded(std::string_view sig){return sig=="MSDM"||sig=="SLIC";}
std::optional<std::uint32_t> u32(std::span<const std::uint8_t> data,std::size_t at){
    if(at>data.size()||data.size()-at<4)return {};
    std::uint32_t value{};std::memcpy(&value,data.data()+at,4);return value;
}
std::string number(std::optional<std::uint32_t> value){return value?std::to_string(*value):"null";}
std::string acpiMetadata(std::span<const std::uint8_t> data){
    const auto sig=signature(data);const auto length=u32(data,4);
    const bool facs=sig=="FACS";
    const bool header=data.size()>=36&&!facs;
    const bool lengthValid=length&&*length>=(facs?8u:36u)&&*length<=data.size();
    std::optional<unsigned> checksum;
    if(header&&lengthValid){unsigned sum{};for(std::size_t i=0;i<*length;++i)sum=(sum+data[i])&255;checksum=sum;}
    std::string out=std::format("{{\"signature\":{},\"actual_bytes\":{},\"declared_bytes\":{},\"length_fits_returned_bytes\":{},\"length_matches_returned_bytes\":{},\"standard_header_applicable\":{},\"checksum_sum_mod256\":{},\"checksum_valid\":{},\"raw_retained_without_padding\":true,\"snapshot_is_not_flash_image\":true",
        firmwareQuote(sig),data.size(),number(length),lengthValid,length&&*length==data.size(),!facs,
        checksum?std::to_string(*checksum):"null",checksum?(*checksum==0?"true":"false"):"null");
    if(header)out+=std::format(",\"oem_id\":{},\"oem_table_id\":{},\"oem_revision\":{},\"creator_id\":{},\"creator_revision\":{}",
        firmwareQuote(printable(data.subspan(10,6))),firmwareQuote(printable(data.subspan(16,8))),number(u32(data,24)),
        firmwareQuote(printable(data.subspan(28,4))),number(u32(data,32)));
    return out+'}';
}

// Only this already-isolated child process receives this privilege. Always
// restore the previous token attributes, including partial assignment failure.
class FirmwarePrivilege {
    HANDLE token_{};TOKEN_PRIVILEGES previous_{};bool restore_{};
public:
    bool available{};DWORD error{};std::string stage;
    FirmwarePrivilege(){
        if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY|TOKEN_ADJUST_PRIVILEGES,&token_)){
            error=GetLastError();stage="OpenProcessToken";return;
        }
        TOKEN_PRIVILEGES requested{};requested.PrivilegeCount=1;
        if(!LookupPrivilegeValueW(nullptr,L"SeSystemEnvironmentPrivilege",&requested.Privileges[0].Luid)){
            error=GetLastError();stage="LookupPrivilegeValue SeSystemEnvironmentPrivilege";return;
        }
        requested.Privileges[0].Attributes=SE_PRIVILEGE_ENABLED;
        DWORD returned{};SetLastError(ERROR_SUCCESS);
        const BOOL adjusted=AdjustTokenPrivileges(token_,FALSE,&requested,sizeof(previous_),&previous_,&returned);
        error=GetLastError();restore_=adjusted!=FALSE;
        available=adjusted&&error==ERROR_SUCCESS;
        stage="AdjustTokenPrivileges SeSystemEnvironmentPrivilege (child process only)";
    }
    ~FirmwarePrivilege(){
        if(token_){if(restore_)AdjustTokenPrivileges(token_,FALSE,&previous_,0,nullptr,nullptr);CloseHandle(token_);}
    }
    FirmwarePrivilege(const FirmwarePrivilege&)=delete;
    FirmwarePrivilege& operator=(const FirmwarePrivilege&)=delete;
};

class Collector {
    const FirmwareEmitter& emit_;
    const bool stateOnly_;
    const Clock::time_point started_=Clock::now(),deadline_=started_+std::chrono::seconds(20);
    std::size_t records_{},bytes_{},registryKeys_{};
    unsigned apiIndex_{},registryIndex_{};
    bool limitReported_{};
    void add(FirmwareRecord record){
        if(records_>=maximumFirmwareRecords)return;
        if(record.bytes.size()>maximumFirmwareArtifactBytes||record.bytes.size()>maximumFirmwareTotalBytes-bytes_){
            record.bytes.clear();record.name.clear();record.success=false;record.errorDomain.clear();record.errorCode.reset();
            record.status="Collector byte limit reached; raw data not emitted";
        }
        const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-started_).count();
        SYSTEMTIME time{};GetSystemTime(&time);
        const auto utc=std::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}.{:03}Z",time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond,time.wMilliseconds);
        record.metadataJson=std::format("{{\"state_only\":{},\"collector_elapsed_ms\":{},\"utc\":{},\"details\":{}}}",stateOnly_,elapsed,firmwareQuote(utc),record.metadataJson);
        bytes_+=record.bytes.size();++records_;emit_(record);
    }
    bool alive(){
        const bool ready=Clock::now()<deadline_&&records_<maximumFirmwareRecords&&bytes_<maximumFirmwareTotalBytes;
        if(!ready&&!limitReported_){
            limitReported_=true;FirmwareRecord r;r.kind="collector_limit";r.source="cooperative read-only worker budget";
            r.status="Collection stopped at time/record/byte limit; remaining sources are not claimed complete";
            r.metadataJson=std::format("{{\"deadline_ms\":20000,\"records\":{},\"bytes\":{},\"registry_keys\":{}}}",records_,bytes_,registryKeys_);add(std::move(r));
        }
        return ready;
    }
    void failure(std::string kind,std::string source,std::string status,DWORD code,std::string metadata="{}"){
        FirmwareRecord r;r.kind=std::move(kind);r.source=std::move(source);r.status=std::move(status);
        r.errorDomain="win32";r.errorCode=code;r.metadataJson=std::move(metadata);add(std::move(r));
    }
    void note(std::string kind,std::string source,std::string status,std::string metadata="{}"){
        FirmwareRecord r;r.kind=std::move(kind);r.source=std::move(source);r.status=std::move(status);r.metadataJson=std::move(metadata);add(std::move(r));
    }
    void exception(std::string source,const std::exception& e){note("source_exception",std::move(source),e.what());}

    void uefi(){
        const auto board=readBoardDescriptor();const auto id=deviceProfile(board).id;
        const bool gm6=id=="mechrevo-gm6ir0c"&&board.manufacturer=="MECHREVO"&&board.baseBoard=="GM6IR0C"&&board.productName=="JiguangX Series GM6IR0C";
        const bool hydroc=id=="eluktronics-hydroc16g2"&&board.manufacturer=="Eluktronics Inc."&&board.baseBoard=="HYDROC-16"&&board.productName=="HYDROC-16 G2";
        if(!gm6&&!hydroc){
            for(const auto name:variableNames)if(alive())note("uefi_variable",utf8(name),"Not read: actual SMBIOS is outside the reviewed GM6/HYDROC allowlist",
                std::format("{{\"guid\":{},\"actual_profile\":{},\"allowlist_applicable\":false}}",firmwareQuote(utf8(variableGuid)),firmwareQuote(std::string(id))));
            return;
        }
        FirmwarePrivilege privilege;
        for(const auto name:variableNames){
            if(!alive())break;
            const auto variable=utf8(name);const auto source="GetFirmwareEnvironmentVariableExW "+utf8(variableGuid)+" / "+variable;
            if(!privilege.available){
                failure("uefi_variable",source,"Variable not read: "+privilege.stage,privilege.error,
                    std::format("{{\"guid\":{},\"variable\":{},\"independent_read\":true}}",firmwareQuote(utf8(variableGuid)),firmwareQuote(variable)));continue;
            }
            std::size_t capacity=512;unsigned attempts{};bool emitted{};
            while(alive()){
                ++attempts;std::vector<std::uint8_t> data(capacity);DWORD attributes{};SetLastError(ERROR_SUCCESS);
                const DWORD returned=GetFirmwareEnvironmentVariableExW(name,variableGuid,data.data(),static_cast<DWORD>(data.size()),&attributes);
                const DWORD error=GetLastError();
                if(returned&&returned<=data.size()){
                    data.resize(returned);FirmwareRecord r;r.kind="uefi_variable";r.name="uefi/"+variable+".bin";
                    r.source=source;r.success=true;r.attributes=attributes;r.status="Independent allowlisted variable read; raw bytes retained, layout not confirmed";
                    const bool door=variable=="OemMagicDoor";
                    r.metadataJson=std::format("{{\"guid\":{},\"variable\":{},\"actual_profile\":{},\"independent_read\":true,\"fallback_substitution\":false,\"returned_bytes\":{},\"buffer_capacity\":{},\"attempts\":{},\"native_reference_capacity\":512,\"managed_reference_prefix_bytes\":{},\"layout_confirmed\":false,\"candidates_are_applied_state\":false",
                        firmwareQuote(utf8(variableGuid)),firmwareQuote(variable),firmwareQuote(std::string(id)),returned,capacity,attempts,door?"null":"188");
                    if(door){r.metadataJson+=",\"door_byte0_raw_candidate\":"+(data.empty()?std::string("null"):std::to_string(data[0]));}
                    else{
                        r.metadataJson+=std::format(",\"managed_prefix_present\":{},\"candidate_offsets\":{{\"project_id\":6,\"battery_limitation\":48,\"charge_maximum\":49,\"charge_minimum\":50,\"ap_version_check\":67}},\"raw_candidates\":{{",data.size()>=188);
                        bool first=true;for(const auto [key,offset]:std::array{std::pair{"project_id",6u},std::pair{"battery_limitation",48u},std::pair{"charge_maximum",49u},std::pair{"charge_minimum",50u},std::pair{"ap_version_check",67u}}){
                            if(!first)r.metadataJson+=',';first=false;r.metadataJson+=firmwareQuote(key)+":"+(offset<data.size()?std::to_string(data[offset]):"null");}
                        r.metadataJson+="}";
                    }
                    r.metadataJson+='}';r.bytes=std::move(data);add(std::move(r));emitted=true;break;
                }
                if(!returned&&error==ERROR_INSUFFICIENT_BUFFER&&capacity<maximumUefiBytes){capacity=std::min(capacity*2,maximumUefiBytes);continue;}
                failure("uefi_variable",source,returned?"Invalid variable return length; no bytes emitted":"Variable unavailable/denied/legacy or read failed; exact Win32 retained",error,
                    std::format("{{\"guid\":{},\"variable\":{},\"independent_read\":true,\"returned_bytes\":{},\"buffer_capacity\":{},\"attempts\":{},\"maximum_bytes\":{}}}",firmwareQuote(utf8(variableGuid)),firmwareQuote(variable),returned,capacity,attempts,maximumUefiBytes));
                emitted=true;break;
            }
            if(!emitted&&records_<maximumFirmwareRecords)note("uefi_variable",source,"Not completed before collector budget expired");
        }
    }

    void apiAcpi(){
        if(!alive())return;SetLastError(ERROR_SUCCESS);
        DWORD needed=EnumSystemFirmwareTables(acpiProvider,nullptr,0);const DWORD firstError=GetLastError();
        if(!needed){failure("acpi_enumeration","EnumSystemFirmwareTables ACPI","ACPI enumeration unavailable",firstError);return;}
        std::vector<std::uint8_t> ids;DWORD returned{};
        for(unsigned attempt=0;attempt<3&&alive();++attempt){
            if(needed>maximumFirmwareArtifactBytes||needed>maximumFirmwareRecords*4){note("acpi_enumeration","EnumSystemFirmwareTables ACPI","Enumeration exceeds bounded table-ID/record limit",std::format("{{\"required_bytes\":{},\"maximum_ids\":{}}}",needed,maximumFirmwareRecords));return;}
            ids.resize(needed);SetLastError(ERROR_SUCCESS);returned=EnumSystemFirmwareTables(acpiProvider,ids.data(),needed);const auto error=GetLastError();
            if(!returned){failure("acpi_enumeration","EnumSystemFirmwareTables ACPI","Enumeration read failed",error);return;}
            if(returned>ids.size()){needed=returned;continue;}
            ids.resize(returned);break;
        }
        if(!alive())return;
        if(returned>ids.size()||returned%4){note("acpi_enumeration","EnumSystemFirmwareTables ACPI","Malformed/changing table ID list; no padded IDs accepted");return;}
        std::map<std::uint32_t,unsigned> count,occurrence;
        for(std::size_t i=0;i<ids.size();i+=4)++count[*u32(ids,i)];
        std::string metadata="{\"api_returns_first_for_duplicate_signature\":true,\"all_duplicate_instances_claimed_complete\":false,\"registry_backup_collected_separately\":true,\"ids\":[";
        bool first=true;for(std::size_t i=0;i<ids.size();i+=4){if(!first)metadata+=',';first=false;const auto id=*u32(ids,i);
            metadata+=std::format("{{\"id\":{},\"signature\":{},\"signature_occurrences\":{}}}",id,firmwareQuote(signature(id)),count[id]);}metadata+="]}";
        note("acpi_enumeration","EnumSystemFirmwareTables ACPI","Table IDs enumerated; duplicate signature APIs return only the first matching table",metadata);
        for(std::size_t i=0;i<ids.size()&&alive();i+=4){
            const auto id=*u32(ids,i);const auto sig=signature(id);const auto index=++apiIndex_,which=++occurrence[id];
            const auto source=std::format("GetSystemFirmwareTable ACPI id=0x{:08X} occurrence={}/{}",id,which,count[id]);
            if(excluded(sig)){note("acpi_excluded",source,"Activation/licensing table excluded; no raw read");continue;}
            std::vector<std::uint8_t> data;bool got=false;
            for(unsigned attempt=0;attempt<3&&alive();++attempt){
                SetLastError(ERROR_SUCCESS);const DWORD size=GetSystemFirmwareTable(acpiProvider,id,nullptr,0);const auto queryError=GetLastError();
                if(!size){failure("acpi_api",source,"Table size query failed",queryError);break;}
                if(size>maximumFirmwareArtifactBytes){note("acpi_api",source,"Table exceeds artifact limit",std::format("{{\"required_bytes\":{}}}",size));break;}
                data.resize(size);SetLastError(ERROR_SUCCESS);const DWORD actual=GetSystemFirmwareTable(acpiProvider,id,data.data(),size);const auto error=GetLastError();
                if(!actual){failure("acpi_api",source,"Table read failed",error);break;}
                if(actual>data.size()){if(attempt==2)note("acpi_api",source,"Table grew on all bounded retries; no raw bytes claimed");continue;}
                data.resize(actual);got=true;break;
            }
            if(!got)continue;
            if(excluded(signature(data))){note("acpi_excluded",source,"Raw activation/licensing signature excluded; no bytes emitted");continue;}
            FirmwareRecord r;r.kind="acpi_api";r.name=std::format("acpi/api-{:04}-{}.aml",index,fileSignature(sig));r.source=source;r.success=true;
            r.status="OS table read; checksum/length validation is metadata, not AML execution";
            r.metadataJson=std::format("{{\"listed_signature\":{},\"id\":{},\"occurrence_index\":{},\"signature_occurrences\":{},\"api_returns_first_for_duplicate_signature\":true,\"table_validation\":{}}}",firmwareQuote(sig),id,which,count[id],acpiMetadata(data));
            r.bytes=std::move(data);add(std::move(r));
        }
    }

    void smbios(){
        const std::string source="GetSystemFirmwareTable RSMB id=0";
        for(unsigned attempt=0;attempt<3&&alive();++attempt){
            SetLastError(ERROR_SUCCESS);const DWORD size=GetSystemFirmwareTable(rsmbProvider,0,nullptr,0);const auto queryError=GetLastError();
            if(!size){failure("smbios",source,"RawSMBIOS size query failed",queryError);return;}
            if(size>maximumFirmwareArtifactBytes){note("smbios",source,"RawSMBIOS exceeds artifact limit",std::format("{{\"required_bytes\":{}}}",size));return;}
            std::vector<std::uint8_t> data(size);SetLastError(ERROR_SUCCESS);const DWORD actual=GetSystemFirmwareTable(rsmbProvider,0,data.data(),size);const auto error=GetLastError();
            if(!actual){failure("smbios",source,"RawSMBIOS read failed",error);return;}
            if(actual>data.size()){if(attempt==2)note("smbios",source,"RawSMBIOS grew on all bounded retries");continue;}
            data.resize(actual);FirmwareRecord r;r.kind="smbios";r.name="smbios/windows-rsmb.bin";r.source=source;r.success=true;r.status="Windows RawSMBIOSData retained, including its API header";
            const auto payload=u32(data,4);
            r.metadataJson=std::format("{{\"actual_bytes\":{},\"api_header_bytes\":8,\"declared_payload_bytes\":{},\"payload_length_matches\":{},\"raw_retained_without_padding\":true",data.size(),number(payload),payload&&data.size()>=8&&*payload==data.size()-8);
            if(data.size()>=4)r.metadataJson+=std::format(",\"used_20_calling_method\":{},\"major\":{},\"minor\":{},\"dmi_revision\":{}",data[0],data[1],data[2],data[3]);
            r.metadataJson+='}';r.bytes=std::move(data);add(std::move(r));return;
        }
    }

    void registryKey(HKEY key,const std::wstring& path,unsigned depth){
        if(!alive())return;
        if(depth>maximumRegistryDepth||registryKeys_>=maximumRegistryKeys){note("acpi_registry_limit",utf8(path),"Registry key/depth bound reached; branch not claimed complete");return;}
        ++registryKeys_;
        DWORD subkeys{},values{};const auto info=RegQueryInfoKeyW(key,nullptr,nullptr,nullptr,&subkeys,nullptr,nullptr,&values,nullptr,nullptr,nullptr,nullptr);
        if(info!=ERROR_SUCCESS){failure("acpi_registry",utf8(path),"RegQueryInfoKey failed",info);return;}
        if(values>maximumFirmwareRecords||subkeys>maximumRegistryKeys)
            note("acpi_registry_limit",utf8(path),"Per-key value/subkey index bound will truncate this branch",
                std::format("{{\"native_value_count\":{},\"native_subkey_count\":{},\"maximum_values_examined\":{},\"maximum_subkeys_examined\":{}}}",values,subkeys,maximumFirmwareRecords,maximumRegistryKeys));
        std::vector<wchar_t> valueName(32768);
        for(DWORD index=0;index<values&&index<maximumFirmwareRecords&&alive();++index){
            DWORD nameSize=static_cast<DWORD>(valueName.size()),type{},size{};
            const auto listed=RegEnumValueW(key,index,valueName.data(),&nameSize,nullptr,&type,nullptr,&size);
            if(listed==ERROR_NO_MORE_ITEMS)break;
            if(listed!=ERROR_SUCCESS){failure("acpi_registry",utf8(path),"RegEnumValue failed",listed);continue;}
            if(type!=REG_BINARY)continue;
            const std::wstring name(valueName.data(),nameSize);const auto source=utf8(path)+" / value="+utf8(name);const auto instance=++registryIndex_;
            std::vector<std::uint8_t> data;bool got=false;
            for(unsigned attempt=0;attempt<3&&alive();++attempt){
                DWORD actualType{},required{};const auto queried=RegQueryValueExW(key,name.c_str(),nullptr,&actualType,nullptr,&required);
                if(queried!=ERROR_SUCCESS){failure("acpi_registry",source,"RegQueryValue size failed",queried);break;}
                if(actualType!=REG_BINARY){note("acpi_registry",source,"Value type changed; not treated as a binary table");break;}
                if(required>maximumFirmwareArtifactBytes){note("acpi_registry",source,"Binary value exceeds artifact limit",std::format("{{\"required_bytes\":{}}}",required));break;}
                data.resize(required);DWORD actual=required;
                const auto read=RegQueryValueExW(key,name.c_str(),nullptr,&actualType,data.empty()?nullptr:data.data(),&actual);
                if(read==ERROR_MORE_DATA){if(attempt==2)failure("acpi_registry",source,"Binary value changed on all bounded retries",read);continue;}
                if(read!=ERROR_SUCCESS){failure("acpi_registry",source,"RegQueryValue data failed",read);break;}
                if(actualType!=REG_BINARY||actual>data.size()){note("acpi_registry",source,"Binary value size/type changed; no padded bytes emitted");break;}
                data.resize(actual);got=true;break;
            }
            if(!got)continue;
            const auto sig=signature(data);
            if(excluded(sig)){note("acpi_excluded",source,"Activation/licensing signature excluded; no raw bytes emitted");continue;}
            FirmwareRecord r;r.kind="acpi_registry";r.name=std::format("acpi/registry-{:04}-{}.aml",instance,fileSignature(sig));r.source=source;r.success=true;
            r.status="Independent Windows ACPI registry-cache instance; not a full firmware image";
            r.metadataJson=std::format("{{\"registry_path\":{},\"value_name\":{},\"registry_depth\":{},\"instance_index\":{},\"deduplicated\":false,\"origin\":\"current Windows boot registry cache\",\"table_validation\":{}}}",firmwareQuote(utf8(path)),firmwareQuote(utf8(name)),depth,instance,acpiMetadata(data));
            r.bytes=std::move(data);add(std::move(r));
        }
        std::array<wchar_t,256> subkey{};
        for(DWORD index=0;index<subkeys&&index<maximumRegistryKeys&&alive();++index){
            if(depth>=maximumRegistryDepth){note("acpi_registry_limit",utf8(path),"Maximum registry depth reached; child keys not opened");break;}
            if(registryKeys_>=maximumRegistryKeys){note("acpi_registry_limit",utf8(path),"Maximum registry keys reached; remaining branches not read");break;}
            DWORD length=static_cast<DWORD>(subkey.size());const auto listed=RegEnumKeyExW(key,index,subkey.data(),&length,nullptr,nullptr,nullptr,nullptr);
            if(listed==ERROR_NO_MORE_ITEMS)break;
            if(listed!=ERROR_SUCCESS){failure("acpi_registry",utf8(path),"RegEnumKeyEx failed",listed);continue;}
            const std::wstring name(subkey.data(),length);const auto childPath=path+L"\\"+name;
            if(!_wcsicmp(name.c_str(),L"MSDM")||!_wcsicmp(name.c_str(),L"SLIC")){note("acpi_excluded",utf8(childPath),"Activation/licensing registry branch excluded before reading values");continue;}
            RegistryKey child;const auto opened=RegOpenKeyExW(key,name.c_str(),0,KEY_READ|KEY_WOW64_64KEY,&child.value);
            if(opened!=ERROR_SUCCESS){failure("acpi_registry",utf8(childPath),"RegOpenKeyEx failed",opened);continue;}
            registryKey(child.value,childPath,depth+1);
        }
    }
    void registry(){
        if(!alive())return;RegistryKey key;
        const auto opened=RegOpenKeyExW(HKEY_LOCAL_MACHINE,L"HARDWARE\\ACPI",0,KEY_READ|KEY_WOW64_64KEY,&key.value);
        if(opened!=ERROR_SUCCESS){failure("acpi_registry","HKLM\\HARDWARE\\ACPI","ACPI registry cache unavailable",opened);return;}
        registryKey(key.value,L"HKLM\\HARDWARE\\ACPI",0);
        note("acpi_registry_summary","HKLM\\HARDWARE\\ACPI","Bounded registry scan finished/limited; duplicate instances retained independently",
            std::format("{{\"keys_read\":{},\"binary_instances\":{},\"maximum_keys\":{},\"maximum_depth\":{},\"budget_remaining\":{},\"cache_is_full_flash\":false}}",registryKeys_,registryIndex_,maximumRegistryKeys,maximumRegistryDepth,alive()));
    }
    void ownership(){
        if(!alive())return;
        constexpr std::array<const wchar_t*,3> services{L"GCUBridge",L"GCUService",L"AiStoneService"};
        ServiceHandle manager;manager.value=OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT);
        const auto managerError=manager.value?ERROR_SUCCESS:GetLastError();
        for(const auto name:services){
            if(!alive())break;
            const auto serviceName=utf8(name),source="SCM query-only service "+serviceName;
            if(!manager.value){failure("oem_service",source,"OpenSCManager failed; service state unknown",managerError);continue;}
            ServiceHandle service;service.value=OpenServiceW(manager.value,name,SERVICE_QUERY_STATUS);
            if(!service.value){const auto error=GetLastError();
                failure("oem_service",source,error==ERROR_SERVICE_DOES_NOT_EXIST?"Fixed service is not installed":"OpenService failed; state unknown",error,
                    std::format("{{\"service\":{},\"installed\":{},\"read_only\":true}}",firmwareQuote(serviceName),error==ERROR_SERVICE_DOES_NOT_EXIST?"false":"null"));continue;
            }
            SERVICE_STATUS_PROCESS status{};DWORD returned{};
            if(!QueryServiceStatusEx(service.value,SC_STATUS_PROCESS_INFO,reinterpret_cast<LPBYTE>(&status),sizeof(status),&returned)){
                const auto error=GetLastError();failure("oem_service",source,"QueryServiceStatusEx failed; state unknown",error);continue;
            }
            if(returned<sizeof(status)){note("oem_service",source,"QueryServiceStatusEx returned a short status; fields not treated as observations",
                std::format("{{\"returned_bytes\":{},\"required_status_bytes\":{}}}",returned,sizeof(status)));continue;}
            FirmwareRecord r;r.kind="oem_service";r.source=source;r.success=true;r.status="Read-only fixed service state; no service control requested";
            r.metadataJson=std::format("{{\"service\":{},\"installed\":true,\"service_state\":{},\"process_id\":{},\"service_type\":{},\"win32_exit_code\":{},\"service_exit_code\":{},\"read_only\":true}}",
                firmwareQuote(serviceName),status.dwCurrentState,status.dwProcessId,status.dwServiceType,status.dwWin32ExitCode,status.dwServiceSpecificExitCode);add(std::move(r));
        }
        if(!alive())return;
        ProcessHandle snapshot;snapshot.value=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
        if(snapshot.value==INVALID_HANDLE_VALUE){const auto error=GetLastError();failure("oem_processes","Toolhelp fixed OEM process names","Process snapshot failed; ownership unknown",error);return;}
        PROCESSENTRY32W p{sizeof(p)};BOOL present=Process32FirstW(snapshot.value,&p);
        DWORD lastError=present?ERROR_SUCCESS:GetLastError();unsigned examined{},matches{};std::string list="[";
        while(present&&examined<32768&&matches<256&&alive()){
            ++examined;
            if(!_wcsicmp(p.szExeFile,L"GCUService.exe")||!_wcsicmp(p.szExeFile,L"AiStoneService.exe")){
                if(matches++)list+=',';
                list+=std::format("{{\"name\":{},\"process_id\":{}}}",firmwareQuote(!_wcsicmp(p.szExeFile,L"GCUService.exe")?"GCUService.exe":"AiStoneService.exe"),p.th32ProcessID);
            }
            present=Process32NextW(snapshot.value,&p);lastError=present?ERROR_SUCCESS:GetLastError();
        }
        list+=']';const bool complete=!present&&lastError==ERROR_NO_MORE_FILES;
        FirmwareRecord r;r.kind="oem_processes";r.source="Toolhelp fixed GCUService.exe/AiStoneService.exe names";r.success=complete;
        r.status=complete?"Complete read-only process-name snapshot":"Partial/failed process-name snapshot; owner absence not established";
        if(!present&&!complete){r.errorDomain="win32";r.errorCode=lastError;}
        r.metadataJson=std::format("{{\"enumeration_complete\":{},\"owner_absence_confirmed\":{},\"entries_examined\":{},\"matches\":{},\"command_lines_collected\":false,\"service_control_requested\":false}}",complete,complete&&matches==0,examined,list);add(std::move(r));
    }
public:
    Collector(bool stateOnly,const FirmwareEmitter& emit):emit_(emit),stateOnly_(stateOnly){}
    void run(){
        // UEFI first: a large duplicate-table registry cache must not consume
        // the entire deadline before these small before/after state snapshots.
        try{uefi();}catch(const std::exception& e){exception("UEFI allowlist",e);}
        if(stateOnly_){try{ownership();}catch(const std::exception& e){exception("OEM query-only ownership",e);}return;}
        try{apiAcpi();}catch(const std::exception& e){exception("ACPI firmware API",e);}
        try{smbios();}catch(const std::exception& e){exception("Windows RawSMBIOS",e);}
        try{registry();}catch(const std::exception& e){exception("ACPI registry cache",e);}
        try{ownership();}catch(const std::exception& e){exception("OEM query-only ownership",e);}
    }
};
}
void collectPlatformFirmware(bool stateOnly,const FirmwareEmitter& emit){Collector(stateOnly,emit).run();}
}
