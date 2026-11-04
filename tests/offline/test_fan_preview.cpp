#include "MagicControl/DeviceProfile.h"
#include "MagicControl/BoardDescriptor.h"
#include <iostream>
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#include "../../src/platform/windows/FanDeviceActionsWindows.h"
#else
#include "../../src/platform/linux/FansServicesLinux.h"
#endif

namespace {
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
#ifdef _WIN32
struct View final:mc::windows::FansOperationsWindows::ViewPort {
    int notices{};
    void commitEditors(int)override{} void updateEditorText(int)override{}
    void positionControls()override{} void updateAvailability()override{}
    void notifyOwner()override{} void cancelProfileSave()override{}
    void resetCurvePage(int)override{} void clampCurvePages()override{}
    void invalidate(unsigned)override{} void monitoringLayoutChanged()override{}
    std::uint64_t beginNotice(const mc::gui::osd::Message&)override{++notices;return 0;}
    void completeNotice(std::uint64_t,mc::gui::osd::Message)override{++notices;}
};
#endif
}
int main(){try {
    int checked{};
    std::vector<mc::BoardDescriptor> boards;
    for(const auto* p:mc::deviceProfiles()){
        mc::BoardDescriptor board{std::string(p->manufacturer),std::string(p->baseBoard),{},std::string(p->productName)};
        check(mc::deviceProfile(board).id==p->id,"profile fixture does not identify exact device");boards.push_back(std::move(board));
    }
    boards.push_back({"Unknown manufacturer","Unknown board",{},"Unknown product"});
    check(mc::deviceProfile(boards.back()).id.empty(),"unknown fixture unexpectedly matched");
    for(const auto& board:boards){
        const auto& profile=mc::deviceProfile(board);
        int reads{};
        auto source=[&]{++reads;return mc::MonitoringSnapshot{};};
#ifdef _WIN32
        View view;
        mc::windows::FansOperationsWindows m(view,nullptr,{},source,{},board,true);
        m.initialize();m.setVisible(true);m.pollSensors();
        auto set=[&](int ch,mc::FanCurvePreset preset){m.selectPreset(ch,preset);};
#else
        mc::linux_platform::FansOperationsLinux m(board,true,source);
        check(m.loadDraft().empty(),"preview tried loading persistent settings");
        check(!m.poll([]{}),"preview started monitoring");
        auto set=[&](int ch,mc::FanCurvePreset preset){m.requestPreset(ch,preset);};
#endif
        const auto state=m.presentation();
        for(int ch=0;ch<2;++ch)check(state.profiles.activePointCount(ch)==profile.fanPointCounts[ch],"wrong preview point count");
        set(0,mc::FanCurvePreset::Max);
        check(state.profiles.currentPreset(0)==mc::FanCurvePreset::Max&&state.profiles.currentPreset(1)==mc::FanCurvePreset::Max,"Max preview selection");
        set(0,mc::FanCurvePreset::Manual);
        check(state.profiles.currentPreset(0)==mc::FanCurvePreset::Manual,"Manual preview selection");
        auto curve=state.profiles.current(0);curve[0]={20,5};curve[1]={100,100};m.editCurveDraft(0,curve);
        check(state.profiles.current(0)[0].temperature==20&&state.profiles.current(0)[0].duty==5&&state.profiles.current(0)[1].temperature==100&&state.profiles.current(0)[1].duty==100,"preview endpoint draft changed");
        set(0,mc::FanCurvePreset::Default);
        check(state.profiles.currentPreset(0)==mc::FanCurvePreset::Default&&state.profiles.currentPreset(1)==mc::FanCurvePreset::Default,"Default preview selection");
        set(0,mc::FanCurvePreset::Manual);
        check(state.profiles.current(0)[0].temperature==20&&state.profiles.current(0)[1].duty==100,"preview switch lost manual draft");
#ifdef _WIN32
        m.saveLocalCurveEdits();m.pollSensors();m.shutdown();check(view.notices==0,"preview submitted a hardware notice");
#else
        check(m.saveDraft().empty(),"preview tried saving persistent settings");m.shutdown();
#endif
        check(reads==0,"preview invoked hardware monitoring");++checked;
    }
    check(checked>0,"no device profiles");std::cout<<"fan preview: "<<checked<<" profiles passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
