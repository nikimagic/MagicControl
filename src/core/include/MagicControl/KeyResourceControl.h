#pragma once
#include "KeysLifecycle.h"
#include "ResourceControl.h"
#include "KeysBackendPolicy.h"

namespace mc {
enum class KeyCommandPlan { UnsupportedRoute, Unavailable, Busy, LocalPreference, Queue, Start };
inline KeyCommandPlan planKeyCommand(std::string_view profile,int index,bool preview,bool available,
        bool busy,bool refreshing,bool queued,bool linuxHost=false,bool present=true) noexcept{
    if(index<0||index>=static_cast<int>(kKeyCount))return KeyCommandPlan::Unavailable;
    const auto action=static_cast<KeyAction>(index);
    if(keysBackend(profile,action,linuxHost,preview,present)==KeysBackend::Unavailable)return KeyCommandPlan::UnsupportedRoute;
    if(!available)return KeyCommandPlan::Unavailable;
    if(busy&&(!refreshing||queued))return KeyCommandPlan::Busy;
    if(action==KeyAction::Osd)return KeyCommandPlan::LocalPreference;
    return busy?KeyCommandPlan::Queue:KeyCommandPlan::Start;
}
struct KeySnapshot {
    std::array<int,kKeyCount> value;
    std::array<bool,kKeyCount> available{},retainForDisplay{};
    std::array<std::string,kKeyCount> detail{};
    std::string status;
    KeySnapshot(){value.fill(-1);}
};
struct KeyCommand {KeyAction action;bool enabled{};std::uint64_t noticeToken{};};
struct KeyBackendResult {
    KeySnapshot snapshot;
    bool success{};
    std::string error,noticeDetail;
};
struct KeyOperationResult {
    KeySnapshot snapshot;
    std::optional<KeyCommand> command;
    bool success{},announceOsdEnabled{};
    std::string message,noticeDetail;
};
struct KeySubmission {
    bool accepted{};
    std::string message;
    std::optional<KeyOperationResult> completed;
};
// Only typed OS adapters and UI-affine bindings. No callback is permission to
// invent a hardware route. Local preference/radio preparation run on submit/take
// (the UI thread); read/execute run on the owned worker and never touch widgets.
struct KeyOperationServices {
    std::function<KeySnapshot()> read;
    std::function<KeyBackendResult(const KeyCommand&,KeySnapshot,const std::function<void()>&)> execute;
    std::function<KeyBackendResult(bool)> localPreference;
    std::function<bool()> localState;
    std::function<void()> reloadLocal;
    std::function<std::function<void()>(KeyAction)> prepare;
    std::function<std::function<void()>()> prepareRestore;
    KeysLifecycle::Reader lifecycleRead;
    KeysLifecycle::Setter lifecycleSet;
    KeysLifecycle::Observer lifecycleObserve;
    std::function<std::uint64_t(KeyAction,bool)> beginNotice;
    std::function<void(std::string_view,int,std::string_view)> record;
};
// One explicit-command queue/worker/result path for Win32 and GTK. Lifecycle
// restoration retains its existing policy/worker and uses the same OS setters.
// All methods except destruction/worker callbacks are called on the UI thread.
class KeysOperations {
public:
    // presentationProfile selects rows only during preview; all hardware
    // admission and lifecycle restoration continue to use the actual profile.
    KeysOperations(std::string profile,bool preview,bool linuxHost,KeyOperationServices,std::function<void()> ready,
                   std::string presentationProfile={});
    ~KeysOperations();
    KeySubmission submit(int row,bool on);
    bool refresh();
    std::optional<KeyOperationResult> take();
    void observe(int row,bool on);
    void restore(std::string reason);
    void suspend();
    void powerSleep(bool sleeping);
    bool disabled(int row)const noexcept;
    bool visible(int row)const noexcept;
    const std::array<bool,kKeyCount>& visibleRows()const noexcept;
    const KeySnapshot& snapshot()const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
// One completion contract for explicit GUI/CLI commands on both operating systems.
// The observation must be fresh; presentation caches cannot confirm a write.
inline bool completeKeyResource(KeyAction action,bool requested,const KeyObservation& actual,std::string& error){
    if(!actual.supported||!actual.available||!actual.enabled||*actual.enabled!=requested){
        error=actual.detail.empty()?"KEYs: итоговое состояние не подтверждено":actual.detail;
        return false;
    }
    if(storedKeyPreference(action)&&!rememberKeyPreference(action,requested,error)){
        error="Настройка применена, но не сохранена: "+error;return false;
    }
    return true;
}
// Logical resource labels only. Exact device support, OS access and firmware
// admission remain with the typed key backend; labels never grant permission.
inline ResourceControlKey keyResourceControlKey(KeyAction action){
    using R=ResourceKind;
    switch(action){
    case KeyAction::Windows:return {R::Input,"keyboard","windows-lock"};
    case KeyAction::Osd:return {R::Other,"application","osd"};
    case KeyAction::Fn:return {R::Input,"keyboard","fn-lock"};
    case KeyAction::Usb:return {R::Other,"usb","power"};
    case KeyAction::Ac:return {R::Other,"power","ac-start"};
    case KeyAction::Copilot:return {R::Input,"keyboard","copilot-lock"};
    case KeyAction::Wifi:return {R::Connectivity,"wifi","enabled"};
    case KeyAction::Bluetooth:return {R::Connectivity,"bluetooth","enabled"};
    case KeyAction::Camera:return {R::Camera,"camera","enabled"};
    case KeyAction::Touchpad:return {R::Input,"touchpad","enabled"};
    case KeyAction::Microphone:return {R::Audio,"microphone","enabled"};
    }
    throw std::invalid_argument("KEYs: unknown resource operation");
}
}
