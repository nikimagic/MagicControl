#include "../src/platform/windows/SettingsOperationsWindows.h"
#include "../src/platform/windows/SettingsSnapshotWindows.h"
#include "MagicControl/ActionLog.h"
#include "MagicControl/Logging.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
std::size_t findAfter(const std::string& text,std::string_view needle,std::size_t from){
    const auto position=text.find(needle,from);check(position!=std::string::npos,"missing or out-of-order audit event");return position;
}
}
int main(){
    using namespace mc::gui;
    const auto folder=std::filesystem::path(MAGICCONTROL_TEST_ROOT)/
        ("settings-audit-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    check(std::filesystem::create_directory(folder),"cannot create isolated log directory");
    struct Cleanup {std::filesystem::path folder;~Cleanup(){
        mc::initializeLogging(false,folder);std::error_code e;
        for(const auto* name:{"MagicControl-1.jsonl","battery-1.jsonl"})std::filesystem::remove(folder/name,e);
        std::filesystem::remove(folder,e);
    }} cleanup{folder};
    check(mc::initializeLogging(true,folder),"cannot initialize isolated log");
    SettingsSnapshot before,after;bool success=false;std::wstring published;
    SettingsWorkerAuditContext context{before,after,100,0,true,success,
        [](const std::wstring& value){return std::string(value.begin(),value.end());},
        [&](std::wstring value){published=std::move(value);mc::audit::event("test_published");}};
    executeAuditedSettingsWorker(context,[&](const SettingsFailureMarkers&){after.valid=true;success=true;return std::wstring(L"ok");});
    check(published==L"ok","successful result not published");
    context.command=101;success=false;
    executeAuditedSettingsWorker(context,[&](const SettingsFailureMarkers& markers){markers.history("synthetic");markers.audit("synthetic");return std::wstring(L"failed");});
    check(published==L"failed","failure result not published");
    context.command=102;context.saveChoice=false;after.valid=false;
    executeAuditedSettingsWorker(context,[&](const SettingsFailureMarkers&){return std::wstring(L"poll");});
    check(published==L"poll","passive result not published");
    const auto path=mc::currentLogPath();mc::initializeLogging(false,folder);
    std::ifstream input(path,std::ios::binary);const std::string text{std::istreambuf_iterator<char>(input),{}};
    const auto requested=findAfter(text,"\"control_id\":100",0);
    const auto observed=findAfter(text,"\"source\":\"windows.settings\"",requested);
    const auto confirmed=findAfter(text,"\"outcome\":\"completed_with_observation\"",observed);
    const auto publishedOk=findAfter(text,"\"event\":\"test_published\"",confirmed);
    const auto requestedFail=findAfter(text,"\"control_id\":101",publishedOk);
    const auto failed=findAfter(text,"\"outcome\":\"failed\"",requestedFail);
    const auto observedFail=findAfter(text,"\"source\":\"windows.settings\"",failed);
    const auto publishedFail=findAfter(text,"\"event\":\"test_published\"",observedFail);
    const auto passive=findAfter(text,"\"event\":\"test_published\"",publishedFail+1);
    check(text.substr(publishedFail,passive-publishedFail).find("\"control_id\":102")==std::string::npos,
        "passive poll emitted interactive command");
}
