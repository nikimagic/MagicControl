#pragma once
#include "DiagnosticDump.h"
#include <functional>
#include <string>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <utility>
namespace mc {
// Worker/UI rendezvous, independent of Win32/GTK and never holding a device gate.
struct DiagnosticPromptRequest {std::string instructions,reason;};
class DiagnosticPromptChannel {
    std::mutex mutex_;std::condition_variable_any wake_;
    std::optional<DiagnosticPromptRequest> pending_;std::optional<bool> answer_;bool shown_{};
public:
    bool ask(std::string instructions,std::string reason,std::stop_token stop){
        std::unique_lock lock(mutex_);if(stop.stop_requested())return false;
        pending_=DiagnosticPromptRequest{std::move(instructions),std::move(reason)};answer_.reset();shown_=false;
        wake_.wait(lock,stop,[&]{return answer_.has_value();});
        const bool result=!stop.stop_requested()&&answer_.value_or(false);pending_.reset();return result;
    }
    std::optional<DiagnosticPromptRequest> take(){std::lock_guard lock(mutex_);if(!pending_||shown_)return {};shown_=true;return pending_;}
    void reply(bool accepted){std::lock_guard lock(mutex_);answer_=accepted;wake_.notify_all();}
};
// UI retains this session between attempts in this process. All completed and
// rejected attempts are also saved on disk; no automatic cross-machine replay.
struct PowerInputDumpSession {
    std::filesystem::path directory;
    std::string profile,identity,displaySnapshot;
    unsigned nextStage{},attempt{};
    bool allEcComplete{true};
    std::string attempts;
};
// Actual SMBIOS only, independent of a selected/preview profile. No hardware IO.
bool powerInputDumpSupported();
// Called on worker: prompt bridges to the UI; false pauses with partial data.
using PowerInputPrompt=std::function<bool(const std::string& instructions,const std::string& reason)>;
// Three read-only60s snapshots: disconnected, connected/actually charging,
// connected/full/near-zero rate. No Default/Max/FAN/power/threshold writes and no load drain.
DiagnosticDumpResult createPowerInputDump(PowerInputDumpSession&,const PowerInputPrompt&,
    std::stop_token cancellation={},std::shared_ptr<EcAccess> access={},std::string displaySnapshot={});
}
