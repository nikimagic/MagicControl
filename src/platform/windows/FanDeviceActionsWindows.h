#pragma once
#include "../../devices/mechrevo/gm6ir0c/ControlWindows.h"
#include "NativeFixedFans.h"
#include "FanControlWindows.h"
#include "MagicControl/FanResourceControl.h"
#include <array>
#include <functional>
#include <string>
#include <filesystem>
#include "MagicControl/DisplayContinuity.h"
#include "MagicControl/BoardIdentity.h"
#include "MagicControl/OsdMessage.h"
#include "FanMonitoringWindows.h"

namespace mc::windows {
// Pure admission only; the caller must still reserve DeviceOperation in its
// existing position before any write. Reasons retain the existing audit keys.
const char* gmFanAdmissionReason(bool preview,bool busy,bool hasController,bool connected,bool writable) noexcept;
const char* nativeFanAdmissionReason(bool busy,bool hasController,bool known,bool writable) noexcept;
// Honor/HPBSD keep their established fixed-mode mapping. Empty means no action.
std::optional<int> nativeFanPresetMode(FanCurvePreset preset,bool nativeCurves) noexcept;
// Selection updates only mode intent. It must never resample or rewrite curves.
void selectFanPresetIntent(FanCurveProfiles&,int channel,FanCurvePreset);
bool toggleGmFanLinkIntent(FanCurveProfiles&);
void toggleOnlineFanLinkIntent(FanCurveProfiles&,const FanControlStatus&);
std::array<bool,2> fanOnlineControlled(bool online,std::array<bool,2> current,int channel,bool linked,int mode);
std::array<int,2> nativeFanTarget(int mode,const std::array<int,2>& minimum,const std::array<int,2>& draft);
struct FanResourceContext {
    Gm6Controller* gm{};NativeFixedFans* native{};FanController* ec{};
    std::shared_ptr<DeviceOperation> operation{};bool nativeCurves{};
    std::string_view profileId{};
};
struct FanResourceResult {
    bool success{},pending{};std::string error{};std::wstring message{};
    std::optional<Gm6State> gm6{};std::optional<NativeFixedFanState> native{};
    std::optional<FanControlStatus> ec{};
    void requireAccepted()const{if(!success&&!pending)throw std::runtime_error(error.empty()?"FAN command not confirmed":error);}
};
// One GUI/CLI/external restore/compound entry. The bound real controller owns
// identity, ownership, exact curves, thermal safeguards, readback and rollback.
// EC GUI submission may be pending; wait requests never report queueing as proof.
FanResourceResult executeFanResource(const FanResourceContext&,const FanWriteRequest&);
struct NativeFanTableSnapshot {
    FanCurveTable cpu{},gpu{};
    bool shared{},active{},defaultMode{},boost{};
};
NativeFanTableSnapshot readNativeFanTables(EcAccess&,bool ecManual);
class FansOperationsWindows {
    struct FanRequest;
public:
    enum ViewChange:unsigned {View=1,Page=2,Presets=4,Manual=8,Link=16,Immediate=32};
    using CurveCell=mc::FanCurveCell;using CurveTable=mc::FanCurveTable;
private:
    struct JobResult {
        std::uint64_t typedGeneration{};
        std::optional<mc::windows::Gm6State> gm6;
        std::optional<mc::windows::NativeFixedFanState> hlyl;
        bool success{};
        bool shared{};
        bool active{};
        bool defaultMode{};
        bool boost{};
        std::array<CurveCell, mc::ec::kTablePointCount> cpu{};
        std::array<CurveCell, mc::ec::kTablePointCount> gpu{};
        std::wstring message;
    };

private:
    mc::EcAccess* ec{};
    bool visible{},busy{},curvesLoaded{},localCurveSettingsLoaded{},draftEdited{};
    bool nativeActive{},nativeDefaultMode{},nativeFanBoost{},sharedMode{},modeKnown{};
    std::uint64_t pollCount{};
    FanReadout cpu,gpu;
    std::unique_ptr<MonitoringReader> monitoringReader;
    MonitoringDisplayCache displayCache;
    std::function<MonitoringSnapshot()> monitoringSource;
    MonitoringSnapshot monitoring;
    const BoardDescriptor resourceBoard;
    ResourceState resourceState;
    std::optional<int> ecTemperature;
    std::function<void(std::string)> telemetryLog;
    std::uint64_t nextBackgroundPoll{};
    std::string lastProcessorStatus;bool processorModelsLogged{};
    std::wstring operationStatus{L"Читаю таблицы вентиляторов EC…"};
    bool huawei{},preview{},gm6Ui{};
    std::shared_ptr<Gm6Controller> gm6Controller;Gm6State gm6State;
    bool gm6TablesKnown{},gm6DraftKnown{};
    std::uint64_t seenCustomPresetRevision{};
    std::optional<std::filesystem::file_time_type> seenCustomPresetFile;
    std::optional<int> gm6Requested;std::array<bool,2> gm6RequestedChannels{};
    std::shared_ptr<NativeFixedFans> hlylController;NativeFixedFanState hlylState;
    bool hlylKnown{},nativeCurves{};std::array<int,2> hlylDraft{50,50};
    std::optional<NativeFanCurves> nativeObservedDraft;std::optional<int> hlylRequested;
    std::wstring localCurveStatus;std::string profileError;std::filesystem::path localCurveSettingsPath;
    FanCurveProfiles profiles;std::optional<FanCurveProfiles> requestedHighlight;
    std::uint64_t highlightBaselineRevision{};
    std::shared_ptr<FanController> fanController;FanControlStatus controlStatus;
    std::string lastControlMessage;CurveTable appliedCpu{},appliedGpu{};
    std::uint64_t typedStateGeneration{};
    std::uint64_t fanNoticeToken{},fanNoticeRevision{},fanNoticeGeneration{};
    mc::gui::osd::Message fanNotice;

public:
    class ViewPort {
    public:
        virtual ~ViewPort()=default;
    virtual void commitEditors(int channel=-1)=0;
    virtual void updateEditorText(int)=0;
    virtual void positionControls()=0;
    virtual void updateAvailability()=0;
    virtual void notifyOwner()=0;
    virtual void cancelProfileSave()=0;
    virtual void resetCurvePage(int)=0;
    virtual void clampCurvePages()=0;
    virtual void invalidate(unsigned)=0;
    virtual void monitoringLayoutChanged()=0;
    virtual std::uint64_t beginNotice(const mc::gui::osd::Message&)=0;
    virtual void completeNotice(std::uint64_t,mc::gui::osd::Message)=0;
    };
    // UI-thread read bindings: no copied cache, backend handle or writable state.
    struct Presentation {
        const decltype(FansOperationsWindows::visible)& visible;
        const decltype(FansOperationsWindows::busy)& busy;
        const decltype(FansOperationsWindows::draftEdited)& draftEdited;
        const decltype(FansOperationsWindows::nativeDefaultMode)& nativeDefaultMode;
        const decltype(FansOperationsWindows::nativeFanBoost)& nativeFanBoost;
        const decltype(FansOperationsWindows::modeKnown)& modeKnown;
        const decltype(FansOperationsWindows::pollCount)& pollCount;
        const decltype(FansOperationsWindows::cpu)& cpu;
        const decltype(FansOperationsWindows::gpu)& gpu;
        const decltype(FansOperationsWindows::monitoring)& monitoring;
        const decltype(FansOperationsWindows::resourceBoard)& resourceBoard;
        const decltype(FansOperationsWindows::resourceState)& resourceState;
        const decltype(FansOperationsWindows::operationStatus)& operationStatus;
        const decltype(FansOperationsWindows::huawei)& huawei;
        const decltype(FansOperationsWindows::preview)& preview;
        const decltype(FansOperationsWindows::gm6State)& gm6State;
        const decltype(FansOperationsWindows::gm6DraftKnown)& gm6DraftKnown;
        const decltype(FansOperationsWindows::gm6Requested)& gm6Requested;
        const decltype(FansOperationsWindows::gm6RequestedChannels)& gm6RequestedChannels;
        const decltype(FansOperationsWindows::hlylState)& hlylState;
        const decltype(FansOperationsWindows::hlylKnown)& hlylKnown;
        const decltype(FansOperationsWindows::nativeCurves)& nativeCurves;
        const decltype(FansOperationsWindows::hlylDraft)& hlylDraft;
        const decltype(FansOperationsWindows::profiles)& profiles;
        const decltype(FansOperationsWindows::requestedHighlight)& requestedHighlight;
        const decltype(FansOperationsWindows::controlStatus)& controlStatus;
        const bool ec;
        const bool gm6Controller;
        const bool telemetryLog;
    };
    Presentation presentation()const{return {visible,busy,draftEdited,nativeDefaultMode,nativeFanBoost,modeKnown,pollCount,cpu,gpu,monitoring,resourceBoard,resourceState,operationStatus,huawei,preview,gm6State,gm6DraftKnown,gm6Requested,gm6RequestedChannels,hlylState,hlylKnown,nativeCurves,hlylDraft,profiles,requestedHighlight,controlStatus,static_cast<bool>(ec),static_cast<bool>(gm6Controller),static_cast<bool>(telemetryLog)};}
    void setVisible(bool value)noexcept{visible=value;}
    bool hasFanController()const noexcept{return static_cast<bool>(fanController);}
    bool fanNeedsRestore()const;
    bool fanObservedDefault()const;
    int nativeRequestedModeOr(int fallback)const{return hlylRequested.value_or(fallback);}
    void traceView(std::string text)const{if(telemetryLog)telemetryLog(std::move(text));}
    void configurePreviewMonitoring(MonitoringSnapshot seed,bool providePreview);
    void markDraftEdited()noexcept{draftEdited=true;}
    CurveTable normalizedEditorCurve(int channel);
    void editCurveDraft(int channel,const CurveTable& value){profiles.edit(channel,value);}
    void ensureNativeEditorDraft();
    void editNativeBand(int channel,int band,int temperature,int duty);
    void editFixedTarget(int channel,int value);
    FansOperationsWindows(ViewPort&,EcAccess*,std::shared_ptr<FanController>,std::function<MonitoringSnapshot()>,std::function<void(std::string)>,BoardDescriptor,bool);
    ~FansOperationsWindows(){shutdown();}
    void saveLocalCurveEdits();
    void applyGm(int mode,int onlineChannel=-1);
    void applyHlyl(int mode);
    void selectEcManual();
    void selectPreset(int channel, mc::FanCurvePreset preset);
    void readCurves();
    void toggleCurveLink();
    void completeWorker();
    void syncControlStatus();
    void pollSensors();
    void completeSensors();
    bool curveApplied(int channel) const;
    int definedCount(int channel) const;
    void initialize();
    void refreshDeviceState();
    void shutdown();
    void bindReady(std::function<void(bool)> ready);
    void syncCustomCurve();

private:
    struct FanRequest{bool writing{};FanWriteRequest command{};std::uint64_t generation{};std::shared_ptr<DeviceOperation> reservation{};};
    ViewPort& view_;
    void resetCurves();
    void loadLocalCurveSettings();
    bool saveLocalCurveSettings();
    void highlightRequestedMode();
    void adoptCustomCurves(const mc::gm6::Curves& curves);
    void observeGm(const mc::windows::Gm6State& value);
    void readNativeResult(JobResult& result) const;
    void launchCommand(FanRequest request);
    void beginFanNotice(int mode);
    void finishFanNotice(bool success,const std::wstring& detail);
    ResourceWorker commandWorker,sensorWorker;
    std::optional<JobResult> pendingJob;std::optional<FanSensorSnapshot> pendingSensors;
    std::function<void(bool)> ready_;bool stopped_{};
};

}
