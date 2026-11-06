#include "MagicControl/VbiosRomDump.h"
#include "MagicControl/NvidiaGpu.h"
#include "MagicControl/Logging.h"
#include "MagicControl/Version.h"
#include "MagicControl/DeviceOperation.h"
#include "MagicControl/TelemetryGate.h"
#include "MagicControl/GpuPresence.h"
#include "MagicControl/ControlAudit.h"
#include "MagicControl/BoardIdentity.h"
#include "MagicControl/DeviceProfile.h"
#include "FirmwareEvidence.h"
#include "NvflashPlatform.h"
#include "NvflashOutput.h"
#include "VbiosImageValidation.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <format>
#include <fstream>
#include <regex>
#include <sstream>
#include <cctype>
namespace mc {
namespace {
namespace fs=std::filesystem;
using diagnostics::firmwareQuote;
std::string q(std::string_view s){return firmwareQuote(s);}
std::string pathText(const fs::path& p){const auto s=p.u8string();return {reinterpret_cast<const char*>(s.data()),s.size()};}
std::string time(){return std::format("{:%FT%TZ}",std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));}
void cancelled(std::stop_token t){if(t.stop_requested())throw std::runtime_error("VBIOS: cancelled; raw/partial files retained");}
std::string pci(const GraphicsAdapter& a){
    const auto& l=a.location;
    if(!l.domain||!l.bus||!l.device||!l.function)return {};
    return std::format("{:04x}:{:02x}:{:02x}.{:x}",*l.domain,*l.bus,*l.device,*l.function);
}
std::string identity(const GraphicsAdapter& a){return "{\"name\":"+q(a.name)+",\"pci\":"+q(pci(a))+",\"vendor\":"+std::to_string(a.vendorId)+",\"device\":"+std::to_string(a.deviceId)+",\"subsystem\":"+std::to_string(a.subsystemId)+",\"os_id\":"+q(a.id)+",\"physical_id\":"+q(a.physicalId)+",\"identity_source\":"+q(a.identitySource)+",\"identity_status\":"+q(a.identityStatus)+",\"driver\":"+q(a.driver)+"}";}
// Use the same physical-address reconstruction for initial selection and every
// revalidation. Windows PnP may report B/D/F without a PCI segment each time.
std::vector<GraphicsAdapter> bindProviderPci(std::vector<GraphicsAdapter> adapters,const std::vector<NvidiaGpuReading>& providerRows){
    const std::regex bdf(R"(^([0-9A-Fa-f]{4,8}):([0-9A-Fa-f]{2}):([0-9A-Fa-f]{2})\.([0-7])$)");
    for(auto& a:adapters){
        if(a.physicalId.empty()||!a.location.bus||!a.location.device||!a.location.function)continue;
        unsigned count{};GpuPciLocation resolved;
        for(const auto& row:providerRows){
            if(row.pciDeviceId!=std::optional<unsigned>((a.deviceId<<16)|a.vendorId)||row.pciSubsystemId!=std::optional<unsigned>(a.subsystemId))continue;
            std::smatch m;if(!std::regex_match(row.pci,m,bdf))continue;
            const auto h=[&](unsigned i){return static_cast<unsigned>(std::stoul(m[i].str(),nullptr,16));};
            if(h(1)>65535||h(2)!=a.location.bus||h(3)!=a.location.device||h(4)!=a.location.function)continue;
            if(a.location.domain&&h(1)!=a.location.domain)continue;
            const auto peers=std::count_if(adapters.begin(),adapters.end(),[&](const auto& peer){return peer.vendorId==a.vendorId&&peer.deviceId==a.deviceId&&peer.subsystemId==a.subsystemId&&peer.location.bus==a.location.bus&&peer.location.device==a.location.device&&peer.location.function==a.location.function;});
            if(peers!=1)continue;
            ++count;resolved={h(1),h(2),h(3),h(4),"NVML BDF + unique Windows PCI B/D/F and VEN/DEV/SUBSYS"};
        }
        if(count==1){a.location=resolved;a.pci=pci(a);a.identitySource+="; "+resolved.source;}
    }
    return adapters;
}
std::vector<std::uint8_t> bytes(const fs::path& p,std::uintmax_t limit=64*1024*1024){
    const auto n=fs::file_size(p);if(n>limit)throw std::runtime_error("VBIOS artifact exceeds size limit");
    std::vector<std::uint8_t> b(static_cast<std::size_t>(n));std::ifstream f(p,std::ios::binary);
    if(n)f.read(reinterpret_cast<char*>(b.data()),static_cast<std::streamsize>(n));if(!f)throw std::runtime_error("Cannot read VBIOS artifact");return b;
}
struct Listed {unsigned index{},vendor{},device{},subvendor{},subdevice{},domain{},bus{},slot{},function{};};
unsigned indexFor(const fs::path& p,const GraphicsAdapter& target){
    const auto raw=bytes(p,32*1024*1024);const std::string text(raw.begin(),raw.end());
    // Standard 5.867 listing: <index> name (VEN,DEV,SUBVEN,SUBDEV) ...
    // S:segment,B:bus,D:device,F:function. No name/index0/display-route fallback.
    const std::regex row(R"(<([0-9]+)>[^\r\n]*\(([0-9A-Fa-f]{4}),\s*([0-9A-Fa-f]{4}),\s*([0-9A-Fa-f]{4}),\s*([0-9A-Fa-f]{4})\)[^\r\n]*S:([0-9A-Fa-f]{1,4}),\s*B:([0-9A-Fa-f]{1,2}),\s*D:([0-9A-Fa-f]{1,2}),\s*F:([0-9A-Fa-f]{1,2})(?=\s|$|,))");
    unsigned matches{},result{};std::vector<unsigned> indexes;
    for(std::sregex_iterator it(text.begin(),text.end(),row),end;it!=end;++it){
        const auto& m=*it;Listed a;a.index=static_cast<unsigned>(std::stoul(m[1].str()));
        const auto h=[&](unsigned i){return static_cast<unsigned>(std::stoul(m[i].str(),nullptr,16));};
        a.vendor=h(2);a.device=h(3);a.subvendor=h(4);a.subdevice=h(5);a.domain=h(6);a.bus=h(7);a.slot=h(8);a.function=h(9);
        if(a.index>255||a.slot>31||a.function>7||std::find(indexes.begin(),indexes.end(),a.index)!=indexes.end())throw std::runtime_error("NVFlash: invalid/duplicate adapter index or BDF");indexes.push_back(a.index);
        const auto& l=target.location;
        if(a.vendor==target.vendorId&&a.device==target.deviceId&&a.subvendor==(target.subsystemId&0xffff)&&a.subdevice==(target.subsystemId>>16)&&
           a.domain==l.domain&&a.bus==l.bus&&a.slot==l.device&&a.function==l.function){++matches;result=a.index;}
    }
    if(matches!=1)throw std::runtime_error("NVFlash list cannot be bound uniquely to the selected PCI address and all four IDs; raw list retained");
    return result;
}
struct Capture {
    std::string name,prefix,canonical,kind,state{"not_started"},error,shape{"null"};
    std::array<std::uint64_t,2> sizes{};
    std::array<std::string,2> hashes{};
    bool equal{},verified{},compared{};
    Capture(std::string n,std::string p,std::string file,std::string type):name(std::move(n)),prefix(std::move(p)),canonical(std::move(file)),kind(std::move(type)){}
};
std::string captureJson(const Capture& c){return "{\"capture_kind\":"+q(c.kind)+",\"state\":"+q(c.state)+",\"canonical\":"+q(c.canonical)+",\"pass1_bytes\":"+std::to_string(c.sizes[0])+",\"pass2_bytes\":"+std::to_string(c.sizes[1])+",\"pass1_sha256\":"+q(c.hashes[0])+",\"pass2_sha256\":"+q(c.hashes[1])+",\"two_passes_equal\":"+(c.equal?"true":"false")+",\"verified\":"+(c.verified?"true":"false")+",\"vendor_raw_compare\":"+(c.compared?"true":"false")+",\"shape\":"+c.shape+",\"error\":"+q(c.error)+"}";}
std::string capacityJson(const diagnostics::nvflash::Capacity& c){return "{\"recognized\":"+std::string(c.recognized?"true":"false")+",\"admitted\":"+(c.admitted?"true":"false")+",\"bytes\":"+std::to_string(c.bytes)+",\"k_units\":"+std::to_string(c.kUnits)+",\"organization_bits\":"+std::to_string(c.width)+",\"manufacturer\":"+std::to_string(c.manufacturer)+",\"device\":"+std::to_string(c.device)+",\"part\":"+q(c.part)+",\"descriptor\":"+q(c.descriptor)+",\"reason\":"+q(c.reason)+",\"source\":\"NVFlash --check recognized EEPROM descriptor; K * width *128 bytes\",\"derived_from_rom_file_size\":false}";}
std::string structure(const std::vector<std::uint8_t>& image,unsigned device){
    const auto s=diagnostics::vbios::validate(image,device);
    std::string out="{\"validated\":true,\"ifr_version\":"+std::to_string(s.ifrVersion)+",\"prefix_bytes_preserved\":"+std::to_string(s.prefixBytes)+",\"trailing_bytes_preserved\":"+std::to_string(s.trailingBytes)+",\"images\":[";
    for(const auto& i:s.images){if(out.back()!='[')out+=',';out+="{\"offset\":"+std::to_string(i.offset)+",\"bytes\":"+std::to_string(i.bytes)+",\"vendor\":"+std::to_string(i.vendor)+",\"device\":"+std::to_string(i.device)+",\"code_type\":"+std::to_string(i.codeType)+",\"checksum8\":"+std::to_string(i.checksum)+"}";}
    return out+"]}";
}
}
namespace {
std::vector<GraphicsAdapter> captureAdapters(){auto rows=graphicsAdapters();std::erase_if(rows,[](const auto& a){return a.software||a.vendorId!=0x10de;});return rows;}
}
std::vector<GraphicsAdapter> vbiosDumpAdapters(){
    auto observation=telemetryGate().tryObservation();
    if(!observation)throw std::runtime_error("NVIDIA adapter selection unavailable during firmware capture");
    return captureAdapters();
}
std::string vbiosAdapterPci(const GraphicsAdapter& a){return pci(a);}
bool vbiosRomDumpAvailable() noexcept{return observeGpuPresence([]()->GpuPresence&{return gpuPresence();},GpuPresenceKind::VbiosDump,[]{return !captureAdapters().empty();});}
VbiosRomDumpResult createVbiosRomDump(std::stop_token token,std::atomic<std::uint64_t>* progress,std::string selectedPci,fs::path outputRoot,std::shared_ptr<TelemetryGate::FirmwarePause> telemetryPause){
    using namespace diagnostics::nvflash;
    VbiosRomDumpResult result;
    Capture vbios{"vbios","pass","vbios.rom","nvflash_saved_vbios"};
    Capture eeprom{"eeprom","eeprom-pass","eeprom-full.rom","nvflash_entire_primary_eeprom"};
    Capacity capacity;std::string error,state="precheck",chosen="null";std::ofstream events;bool fatal{};std::uint64_t completedBytes{};
    const auto synchronize=[&]{result.vbiosSaved=vbios.verified;result.eepromSaved=eeprom.verified;result.saved=result.vbiosSaved&&result.eepromSaved;result.eepromBytes=capacity.bytes;};
    const auto checkpoint=[&]{
        synchronize();if(result.directory.empty())return;
        atomicText(result.directory/"manifest.json","{\"schema\":2,\"app_version\":"+q(MAGICCONTROL_VERSION_STRING)+",\"utc\":"+q(time())+
            ",\"state\":"+q(state)+",\"target\":"+chosen+",\"requested_modes\":[\"vbios\",\"entire_eeprom\"]"+
            ",\"vbios_saved\":"+(result.vbiosSaved?"true":"false")+",\"eeprom_saved\":"+(result.eepromSaved?"true":"false")+
            ",\"saved\":"+(result.saved?"true":"false")+",\"capacity_verified\":"+(result.capacityVerified?"true":"false")+
            ",\"eeprom_expected_bytes\":"+std::to_string(capacity.bytes)+",\"capacity\":"+capacityJson(capacity)+
            ",\"vbios\":"+captureJson(vbios)+",\"eeprom\":"+captureJson(eeprom)+",\"physical_spi_complete\":"+(eeprom.verified?"true":"null")+
            ",\"physical_spi_scope\":\"selected primary NVIDIA EEPROM only\",\"firmware_bootability_verified\":false"+
            ",\"capacity_independently_measured\":false,\"flash_programming_requested\":false,\"error\":"+q(error)+"}\n");
    };
    const Event event=[&](const char* name,const std::string& detail){
        const auto row="\"schema\":2,\"app_version\":"+q(MAGICCONTROL_VERSION_STRING)+",\"utc\":"+q(time())+
            ",\"action_id\":"+std::to_string(audit::capture())+",\"event\":"+q(name)+",\"details\":"+detail;
        events<<'{'<<row<<"}\n";events.flush();recordLog("vbios_dump",row);
        if(!events)throw std::runtime_error("Cannot flush VBIOS/EEPROM event log");
    };
    const auto fileSnapshot=[&](Capture& c,unsigned pass){
        const auto p=result.directory/(c.prefix+std::to_string(pass+1)+".rom");if(!fs::exists(p))return;
        c.sizes[pass]=fs::file_size(p);if(c.sizes[pass]<=64*1024*1024)c.hashes[pass]=diagnostics::firmwareSha256(bytes(p));
    };
    try{
        auto root=outputRoot.empty()?currentLogPath().parent_path():outputRoot;if(root.empty())root=defaultLogDirectory();
        root=fs::absolute(root)/"vbios-dumps";fs::create_directories(root);
        const auto prefix=std::format("vbios-{:%Y%m%d-%H%M%S}",std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));
        for(unsigned i=0;i<1000;++i){const auto p=root/(prefix+"-"+std::to_string(i));if(fs::create_directory(p)){result.directory=p;break;}}
        if(result.directory.empty())throw std::runtime_error("Cannot create unique VBIOS/EEPROM directory");
        events.open(result.directory/"events.jsonl",std::ios::binary);if(!events)throw std::runtime_error("Cannot create VBIOS/EEPROM event log");
        event("capture_started","{\"requested_pci\":"+q(selectedPci)+",\"modes\":[\"vbios\",\"entire_eeprom\"],\"reads_per_mode\":2}");checkpoint();
        if(telemetryPause&&!telemetryGate().owns(telemetryPause))throw std::invalid_argument("VBIOS: foreign telemetry pause");
        if(!telemetryPause)telemetryPause=telemetryGate().requestFirmwareCapture(token);
        telemetryPause->waitForQuiescence(token,std::chrono::seconds(10));
        const auto board=readBoardDescriptor();event("actual_platform","{\"profile\":"+q(deviceProfile(board).id)+",\"manufacturer\":"+q(board.manufacturer)+",\"baseboard\":"+q(board.baseBoard)+",\"product\":"+q(board.productName)+"}");
        auto operation=deviceOperations().tryBeginFirmware(telemetryPause);if(!operation)throw std::runtime_error("Device operation busy; NVIDIA reader not started");
        cancelled(token);for(auto& c:selectedPci)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        auto adapters=captureAdapters();
        // SPDRP provides B/D/F but no segment. Corroborate NVML's full address
        // with the unique physical OS node AND both hardware IDs. No domain0
        // default, display-route inference, or adapter-index fallback.
        auto provider=std::make_shared<NvidiaGpu>();const auto providerRows=provider->read();
        adapters=bindProviderPci(std::move(adapters),providerRows);
        std::string all="[";for(const auto& a:adapters){if(all.size()>1)all+=',';all+=identity(a);}event("os_adapters",all+"]");
        std::vector<GraphicsAdapter> matches;for(const auto& a:adapters)if(selectedPci.empty()||pci(a)==selectedPci)matches.push_back(a);
        if(matches.size()!=1)throw std::runtime_error("Select exactly one actual NVIDIA PCI address; no index0/display fallback");
        const auto target=matches.front();if(pci(target).empty()||target.physicalId.empty()||!target.subsystemId)throw std::runtime_error("NVIDIA physical PCI identity is incomplete");
        chosen=identity(target);event("target_selected",chosen);checkpoint();
        try{for(const auto& row:providerRows)event("provider_before","{\"name\":"+q(row.name)+",\"uuid\":"+q(row.uuid)+",\"pci\":"+q(row.pci)+",\"vbios\":"+q(row.vbios)+",\"driver\":"+q(row.driver)+",\"status\":"+q(row.status)+"}");}
        catch(const std::exception& e){event("provider_unavailable","{\"error\":"+q(e.what())+"}");}
        auto nativeLease=preflight(result.directory,event);
        // Destruction is reverse declaration order: tear down the NVML/NVAPI
        // provider while the capture semaphore is still held, then release the
        // semaphore and finally the device operation. Move every local owner
        // into the same lifetime; otherwise a local shared_ptr can still release
        // the semaphore before the provider. Pending readers retain this alias.
        struct CaptureLifetime {std::shared_ptr<TelemetryGate::FirmwarePause> pause;std::shared_ptr<DeviceOperation> operation;std::shared_ptr<void> native;std::shared_ptr<NvidiaGpu> provider;};
        const auto lifetime=std::make_shared<CaptureLifetime>(CaptureLifetime{std::move(telemetryPause),std::move(operation),std::move(nativeLease),std::move(provider)});
        const std::shared_ptr<void> lease(lifetime,lifetime->native.get());
        const auto rawOutput=[&](const std::string& label){
            std::string raw;for(const auto* suffix:{".stdout.log",".stderr.log"}){
                const auto p=result.directory/(label+suffix);const auto data=bytes(p,32*1024*1024);
                event("raw_output_snapshot","{\"label\":"+q(label)+",\"file\":"+q(pathText(p))+",\"bytes\":"+std::to_string(data.size())+",\"sha256\":"+q(diagnostics::firmwareSha256(data))+"}");
                raw.append(data.begin(),data.end());raw+='\n';
            }return raw;
        };
        const auto invoke=[&](Command command,unsigned index,const fs::path& path,const std::string& label){
            cancelled(token);unsigned code{};
            // A live/pending reader exception aborts both modes. Never start
            // another vendor process while the previous reader may still live.
            try{code=run(command,index,path,result.directory,label,event,token,saves(command)?progress:nullptr,completedBytes,lease);}
            catch(...){fatal=true;throw;}
            return std::pair{code,rawOutput(label)};
        };
        const auto verifyOs=[&]{
            // Pin only the missing PCI segment from the initial NVML identity.
            // Re-read the actual OS devnode and NVFlash list for every pass;
            // do not repeat unrelated NVML power/thermal queries inside capture.
            const auto current=bindProviderPci(captureAdapters(),providerRows);
            if(std::count_if(current.begin(),current.end(),[&](const auto& a){return pci(a)==pci(target)&&a.physicalId==target.physicalId&&a.vendorId==target.vendorId&&a.deviceId==target.deviceId&&a.subsystemId==target.subsystemId;})!=1){
                fatal=true;throw std::runtime_error("OS NVIDIA identity changed during capture");
            }
        };
        state="listing";checkpoint();const auto firstList=invoke(Command::List,0,{},"list-before");
        if(firstList.first||!readProblem(firstList.second).empty())throw std::runtime_error("NVFlash --list failed; raw output retained");
        const auto index=indexFor(result.directory/"list-before.stdout.log",target);event("index_bound","{\"index\":"+std::to_string(index)+",\"target\":"+chosen+"}");
        const auto rebind=[&](const std::string& label){
            verifyOs();const auto listed=invoke(Command::List,0,{},label);
            try{if(listed.first||!readProblem(listed.second).empty()||indexFor(result.directory/(label+".stdout.log"),target)!=index)throw std::runtime_error("NVFlash index/PCI identity changed");}
            catch(...){fatal=true;throw;}
        };
        const auto checkCapacity=[&](const std::string& label){
            const auto checked=invoke(Command::Check,index,{},label);auto c=capacityFromCheck(checked.second);
            if(checked.first){c.admitted=false;c.recognized=false;c.reason="NVFlash --check failed: exit="+std::to_string(checked.first);}
            event("eeprom_capacity","{\"label\":"+q(label)+",\"exit_code\":"+std::to_string(checked.first)+",\"capacity\":"+capacityJson(c)+"}");return c;
        };
        const auto capturePair=[&](Capture& c,Command command){
            const bool entire=command==Command::SaveEntire;c.state="reading";
            event("mode_started","{\"mode\":"+q(c.name)+"}");checkpoint();
            try{
                for(unsigned pass=0;pass<2;++pass){
                    const auto label=c.prefix+std::to_string(pass+1);state=c.name+"_"+std::to_string(pass+1);checkpoint();rebind("list-"+label);
                    if(entire){const auto current=checkCapacity("check-"+label);if(!sameCapacity(capacity,current)){result.capacityVerified=false;throw std::runtime_error("EEPROM identity/uncapped capacity unavailable or changed before read");}}
                    const auto output=result.directory/(label+".rom");const auto returned=invoke(command,index,output,label);
                    fileSnapshot(c,pass);completedBytes+=c.sizes[pass];if(progress)progress->store(completedBytes);
                    event("pass_file","{\"mode\":"+q(c.name)+",\"pass\":"+std::to_string(pass+1)+",\"bytes\":"+std::to_string(c.sizes[pass])+",\"sha256\":"+q(c.hashes[pass])+",\"exit_code\":"+std::to_string(returned.first)+"}");checkpoint();
                    if(returned.first)throw std::runtime_error("NVFlash save failed; raw and partial files retained");
                    if(const auto problem=readProblem(returned.second,entire);!problem.empty())throw std::runtime_error("NVFlash read reported "+problem);
                    if(entire&&!entireSizeMatches(capacity,c.sizes[pass]))throw std::runtime_error("Entire EEPROM file size differs from recognized chip capacity");
                }
                cancelled(token);c.state="verifying";checkpoint();
                c.equal=c.sizes[0]!=0&&c.sizes[0]==c.sizes[1]&&!c.hashes[0].empty()&&c.hashes[0]==c.hashes[1];
                if(!c.equal)throw std::runtime_error("Two reads are empty or differ in size/SHA256");
                const auto image=bytes(result.directory/(c.prefix+"1.rom"));if(diagnostics::firmwareSha256(image)!=c.hashes[0])throw std::runtime_error("Saved file changed before validation");
                if(entire){
                    // Address-space completeness is independent of bootability.
                    try{c.shape=structure(image,target.deviceId);}
                    catch(const std::exception& e){c.shape="{\"validated\":false,\"error\":"+q(e.what())+"}";event("eeprom_structure_observation",c.shape);}
                    rebind("list-eeprom-compare");const auto compared=invoke(Command::Compare,index,result.directory/(c.prefix+"1.rom"),"eeprom-compare");
                    if(compared.first)throw std::runtime_error("Raw EEPROM compare failed");
                    if(const auto problem=readProblem(compared.second);!problem.empty())throw std::runtime_error("Raw EEPROM compare reported "+problem);
                    if(!compareConfirmed(compared.second))throw std::runtime_error("NVFlash did not positively confirm identical firmware images in raw compare");
                    c.compared=true;const auto last=checkCapacity("check-after");
                    if(!sameCapacity(capacity,last)){result.capacityVerified=false;throw std::runtime_error("EEPROM identity/capacity changed after raw compare");}
                }else c.shape=structure(image,target.deviceId);
                rebind("list-"+c.name+"-after");
                fs::copy_file(result.directory/(c.prefix+"1.rom"),result.directory/c.canonical,fs::copy_options::none);
                if(diagnostics::firmwareSha256(bytes(result.directory/c.canonical))!=c.hashes[0])throw std::runtime_error("Canonical file SHA256 differs from verified reads");
                cancelled(token);c.verified=true;c.state="verified";event("mode_verified","{\"mode\":"+q(c.name)+",\"capture\":"+captureJson(c)+"}");checkpoint();
            }catch(const std::exception& e){
                c.error=e.what();c.verified=false;c.state=token.stop_requested()?"cancelled":"failed";event("mode_failed","{\"mode\":"+q(c.name)+",\"error\":"+q(c.error)+"}");checkpoint();
                if(fatal||token.stop_requested())throw;
            }
        };
        // Ordinary VBIOS does not require EEPROM geometry. A restricted --check
        // must not prevent its two reads or erase their independently verified result.
        capturePair(vbios,Command::Save);
        state="checking_capacity";event("capacity_probe_started","{\"ordinary_vbios_saved\":"+std::string(vbios.verified?"true":"false")+"}");checkpoint();
        try{capacity=checkCapacity("check-before");result.capacityVerified=capacity.admitted;}
        catch(const std::exception& e){
            if(fatal||token.stop_requested())throw;
            capacity={};capacity.reason=std::string("EEPROM capacity check failed: ")+e.what();
            event("capacity_probe_failed","{\"error\":"+q(capacity.reason)+"}");
        }
        event("capacity_probe_finished","{\"admitted\":"+std::string(capacity.admitted?"true":"false")+"}");checkpoint();
        if(capacity.admitted)capturePair(eeprom,Command::SaveEntire);
        else{eeprom.state="refused";eeprom.error=capacity.reason;event("entire_refused","{\"reason\":"+q(eeprom.error)+",\"ordinary_vbios_preserved\":"+std::string(vbios.verified?"true":"false")+"}");}
        cancelled(token);state=vbios.verified&&eeprom.verified?"verified":
            vbios.verified||eeprom.verified?"partial":"failed";
        if(!vbios.verified)error="VBIOS: "+vbios.error;if(!eeprom.verified){if(!error.empty())error+="; ";error+="EEPROM: "+eeprom.error;}
        checkpoint();event("capture_finished","{\"outcome\":"+q(result.outcome())+",\"vbios_saved\":"+std::string(result.vbiosSaved?"true":"false")+",\"eeprom_saved\":"+(result.eepromSaved?"true":"false")+",\"error\":"+q(error)+"}");
    }catch(const std::exception& e){
        error=e.what();result.cancelled=token.stop_requested();state=result.cancelled?"cancelled":"failed";
        eeprom.verified=false;if(eeprom.error.empty())eeprom.error=error;
        for(auto* c:{&vbios,&eeprom})for(unsigned pass=0;pass<2;++pass){try{if(!result.directory.empty()){
            fileSnapshot(*c,pass);if(events&&c->sizes[pass])event("partial_file_snapshot","{\"mode\":"+q(c->name)+",\"pass\":"+std::to_string(pass+1)+",\"bytes\":"+std::to_string(c->sizes[pass])+",\"sha256\":"+q(c->hashes[pass])+",\"unverified\":true}");
        }}catch(...){}}
        try{if(events)event("capture_failed","{\"error\":"+q(error)+"}");checkpoint();}
        catch(const std::exception& metadataError){logMessage("vbios_dump",std::string("Failure metadata could not be saved: ")+metadataError.what());}
        catch(...){logMessage("vbios_dump","Failure metadata could not be saved: unknown exception");}
    }catch(...){
        error="Unexpected VBIOS capture exception; raw/partial files retained";result.cancelled=token.stop_requested();state=result.cancelled?"cancelled":"failed";
        if(!vbios.verified){vbios.state=state;if(vbios.error.empty())vbios.error=error;}
        eeprom.verified=false;if(eeprom.error.empty())eeprom.error=error;
        try{event("capture_failed","{\"error\":"+q(error)+"}");checkpoint();}catch(...){logMessage("vbios_dump",error);}
    }
    synchronize();
    result.message=result.saved?"VBIOS и EEPROM: сохранены по два одинаковых чтения; ёмкость, SHA256 и raw compare проверены.":
        std::string(result.vbiosSaved||result.eepromSaved?"Частично: ":"Дамп не завершён: ")+"VBIOS "+(result.vbiosSaved?"сохранён":"не подтверждён")+", EEPROM "+(result.eepromSaved?"сохранён":"не подтверждён")+". "+error;
    if(!result.directory.empty())result.message+=" Файлы: "+pathText(result.directory);
    return result;
}
}
