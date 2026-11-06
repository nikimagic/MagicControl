// Authorized offline verification only. Never links a native platform adapter,
// NVML, the product entry points, vendor payloads, EC or a GPU hardware API.
#include "MagicControl/VbiosRomDump.h"
#include "MagicControl/NvidiaGpu.h"
#include "MagicControl/Logging.h"
#include "MagicControl/ActionLog.h"
#include "MagicControl/BoardIdentity.h"
#include "MagicControl/DeviceProfile.h"
#include "MagicControl/Json.h"
#include "MagicControl/InfoResourceControl.h"
#include "MagicControl/OperationMessages.h"
#include "MagicControl/TelemetryGate.h"
#include "FirmwareEvidence.h"
#include "NvflashPlatform.h"
#include "VbiosImageValidation.h"
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <chrono>
#include <future>
#include <thread>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#else
#include <glib.h>
#endif
namespace fs=std::filesystem;
using mc::diagnostics::nvflash::Command;
unsigned fixtureInfoResults();
unsigned fixtureInfoFinishes();
namespace {
enum class Case { Both, Unknown, Large, Truncated, DifferentEntire, SilentCompare, ChipChanged, PendingReader, PendingCancelled, DifferentVbios, ResolveDomain, AmbiguousDomain, ChangedOs, CleanupFailure, CancelBefore, CancelAfterFirst, HybridIntelPrimary, HybridMissingNvml, ListMismatch, IfrV3Unknown, IfrV3Invalid, IfrV3Different } scenario;
unsigned normalReads{},entireReads{},checks{},compares{},calls{},assertions{};
std::atomic<unsigned> graphicsCalls{};
bool interrupted{};std::stop_source cancellation;
std::vector<std::string> teardown;
std::shared_ptr<void> pendingReaderLease;
void require(bool ok,const char* reason){++assertions;if(!ok)throw std::runtime_error(reason);}
void write(const fs::path& p,const std::string& text){std::ofstream f(p,std::ios::binary);f<<text;if(!f)throw std::runtime_error("fixture write failed");}
std::string read(const fs::path& p){std::ifstream f(p,std::ios::binary);if(!f)throw std::runtime_error("fixture read failed");return {std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};}
bool ifrCase(){return scenario==Case::IfrV3Unknown||scenario==Case::IfrV3Invalid||scenario==Case::IfrV3Different;}
bool unknownCapacity(){return scenario==Case::Unknown||ifrCase();}
void image(const fs::path& p,bool entire){
    const std::size_t at=!entire&&ifrCase()?8192:0;
    std::vector<std::uint8_t> b(entire?2097152:at+512,entire?255:0);
    if(at){
        // Synthetic IFR v3 wrapper: fixed header points to RFRD at 4096,
        // whose PCI image offset points to an independent 512-byte ROM.
        b[0]='N';b[1]='V';b[2]='G';b[3]='I';b[5]=3;b[8]=16;
        b[4096]='R';b[4097]='F';b[4098]='R';b[4099]=scenario==Case::IfrV3Invalid?'X':'D';
        b[4105]=0x20; // little-endian offset 0x2000
    }
    std::fill(b.begin()+at,b.begin()+at+512,0);b[at]=0x55;b[at+1]=0xaa;b[at+0x18]=0x1c;
    const std::size_t pcir=at+0x1c;b[pcir]='P';b[pcir+1]='C';b[pcir+2]='I';b[pcir+3]='R';
    b[pcir+4]=0xde;b[pcir+5]=0x10;b[pcir+6]=0xa0;b[pcir+7]=0x28;
    b[pcir+0xa]=0x18;b[pcir+0x10]=1;b[pcir+0x15]=0x80;
    if(!entire&&(scenario==Case::DifferentVbios||scenario==Case::IfrV3Different)&&normalReads==2)b[at+0x80]=1;
    unsigned sum{};for(unsigned i=0;i<511;++i)sum+=b[at+i];b[at+511]=static_cast<std::uint8_t>(0-sum);
    if(entire&&scenario==Case::DifferentEntire&&entireReads==2)b.back()^=1;
    if(entire&&scenario==Case::Truncated)b.resize(2048000);
    std::ofstream f(p,std::ios::binary);f.write(reinterpret_cast<const char*>(b.data()),static_cast<std::streamsize>(b.size()));
    if(!f)throw std::runtime_error("fixture image write failed");
}
}
namespace mc {
std::vector<GraphicsAdapter> graphicsAdapters(){
    ++graphicsCalls;
    GraphicsAdapter a;a.name="Synthetic NVIDIA";a.id="mock-adapter";a.physicalId="mock-pci";
    a.vendorId=0x10de;a.deviceId=0x28a0;a.subsystemId=0x12731d05;
    a.location={0,1,0,0,"synthetic"};
    const bool hybrid=scenario==Case::HybridIntelPrimary||scenario==Case::HybridMissingNvml;
    if(scenario==Case::ResolveDomain||scenario==Case::AmbiguousDomain||hybrid)a.location.domain.reset();
    if(scenario==Case::ChangedOs&&normalReads)a.location.bus=2;
    if(hybrid){
        GraphicsAdapter intel;intel.name="Synthetic Intel UHD display GPU";intel.vendorId=0x8086;intel.deviceId=0xa788;
        intel.id="mock-primary-intel";intel.physicalId="mock-intel-pci";intel.location={0,0,2,0,"synthetic Intel"};
        intel.type=GpuType::Integrated;intel.primary=true;intel.primaryKnown=true;
        a.type=GpuType::Discrete;a.primary=false;a.primaryKnown=true;return {intel,a};
    }return {a};
}
BoardDescriptor readBoardDescriptor(){return {"Synthetic","Synthetic", "mock", "Synthetic"};}
const DeviceProfile& deviceProfile(const BoardDescriptor&) noexcept {static const DeviceProfile p{"mock","Synthetic","Synthetic","Synthetic"};return p;}
struct NvidiaGpu::Impl {};
NvidiaGpu::NvidiaGpu():impl_(std::make_unique<Impl>()){}
NvidiaGpu::~NvidiaGpu(){teardown.push_back("provider_destroyed");}
std::vector<NvidiaGpuReading> NvidiaGpu::read(bool){
    require(!telemetryGate().tryObservation(),"VBIOS identification must run inside the firmware pause");
    if(scenario!=Case::ResolveDomain&&scenario!=Case::AmbiguousDomain&&scenario!=Case::HybridIntelPrimary)return {};
    NvidiaGpuReading row;row.pci="0000:01:00.0";row.pciDeviceId=0x28a010de;row.pciSubsystemId=0x12731d05;
    if(scenario==Case::AmbiguousDomain)return {row,row};return {row};
}
fs::path currentLogPath(){throw std::runtime_error("simulation must use explicit output root");}
fs::path defaultLogDirectory(){throw std::runtime_error("simulation must use explicit output root");}
// Exercise the same transcript delivery as the production logger, without
// opening any on-disk logger or retaining real device evidence.
void recordLog(std::string_view channel,std::string_view fields) noexcept {
    OperationMessages::publish(channel,fields,42);
}
}
namespace mc::diagnostics {
bool firmwareValidUtf8(std::string_view) noexcept{return true;}
std::string firmwareSha256(std::span<const std::uint8_t> b){
#ifdef _WIN32
    BCRYPT_ALG_HANDLE algorithm{};BCRYPT_HASH_HANDLE hash{};
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw std::runtime_error("BCrypt algorithm");
    DWORD n{},copied{};BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&n),sizeof(n),&copied,0);
    std::vector<unsigned char> object(n),digest(32);
    if(BCryptCreateHash(algorithm,&hash,object.data(),n,nullptr,0,0)<0)throw std::runtime_error("BCrypt hash");
    if(BCryptHashData(hash,const_cast<PUCHAR>(b.data()),static_cast<ULONG>(b.size()),0)<0||BCryptFinishHash(hash,digest.data(),32,0)<0)throw std::runtime_error("BCrypt finish");
    BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(algorithm,0);
    std::ostringstream s;for(auto c:digest)s<<std::hex<<std::setfill('0')<<std::setw(2)<<unsigned(c);return s.str();
#else
    auto* value=g_compute_checksum_for_data(G_CHECKSUM_SHA256,b.data(),b.size());if(!value)throw std::runtime_error("GLib hash");std::string out=value;g_free(value);return out;
#endif
}
namespace nvflash {
std::shared_ptr<void> preflight(const fs::path&,const Event&){
    require(!telemetryGate().tryObservation(),"native VBIOS preflight must run with background observations paused");
    require(!telemetryGate().trySafetyControl(),"capture must prevent new online cooling loops");
    return std::shared_ptr<void>(new int(1),[](void* p){teardown.push_back("native_lease_released");delete static_cast<int*>(p);});
}
void atomicText(const fs::path& p,const std::string& text){write(p,text);}
unsigned run(Command c,unsigned index,const fs::path& output,const fs::path& artifacts,const std::string& label,const Event& event,std::stop_token,std::atomic<std::uint64_t>*,std::uint64_t,const std::shared_ptr<void>& lease){
    ++calls;require(!interrupted,"another command after pending-reader failure");
    require(c==Command::List||index==3,"wrong synthetic PCI binding/index");
    const auto args=arguments(c,index,output.string());require(!args.empty(),"missing readonly argv");
    std::string text;
    switch(c){
    case Command::List:text=scenario==Case::ListMismatch?"<3> Synthetic NVIDIA (10DE,28A0,1D05,1273) S:00,B:02,D:00,F:00\n":"<3> Synthetic NVIDIA (10DE,28A0,1D05,1273) S:00,B:01,D:00,F:00\n";break;
    case Command::Check:
        ++checks;
        text=unknownCapacity()?"EEPROM ID (EF,4015) : Unknown\n":
            std::string("EEPROM ID (")+(scenario==Case::ChipChanged&&checks>=3?"C2,2015":"EF,4015")+
            ") : Vendor SyntheticPart 2.7-3.6V "+(scenario==Case::Large?"262144Kx1S":"16384Kx1S")+", page\n";
        break;
    case Command::Save:
        ++normalReads;if(scenario==Case::PendingReader||scenario==Case::PendingCancelled){pendingReaderLease=lease;interrupted=true;if(scenario==Case::PendingCancelled)cancellation.request_stop();throw std::runtime_error("synthetic pending reader");}
        image(output,false);
        if(scenario==Case::CleanupFailure){interrupted=true;throw std::runtime_error("synthetic post-reader cleanup failure");}
        if(scenario==Case::CancelAfterFirst)cancellation.request_stop();break;
    case Command::SaveEntire:++entireReads;image(output,true);break;
    case Command::Compare:++compares;if(scenario!=Case::SilentCompare)text="Firmware images are identical\n";break;
    }
    write(artifacts/(label+".stdout.log"),text);write(artifacts/(label+".stderr.log"),"");
    event("synthetic_native_return","{\"exit_code\":0}");return 0;
}
}
}
int main(int argc,char** argv){try{
    require(argc==2,"explicit synthetic output directory required");
    const fs::path root=fs::absolute(argv[1]);fs::create_directories(root);
    // UI discovery must not touch graphics while a previous reader is still
    // holding the ticket; the collector's own identification remains allowed.
    scenario=Case::Unknown;normalReads=entireReads=checks=compares=calls=0;interrupted=false;cancellation=std::stop_source{};teardown.clear();graphicsCalls=0;
    require(mc::vbiosRomDumpAvailable(),"fake VBIOS availability must be warmed from the adapter list");
    const auto beforePause=graphicsCalls.load();auto selectionTicket=mc::telemetryGate().requestFirmwareCapture();
    require(mc::vbiosRomDumpAvailable()&&graphicsCalls==beforePause,"availability must retain its previous value while paused without querying adapters");
    bool selectionRefused=false;try{(void)mc::vbiosDumpAdapters();}catch(const std::runtime_error&){selectionRefused=true;}
    require(selectionRefused&&graphicsCalls==beforePause,"public VBIOS selection must not query adapters while paused");
    const auto ownIdentification=mc::createVbiosRomDump({},nullptr,"0000:01:00.0",root/"own-identification",selectionTicket);
    require(ownIdentification.vbiosSaved&&graphicsCalls>beforePause,"VBIOS collector must still make its own admitted adapter identification");
    selectionTicket.reset();require(bool(mc::telemetryGate().tryObservation()),"selection ticket did not release");
    // Exercise the actual VBIOS collector while an already-running synthetic
    // observation finishes. Only the transport and adapters are faked.
    scenario=Case::Unknown;normalReads=entireReads=checks=compares=calls=0;interrupted=false;cancellation=std::stop_source{};teardown.clear();
    auto inflight=mc::telemetryGate().tryObservation();require(bool(inflight),"synthetic in-flight observation admitted");
    auto waiting=std::async(std::launch::async,[&]{return mc::createVbiosRomDump({},nullptr,"0000:01:00.0",root/"drain-inflight");});
    const auto waitUntil=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    bool blockedNew{};while(std::chrono::steady_clock::now()<waitUntil){if(!mc::telemetryGate().tryObservation()){blockedNew=true;break;}std::this_thread::yield();}
    const bool waited=waiting.wait_for(std::chrono::milliseconds(0))==std::future_status::timeout;
    inflight.reset();
    const auto drained=waiting.get();
    require(blockedNew&&waited&&drained.vbiosSaved,"VBIOS closes admission and waits for an in-flight observation before reading");
    require(bool(mc::telemetryGate().tryObservation())&&!mc::deviceOperations().busy(),"VBIOS releases ownership after a drained capture");

    normalReads=entireReads=checks=compares=calls=0;interrupted=false;cancellation=std::stop_source{};teardown.clear();
    auto supplied=mc::telemetryGate().requestFirmwareCapture();
    const auto suppliedResult=mc::createVbiosRomDump({},nullptr,"0000:01:00.0",root/"supplied-ticket",supplied);
    require(suppliedResult.vbiosSaved&&!mc::telemetryGate().tryObservation(),"VBIOS must accept the caller's existing ticket without re-acquiring it");
    supplied.reset();require(bool(mc::telemetryGate().tryObservation())&&!mc::deviceOperations().busy(),"caller releases the VBIOS ticket after collector completion");
    mc::TelemetryGate foreignGate;auto foreign=foreignGate.requestFirmwareCapture();calls=0;
    const auto foreignResult=mc::createVbiosRomDump({},nullptr,"0000:01:00.0",root/"foreign-ticket",foreign);
    foreign.reset();require(!foreignResult.vbiosSaved&&calls==0&&bool(mc::telemetryGate().tryObservation()),"VBIOS must reject a foreign ticket before native reader");

    scenario=Case::Both;normalReads=entireReads=checks=compares=calls=0;interrupted=false;cancellation=std::stop_source{};teardown.clear();
    auto safety=mc::telemetryGate().trySafetyControl();require(bool(safety),"synthetic online cooling admitted");
    const auto safetyResult=mc::createVbiosRomDump({},nullptr,"0000:01:00.0",root/"active-safety-control");
    safety.reset();
    require(!safetyResult.saved&&!safetyResult.vbiosSaved&&calls==0,"VBIOS refuses an active safety-controlled loop before any native reader");
    require(bool(mc::telemetryGate().tryObservation())&&!mc::deviceOperations().busy(),"safety refusal restores ordinary telemetry admission");

    auto ecDiagnostic=mc::telemetryGate().tryDiagnosticCollection();require(bool(ecDiagnostic),"synthetic EC diagnostic admitted");
    const auto ecBusyResult=mc::createVbiosRomDump({},nullptr,"0000:01:00.0",root/"active-ec-diagnostic");
    require(!ecBusyResult.saved&&!ecBusyResult.vbiosSaved&&calls==0&&bool(mc::telemetryGate().tryObservation()),
        "VBIOS refuses an active EC diagnostic without interrupting its telemetry");
    ecDiagnostic.reset();

    for(const auto test:{Case::Both,Case::Unknown,Case::Large,Case::Truncated,Case::DifferentEntire,Case::SilentCompare,Case::ChipChanged,Case::PendingReader,Case::PendingCancelled,Case::DifferentVbios,Case::ResolveDomain,Case::AmbiguousDomain,Case::ChangedOs,Case::CleanupFailure,Case::CancelBefore,Case::CancelAfterFirst,Case::HybridIntelPrimary,Case::HybridMissingNvml,Case::ListMismatch,Case::IfrV3Unknown,Case::IfrV3Invalid,Case::IfrV3Different}){
        scenario=test;normalReads=entireReads=checks=compares=calls=0;interrupted=false;cancellation=std::stop_source{};teardown.clear();if(test==Case::CancelBefore)cancellation.request_stop();
        const auto r=mc::createVbiosRomDump(cancellation.get_token(),nullptr,"0000:01:00.0",root/std::to_string(static_cast<int>(test)));
        require(fs::exists(r.directory/"manifest.json")&&fs::exists(r.directory/"events.jsonl"),"missing forensic report");
        if(test==Case::Both||test==Case::ResolveDomain||test==Case::HybridIntelPrimary)require(r.saved&&r.vbiosSaved&&r.eepromSaved&&r.capacityVerified&&r.eepromBytes==2097152&&normalReads==2&&entireReads==2&&compares==1,"both capture path");
        else if(test==Case::AmbiguousDomain||test==Case::CancelBefore||test==Case::HybridMissingNvml)require(!r.saved&&!r.vbiosSaved&&!r.eepromSaved&&calls==0,"ambiguous/cancelled target must not launch a reader");
        else if(test==Case::ListMismatch)require(!r.saved&&calls==1&&checks==0&&normalReads==0&&entireReads==0,"NVFlash list mismatch must stop before reads");
        else if(test==Case::ChangedOs||test==Case::CleanupFailure||test==Case::CancelAfterFirst){
            require(!r.saved&&!r.vbiosSaved&&!r.eepromSaved&&normalReads==1&&entireReads==0,"identity/cleanup/cancellation must stop further reads");
            require(fs::exists(r.directory/"pass1.rom"),"retain first raw read on failure");
            if(test==Case::CancelAfterFirst)require(r.cancelled,"cancellation recorded");
        }
        else if(test==Case::DifferentVbios)require(!r.saved&&!r.vbiosSaved&&r.eepromSaved&&entireReads==2&&compares==1,"full success must survive ordinary mismatch");
        else if(test==Case::IfrV3Invalid){
            require(!r.saved&&!r.vbiosSaved&&!r.eepromSaved&&r.outcome()=="failed"&&normalReads==2&&entireReads==0,
                "invalid IFR v3 must not produce canonical VBIOS or EEPROM");
            require(r.message.find("invalid IFR ROM directory")!=std::string::npos,"IFR structure failure reported");
            require(fs::exists(r.directory/"pass1.rom")&&!fs::exists(r.directory/"vbios.rom"),"invalid image retained only as raw");
        }
        else if(test==Case::IfrV3Different){
            require(!r.saved&&!r.vbiosSaved&&!r.eepromSaved&&r.outcome()=="failed"&&normalReads==2&&entireReads==0,
                "different IFR v3 reads must not produce a canonical image");
            require(read(r.directory/"pass1.rom")!=read(r.directory/"pass2.rom")&&!fs::exists(r.directory/"vbios.rom"),
                "both different IFR v3 raw reads are retained without confirmation");
        }
        else if(test==Case::PendingReader||test==Case::PendingCancelled){
            require(!r.saved&&!r.vbiosSaved&&!r.eepromSaved&&normalReads==1&&entireReads==0,"pending reader must stop all subsequent commands");
            if(test==Case::PendingCancelled)require(r.cancelled,"cancellation after fake reader start is recorded");
            require(pendingReaderLease&&teardown.empty(),"pending reader retains provider and native lease after collector returns");
            require(!mc::telemetryGate().tryObservation()&&mc::deviceOperations().busy(),"pending fake child retains telemetry pause and device reservation");
            pendingReaderLease.reset();
            require(teardown==std::vector<std::string>({"provider_destroyed","native_lease_released"}),
                "pending reader releases provider before native lease");
            require(bool(mc::telemetryGate().tryObservation())&&!mc::deviceOperations().busy(),"fake child exit releases telemetry pause and device reservation");
        }
        else {
            require(!r.saved&&r.vbiosSaved&&!r.eepromSaved&&normalReads==2,"ordinary success must survive full refusal");
            if(test==Case::Unknown||test==Case::Large||test==Case::IfrV3Unknown)require(entireReads==0,"unknown/oversize must never read entire");
            if(test==Case::Truncated||test==Case::ChipChanged)require(entireReads==1&&compares==0,"invalid first read/capacity change must stop pair");
            if(test==Case::DifferentEntire)require(entireReads==2&&compares==0,"mismatched full hashes must skip compare");
            if(test==Case::SilentCompare)require(entireReads==2&&compares==1,"silent exit0 compare must refuse full");
        }
        if(r.vbiosSaved)require(fs::exists(r.directory/"vbios.rom"),"missing ordinary canonical");
        if(r.eepromSaved)require(fs::file_size(r.directory/"eeprom-full.rom")==2097152,"missing exact-capacity full canonical");
        require(bool(mc::telemetryGate().tryObservation())&&!mc::deviceOperations().busy(),"completed or failed collector leaves no firmware pause or reservation");
        if(test==Case::Unknown||test==Case::IfrV3Unknown)
            require(teardown==std::vector<std::string>({"provider_destroyed","native_lease_released"}),
                "partial capture releases provider before native lease");
        if(test==Case::IfrV3Unknown){
            require(r.outcome()=="completed_partial"&&!r.capacityVerified&&checks==1,
                "IFR v3: valid VBIOS with unknown EEPROM is partial");
            const auto raw1=read(r.directory/"pass1.rom"),raw2=read(r.directory/"pass2.rom"),canonical=read(r.directory/"vbios.rom");
            require(raw1==raw2&&raw1==canonical&&raw1.size()==8704,"both synthetic IFR reads and canonical bytes agree");
            const auto shape=mc::diagnostics::vbios::validate(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(canonical.data()),canonical.size()),0x28a0);
            require(shape.ifrVersion==3&&shape.prefixBytes==8192&&shape.images.size()==1,"IFR v3 wrapper and PCI image retained");
            const auto manifest=mc::json::JsonReader::parse(read(r.directory/"manifest.json"));
            require(manifest.at("state").text()=="partial"&&manifest.at("vbios_saved").flag()&&!manifest.at("eeprom_saved").flag(),"manifest keeps partial VBIOS result");
            require(manifest.at("vbios").at("shape").at("ifr_version").number()==3&&manifest.at("vbios").at("two_passes_equal").flag(),"manifest records IFR v3 and both equal reads");
            require(manifest.at("eeprom").at("state").text()=="refused"&&!fs::exists(r.directory/"eeprom-full.rom"),"unknown capacity refuses full EEPROM");
            require(read(r.directory/"events.jsonl").find("\"outcome\":\"completed_partial\"")!=std::string::npos,"partial terminal event recorded");
            // Repeat against the same target and output root; no stale result or
            // reader lease may make a later request overwrite the prior dump.
            normalReads=entireReads=checks=compares=calls=0;interrupted=false;cancellation=std::stop_source{};
            const auto again=mc::createVbiosRomDump(cancellation.get_token(),nullptr,"0000:01:00.0",root/std::to_string(static_cast<int>(test)));
            require(again.directory!=r.directory&&again.outcome()=="completed_partial"&&again.vbiosSaved&&!again.eepromSaved,
                "repeated IFR v3 operation completes separately with its own result");
            require(normalReads==2&&checks==1&&entireReads==0&&read(again.directory/"vbios.rom")==canonical,
                "repeated IFR v3 operation reads twice without full EEPROM");
        }
        std::cout<<"PASS synthetic scenario "<<static_cast<int>(test)<<"; saved="<<r.saved<<"; normal_reads="<<normalReads<<"; entire_reads="<<entireReads<<"\n";
    }
    // Exercise the same asynchronous INFO owner and resource route used by the
    // application, with only the platform transport supplied by the fixture.
    scenario=Case::IfrV3Unknown;normalReads=entireReads=checks=compares=calls=0;interrupted=false;teardown.clear();
    mc::InfoOperationServices infoServices;
    infoServices.dump=[](const mc::InfoDumpResourceRequest& request){
        mc::InfoDumpResourceServices services;
        services.vbios=[](const auto& input){return mc::createVbiosRomDump(input.cancellation,input.progress,input.pci,input.outputRoot,input.firmwarePause);};
        return mc::executeInfoDumpResource(request,&services);
    };
    fs::path firstDirectory;
    for(unsigned attempt=0;attempt<2;++attempt){
        mc::InfoOperationRequest request;request.operation=mc::InfoResourceOperation::VbiosDump;request.auditId=42;
        request.dump.pci="0000:01:00.0";request.dump.outputRoot=root/"info-lifecycle";
        const auto resultsBefore=fixtureInfoResults(),finishesBefore=fixtureInfoFinishes();
        auto job=mc::InfoOperation::start(std::move(request),infoServices);
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
        while(!job->done()&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(5));
        require(job->done(),"INFO VBIOS operation finished without a hardware reader");
        const auto* result=job->poll();
        require(result&&result->vbios.outcome()=="completed_partial"&&result->vbios.vbiosSaved&&!result->vbios.eepromSaved,
            "INFO route preserves independently verified VBIOS and partial result");
        require(!result->success&&job->messages()->done(),"INFO transcript reaches terminal state for partial dump");
        std::size_t cursor{};const auto transcript=job->messages()->read(cursor);
        const auto success=std::find_if(transcript.begin(),transcript.end(),[](const auto& row){
            return row.text=="VBIOS: два чтения совпали, дамп проверен."&&row.tone==mc::OperationMessages::Tone::Success;});
        const auto refused=std::find_if(transcript.begin(),transcript.end(),[](const auto& row){
            return row.text.starts_with("EEPROM: ")&&row.tone==mc::OperationMessages::Tone::Error;});
        require(success!=transcript.end()&&refused!=transcript.end(),"INFO transcript reports saved VBIOS and refused EEPROM separately");
        require(fixtureInfoResults()==resultsBefore+1,"INFO publishes its terminal result exactly once");
        require(read(result->vbios.directory/"vbios.rom")==read(result->vbios.directory/"pass1.rom"),"INFO retains canonical synthetic image");
        if(!attempt)firstDirectory=result->vbios.directory;
        else require(result->vbios.directory!=firstDirectory,"repeated INFO request has a separate output directory");
        while(fixtureInfoFinishes()==finishesBefore&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(5));
        require(fixtureInfoFinishes()==finishesBefore+1,"INFO worker finishes after terminal publication");
    }
    std::cout<<"PASS 22 real-collector synthetic scenarios plus repeats and 2 INFO lifecycles; "<<assertions<<" checks; no native platform/vendor/hardware execution.\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
