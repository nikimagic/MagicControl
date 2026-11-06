#pragma once
#include "ResourceControl.h"
#include "InfoDumpResource.h"
#include "InfoInventoryResource.h"

namespace mc {
class OperationMessages;
enum class InfoResourceOperation { Inventory, EcDump, BiosDump, VbiosDump, PawnIoPrepare, PowerInputDump, LenovoEnergyPrepare };
// Resource routing only; each collector or firmware reader independently
// verifies platform, driver, access, cancellation and readback.
inline ResourceControlKey infoResourceControlKey(InfoResourceOperation action){
    switch(action){
    case InfoResourceOperation::Inventory:return {ResourceKind::Other,"machine","inventory.read"};
    case InfoResourceOperation::EcDump:return {ResourceKind::Other,"ec","diagnostics.read"};
    case InfoResourceOperation::BiosDump:return {ResourceKind::Other,"firmware","bios.read"};
    case InfoResourceOperation::VbiosDump:return {ResourceKind::Graphics,"selected-adapter","vbios.read"};
    case InfoResourceOperation::PawnIoPrepare:return {ResourceKind::Other,"driver","installer.prepare"};
    case InfoResourceOperation::LenovoEnergyPrepare:return {ResourceKind::Other,"lenovo-energy","installer.prepare"};
    case InfoResourceOperation::PowerInputDump:return {ResourceKind::Other,"ec","power-input.read"};
    }
    throw std::invalid_argument("INFO: unknown resource operation");
}

// One INFO lifecycle for both frontends. UI supplies only explicit choices and
// OS-owned capabilities; collector admission remains in executeInfoDumpResource.
struct InfoOperationRequest {
    InfoResourceOperation operation{InfoResourceOperation::Inventory};
    InfoDumpResourceRequest dump;
    std::shared_ptr<PowerInputDumpSession> powerSession;
    std::uint64_t auditId{};
};
struct InfoOperationResult {
    HardwareInventory inventory;
    DiagnosticDumpResult dump;
    VbiosRomDumpResult vbios;
    std::string message;
    bool success{},cancelled{};
};
// Installer launch stays on the UI apartment; preparation and verification do
// not. The OS adapter reports actual completion, not merely process creation.
struct InfoOperationServices {
    using Completion=std::function<void(bool,std::string)>;
    // Optional already-owned collectors/fixtures, as in the resource APIs.
    // Empty callbacks use the existing actual-machine production collectors.
    std::function<HardwareInventory(std::stop_token)> collect;
    std::function<bool(const HardwareInventory&,std::string&)> save;
    std::function<InfoDumpResourceResult(const InfoDumpResourceRequest&)> dump;
    std::function<std::filesystem::path(std::stop_token)> prepare;
    std::function<void(const std::filesystem::path&,const std::shared_ptr<OperationMessages>&,Completion)> launch;
};
class InfoOperation:public std::enable_shared_from_this<InfoOperation> {
public:
    static std::shared_ptr<InfoOperation> start(InfoOperationRequest,InfoOperationServices={});
    InfoResourceOperation operation()const noexcept{return request_.operation;}
    bool inventory()const noexcept{return operation()==InfoResourceOperation::Inventory;}
    bool firmwareDump()const noexcept{return operation()==InfoResourceOperation::BiosDump||operation()==InfoResourceOperation::VbiosDump;}
    bool dumping()const noexcept{return !inventory()&&operation()!=InfoResourceOperation::PawnIoPrepare&&operation()!=InfoResourceOperation::LenovoEnergyPrepare;}
    void cancel();
    // UI-thread pump: launches a prepared interactive wizard at most once.
    // Returned data is immutable after the acquire; no GUI owns worker results.
    const InfoOperationResult* poll();
    bool done()const noexcept;
    bool installing()const noexcept;
    void releaseHardware();
    std::uint64_t progress()const noexcept{return progress_.load(std::memory_order_relaxed);}
    const std::shared_ptr<OperationMessages>& messages()const noexcept{return messages_;}
    DiagnosticPromptChannel prompts;
private:
    enum class Phase {Running,AwaitingUi,Installing,Finished};
    InfoOperation(InfoOperationRequest,InfoOperationServices);
    void run();
    void finish();
    void fail(std::string);
    bool complete(bool success,std::string message);
    void publish(); // Caller holds resultGate_; the sole terminal publication.
    InfoOperationRequest request_;
    InfoOperationServices services_;
    InfoOperationResult result_;
    std::filesystem::path installer_;
    std::shared_ptr<OperationMessages> messages_;
    std::stop_source cancellation_;
    std::mutex resultGate_;
    bool workerStarted_{}; // Guarded by resultGate_; pending cancellation may release the ticket.
    std::atomic<Phase> phase_{Phase::Running};
    std::atomic<std::uint64_t> progress_{};
};
}
