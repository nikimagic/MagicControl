#include "InstanceHandoff.h"
#include "../../platform/windows/InstanceNamesWindows.h"
#include "../../platform/windows/TextWindows.h"
#include "MagicControl/AppUpdate.h"
#include "MagicControl/PortableSettings.h"
#include "MagicControl/Version.h"
#include "MagicControl/BoardIdentity.h"
#include "../../platform/windows/BatteryEnforcementWindows.h"
#include "../../platform/windows/FileVersionWindows.h"
#include <winver.h>
#include <format>
#include <string_view>
#include <stdexcept>
#include <vector>

namespace mc::gui {
namespace {
struct Handle{HANDLE h{};~Handle(){if(h)CloseHandle(h);}};
bool acquired(DWORD result){return result==WAIT_OBJECT_0||result==WAIT_ABANDONED;}
std::wstring processPath(HANDLE process){
    std::wstring path(32768,L'\0');DWORD size=static_cast<DWORD>(path.size());
    if(!QueryFullProcessImageNameW(process,0,path.data(),&size))throw std::runtime_error("Cannot read running executable path");
    path.resize(size);return path;
}
std::string fileVersion(const std::wstring& path){
    const auto info=windows::readFileVersionResource(path);
    if(info.productName!=L"MagicControl"||info.fileType!=VFT_APP||info.numbers[3]!=0)
        throw std::runtime_error("Invalid running MagicControl version resource");
    return std::format("{}.{}.{}",info.numbers[0],info.numbers[1],info.numbers[2]);
}
std::string jsonQuoted(const std::string& value){
    std::string out="\"";for(unsigned char c:value){switch(c){case '\\':out+="\\\\";break;case '"':out+="\\\"";break;
        case '\n':out+="\\n";break;case '\r':out+="\\r";break;case '\t':out+="\\t";break;
        default:if(c<32)out+=std::format("\\u{:04x}",c);else out+=static_cast<char>(c);}}
    return out+'"';
}
}
InstanceLease::~InstanceLease(){
    releaseLaunchGate();
    if(instanceOwned_)ReleaseMutex(instance_);
    if(instance_)CloseHandle(instance_);
}
void InstanceLease::releaseLaunchGate(){
    if(launchOwned_){ReleaseMutex(launch_);launchOwned_=false;}
    if(launch_){CloseHandle(launch_);launch_=nullptr;}
}
void InstanceLease::migrateSettings(){
    if(!instanceOwned_)throw std::runtime_error("Настройки требуют исключительного запуска");
    const auto id=std::string(deviceProfile(readBoardDescriptor()).id);
    const auto incoming=std::filesystem::path(incomingPath_).parent_path();
    const auto previous=previousPath_.empty()?incoming:std::filesystem::path(previousPath_).parent_path();
    std::vector<std::filesystem::path> sources;
    if(previous!=incoming)sources.push_back(previous/"settings");
    // Compatibility with the briefly used 0.1.161 GM6 store. Never choose it
    // for another board, and never move the separately verified ownership receipt.
    bool oldShared{};
    if(id=="mechrevo-gm6ir0c")try{
        const auto legacy=windows::batteryEnforcement::stateDirectory()/"Settings";
        oldShared=std::filesystem::is_directory(legacy);if(oldShared)sources.push_back(legacy);
        (void)windows::batteryEnforcement::ownerPath(previous,id);
        (void)windows::batteryEnforcement::ownerPath(incoming,id);
    }catch(const std::exception& e){OutputDebugStringA(e.what());}
    if(previous!=incoming)sources.push_back(previous);
    migratePortableSettings(sources,previous!=incoming||oldShared);
}
void InstanceLease::record(const char* outcome){
    if(!logging_)return;
    try{
    SYSTEMTIME time{};GetSystemTime(&time);
    const auto utc=std::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}Z",time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond);
    const auto body=std::format("{{\"schema\":1,\"utc\":{},\"outcome\":{},\"incoming_version\":{},\"incoming_path\":{},\"previous_version\":{},\"previous_path\":{},\"previous_pid\":{},\"error\":{},\"summary\":{}}}\n",
        jsonQuoted(utc),jsonQuoted(outcome),jsonQuoted(MAGICCONTROL_VERSION_STRING),jsonQuoted(mc::windows::encoding::utf8(incomingPath_)),
        jsonQuoted(previousVersion_),jsonQuoted(mc::windows::encoding::utf8(previousPath_)),previousPid_,jsonQuoted(mc::windows::encoding::utf8(error)),jsonQuoted(summary));
    std::string ignored;savePortableText(portableSettingsPath("launch-handoff.json"),body,ignored);
    }catch(...){OutputDebugStringW(L"MagicControl: launch handoff diagnostic could not be saved.\n");}
}
InstanceLease::Result InstanceLease::fail(std::wstring text){
    error=std::move(text);record("failed");releaseLaunchGate();return Result::Failed;
}
InstanceLease::Result InstanceLease::acquire(bool logging){
    logging_=logging;
    try{
        incomingPath_=processPath(GetCurrentProcess());
        launch_=CreateMutexW(nullptr,FALSE,L"Global\\MagicControl.LaunchHandoff.v1");
        if(!launch_||!acquired(WaitForSingleObject(launch_,30000)))return fail(L"Другой запуск MagicControl ещё не завершён. Повторите запуск позже.");
        launchOwned_=true;
        instance_=CreateMutexW(nullptr,FALSE,windows::applicationInstanceMutex);
        if(!instance_)return fail(L"Не удалось проверить запущенную копию MagicControl.");
        HWND running{};const auto startupDeadline=GetTickCount64()+10000;
        for(;;){
            const auto state=WaitForSingleObject(instance_,0);
            if(acquired(state)){instanceOwned_=true;summary="No active instance; startup lease acquired";record("start");return Result::Start;}
            if(state==WAIT_FAILED)return fail(L"Не удалось проверить запущенную копию MagicControl.");
            running=FindWindowW(L"MagicControlMainWindow",nullptr);
            if(running)break;
            if(GetTickCount64()>=startupDeadline)return fail(L"MagicControl занят в другом сеансе или ещё запускается. Новая копия не запущена.");
            Sleep(50);
        }
        GetWindowThreadProcessId(running,&previousPid_);
        Handle previous{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,previousPid_)};
        if(!previous.h)return fail(L"Не удалось проверить версию работающей копии MagicControl.");
        previousPath_=processPath(previous.h);previousVersion_=fileVersion(previousPath_);
        const auto order=update::compareVersions(MAGICCONTROL_VERSION_STRING,previousVersion_);
        if(!order)return fail(L"Не удалось сравнить версии MagicControl. Новая копия не запущена.");
        summary=std::format("Launch {} [{}]; existing {} [{}] PID={}",MAGICCONTROL_VERSION_STRING,mc::windows::encoding::utf8(incomingPath_),previousVersion_,mc::windows::encoding::utf8(previousPath_),previousPid_);
        if(*order<=0){
            AllowSetForegroundWindow(previousPid_);PostMessageW(running,WM_APP+55,0,0);
            record("activated_existing_same_or_newer");releaseLaunchGate();return Result::ActivatedExisting;
        }
        DWORD owner{};GetWindowThreadProcessId(running,&owner);
        if(owner!=previousPid_||WaitForSingleObject(previous.h,0)!=WAIT_TIMEOUT)
            return fail(L"Работающая копия MagicControl изменилась. Повторите запуск.");
        const auto protocol=gracefulInstanceExitMessage();DWORD_PTR response{};
        if(protocol&&SendMessageTimeoutW(running,protocol,0,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,1000,&response)&&response==kInstanceExitProtocol){
            summary+="; exit_request=registered_graceful_protocol";
            record("request_graceful_exit");
            if(!PostMessageW(running,protocol,1,0))return fail(L"Не удалось запросить штатный выход старой копии MagicControl.");
        }else{
            // Releases0.1.62–0.1.148 already handle these two messages with
            // the same DestroyWindow/onDestroy cleanup used by tray Exit.
            // Only this known legacy range is admitted; never broadcast and
            // never substitute WM_CLOSE (which merely hides to tray).
            const auto min=update::compareVersions(previousVersion_,"0.1.62");
            const auto max=update::compareVersions(previousVersion_,"0.1.149");DWORD_PTR consent{};
            if(!min||!max||*min<0||*max>=0||!SendMessageTimeoutW(running,WM_QUERYENDSESSION,0,ENDSESSION_CLOSEAPP,
                    SMTO_ABORTIFHUNG|SMTO_BLOCK,2000,&consent)||!consent)
                return fail(L"Эта копия MagicControl не подтвердила штатный выход. Закройте её через трей и повторите запуск.");
            summary+="; exit_request=legacy_window_cleanup";
            record("request_legacy_graceful_exit");
            if(!PostMessageW(running,WM_ENDSESSION,TRUE,ENDSESSION_CLOSEAPP))return fail(L"Не удалось запросить штатный выход старой копии MagicControl.");
        }
        if(WaitForSingleObject(previous.h,60000)!=WAIT_OBJECT_0)
            return fail(L"Старая копия MagicControl не завершилась за 60 секунд. Новая копия не запущена. Закройте старую через трей и повторите запуск.");
        if(!acquired(WaitForSingleObject(instance_,10000)))return fail(L"Не удалось получить доступ после выхода старой копии MagicControl.");
        DWORD exitCode{};if(GetExitCodeProcess(previous.h,&exitCode))summary+=std::format("; previous_exit_code={}",exitCode);
        instanceOwned_=true;summary+="; previous process exit confirmed; exclusive startup lease acquired";
        record("handoff_complete");return Result::Start;
    }catch(const std::exception& exception){
        summary=exception.what();return fail(L"Не удалось проверить работающую копию MagicControl. Закройте её через трей и повторите запуск.");
    }
}
}
