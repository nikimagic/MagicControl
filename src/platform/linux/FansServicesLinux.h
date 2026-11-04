#pragma once

#include "MagicControl/DisplayContinuity.h"
#include "MagicControl/ResourceControl.h"
#include "MagicControl/ResourceProvider.h"
#include "MagicControl/Monitoring.h"
#include "MagicControl/PortableSettings.h"
#include "MagicControl/FanResourceControl.h"
#include <string>
#include <string_view>

namespace mc::linux_platform {
// Explicit unavailable adapter: do not borrow Windows transport/commands.
std::string executeFanResource(const FanWriteRequest&);
// Owns monitoring/result projection and existing local profile I/O. Linux
// hardware FAN commands remain unavailable; no foreign EC map is admitted.
class FansOperationsLinux {
public:
    FansOperationsLinux(BoardDescriptor board,bool preview,std::function<MonitoringSnapshot()> source={});
    ~FansOperationsLinux(){worker_.join();}
private:
    const BoardDescriptor resourceBoard;
    bool preview{};
    FanCurveProfiles profiles;
    MonitoringSnapshot monitoring;
    ResourceState resourceState;
    std::function<MonitoringSnapshot()> monitoringSource;
public:
    // UI-thread read bindings: no copied cache, backend handle or writable state.
    struct Presentation {
        const decltype(FansOperationsLinux::resourceBoard)& resourceBoard;
        const decltype(FansOperationsLinux::preview)& preview;
        const decltype(FansOperationsLinux::profiles)& profiles;
        const decltype(FansOperationsLinux::monitoring)& monitoring;
        const decltype(FansOperationsLinux::resourceState)& resourceState;
    };
    Presentation presentation()const{return {resourceBoard,preview,profiles,monitoring,resourceState};}
    void seedPreviewModels(std::array<ProcessorVendor,2> vendors,std::array<std::string,2> names){if(preview){monitoring.processorVendors=vendors;monitoring.processorModels=std::move(names);}}
    void normalizeEditorDraft(int channel);
    void editCurveDraft(int channel,const FanCurveTable& curve){if(profiles.ecManual)profiles.ecManualCurve=curve;else profiles.edit(channel,curve);}
    bool poll(std::function<void()> ready);
    std::optional<bool> take();
    void shutdown(){worker_.join();}
    std::string loadDraft();
    std::string saveDraft();
    std::string requestPreset(int,FanCurvePreset);
    std::string requestLink();
    MonitoringSnapshot readMonitoring();
    MonitoringSnapshot presentMonitoring(MonitoringSnapshot sample);
    static bool loadProfiles(std::string_view profile,FanCurveProfiles& curves,std::string& error);
    static bool saveProfiles(std::string_view profile,const FanCurveProfiles& curves,std::string& error);
private:
    ResourceWorker worker_;MonitoringSnapshot pending_;bool loggedModels_{};
    std::unique_ptr<MonitoringReader> reader_;
    MonitoringDisplayCache displayCache_;
};
}
