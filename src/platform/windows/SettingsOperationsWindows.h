#pragma once

#include "MagicControl/DeviceOperation.h"
#include "MagicControl/ResourceControl.h"
#include "MagicControl/SettingsResourceControl.h"
#include "SettingsActionWindows.h"
#include "SettingsSnapshotWindows.h"
#include "SettingsPowerSelectionWindows.h"
#include "IntelTelemetryWindows.h"
#include "IteLightingWindows.h"
#include "ApuPowerPolicyWindows.h"
#include "MagicControl/BoardIdentity.h"
#include "MagicControl/BatteryResourceState.h"
#include "MagicControl/KeyboardPulsePolicy.h"
#include "MagicControl/BatteryUiPolicy.h"
#include "MagicControl/SettingsPreview.h"

#include "MagicControl/OsdMessage.h"
#include <atomic>
#include <array>
#include <cstdint>
#include <functional>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace mc::gui {
struct SettingsSnapshot;
struct SettingsSnapshotContext;
struct SettingsSupplementalContext;
}
namespace mc { class MonitoringReader; }
namespace mc::windows { class NativeBatteryController; class IntelTelemetryReader; class ApuPowerReader; }
namespace mc::gui {
// Explicit command categories: the worker keeps deciding when to call these
// pure checks, so action/readback/persistence and UI scheduling stay ordered.
struct SettingsResultContext {
    bool saveChoice{},hasEc{},hasOem{},hasGm{},hasNativeBattery{},hasHid{},hasCycle{};
    bool batteryUpper{},nativeMode{},batteryPolicy{},cycleTrigger{},colorChoice{},lightingChoice{};
};
bool validateSettingsCycle(const SettingsResultContext&,const SettingsSnapshot& previous,const SettingsSnapshot& actual);
std::wstring classifySettingsResult(const SettingsResultContext&,const SettingsSnapshot& actual,bool cycleCommand,
    std::wstring initial,std::wstring (*toWide)(const std::string&));
// Synchronous worker stages. The caller owns every captured object and the
// worker lifetime; exceptions propagate to its existing recovery path.
struct SettingsWorkSteps {
    std::function<std::uint64_t()> clock;
    std::function<void()> action,observe,verify,cycleCheck,persist;
    std::function<void(std::uint64_t,std::uint64_t)> supplemental;
    bool saveChoice{};
};
void executeSettingsWorkSteps(const SettingsWorkSteps&);
// Executes the successful worker path synchronously. Callbacks and references
// remain owned by the caller until the already-managed worker has joined.
struct SettingsSuccessContext {
    SettingsWorkSteps steps;
    const SettingsSnapshot& previous;
    SettingsSnapshot& pending;
    std::function<SettingsResultContext()> classification;
    std::function<std::wstring()> initialMessage;
    bool& pendingSuccess;
    std::string& lastReadError;
    std::wstring (*toWide)(const std::string&);
};
std::wstring executeSettingsSuccess(SettingsSuccessContext&);
struct SettingsFailureContext {
    bool reading{},hasRecoveryBackend{};
    std::string& lastReadError;
    std::function<void(const char*)> failHistory,failAudit,recordFailure;
    std::function<void(std::string)> log;
    std::wstring (*toWide)(const std::string&);
    std::function<bool()> recover;
};
struct SettingsFailureResult {std::wstring message;bool clearActual{};};
SettingsFailureResult processSettingsFailure(const std::exception&,const SettingsFailureContext&);
// Context is built only if the operation fails: backend availability and the
// last error can change while the action executes.
std::wstring executeSettingsOperation(const std::function<std::wstring()>& work,
    const std::function<SettingsFailureContext()>& failureContext, bool& clearActual);
// Runs synchronously on the existing worker thread. The caller owns all
// referenced backends and callbacks until that thread joins.
struct SettingsWorkerContext {
    const SettingsSnapshot& previous;
    SettingsSnapshot& pending;
    std::function<SettingsSnapshotContext()> snapshot;
    const std::optional<std::array<int,3>>& expectedPower;
    bool quickBattery{},amdPower{},saveChoice{};
    std::function<std::string_view()> profile;
    std::function<void()> action;
    std::function<void(const SettingsSnapshot&)> persist;
    std::function<SettingsSupplementalContext(std::uint64_t,std::uint64_t)> supplemental;
    std::function<SettingsResultContext()> classification;
    std::function<std::wstring()> initialMessage;
    std::function<SettingsFailureContext()> failureContext;
    bool& pendingSuccess;
    bool& clearActual;
    std::string& lastReadError;
    std::wstring (*toWide)(const std::string&);
};
std::wstring executeSettingsWorker(SettingsWorkerContext&);
struct SettingsFailureMarkers {std::function<void(const char*)> history,audit;};
struct SettingsWorkerAuditContext {
    const SettingsSnapshot& previous;
    const SettingsSnapshot& pending;
    int command{};std::uint64_t auditId{};bool saveChoice{};
    const bool& pendingSuccess;
    std::function<std::string(const std::wstring&)> encode;
    std::function<void(std::wstring)> publish;
};
// Runs on the existing worker thread. Failure markers are valid only during
// execute and must not be retained. It does not grant admission or start work.
void executeAuditedSettingsWorker(const SettingsWorkerAuditContext&,
    const std::function<std::wstring(const SettingsFailureMarkers&)>& execute);
// Data supplied by the page to the synchronous worker. No HWND or GUI control
// is used by the coordinator; existing backend objects stay owned by the page.
struct SettingsExecutionFlags {
    bool saveChoice{},quickBattery{},quickProfile{},gm6NativePower{},intelCpuUi{},gm5Ui{};
    bool nativeMode{},batteryUpper{},batteryPolicy{},cycleTrigger{},colorChoice{},lightingChoice{};
    bool keyboardIdleOnly{};
};
struct SettingsExecutionContext {
    const SettingsSnapshot& previous;SettingsSnapshot& pending;
    int command{},batteryVoltageMode{-1};std::uint64_t auditId{};
    SettingsExecutionFlags flags;
    std::function<SettingsSnapshotContext()> snapshot;
    const std::optional<std::array<int,3>>& expectedPower;
    std::function<std::string_view()> profile;
    std::function<void()> action;
    std::function<void(const SettingsSnapshot&)> persist;
    windows::NativeBatteryController* nativeBattery{};
    windows::NativeBatteryController* voltageBattery{};
    MonitoringReader& batteryReader;
    windows::IntelTelemetryReader& intelMonitor;
    windows::ApuPowerReader& apuPowerReader;
    std::function<bool()> priorityPending;
    std::function<void(std::string)> log;
    std::string& lastApuLog;
    std::function<std::wstring()> initialMessage;
    const bool& reading;bool& pendingSuccess;bool& clearActual;
    std::string& lastReadError;
    std::wstring (*toWide)(const std::string&);
    std::function<std::string(const std::wstring&)> encode;
    std::function<void(std::wstring)> publish;
};
// Synchronous: caller must keep all references and callbacks alive until return.
// The existing worker, operation reservation and backend guards remain in force.
void executeSettingsExecution(SettingsExecutionContext&);
struct SettingsObservationRevision {
    std::uint64_t nativeAtStart{},nativeCurrent{},observationAtStart{},observationCurrent{};
    bool passive{},previousValid{};
    std::optional<int> observedKeyboard;
};
// Resolves only presentation freshness after the worker has joined. Never
// issues commands, resumes a device, or turns a stale result into admission.
bool resolveSettingsObservationRevision(SettingsSnapshot& pending,bool& clearActual,
    std::wstring& message,const SettingsObservationRevision&) noexcept;
struct SettingsCompletionResolution {
    bool superseded{},invalidatePendingLogger{},invalidateCurrentLogger{},markLoggerTouched{};
};
// Resolve a joined worker before any presentation is accepted. An exception
// may follow a device write, so it must revoke confirmation even if the
// worker had tentatively succeeded. This does not retry or touch hardware.
SettingsCompletionResolution resolveSettingsCompletion(SettingsSnapshot& pending,bool& clearActual,
    bool& success,std::wstring& message,const SettingsObservationRevision&,bool unhandledFailure,bool loggerTouched);
// A successful readback from an invalidated epoch is no longer confirmation
// of current state, even though the command itself might have been applied.
[[nodiscard]] constexpr bool settingsCompletionConfirmed(bool succeeded,bool superseded) noexcept {
    return succeeded&&!superseded;
}

struct SettingsRequest {
    std::optional<windows::SettingsAction> action;
    std::wstring message,success;osd::Message notice;std::uint64_t token{};
    std::shared_ptr<DeviceOperation> reservation;int command{};
};
using SettingsDispatch=ResourceDispatch<SettingsRequest>;
class SettingsOperationsWindows {
public:
    using State=SettingsSnapshot;
private:
    bool closing{};
    std::optional<mc::EcByteObservation> loggerObservation,pendingLoggerObservation;bool pendingLoggerTouched{};
    KeyboardCycleSettings cycleSettings;std::unique_ptr<KeyboardColorCycle> cycle;
    std::shared_ptr<windows::IteLightingController> iteLighting;LightingZone selectedLightingZone{LightingZone::Keyboard};
    BatteryCardView batteryCardView;bool cycleRestorePending{};
    std::uint64_t lastFullPoll{};
    windows::ApuPowerReader apuPowerReader;std::unique_ptr<windows::IntelTelemetryReader> intelMonitor;std::unique_ptr<MonitoringReader> batteryReader;
    std::unique_ptr<LaptopControl> oemControl;std::shared_ptr<windows::Gm6Controller> gm6Controller;
    std::shared_ptr<windows::NativeBatteryController> nativeBattery,voltageBattery;
    const BoardDescriptor pageBoard;bool preview{},gm5Ui{};const bool gm6NativePower{};
    SettingsPreviewState previewState_;
    bool intelUi{},intelCpuUi{},huaweiUi{};std::string lastApuLog;
    int selectedCustomPreset{};bool savedCustomIntent{};std::optional<int> pendingCustomPreset;
    int customBase{};std::optional<int> pendingCustomBase;
    std::optional<std::array<int,3>> pendingPresetValues;
    std::array<int,3> powerValues{25,25,30};
    PowerOptions powerOptionValues,powerOptionsBeforeEdit;
    bool powerEditorsInitialized{},powerEditing{};
    std::array<int,3> powerBeforeEdit{};std::optional<int> pendingPerformance,pendingBatteryView;int chosenPowerMode{-1};
    EcAccess* ec{};std::shared_ptr<windows::FanController> controller;std::shared_ptr<DeviceOperation> operation;
    std::function<void(std::string)> log;
    bool busy{},reading{},loaded{},idleObservation{};
    SettingsDispatch operations;std::wstring statusText;osd::Message requestedNotice,activeNotice;
    std::uint64_t operationToken{},revision{},readRevision{},nativeProfileRevision{},readNativeProfileRevision{};
    bool nativeProfileRefreshPending{},pendingSuccess{},pendingClearActual{};
    std::optional<std::array<int,3>> expectedGmPower;std::optional<std::wstring> operationSuccess;bool pendingPresetApplied{};
    int requestedCommand{};std::optional<int> observedKeyboard;State state{},pending{};
    mc::BatteryResourceState batteryResources;EcBatteryDisplayCache ecDisplayCache;LaptopDisplayCache oemDisplayCache;KeyboardDisplayCache keyboardDisplayCache;
    std::wstring pendingMessage;std::string lastReadError;

public:
    class View {
    public:
        virtual ~View()=default;
        virtual osd::Message notice(const windows::SettingsAction&)const=0;
    virtual void statusChanged(const std::wstring&)=0;
    virtual bool readPowerFields(std::array<int,3>&)=0;
    virtual bool readPowerOptions(PowerOptions&){return true;}
    virtual void renderSelection()=0;
    virtual void updateButtons()=0;
    virtual void loggerChanged()=0;
    virtual std::uint64_t beginNotice(const osd::Message&)=0;
    virtual void completeNotice(std::uint64_t,osd::Message)=0;
    };
    // UI-thread read bindings: no copied cache, backend handle or writable state.
    struct Presentation {
        const decltype(SettingsOperationsWindows::closing)& closing;
        const decltype(SettingsOperationsWindows::loggerObservation)& loggerObservation;
        const decltype(SettingsOperationsWindows::cycleSettings)& cycleSettings;
        const decltype(SettingsOperationsWindows::selectedLightingZone)& selectedLightingZone;
        const decltype(SettingsOperationsWindows::batteryCardView)& batteryCardView;
        const decltype(SettingsOperationsWindows::pageBoard)& pageBoard;
        const decltype(SettingsOperationsWindows::preview)& preview;
        const decltype(SettingsOperationsWindows::gm5Ui)& gm5Ui;
        const decltype(SettingsOperationsWindows::gm6NativePower)& gm6NativePower;
        const decltype(SettingsOperationsWindows::intelCpuUi)& intelCpuUi;
        const decltype(SettingsOperationsWindows::huaweiUi)& huaweiUi;
        const decltype(SettingsOperationsWindows::selectedCustomPreset)& selectedCustomPreset;
        const decltype(SettingsOperationsWindows::powerValues)& powerValues;
        const decltype(SettingsOperationsWindows::powerOptionValues)& powerOptionValues;
        const decltype(SettingsOperationsWindows::powerEditing)& powerEditing;
        const decltype(SettingsOperationsWindows::busy)& busy;
        const decltype(SettingsOperationsWindows::reading)& reading;
        const decltype(SettingsOperationsWindows::loaded)& loaded;
        const decltype(SettingsOperationsWindows::state)& state;
        const decltype(SettingsOperationsWindows::batteryResources)& batteryResources;
        const bool ec;
        const bool oemControl;
        const bool gm6Controller;
        const bool nativeBattery;
        const bool voltageBattery;
        const bool iteLighting;
        const bool controller;
        const bool cycle;
    };
    Presentation presentation()const{return {closing,loggerObservation,cycleSettings,selectedLightingZone,batteryCardView,pageBoard,preview,gm5Ui,gm6NativePower,intelCpuUi,huaweiUi,selectedCustomPreset,powerValues,powerOptionValues,powerEditing,busy,reading,loaded,state,batteryResources,static_cast<bool>(ec),static_cast<bool>(oemControl),static_cast<bool>(gm6Controller),static_cast<bool>(nativeBattery),static_cast<bool>(voltageBattery),static_cast<bool>(iteLighting),static_cast<bool>(controller),static_cast<bool>(cycle)};}
    bool hasQueuedCommand()const noexcept{return operations.hasQueued();}
    bool ecVerified()const;
    bool fanNeedsRestore()const;
    bool honorBatteryBackend()const;
    bool honorPowerBackend()const;
    bool nativePowerBackend()const;
    int customMinimum(int index=-1){return windows::customFieldMinimum(customContext(),index);}
    int customMaximum(int index){return windows::customFieldMaximum(customContext(),index);}
    void beginClosing()noexcept{closing=true;}
    void requestCycleRestore(){if(gm6NativePower&&!preview)cycleRestorePending=true;}
    void useCapacityViewForTray(){batteryCardView=BatteryCardView::Capacity;}
    void initializePowerDraft();
    int displayedPowerMode(bool preferEditor)const{return pendingPerformance.value_or(preferEditor&&powerEditing?customPowerIndex():selectedPowerMode());}
    int highlightedCustomSlot()const{return preview?previewState_.preset:pendingCustomPreset.value_or(selectedCustomPreset);}
    const SettingsPreviewState& previewState()const noexcept{return previewState_;}
    int selectedCustomBaseValue()const;
    bool customBaseAvailable()const;
    void setCustomBase(int base);
    void stopPowerEditing()noexcept{powerEditing=false;}
    void editPowerDraftField(int index,const std::wstring& text);
    void editPowerOptionDraft(std::string_view key,int value);
    void submit(int command,osd::Message notice,std::optional<windows::SettingsAction> action,const wchar_t* message,
        const wchar_t* success=L"Изменение подтверждено EC."){
        if(preview){
            if(action){using K=windows::SettingsActionKind;switch(action->kind){
                case K::StockPower:previewState_.stockPower(action->value);powerEditing=false;break;
                case K::LightingLevel:previewState_.brightness=action->value;break;
                case K::LightingEffect:previewState_.effect=action->value;break;
                case K::LightingColor:previewState_.color=action->value;break;
                case K::BatteryUpper:previewState_.batteryUpper=action->value;previewState_.batteryMode=-1;break;
                default:break;
            }}
            setStatus(L"Просмотр: изменения только в макете, на устройство не передаются.");
            view_.renderSelection();view_.updateButtons();return;
        }
        if(action&&action->kind==windows::SettingsActionKind::StockPower){powerEditing=false;pendingPerformance=action->value;view_.renderSelection();}
        requestedCommand=command;requestedNotice=std::move(notice);start(std::move(action),message,success);
    }
    SettingsOperationsWindows(View&,EcAccess*,std::function<void(std::string)>,std::shared_ptr<windows::FanController>,BoardDescriptor,bool,ResourceWorker::ThreadFactory={});
    ~SettingsOperationsWindows(){shutdown();}
    void bindReady(std::function<void()> ready){ready_=std::move(ready);}
    void shutdown();
    TraySettingsState trayState()const;
    void refreshForTray(bool invalidateObservation);
    bool observePowerSupply();
    bool cancelCustomEditor();
    void initializePowerEditors();
    void selectBatteryView(BatteryCardView);
    void requestLogger(const std::string& address,const std::string& value,bool writing);
    void changeCycleInterval(int seconds);
    void changeCycleColor(int index,KeyboardColor color);
    static std::string auditState(const State& s);
    bool wujiePower() const;
    int customPowerIndex() const;
    bool apuLockedByFanDefault();
    static std::array<int,3> hlylMaximum();
    int selectedPowerMode() const;
    void applyPowerFromFields();
    bool batteryAutoActive() const;
    int batteryIndex() const;
    static int performanceIndexFromRaw(std::uint8_t raw);
    int performanceIndex() const;
    LightingConfiguration hidConfiguration()const;
    void selectLightingZoneLocal(LightingZone zone);
    void applyVoltageMode(int mode);
    void applyBatteryAging(bool enabled);
    SingleZoneModel lightingModel() const;
    const KeyboardPulsePalette& pulsePalette()const;
    std::filesystem::path lightingPreferences()const;
    int effectIndex() const;
    int brightnessMaximum() const;
    std::optional<int> currentBrightness() const;
    int brightnessIndex() const;
    void setStatus(std::wstring text);
    void readState(bool timerTick=false);
    void complete();
    void observeKeyboardLevel(int level);
    osd::Message lightingNotice(std::string action,std::optional<int> level={})const;
    void selectCustomPreset(int slot);
private:
    friend struct SettingsOperationsTestAccess;
    friend struct SettingsBackendTestAccess;
    struct Backends {
        std::unique_ptr<LaptopControl> laptop;std::shared_ptr<windows::Gm6Controller> gm;
        std::shared_ptr<windows::NativeBatteryController> battery,voltage;
        std::shared_ptr<windows::IteLightingController> lighting;
    };
    struct BackendFactories {
        std::function<std::unique_ptr<LaptopControl>()> laptop;
        std::function<std::shared_ptr<windows::Gm6Controller>()> gm;
        std::function<std::shared_ptr<windows::NativeBatteryController>()> battery,voltage;
        std::function<std::shared_ptr<windows::IteLightingController>()> lighting;
    };
    static Backends acquireBackends(bool preview,bool hasEc,const BackendFactories&);
    static Backends acquireBackends(bool preview,bool hasEc);
    View& view_;
    void start(std::optional<windows::SettingsAction> action, const wchar_t* message,
               const wchar_t* successMessage = L"Изменение подтверждено EC.", bool passive = false,
               std::uint64_t existingToken = 0, std::shared_ptr<DeviceOperation> reservation = {},bool keyboardIdleOnly=false);

    windows::CustomModeContext customContext();
    LightingControlContext lightingContext()const;
    LightingControlContext lightingHardwareContext()const;
    void rememberChoice(const State& actual,int command,const windows::SettingsCommandSemantics&);
    windows::BatterySettingsContext batteryContext()const;
    void runAction(const windows::SettingsAction& action);
    std::function<void()> ready_;bool stopped_{};
};

}
