// Exercises the production presentation/profile/KEYs owner with injected
// SMBIOS and memory-only services. No OS device adapter or real EC is opened.
#include "OfflineTest.h"
#include "../TestBoardFixture.h"
#include "MagicControl/DeviceProfile.h"
#include "MagicControl/KeyResourceControl.h"
#include "MagicControl/Logging.h"
#include "src/gui/common/DeviceHeader.h"
#include "src/gui/common/Geometry.h"
#include "src/gui/windows/SettingsCardsModel.h"
#include "src/gui/linux/SettingsCardsModel.h"
#ifdef _WIN32
#include "src/platform/windows/SettingsOperationsWindows.h"
#include "src/platform/windows/SettingsActionWindows.h"
#else
#include "src/platform/linux/SettingsServicesLinux.h"
#endif
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

namespace ui=mc::gui::ui;
namespace {
const mc::DeviceProfile& catalogProfile(std::string_view id){
    if(id=="noname")return mc::deviceProfile({});
    for(const auto* profile:mc::deviceProfiles())if(profile&&profile->id==id)return *profile;
    CHECK(false);return mc::deviceProfile({});
}
mc::BoardDescriptor fakeBoard(const mc::DeviceProfile& p){
    if(p.id.empty())return {"Preview test unknown","Unknown board",{},"Unknown product"};
    return {std::string(p.manufacturer),std::string(p.baseBoard),{},std::string(p.productName)};
}
struct Signal {
    std::mutex gate;std::condition_variable ready;bool fired{};
    void notify(){std::lock_guard lock(gate);fired=true;ready.notify_one();}
    void wait(){std::unique_lock lock(gate);CHECK(ready.wait_for(lock,std::chrono::seconds(3),[&]{return fired;}));fired=false;}
};
#ifdef _WIN32
struct FakeSettingsView final:mc::gui::SettingsOperationsWindows::View {
    int fields{},notices{};
    mc::gui::osd::Message notice(const mc::windows::SettingsAction&)const override{return {};}
    void statusChanged(const std::wstring&)override{}
    bool readPowerFields(std::array<int,3>&)override{++fields;return true;}
    void renderSelection()override{} void updateButtons()override{} void loggerChanged()override{}
    std::uint64_t beginNotice(const mc::gui::osd::Message&)override{++notices;return 0;}
    void completeNotice(std::uint64_t,mc::gui::osd::Message)override{++notices;}
};
#endif
void checkSettingsPreview(const mc::BoardDescriptor& board){
#ifdef _WIN32
    FakeSettingsView view;int workers{};
    mc::gui::SettingsOperationsWindows model(view,nullptr,{},nullptr,board,true,
        [&](std::function<void()>)->std::thread{++workers;throw std::runtime_error("preview attempted to launch a hardware worker");});
    model.readState();
    for(int kind=0;kind<=static_cast<int>(mc::windows::SettingsActionKind::CustomBase);++kind)
        model.submit(0,{},mc::windows::SettingsAction{static_cast<mc::windows::SettingsActionKind>(kind),1},L"preview");
    model.applyPowerFromFields();model.selectCustomPreset(2);model.applyVoltageMode(1);model.applyBatteryAging(true);
    model.selectBatteryView(mc::BatteryCardView::Capacity);model.editPowerDraftField(0,L"42");model.setCustomBase(1);
    CHECK(model.previewState().preset==2);CHECK(model.previewState().batteryMode==1);
    CHECK(workers==0&&view.fields==0&&view.notices==0);CHECK(!model.hasQueuedCommand());
#else
    mc::linux_platform::SettingsOperationsLinux model(board,true);unsigned notices{},callbacks{};
    model.bindNotice([&](const mc::gui::osd::Message&){++notices;return 0;});
    CHECK(model.previewStockPower(1));CHECK(model.previewCustomPreset(2));
    CHECK(model.previewBatteryMode(1));CHECK(model.previewEffect(1));
    for(int field=0;field<4;++field){
        mc::linux_platform::SettingsWorkerRequest request;
        if(field==0)request.upper=85;else if(field==1)request.restart=80;
        else if(field==2)request.keyboardLevel=1;else request.keyboardTimeout=30;
        CHECK(!model.submit(request,[&]{++callbacks;}));CHECK(!model.busy());
        const auto result=model.take();CHECK(result&&!result->accepted);
    }
    CHECK(model.selectBatteryView(mc::BatteryCardView::Capacity).empty());
    CHECK(model.previewState().preset==2&&model.previewState().batteryMode==1);
    CHECK(notices==0&&callbacks==0);
#endif
}
}

int main(){
    mc::setLoggingEnabled(false);mc::setBatteryLoggingEnabled(false);
    constexpr auto count=ui::kSupportedLaptops.size();
    unsigned pairs{},operations{};
    for(std::size_t actual=0;actual<count;++actual){
        const auto& actualProfile=catalogProfile(ui::kSupportedLaptops[actual].id);
        mc::tests::setTestBoard(fakeBoard(actualProfile));
        CHECK(ui::currentLaptop(mc::readBoardDescriptor())==static_cast<int>(actual));
        for(std::size_t selected=0;selected<count;++selected){
            ui::laptopSelection.store(static_cast<int>(selected));
            const auto& expected=catalogProfile(ui::kSupportedLaptops[selected].id);
            const auto selectedBoard=ui::presentationBoard();
            const auto& presented=mc::deviceProfile(selectedBoard);
            const bool preview=actual!=selected;
            CHECK(ui::laptopPreview()==preview);
            CHECK(presented.id==expected.id);
            CHECK(mc::deviceProfile(mc::readBoardDescriptor()).id==actualProfile.id);
            if(preview)CHECK(!ui::biosFanProbesAllowed());

            // Exercise the real OS-specific SETs owner once for every selected
            // profile. Its preview state may change, but it must not start a
            // worker, access a backend, save settings or emit hardware notices.
            if(actual==0)checkSettingsPreview(selectedBoard);

            // Both SETs adapters must project the selected profile, including
            // the mini-PC's documented absence of a battery and keyboard RGB.
            const mc::BatteryReading os;const mc::EcBatteryReading ec;const mc::BatteryPowerPolicy policy;
            const auto win=mc::gui::windowsSettingsCards(presented,os,ec,policy,false);
            const auto lin=mc::gtkui::linuxSettingsCards(presented,os,ec,policy,false);
            CHECK(win.visible==lin.visible);CHECK(win.order==lin.order);
            CHECK(win.brightnessMaximum==expected.keyboardBrightnessMaximum);
            CHECK(win.brightnessMaximum==lin.brightnessMaximum);
            CHECK(win.visible[0]!=(expected.id=="minisforum-hpbsd"));
            CHECK(win.visible[2]!=(expected.id=="minisforum-hpbsd"));
            CHECK(win.primary.size()==lin.primary.size());
            for(std::size_t i=0;i<win.primary.size();++i)CHECK(win.primary[i].label==lin.primary[i].label);

            for(const bool linuxHost:{false,true}){
                Signal signal;std::atomic<unsigned> reads{},executions{};unsigned localChanges{};bool osdDisabled{};
                mc::KeyOperationServices fake;
                fake.read=[&]{++reads;mc::KeySnapshot s;s.available.fill(true);return s;};
                fake.localState=[&] {return osdDisabled;};
                fake.localPreference=[&](bool value){++localChanges;osdDisabled=value;mc::KeyBackendResult r;r.success=true;return r;};
                fake.execute=[&](const mc::KeyCommand&,mc::KeySnapshot s,const std::function<void()>&){
                    ++executions;mc::KeyBackendResult result;result.snapshot=std::move(s);result.success=true;return result;
                };
                mc::KeysOperations owner(std::string(actualProfile.id),preview,linuxHost,std::move(fake),
                    [&]{signal.notify();},std::string(presented.id));
                for(std::size_t row=0;row<mc::kKeyCount;++row){
                    // Independent oracle: only the confirmed GM6 profile lacks
                    // this row; unsupported adapters alone never hide a row.
                    const bool visible=row!=static_cast<std::size_t>(mc::KeyAction::Copilot)||expected.id!="mechrevo-gm6ir0c";
                    if(owner.visible(static_cast<int>(row))!=visible)
                        std::fprintf(stderr,"actual=%.*s selected=%.*s preview=%d linux=%d row=%zu visible=%d expected=%d\n",
                            int(actualProfile.id.size()),actualProfile.id.data(),int(expected.id.size()),expected.id.data(),
                            int(preview),int(linuxHost),row,int(owner.visible(static_cast<int>(row))),int(visible));
                    CHECK(owner.visible(static_cast<int>(row))==visible);
                }
                for(const int dpi:{96,144,192}){
                    const auto g=ui::keysGeometry(1000*dpi/96,600*dpi/96,{dpi},owner.visibleRows());
                    int previousBottom{};
                    for(std::size_t row=0;row<mc::kKeyCount;++row){
                        if(!owner.visible(static_cast<int>(row))){CHECK(g.rows[row].height()==0);continue;}
                        CHECK(g.rows[row].height()>0);CHECK(g.rows[row].top>=previousBottom);
                        CHECK(g.off[row].top>=g.rows[row].top&&g.off[row].bottom<=g.rows[row].bottom);
                        previousBottom=g.rows[row].bottom;
                    }
                    CHECK(g.result.top>=g.devicesCard.bottom);
                }
                CHECK(owner.refresh());signal.wait();CHECK(owner.take().has_value());CHECK(reads==1);
                const auto id=actualProfile.id;
                const bool uniwill=id=="wu-jie-15-xpro"||id=="mechrevo-gm6ir0c"||id=="mechrevo-gm5hg0a"||
                    id=="machenike-l16"||id=="eluktronics-hydroc16g2";
                unsigned expectedExecutions{};
                for(int row=0;row<static_cast<int>(mc::kKeyCount);++row){
                    bool allowed=row==1||row==10||(!linuxHost&&row>=6&&row<=9);
                    if(!preview&&row==5)allowed=!linuxHost&&owner.visible(row);
                    if(!preview&&(row==0||row==3||row==4))allowed=!linuxHost&&uniwill;
                    if(!preview&&row==2)allowed=id=="huawei-hlyl-wxx9"||(!linuxHost&&uniwill);
                    CHECK(owner.disabled(row)!=allowed);
                    for(const bool on:{false,true}){
                        const auto submitted=owner.submit(row,on);CHECK(submitted.accepted==allowed);
                        if(!allowed)continue;
                        if(row==1){CHECK(submitted.completed&&submitted.completed->success);}
                        else{signal.wait();const auto result=owner.take();CHECK(result&&result->success);++expectedExecutions;}
                    }
                }
                CHECK(localChanges==2);CHECK(executions==expectedExecutions);
                ++operations;
            }
            ++pairs;
        }
    }
    ui::laptopSelection.store(-1);mc::tests::resetTestBoard();
    std::printf("Model preview: %u host/selection pairs, %u Windows/Linux policy cases, %u checks; fake services only\n",pairs,operations,offlineChecks);
}
