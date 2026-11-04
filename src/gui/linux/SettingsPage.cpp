#include "MagicControl/ControlAudit.h"
#include "MagicControl/DisplayContinuity.h"
#include "../common/KeyboardBrightness.h"
#include "../common/KeyboardIdleDisplay.h"
#include "SettingsCardsModel.h"
#include "Pages.h"
#include "OsdService.h"
#include "../common/OsdMessages.h"
#include "../common/FooterStatus.h"
#include "MagicControl/Logging.h"
#include "MagicControl/LightingZones.h"
#include "MagicControl/PortableSettings.h"
#include "Ui.h"
#include "NumericField.h"
#include "../common/NumericCommitDelay.h"
#include "../common/EcByteEditor.h"
#include "MagicControl/Monitoring.h"
#include "MagicControl/Performance.h"
#include "MagicControl/CustomPresets.h"
#include "MagicControl/LaptopControl.h"
#include "../../platform/linux/SettingsServicesLinux.h"
#include "../../platform/linux/SettingsObservationEpoch.h"
#include "SettingsSleepMonitor.h"
#include "MagicControl/BoardIdentity.h"
#include "MagicControl/DeviceProfile.h"
#include "../common/MonitoringCards.h"
#include "MagicControl/BatteryResourceState.h"
#include "../common/DeviceHeader.h"
#include "../common/BatteryPolicy.h"
#include "../common/BatteryDetails.h"
#include "../common/CpuPowerLabels.h"
#include <array>
#include <cstdio>
#include <string>
#include <memory>
#include <algorithm>
#include "../../platform/linux/SettingsFailureLinux.h"

#ifdef MAGICCONTROL_GTK_SETTINGS_TEST
namespace mc::linux_platform {
struct SettingsOperationsTestAccess {
    static void seed(SettingsOperationsLinux& model,const BatteryReading& value){model.reading=value;model.batteryResources.update(model.pageBoard,value);}
    static void preview(SettingsOperationsLinux& model,bool value){model.preview=value;}
    static void fault(SettingsOperationsLinux& model,int value){model.testWorkerFault=value;}
    static void factory(SettingsOperationsLinux& model,ResourceWorker::ThreadFactory value){model.worker_.setFactory(std::move(value));}
};
}
#endif
namespace mc::gtkui {
namespace {
struct SetsState {
    linux_platform::SettingsOperationsLinux model;
    decltype(linux_platform::SettingsOperationsLinux::Presentation::pageBoard) pageBoard{model.presentation().pageBoard};
    decltype(linux_platform::SettingsOperationsLinux::Presentation::preview) preview{model.presentation().preview};
    decltype(linux_platform::SettingsOperationsLinux::Presentation::brightnessMaximum) brightnessMaximum{model.presentation().brightnessMaximum};
    decltype(linux_platform::SettingsOperationsLinux::Presentation::selectedCustomPreset) selectedCustomPreset{model.presentation().selectedCustomPreset};
    decltype(linux_platform::SettingsOperationsLinux::Presentation::reading) reading{model.presentation().reading};
    decltype(linux_platform::SettingsOperationsLinux::Presentation::batteryResources) batteryResources{model.presentation().batteryResources};
    decltype(linux_platform::SettingsOperationsLinux::Presentation::osBattery) osBattery{model.presentation().osBattery};
    decltype(linux_platform::SettingsOperationsLinux::Presentation::ecBattery) ecBattery{model.presentation().ecBattery};
    decltype(linux_platform::SettingsOperationsLinux::Presentation::batteryPolicy) batteryPolicy{model.presentation().batteryPolicy};
    decltype(linux_platform::SettingsOperationsLinux::Presentation::oemState) oemState{model.presentation().oemState};
    decltype(linux_platform::SettingsOperationsLinux::Presentation::oem) oem{model.presentation().oem};

    SetsState():model(ui::presentationBoard(),ui::laptopPreview()){}
    OsdService* osd{};
    std::array<GtkWidget*,3> voltageButtons{};
    std::array<GtkWidget*,2> agingButtons{};
    std::array<GtkWidget*,ui::batteryThresholdCount> battery{};
    std::array<GtkWidget*,3> power{};
    std::array<GtkWidget*,6> powerOptions{};
    std::array<GtkWidget*,4> effects{};
    std::array<GtkWidget*,5> performance{};
    std::array<GtkWidget*,3> customPresets{};
    std::array<GtkWidget*,3> policy{};
    std::array<GtkWidget*,2> usbPriority{};
    std::array<GtkWidget*,8> colors{};
    std::array<GtkWidget*,5> levels{};
    GtkWidget *custom{},*customSwatch{},*native{},*manual{},*keyboardTimeoutCheck{},*keyboardTimeout{};
    GtkWidget* keyboardIdleReadout{};
    std::vector<GtkWidget*> hiddenByResource;
    ui::KeyboardIdleGeometry keyboardIdleGeometry;
    GtkWidget* restart{};bool syncRestart{},restartEditing{};
    GtkWidget* loggerAddress{},*loggerValue{},*loggerBytes{};
    ui::NumericCommitDelay restartDelay;audit::Id restartAuditId{};guint restartTimer{};
    bool restartManualDirty{},restartPointerStep{},restartKeyboardStep{},closing{};
    Canvas* canvas{};std::optional<bool> laidOutBatteryVisible;GdkRGBA customColor{0.70,0.0,1.0,1.0};
    std::string colorStatus{"EC/SMU Linux-транспорт не подключён. Аппаратное управление недоступно."};
    void setStatus(std::string value){if(value==colorStatus)return;colorStatus=std::move(value);ui::footerStatuses().publish("sets",colorStatus);}
    guint timer{};bool syncTimeout{},timeoutEditing{};
    std::unique_ptr<SettingsSleepMonitor> sleepMonitor;
    struct Life {SetsState* owner;};
    std::shared_ptr<Life> life{std::make_shared<Life>(Life{this})};
    ~SetsState(){closing=true;life->owner=nullptr;sleepMonitor.reset();if(timer)g_source_remove(timer);if(restartTimer)g_source_remove(restartTimer);model.shutdown();}
};
constexpr std::array<std::array<int,3>,8> swatches{{
    {255,255,255},{255,0,0},{255,80,0},{255,204,0},{0,255,0},{0,255,255},{0,0,255},{180,0,255}}};
void syncBatteryControls(SetsState& s){
    const auto cards=linuxSettingsCards(deviceProfile(ui::presentationBoard()),s.osBattery,s.ecBattery,s.batteryPolicy,mc::batteryLoggingEnabled());
    for(auto b:s.battery)if(b)gtk_widget_set_visible(b,cards.visible[0]);
    for(auto b:s.voltageButtons)if(b)gtk_widget_set_visible(b,cards.visible[0]);
    if(s.restart){gtk_widget_set_visible(s.restart,FALSE);gtk_widget_set_sensitive(s.restart,FALSE);}
    for(int i=0;i<3;++i)if(s.policy[i])gtk_widget_set_visible(s.policy[i],ui::showEcPolicyControls&&!s.oem&&i<ui::ecPolicyButtonCount(deviceProfile(ui::presentationBoard()).id));
}
// Storage order STAPM/Slow/Fast. Measure the exact translated strings used to paint.
bool intelGm6(){const auto id=deviceProfile(ui::presentationBoard()).id;return id=="mechrevo-gm6ir0c"||id=="eluktronics-hydroc16g2"||id=="lenovo-legion-kwcn";}
std::array<const char*,3> powerLabels(){
    auto labels=ui::cpuPowerFieldLabels(deviceProfile(ui::presentationBoard()).id);
    if(intelGm6())std::swap(labels[0],labels[2]); // Existing placement uses reversed stored order.
    return labels;
}

std::size_t powerOptionCount(){return std::min<std::size_t>(6,deviceProfile(ui::presentationBoard()).powerOptions.size());}
std::string powerOptionLabel(std::size_t i){const auto p=deviceProfile(ui::presentationBoard()).powerOptions;
    if(i>=p.size())return {};const auto& f=p[i];return ui::cpuPowerOptionLabel(deviceProfile(ui::presentationBoard()).id,f.label,f.unit);}
ui::SetsGeometry geometry(Canvas& c,int width,SetsState& state) {
    const auto cards=linuxSettingsCards(deviceProfile(ui::presentationBoard()),state.osBattery,state.ecBattery,state.batteryPolicy,mc::batteryLoggingEnabled());
    const ui::Scale s{c.dpi()};auto g=ui::setsGeometry(width,s,{},3,true,true,cards.showDiagnostics,
        {textWidth("Каждые",Font::ClassicBody,c.dpi()),textWidth("сек",Font::ClassicBody,c.dpi()),textWidth("3600",Font::ClassicBody,c.dpi())},
        {textWidth("Default",Font::ClassicBody,c.dpi()),textWidth("Выкл.",Font::ClassicBody,c.dpi()),textWidth("4",Font::ClassicBody,c.dpi())},deviceProfile(ui::presentationBoard()).keyboardBrightnessMaximum,
        {static_cast<int>(cards.diagnostics[0].size()),static_cast<int>(cards.diagnostics[1].size())},stockCpuProfileCount(deviceProfile(ui::presentationBoard()).id),ui::ecPolicyButtonCount(deviceProfile(ui::presentationBoard()).id),cards.visible,static_cast<int>((powerOptionCount()+2)/3));
    std::array<int,3> widths{};
    for(int i=0;i<3;++i)widths[i]=textWidth(powerLabels()[2-i],Font::ClassicBody,c.dpi());
    ui::placeBatteryIdentity(g,s,textWidth("Циклов: 999 · Здоровье: 100,0%",Font::ClassicBody,c.dpi()));
    {
        const int identityWidth=textWidth(ui::batteryIdentityCaption(255,255,255),Font::ClassicBody,c.dpi());
        ui::placeCombinedBatteryControls(g,s,identityWidth);
        const int buttonWidth=std::max(textWidth("Выкл.",Font::ClassicBody,c.dpi()),textWidth("Вкл.",Font::ClassicBody,c.dpi()));
        ui::placeCombinedBatteryStatus(g,s,textWidth(ui::translate(ui::combinedBatteryAgingCaption),Font::ClassicBody,c.dpi()),identityWidth,buttonWidth);
    }
    ui::placeLoggerFields(g,s,{textWidth("EC[",Font::ClassicBody,c.dpi()),textWidth("] == [",Font::ClassicBody,c.dpi()),textWidth("]",Font::ClassicBody,c.dpi())});
    if(powerOptionCount()){std::array<int,6> extra{};for(std::size_t i=0;i<powerOptionCount();++i)extra[i]=textWidth(powerOptionLabel(i),Font::ClassicBody,c.dpi());
        ui::placePowerFieldsWithOptions(g,s,widths,extra,textWidth("250",Font::ClassicBody,c.dpi()),static_cast<int>((powerOptionCount()+2)/3));}
    else ui::placePowerFields(g,s,widths,textWidth("250",Font::ClassicBody,c.dpi()));
    if(!state.oem)ui::placeEcPolicyCaption(g,s,textWidth(ui::ecPolicyCaption(255,255),Font::ClassicBody,c.dpi()));
    if(ui::showUsbPriorityControls&&deviceProfile(ui::presentationBoard()).id=="wu-jie-15-xpro")
        ui::placeUsbPowerPriority(g,s,textWidth("Battery",Font::ClassicBody,c.dpi()),textWidth("Performance",Font::ClassicBody,c.dpi()));
    // Same header geometry and embedded font as Windows. No guessed EC mapping:
    // the current Linux SETs backend has no admitted native Wujie counter reader.
    state.keyboardIdleGeometry=ui::placeKeyboardIdle(g.headings[2],s,
        textWidth("Выключение подсветки через",Font::ClassicBody,c.dpi()),
        textWidth("Отключено",Font::ClassicBody,c.dpi()),textWidth("сек",Font::ClassicBody,c.dpi()));
    return g;
}
void paintBatteryReading(cairo_t* cr,const ui::BatteryRowsGeometry<6>& rows,const BatteryReading& reading,int dpi,bool lenovoEc=false){
    const auto metrics=ui::batteryCurrentMetrics(reading,lenovoEc);
    for(std::size_t i=0;i<metrics.size();++i){
        const auto color=ui::batteryCurrentColor(reading,i);
        text(cr,metrics[i].label,rows.labels[i],Font::ClassicBody,i==1?color:theme::kTextSecondary,dpi);
        text(cr,metrics[i].value,rows.values[i],Font::ClassicBody,color,dpi);
    }

}
void poll(SetsState&,int applyUpper=-1,int keyboardLevel=-1,int keyboardTimeout=-1,int restart=-1);
void renderSettingsPreview(SetsState& state){
    if(!state.preview)return;
    const auto& p=state.model.previewState();
    const auto select=[](GtkWidget* w,bool on){if(!w)return;gtk_widget_remove_css_class(w,"primary");if(on)gtk_widget_add_css_class(w,"primary");};
    for(int i=0;i<5;++i)select(state.performance[i],!p.customEditing&&p.power==i+1);
    select(state.manual,p.customEditing);
    for(int i=0;i<3;++i)select(state.customPresets[i],p.customEditing&&p.preset==i);
    for(int i=0;i<4;++i)select(state.effects[i],p.effect==i);
    for(int i=0;i<8;++i)select(state.colors[i],p.color==i);
    for(int i=0;i<5;++i)select(state.levels[i],p.brightness==i);
    for(int i=0;i<ui::batteryThresholdCount;++i)select(state.battery[i],p.batteryUpper==i);
    for(int i=0;i<3;++i)select(state.voltageButtons[i],p.batteryMode==i);
    for(int i=0;i<2;++i)select(state.agingButtons[i],p.batteryAging==i);
    for(auto* w:state.power)if(w)gtk_widget_set_sensitive(w,p.customEditing);
    for(auto* w:state.powerOptions)if(w)gtk_widget_set_sensitive(w,p.customEditing);
    state.setStatus("Просмотр: изменения только в макете, на устройство не передаются.");
}
void completeSets(SetsState& state){
    const auto result=state.model.take();if(!result)return;auto* owner=&state;
    if(result->workerEscaped){
        for(auto* button:state.battery)gtk_widget_set_sensitive(button,FALSE);
        for(auto* button:state.levels)gtk_widget_set_sensitive(button,FALSE);
        gtk_widget_set_sensitive(state.restart,FALSE);gtk_widget_set_sensitive(state.keyboardTimeoutCheck,FALSE);gtk_widget_set_sensitive(state.keyboardTimeout,FALSE);
    }
    if(result->accepted){
                const bool chargeAvailable=owner->model.chargeAvailable();
                gtk_widget_set_sensitive(owner->restart,chargeAvailable);
                if(owner->oemState.chargeStart&&*owner->oemState.chargeStart>0&&!owner->restartEditing&&!owner->restartDelay.pending&&!owner->restartManualDirty){
                    owner->syncRestart=true;gtk_spin_button_set_value(GTK_SPIN_BUTTON(owner->restart),*owner->oemState.chargeStart);owner->syncRestart=false;}
                if(owner->oem){
                    for(int i=0;i<ui::batteryThresholdCount;++i){gtk_widget_set_sensitive(owner->battery[i],chargeAvailable);
                        gtk_widget_remove_css_class(owner->battery[i],"primary");if(owner->oemState.chargeEnd==ui::batteryThresholdValues[i])gtk_widget_add_css_class(owner->battery[i],"primary");}
                    for(int level=0;level<5;++level){gtk_widget_set_sensitive(owner->levels[level],owner->oemState.keyboardWritable&&level<=owner->brightnessMaximum);
                        gtk_widget_remove_css_class(owner->levels[level],"primary");if(owner->oemState.keyboardLevel==level)gtk_widget_add_css_class(owner->levels[level],"primary");}
                    gtk_widget_set_sensitive(owner->keyboardTimeoutCheck,FALSE);
                    gtk_widget_set_sensitive(owner->keyboardTimeout,FALSE);
                    if(owner->oemState.keyboardTimeoutSeconds&&!owner->timeoutEditing){owner->syncTimeout=true;
                        const auto seconds=*owner->oemState.keyboardTimeoutSeconds;
                        gtk_spin_button_set_value(GTK_SPIN_BUTTON(owner->keyboardTimeout),seconds);
                        gtk_check_button_set_active(GTK_CHECK_BUTTON(owner->keyboardTimeoutCheck),seconds!=0);
                        owner->syncTimeout=false;}}
    }
    if(!result->message.empty())state.setStatus(result->message);
    if(state.osd&&result->token){auto message=result->notice;message.detail=result->message;message.error=!result->success;state.osd->completeOperation(result->token,std::move(message));}
    const bool batteryVisible=linuxSettingsCards(deviceProfile(state.pageBoard),state.osBattery,state.ecBattery,state.batteryPolicy,mc::batteryLoggingEnabled()).visible[0];
    if(state.laidOutBatteryVisible&&*state.laidOutBatteryVisible!=batteryVisible)gtk_widget_queue_allocate(state.canvas->widget());
    syncBatteryControls(state);state.canvas->redraw();
    if(result->retry&&gtk_widget_get_mapped(state.canvas->widget()))poll(state);
}
void poll(SetsState& state,int applyUpper,int keyboardLevel,int keyboardTimeout,int restart){
    const auto life=state.life;
    const bool started=state.model.submit({applyUpper,restart,keyboardLevel,keyboardTimeout,state.oem},[life]{
        g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,+[](gpointer data)->gboolean{
            const auto& life=*static_cast<std::shared_ptr<SetsState::Life>*>(data);if(life->owner)completeSets(*life->owner);return G_SOURCE_REMOVE;
        },new std::shared_ptr<SetsState::Life>(life),+[](gpointer data){delete static_cast<std::shared_ptr<SetsState::Life>*>(data);});
    });
    if(!started){completeSets(state);return;}
    const bool writing=applyUpper>=0||keyboardLevel>=0||keyboardTimeout>=0||restart>=0;
    if(writing)gtk_widget_set_sensitive(state.restart,FALSE);
    if(state.oem&&writing){for(auto* button:state.battery)gtk_widget_set_sensitive(button,FALSE);for(auto* level:state.levels)gtk_widget_set_sensitive(level,FALSE);
        gtk_widget_set_sensitive(state.keyboardTimeoutCheck,FALSE);gtk_widget_set_sensitive(state.keyboardTimeout,FALSE);}
}
void transitionSettingsSleep(SetsState& state,bool asleep){if(state.model.transitionSleep(asleep)&&gtk_widget_get_mapped(state.canvas->widget()))poll(state);}
}
namespace {
std::uint64_t restartNow(){return static_cast<std::uint64_t>(g_get_monotonic_time()/1000);}
void cancelRestart(SetsState& s){s.restartDelay.cancel();if(s.restartTimer){g_source_remove(s.restartTimer);s.restartTimer=0;}}
void flushRestart(SetsState& s){
    if(s.closing||s.model.asleep()||!s.restartDelay.ready(restartNow())||s.model.busy())return;
    audit::Scope trace("battery.restart_commit","",s.restartAuditId);
    s.restartDelay.cancel();s.restartManualDirty=false;
    if(s.model.chargeAvailable()){const int value=gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(s.restart));poll(s,-1,-1,-1,value);}
}
void deferRestart(SetsState& s,std::uint64_t delay=ui::NumericCommitDelay::stepPauseMs){
    if(s.closing||s.preview)return;if(audit::capture())s.restartAuditId=audit::capture();s.restartDelay.schedule(restartNow(),delay);
    if(!delay)flushRestart(s);
    if(s.restartDelay.pending&&!s.restartTimer)s.restartTimer=g_timeout_add(100,+[](gpointer p)->gboolean{
        auto& state=*static_cast<SetsState*>(p);flushRestart(state);
        if(!state.restartDelay.pending){state.restartTimer=0;return G_SOURCE_REMOVE;}return G_SOURCE_CONTINUE;
    },&s);
}
}
GtkWidget* createSettingsPage(OsdService* osd) {
    auto* state=new SetsState();state->osd=osd;state->model.bindNotice([state](const mc::gui::osd::Message& message){return state->osd?state->osd->beginOperation(message):0;});
    auto* canvas=Canvas::create([state](Canvas& c,int width,int){
        for(auto* w:state->hiddenByResource)gtk_widget_set_visible(w,TRUE);
        state->hiddenByResource.clear();
        auto g=geometry(c,width,*state);
        state->laidOutBatteryVisible=g.cards[0].width()>0;
        auto restart=g.restartEdit;restart.right=g.restartSpin.right;c.place(state->restart,restart);
        setNumericArrowWidth(state->restart,g.restartSpin.width());
        auto loggerAddress=g.loggerAddressEdit;loggerAddress.right=g.loggerAddressSpin.right;
        auto loggerValue=g.loggerValueEdit;loggerValue.right=g.loggerValueSpin.right;
        const bool loggerVisible=g.batteryComparison.width()>0;
        for(auto* field:{state->loggerAddress,state->loggerValue,state->loggerBytes})if(field)gtk_widget_set_visible(field,loggerVisible);
        if(loggerVisible){c.place(state->loggerAddress,loggerAddress);c.place(state->loggerValue,loggerValue);
            setNumericArrowWidth(state->loggerAddress,g.loggerAddressSpin.width());setNumericArrowWidth(state->loggerValue,g.loggerValueSpin.width());
            c.place(state->loggerBytes,g.loggerMulti);}
        for(int i=0;i<ui::batteryThresholdCount;++i)c.place(state->battery[i],g.battery[i]);
        c.place(state->agingButtons[0],g.agingOff);c.place(state->agingButtons[1],g.agingOn);
        c.place(state->keyboardIdleReadout,state->keyboardIdleGeometry.value);
        for(int i=0;i<3;++i)c.place(state->voltageButtons[i],g.batteryVoltage[i]);
        syncBatteryControls(*state);
        for(int i=0;i<3;++i){c.place(state->effects[i],g.effects[i]);
            auto r=g.powerEdits[2-i];r.right=g.powerSpins[2-i].right;c.place(state->power[i],r);
            setNumericArrowWidth(state->power[i],g.powerSpins[2-i].width());}
        for(std::size_t i=0;i<state->powerOptions.size();++i){const bool shown=i<powerOptionCount();gtk_widget_set_visible(state->powerOptions[i],shown);
            if(shown){auto r=g.powerOptionEdits[i];r.right=g.powerOptionSpins[i].right;c.place(state->powerOptions[i],r);setNumericArrowWidth(state->powerOptions[i],g.powerOptionSpins[i].width());}}
        c.place(state->effects[3],g.effects[3]);
        for(int i=0;i<stockCpuProfileCount(deviceProfile(ui::presentationBoard()).id);++i)c.place(state->performance[i],g.performance[i]);
        c.place(state->manual,g.manualPowerButton);c.place(state->policy[0],g.apuPolicyOff);c.place(state->policy[1],g.apuPolicyOn);
        for(int i=0;i<2;++i){const bool shown=ui::showUsbPriorityControls&&deviceProfile(ui::presentationBoard()).id=="wu-jie-15-xpro";
            if(shown)c.place(state->usbPriority[i],g.usbPowerPriority[i]);
            if(state->usbPriority[i])gtk_widget_set_visible(state->usbPriority[i],shown);}
        if(nativeEcPolicyProfile(deviceProfile(ui::presentationBoard()).id))c.place(state->policy[2],g.apuPolicyOriginal);
        for(int i=0;i<3;++i)c.place(state->customPresets[i],g.customPowerPresets[i]);
        for(int i=0;i<8;++i)c.place(state->colors[i],g.colors[i]);
        for(int level=0;level<5;++level){const bool shown=level<=state->brightnessMaximum;if(shown)c.place(state->levels[level],g.brightnessLevels[level]);gtk_widget_set_visible(state->levels[level],shown);}
        c.place(state->custom,g.customColor);c.place(state->native,g.nativeButton);
        c.place(state->keyboardTimeoutCheck,g.keyboardTimeoutCheck);
        auto timeout=g.keyboardTimeoutEdit;timeout.right=g.keyboardTimeoutSpin.right;c.place(state->keyboardTimeout,timeout);
        setNumericArrowWidth(state->keyboardTimeout,g.keyboardTimeoutSpin.width());
        // Removed from SETs for all models, including Honor and previews.
        for(auto* field:{state->keyboardTimeoutCheck,state->keyboardTimeout}){gtk_widget_set_visible(field,FALSE);gtk_widget_set_sensitive(field,FALSE);}
        const auto hide=[&](GtkWidget* w){if(w&&gtk_widget_get_visible(w)){state->hiddenByResource.push_back(w);gtk_widget_set_visible(w,FALSE);}};
        if(g.cards[0].width()<=0){
            hide(state->restart);
            for(const auto& list:{std::vector<GtkWidget*>(state->battery.begin(),state->battery.end()),std::vector<GtkWidget*>(state->voltageButtons.begin(),state->voltageButtons.end()),std::vector<GtkWidget*>(state->agingButtons.begin(),state->agingButtons.end())})for(auto* w:list)hide(w);
        }
        if(g.cards[2].width()<=0){
            for(auto* w:{state->custom,state->customSwatch,state->native,state->keyboardIdleReadout})hide(w);
            for(const auto& list:{std::vector<GtkWidget*>(state->effects.begin(),state->effects.end()),std::vector<GtkWidget*>(state->colors.begin(),state->colors.end()),std::vector<GtkWidget*>(state->levels.begin(),state->levels.end())})for(auto* w:list)hide(w);
        }
        return g.extent;
    },[state](Canvas& c,cairo_t* cr,int width,int height){
        const auto g=geometry(c,width,*state);const ui::Scale s{c.dpi()};const int dpi=c.dpi();
        fill(cr,{0,0,width,height},theme::kBackground);
        const char* headings[]{"Батарея",intelGm6()?"Производительность Intel CPU":"Производительность APU",mc::lightingZoneTitle(LightingZone::Keyboard)};
        for(int i=0;i<3;++i)if(g.cards[i].width()>0){fill(cr,g.cards[i],theme::kSurface);text(cr,headings[i],g.headings[i],Font::ClassicHeading,theme::kTextPrimary,dpi);}
        if(g.cards[2].width()>0){
        text(cr,"Выключение подсветки через",state->keyboardIdleGeometry.caption,Font::ClassicBody,theme::kTextSecondary,dpi,Horizontal::Right);
        text(cr,"сек",state->keyboardIdleGeometry.unit,Font::ClassicBody,theme::kTextSecondary,dpi);
        }
        if(g.cards[0].width()>0){
        std::optional<std::uint8_t> project,rom;
        for(const auto& f:state->ecBattery.registers)if(f.value&&*f.value<=255){
            if(f.address==0x770)rom=static_cast<std::uint8_t>(*f.value);
            if(f.address==0x740)project=static_cast<std::uint8_t>(*f.value);
        }
        const bool showIdentity=ui::showBatteryEcIdentity(deviceProfile(ui::presentationBoard()));
        if(showIdentity)text(cr,ui::batteryIdentityCaption(project,state->ecBattery.platformRaw,rom),g.batteryDeviceIdentity,Font::ClassicBody,theme::kTextSecondary,dpi,Horizontal::Right);
        const auto cards=linuxSettingsCards(deviceProfile(ui::presentationBoard()),state->osBattery,state->ecBattery,state->batteryPolicy,mc::batteryLoggingEnabled());
        const auto& times=cards.primary;const auto& sources=cards.sources;
        for(std::size_t card=0;card<sources.size();++card){
            fill(cr,g.batteryInfoCards[card],theme::kFanNestedSurface);
            text(cr,sources[card],g.batteryInfoHeadings[card],Font::ClassicHeading,theme::kTextPrimary,dpi);
        }
        std::array<int,ui::primaryBatteryRows> widths{};
        for(std::size_t row=0;row<times.size();++row)widths[row]=textWidth(times[row].value,Font::ClassicBody,dpi);
        std::array<std::array<int,6>,2> currentWidths{};
        const auto& readings=cards.current;
        const bool lenovoEc=deviceProfile(ui::presentationBoard()).id=="lenovo-legion-kwcn";
        for(std::size_t column=0;column<2;++column){const auto metrics=ui::batteryCurrentMetrics(readings[column],column==0&&lenovoEc);
            for(std::size_t row=0;row<metrics.size();++row)currentWidths[column][row]=textWidth(metrics[row].value,Font::ClassicBody,dpi);}
        const auto primaryRows=ui::batteryPrimaryRowsGeometry(g,{dpi},currentWidths,widths);
        paintBatteryReading(cr,primaryRows.current[0],readings[0],dpi,lenovoEc);
        paintBatteryReading(cr,primaryRows.current[1],readings[1],dpi);
        const auto& batteryRows=primaryRows.details;
        c.setTooltipRegions(ui::batteryPrimaryTooltips(primaryRows,deviceProfile(ui::presentationBoard()).id=="lenovo-legion-kwcn"));
        for(std::size_t row=0;row<times.size();++row){
            const auto& source=sources[row/((ui::primaryBatteryRows+1)/2)];
            const auto label=ui::translate(times[row].label)+(times[row].source==source?"":" · "+times[row].source);
            text(cr,label,batteryRows.labels[row],Font::ClassicBody,theme::kTextSecondary,dpi);
            text(cr,times[row].value,batteryRows.values[row],Font::ClassicBody,ui::primaryBatteryValueColor(row,times[row].value),dpi);
        }
        if(cards.showDiagnostics){
            fill(cr,g.batteryComparison,theme::kSurface);
            text(cr,"Логгер",g.loggerHeading,Font::ClassicHeading,theme::kTextPrimary,dpi);
            text(cr,"EC[",g.loggerPrefix,Font::ClassicBody,theme::kTextSecondary,dpi);
            text(cr,"] == [",g.loggerMiddle,Font::ClassicBody,theme::kTextSecondary,dpi);
            text(cr,"]",g.loggerSuffix,Font::ClassicBody,theme::kTextSecondary,dpi);
            text(cr,ui::ecEditorRangeCaption(),g.loggerFormats,Font::ClassicBody,theme::kTextSecondary,dpi);
            text(cr,ui::ecRegisterDescription(deviceProfile(ui::presentationBoard()).id,0x770),g.loggerMeaning,Font::ClassicBody,theme::kTextSecondary,dpi,Horizontal::Left,Vertical::Top,true);
            text(cr,ui::ecKnownValues(deviceProfile(ui::presentationBoard()).id,0x770),g.loggerKnown,Font::ClassicBody,theme::kTextSecondary,dpi,Horizontal::Left,Vertical::Top,true);
            const auto& columns=cards.diagnostics;
            for(std::size_t column=0;column<columns.size();++column){
                const auto& block=g.batteryDetailColumns[column];
                fill(cr,block.card,theme::kFanNestedSurface);
                text(cr,sources[column]+" · "+ui::translate("Подробные сведения"),block.heading,Font::ClassicHeading,theme::kTextPrimary,dpi);
                text(cr,column==0?ui::ecBatteryStatus(state->ecBattery):state->osBattery.source,block.status,Font::ClassicBody,theme::kTextSecondary,dpi);
                std::array<int,ui::ecBatteryDetailRows> valueWidths{};
                for(std::size_t row=0;row<columns[column].size()&&row<valueWidths.size();++row)valueWidths[row]=textWidth(columns[column][row].value,Font::ClassicBody,dpi);
                const auto detailRows=ui::batteryDetailRowsGeometry(g,{dpi},static_cast<int>(column),valueWidths);
                for(std::size_t row=0;row<columns[column].size()&&row<detailRows.labels.size();++row){
                    text(cr,columns[column][row].label,detailRows.labels[row],Font::ClassicBody,theme::kTextSecondary,dpi);
                    text(cr,columns[column][row].value,detailRows.values[row],Font::ClassicBody,theme::kTextPrimary,dpi);
                }
                if(column==0)text(cr,"Raw: назначение не подтверждено; пороги не доказывают остановку зарядки.",block.note,Font::ClassicBody,theme::kTextMuted,dpi,Horizontal::Left,Vertical::Top,true);
            }
        }
        // Same positions, colours and unavailable markers as Windows. Never invent EC/SMU data.
        auto port=g.portDetail, flow=g.portDetail;
        const auto batteryView=state->batteryResources.view();
        const auto card=ui::batteryCard(state->reading,&batteryView);
        const auto adapter=ui::lenovoAdapterPowerSummary(deviceProfile(state->pageBoard).id,state->preview,state->reading,state->ecBattery);
        const std::string portText=adapter?*adapter:ui::batteryPowerStatus(state->reading,&batteryView);
        const std::string voltageText=ui::batteryVoltageTargets({}, {});
        const auto flowText=card.left[1].label+" "+card.left[1].value;
        const bool showFlow=card.flow==BatteryFlow::Charging;
        text(cr,ui::combinedBatteryAgingCaption,g.agingCaption,Font::ClassicBody,theme::kTextSecondary,dpi);
        const int rowRight=std::max(port.left,(showIdentity?g.batteryDeviceIdentity.left:g.portDetail.right)-s(12));
        const int flowReserve=showFlow?std::min(textWidth(flowText,Font::ClassicBody,dpi)+s(8),std::max(0,(rowRight-port.left)/3)):0;
        port.right=std::max(port.left,std::min(port.left+textWidth(portText,Font::ClassicBody,dpi),rowRight-flowReserve));
        flow.left=std::min(rowRight,port.right+s(8));
        flow.right=std::min(rowRight,flow.left+(showFlow?textWidth(flowText,Font::ClassicBody,dpi):0));
        text(cr,portText,port,Font::ClassicBody,theme::kTextSecondary,dpi);
        if(showFlow)text(cr,flowText,flow,Font::ClassicBody,
            card.flow==BatteryFlow::Charging?theme::kBatteryCharging:theme::kTextSecondary,dpi);
        text(cr,voltageText,g.batteryVoltageSummary,Font::ClassicBody,theme::kTextSecondary,dpi);
        const auto cycles=resourceDisplayUnsigned(&batteryView,ResourceKind::Battery,0,"cycles");
        text(cr,"Циклов: "+(cycles?std::to_string(*cycles):"—")+" · Здоровье: "+
            ui::monitoringMetric(resourceDisplayValue(&batteryView,ResourceKind::Battery,0,"health"),1)+"%",g.batteryIdentity,Font::ClassicBody,theme::kTextSecondary,dpi,Horizontal::Right);
        }
        const auto fullPower=intelGm6()?"Мощность CPU (Package): — Вт":"Мощность APU (SMU Fast): — Вт";
        const auto compactPower=intelGm6()?"Package: — Вт":"APU: — Вт";
        const auto powerText=powerOptionCount()&&textWidth(fullPower,Font::ClassicBody,dpi)>g.apuDiagnostics[0].width()?
            (textWidth(compactPower,Font::ClassicBody,dpi)>g.apuDiagnostics[0].width()?"— Вт":compactPower):fullPower;
        text(cr,powerText,g.apuDiagnostics[0],Font::ClassicBody,theme::kTextPrimary,dpi);
        text(cr,intelGm6()?"Intel CPU · Linux EC-транспорт не подключён":"SMU · показания Linux пока недоступны",g.apuDiagnostics[2],Font::ClassicBody,theme::kTextMuted,dpi);
        for(int i=0;i<3;++i)text(cr,powerLabels()[i],g.powerCaptions[2-i],Font::ClassicBody,theme::kTextSecondary,dpi,Horizontal::Right);
        if(powerOptionCount()){text(cr,"GPU",g.powerOptionHeaders[0],Font::ClassicBody,theme::kTextSecondary,dpi);
            for(std::size_t i=0;i<powerOptionCount();++i)text(cr,powerOptionLabel(i),g.powerOptionCaptions[i],Font::ClassicBody,theme::kTextSecondary,dpi,Horizontal::Right);}
        if(g.cards[2].width()>0)text(cr,"Цвет",g.colorCaption,Font::ClassicBody,theme::kTextSecondary,dpi);
        // No auto-off caption/unit; all other card/scroll positions are unchanged.

    });state->canvas=canvas;installNumericFocusCommit(canvas->widget());
    // Layout-only port: no EC setters are enabled without a verified Linux transport.
    g_object_set_data_full(G_OBJECT(canvas->widget()),"magiccontrol-sets",state,[](gpointer p){delete static_cast<SetsState*>(p);});
    const auto& batteries=ui::batteryPolicyNames();const auto performance=ui::cpuPowerLabels(deviceProfile(ui::presentationBoard()).id);
    for(int i=0;i<3;++i){state->voltageButtons[i]=actionButton(ui::batteryModeNames(deviceProfile(ui::presentationBoard()).batteryModeLabels)[i],[state,i]{if(state->model.previewBatteryMode(i))renderSettingsPreview(*state);},false,false);
        setWidgetTooltip(state->voltageButtons[i],"Штатный EC-транспорт Linux для этой модели не подключён");canvas->add(state->voltageButtons[i]);}
    const char* effects[]{"Моно","Пульсация","Мульти"};
    for(int i=0;i<2;++i){state->agingButtons[i]=actionButton(i?"Вкл.":"Выкл.",[state,i]{if(state->model.previewBatteryAging(i))renderSettingsPreview(*state);},false,false);
        setWidgetTooltip(state->agingButtons[i],"Подмена показаний батареи: проверенный транспорт Linux пока недоступен");canvas->add(state->agingButtons[i]);}
    if(ui::showEcLoggerCard){
    state->loggerAddress=createNumericField(0,ecEditorMaximumAddress,1,true);gtk_spin_button_set_value(GTK_SPIN_BUTTON(state->loggerAddress),0x770);
    state->loggerValue=createNumericField(0,255,1,true);
    state->loggerBytes=gtk_label_new("Raw EC для этой модели/ОС недоступен");
    gtk_label_set_selectable(GTK_LABEL(state->loggerBytes),TRUE);
    gtk_label_set_xalign(GTK_LABEL(state->loggerBytes),0);gtk_label_set_yalign(GTK_LABEL(state->loggerBytes),0);
    gtk_widget_add_css_class(state->loggerBytes,"selectable-data");canvas->add(state->loggerBytes);
    for(auto* field:{state->loggerAddress,state->loggerValue}){gtk_widget_set_sensitive(field,FALSE);g_object_set_data(G_OBJECT(field),"mc-enter-only",GINT_TO_POINTER(1));canvas->add(field);}
    }
    state->restart=createNumericField(1,99,1);gtk_spin_button_set_value(GTK_SPIN_BUTTON(state->restart),20);
    gtk_widget_set_sensitive(state->restart,FALSE);setWidgetAccessibleLabel(state->restart,ui::batteryRestartCaption);canvas->add(state->restart);
    auto* restartFocus=gtk_event_controller_focus_new();
    g_signal_connect(restartFocus,"enter",G_CALLBACK(+[](GtkEventControllerFocus*,gpointer p){static_cast<SetsState*>(p)->restartEditing=true;}),state);
    g_signal_connect(restartFocus,"leave",G_CALLBACK(+[](GtkEventControllerFocus*,gpointer p){auto& s=*static_cast<SetsState*>(p);s.restartEditing=false;
        if(s.closing||s.syncRestart)return;gtk_spin_button_update(GTK_SPIN_BUTTON(s.restart));
        if(s.restartManualDirty){deferRestart(s,0);} // Explicit text commit, possibly waiting for an in-flight read.
    }),state);
    gtk_widget_add_controller(state->restart,restartFocus);
    auto* steps=gtk_gesture_click_new();gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(steps),0);
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(steps),GTK_PHASE_CAPTURE);
    g_signal_connect(steps,"pressed",G_CALLBACK(+[](GtkGestureClick*,int,double x,double y,gpointer p){auto& s=*static_cast<SetsState*>(p);
        auto* target=gtk_widget_pick(s.restart,x,y,GTK_PICK_DEFAULT);s.restartPointerStep=target&&gtk_widget_get_ancestor(target,GTK_TYPE_BUTTON);}),state);
    g_signal_connect(steps,"released",G_CALLBACK(+[](GtkGestureClick*,int,double,double,gpointer p){static_cast<SetsState*>(p)->restartPointerStep=false;}),state);
    gtk_widget_add_controller(state->restart,GTK_EVENT_CONTROLLER(steps));
    auto* restartKeys=gtk_event_controller_key_new();gtk_event_controller_set_propagation_phase(restartKeys,GTK_PHASE_CAPTURE);
    g_signal_connect(restartKeys,"key-pressed",G_CALLBACK(+[](GtkEventControllerKey*,guint key,guint,GdkModifierType,gpointer p)->gboolean{auto& s=*static_cast<SetsState*>(p);
        s.restartKeyboardStep=key==GDK_KEY_Up||key==GDK_KEY_Down||key==GDK_KEY_Page_Up||key==GDK_KEY_Page_Down;
        if(!s.restartKeyboardStep)s.restartPointerStep=false;
        return FALSE;}),state);
    g_signal_connect(restartKeys,"key-released",G_CALLBACK(+[](GtkEventControllerKey*,guint,guint,GdkModifierType,gpointer p){static_cast<SetsState*>(p)->restartKeyboardStep=false;}),state);
    gtk_widget_add_controller(state->restart,restartKeys);
    g_signal_connect(state->restart,"changed",G_CALLBACK(+[](GtkEditable*,gpointer p){auto& s=*static_cast<SetsState*>(p);
        if(s.syncRestart||s.closing||!s.restartEditing||s.restartPointerStep||s.restartKeyboardStep)return;
        auto* root=gtk_widget_get_root(s.restart);auto* focus=root?gtk_root_get_focus(root):nullptr;
        if(!focus||(focus!=s.restart&&!gtk_widget_is_ancestor(focus,s.restart)))return;
        s.restartManualDirty=true;cancelRestart(s);}),state);
    g_signal_connect(state->restart,"value-changed",G_CALLBACK(+[](GtkSpinButton*,gpointer p){auto& s=*static_cast<SetsState*>(p);
        if(!s.syncRestart&&(s.restartPointerStep||s.restartKeyboardStep)){s.restartManualDirty=false;deferRestart(s);}}),state);
    for(int i=0;i<ui::batteryThresholdCount;++i){
        // Crossed 10/80 is a Wujie Windows experiment, not a Huawei pair or a Linux EC writer.
        state->battery[i]=actionButton(batteries[i],[state,i]{if(state->model.previewBatteryUpper(i)){renderSettingsPreview(*state);return;}
            if(state->model.chargeAvailable())poll(*state,ui::batteryThresholdValues[i]);},false,false);
        canvas->add(state->battery[i]);
    }
    for(int i=0;i<3;++i){state->effects[i]=actionButton(effects[i],[state,i]{if(state->model.previewEffect(i))renderSettingsPreview(*state);},false,false);
        canvas->add(state->effects[i]);
        state->power[i]=createNumericField(5,deviceProfile(ui::presentationBoard()).id=="wu-jie-15-xpro"?kApuManualMaximum:100,1);gtk_editable_set_text(GTK_EDITABLE(state->power[i]),"—");
        gtk_widget_set_sensitive(state->power[i],FALSE);canvas->add(state->power[i]);}
    for(auto& field:state->powerOptions){field=createNumericField(0,state->preview?100000:0,1);gtk_editable_set_text(GTK_EDITABLE(field),"—");gtk_widget_set_sensitive(field,FALSE);canvas->add(field);}
    for(int i=0;i<stockCpuProfileCount(deviceProfile(ui::presentationBoard()).id);++i){state->performance[i]=actionButton(performance[i+1],[state,i]{if(state->model.previewStockPower(i+1))renderSettingsPreview(*state);},false,false);canvas->add(state->performance[i]);}
    state->effects[3]=actionButton("Цикл 6",[state]{if(state->model.previewEffect(3))renderSettingsPreview(*state);},false,false);canvas->add(state->effects[3]);
    state->manual=actionButton("Custom",[state]{if(state->model.previewCustomPower())renderSettingsPreview(*state);},false,false);canvas->add(state->manual);
    for(int i=0;i<3;++i){state->customPresets[i]=actionButton(std::to_string(i+1).c_str(),[state,i]{if(state->model.previewCustomPreset(i))renderSettingsPreview(*state);},false,false);setWidgetTooltip(state->customPresets[i],customPresetNames[i]);canvas->add(state->customPresets[i]);}
    for(int i=0;i<3;++i){state->policy[i]=actionButton(state->oem?(i?"Вкл.":"Выкл."):ui::ecPolicyButtonNames[i],{},false,false);canvas->add(state->policy[i]);gtk_widget_set_visible(state->policy[i],ui::showEcPolicyControls&&!state->oem&&i<ui::ecPolicyButtonCount(deviceProfile(ui::presentationBoard()).id));}
    if(ui::showUsbPriorityControls)for(int i=0;i<2;++i){state->usbPriority[i]=actionButton(i?"Performance":"Battery",{},false,false);
        setWidgetTooltip(state->usbPriority[i],"Linux EC-транспорт для приоритета USB-C не подключён.");canvas->add(state->usbPriority[i]);}
    const char* names[]{"Белый","Красный","Оранжевый","Жёлтый","Зелёный","Голубой","Синий","Фиолетовый"};
    for(int i=0;i<8;++i) {
        GtkWidget* button=actionButton("",[state,i]{if(state->model.previewColor(i))renderSettingsPreview(*state);},false,false);
        setWidgetTooltip(button,names[i]);
        GtkWidget* color=gtk_drawing_area_new();
        gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(color),
            +[](GtkDrawingArea*,cairo_t* cr,int width,int height,gpointer p){
                const auto& rgb=*static_cast<const std::array<int,3>*>(p);
                cairo_set_source_rgb(cr,rgb[0]/255.0,rgb[1]/255.0,rgb[2]/255.0);
                cairo_rectangle(cr,0,0,width,height);cairo_fill(cr);
            },const_cast<std::array<int,3>*>(&swatches[i]),nullptr);
        gtk_button_set_child(GTK_BUTTON(button),color);
        state->colors[i]=button; canvas->add(button);
    }
    for(int level=0;level<5;++level){const auto caption=ui::keyboardBrightnessButtonCaption(level);
        state->levels[level]=actionButton(caption.c_str(),[state,level]{if(state->model.previewBrightness(level)){renderSettingsPreview(*state);return;}if(state->oem&&!state->model.busy()&&state->oemState.keyboardWritable&&level<=state->brightnessMaximum)poll(*state,-1,level);},false,false);
        canvas->add(state->levels[level]);}
    state->custom=actionButton("Свой цвет…",[state]{
        GtkRoot* root=gtk_widget_get_root(state->custom);
        GtkWindow* parent=root&&GTK_IS_WINDOW(root)?GTK_WINDOW(root):nullptr;
        GtkColorDialog* dialog=gtk_color_dialog_new();
        setColorDialogTitle(dialog,"Выберите цвет");
        gtk_color_dialog_set_modal(dialog,TRUE);
        gtk_color_dialog_set_with_alpha(dialog,FALSE);
        gtk_color_dialog_choose_rgba(dialog,parent,&state->customColor,nullptr,
            +[](GObject* source,GAsyncResult* result,gpointer data){
                auto* state=static_cast<SetsState*>(data);
                GError* error=nullptr;
                GdkRGBA* selected=gtk_color_dialog_choose_rgba_finish(GTK_COLOR_DIALOG(source),result,&error);
                if(selected) {
                    state->customColor=*selected;
                    g_free(selected);
                    if(state->customSwatch)gtk_widget_queue_draw(state->customSwatch);
                    const auto hex=[](double value){return static_cast<unsigned>(value*255.0+0.5);};
                    char value[8]{};
                    std::snprintf(value,sizeof(value),"#%02X%02X%02X",hex(state->customColor.red),
                        hex(state->customColor.green),hex(state->customColor.blue));
                    state->setStatus(std::string("Выбран ")+value+" · Linux пока не может передать цвет подсветке.");
                    setWidgetTooltip(state->custom,value);
                    if(state->canvas)state->canvas->redraw();
                } else if(error&&!(error->domain==GTK_DIALOG_ERROR&&error->code==GTK_DIALOG_ERROR_DISMISSED)) {
                    g_warning("Color chooser failed: %s",error->message);
                }
                g_clear_error(&error);
            },state);
    },false,true);
    // Build a new label: detaching the button's borrowed child destroys it.
    GtkWidget* label=gtk_label_new("Свой цвет…");
    gtk_label_set_ellipsize(GTK_LABEL(label),PANGO_ELLIPSIZE_END);
    GtkWidget* customContents=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);
    gtk_box_append(GTK_BOX(customContents),label);
    state->customSwatch=gtk_drawing_area_new();
    gtk_widget_set_size_request(state->customSwatch,16,16);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(state->customSwatch),
        +[](GtkDrawingArea*,cairo_t* cr,int width,int height,gpointer data){
            const auto& color=static_cast<SetsState*>(data)->customColor;
            cairo_set_source_rgba(cr,color.red,color.green,color.blue,color.alpha);
            cairo_rectangle(cr,0,0,width,height);cairo_fill(cr);
        },state,nullptr);
    gtk_box_append(GTK_BOX(customContents),state->customSwatch);
    gtk_button_set_child(GTK_BUTTON(state->custom),customContents);
    setWidgetTooltip(state->custom,"Выбрать цвет для предпросмотра");
    state->native=actionButton(ui::keyboardBrightnessDefaultCaption,{},false,false);
    state->keyboardTimeoutCheck=gtk_check_button_new();auditControl(state->keyboardTimeoutCheck);
    state->keyboardIdleReadout=createNumericField(0,600,1);
    gtk_editable_set_editable(GTK_EDITABLE(state->keyboardIdleReadout),FALSE);
    gtk_widget_set_focusable(state->keyboardIdleReadout,FALSE);
    gtk_editable_set_text(GTK_EDITABLE(state->keyboardIdleReadout),"—");
    setNumericArrowWidth(state->keyboardIdleReadout,0);
    canvas->add(state->keyboardIdleReadout);
    setWidgetTooltip(state->keyboardIdleReadout,"Состояние недоступно");
    gtk_widget_add_css_class(state->keyboardTimeoutCheck,"keyboard-auto-off");
    gtk_widget_set_sensitive(state->keyboardTimeoutCheck,FALSE);
    setWidgetTooltip(state->keyboardTimeoutCheck,"Автоматическое выключение подсветки пока недоступно.");
    setWidgetAccessibleLabel(state->keyboardTimeoutCheck,"Автоматическое выключение подсветки");
    canvas->add(state->keyboardTimeoutCheck);
    state->keyboardTimeout=createNumericField(0,3600,1);
    gtk_editable_set_text(GTK_EDITABLE(state->keyboardTimeout),"");
    gtk_widget_set_sensitive(state->keyboardTimeout,FALSE);
    setWidgetTooltip(state->keyboardTimeout,"Настройка таймера EC пока не найдена для этой платы.");
    // Auto-off input handlers removed on every model; backend API/settings stay.
    canvas->add(state->keyboardTimeout);
    gtk_widget_set_visible(state->keyboardTimeoutCheck,FALSE);
    gtk_widget_set_visible(state->keyboardTimeout,FALSE);
    canvas->add(state->custom);canvas->add(state->native);
    g_signal_connect(canvas->widget(),"map",G_CALLBACK(+[](GtkWidget*,gpointer p){auto& state=*static_cast<SetsState*>(p);poll(state);
        if(!state.timer)state.timer=g_timeout_add_seconds(3,+[](gpointer p)->gboolean{poll(*static_cast<SetsState*>(p));return G_SOURCE_CONTINUE;},&state);}),state);
    g_signal_connect(canvas->widget(),"unmap",G_CALLBACK(+[](GtkWidget*,gpointer p){auto& state=*static_cast<SetsState*>(p);if(state.timer){g_source_remove(state.timer);state.timer=0;}}),state);
    GError* busError{};auto* sleepBus=g_bus_get_sync(G_BUS_TYPE_SYSTEM,nullptr,&busError);
    if(sleepBus){state->sleepMonitor=std::make_unique<SettingsSleepMonitor>(sleepBus,[state](bool asleep){
            transitionSettingsSleep(*state,asleep);
        });g_object_unref(sleepBus);}
    if(busError){logMessage("sets_lifecycle",std::string("logind notification unavailable: ")+busError->message);g_error_free(busError);}
    if(state->preview){
        for(auto* child=gtk_widget_get_first_child(canvas->widget());child;child=gtk_widget_get_next_sibling(child))gtk_widget_set_sensitive(child,FALSE);
        for(auto* w:state->performance)if(w)gtk_widget_set_sensitive(w,TRUE);
        for(auto* w:state->customPresets)gtk_widget_set_sensitive(w,TRUE);
        for(auto* w:state->battery)gtk_widget_set_sensitive(w,TRUE);
        for(auto* w:state->voltageButtons)gtk_widget_set_sensitive(w,TRUE);
        for(auto* w:state->agingButtons)gtk_widget_set_sensitive(w,TRUE);
        for(auto* w:state->effects)gtk_widget_set_sensitive(w,TRUE);
        for(auto* w:state->colors)gtk_widget_set_sensitive(w,TRUE);
        for(int i=0;i<5;++i)gtk_widget_set_sensitive(state->levels[i],i<=state->brightnessMaximum);
        gtk_widget_set_sensitive(state->manual,TRUE);renderSettingsPreview(*state);
    }
    syncBatteryControls(*state);return scrolledPage(canvas);
}
#ifdef MAGICCONTROL_GTK_SETTINGS_TEST
bool testSettingsThreadStartFailure(GtkWidget* root){
    // Only a preview-created page may enter this path: it has no OEM backend.
    const auto find=[](const auto& self,GtkWidget* widget)->SetsState*{
        if(auto* found=static_cast<SetsState*>(g_object_get_data(G_OBJECT(widget),"magiccontrol-sets")))return found;
        for(auto* child=gtk_widget_get_first_child(widget);child;child=gtk_widget_get_next_sibling(child))
            if(auto* found=self(self,child))return found;
        return nullptr;
    };
    auto* state=find(find,root);
    if(!state||!state->preview||state->oem)return false;
    BatteryReading battery;battery.present=true;battery.percent=42;battery.dynamicAtMs[0]=123;
    linux_platform::SettingsOperationsTestAccess::seed(state->model,battery);
    const auto beforeSleep=state->batteryResources.view();
    const bool projectionAvailable=beforeSleep.available();
    transitionSettingsSleep(*state,true);
    const bool sleepCleared=!beforeSleep.available()&&!state->batteryResources.view().available();
    transitionSettingsSleep(*state,false);
    const bool resumeStillCleared=!state->batteryResources.view().available();
    linux_platform::SettingsOperationsTestAccess::factory(state->model,[](std::function<void()>)->ResourceWorker::Thread{throw 42;});
    linux_platform::SettingsOperationsTestAccess::preview(state->model,false);
    gtk_widget_set_sensitive(state->restart,TRUE);
    poll(*state,85);
    const auto startNotice=state->osd?state->osd->testMessage():std::nullopt;
    const bool ok=projectionAvailable&&sleepCleared&&resumeStillCleared&&
        !state->model.busy()&&gtk_widget_get_sensitive(state->restart)&&
        state->colorStatus=="Не удалось запустить обработчик SETs."&&startNotice&&startNotice->error&&!startNotice->pending&&
        startNotice->detail=="Не удалось запустить обработчик SETs.";
    // Exercise an exception outside the stage handlers on the actual worker.
    // The preview-created page has no OEM backend, and the throw occurs before
    // any reader or writer is called.
    linux_platform::SettingsOperationsTestAccess::factory(state->model,{});bool workerRecovered=true;
    for(int fault:{1,2}){
        linux_platform::SettingsOperationsTestAccess::fault(state->model,fault);
        linux_platform::SettingsOperationsTestAccess::seed(state->model,battery);
        gtk_widget_set_sensitive(state->battery[0],TRUE);gtk_widget_set_sensitive(state->levels[0],TRUE);
        poll(*state,85);
        const auto deadline=g_get_monotonic_time()+3*G_TIME_SPAN_SECOND;
        while(state->model.busy()&&g_get_monotonic_time()<deadline){
            while(g_main_context_iteration(nullptr,FALSE)){}g_usleep(1000);
        }
        const auto notice=state->osd?state->osd->testMessage():std::nullopt;
        workerRecovered=workerRecovered&&!state->model.busy()&&state->reading.dynamicStale&&
            !state->batteryResources.view().available()&&!gtk_widget_get_sensitive(state->battery[0])&&
            !gtk_widget_get_sensitive(state->levels[0])&&state->colorStatus=="Ошибка SETs: состояние не подтверждено."&&
            notice&&notice->error&&!notice->pending&&notice->detail=="Ошибка SETs: состояние не подтверждено.";
        state->model.shutdown();
    }
    linux_platform::SettingsOperationsTestAccess::fault(state->model,0);
    linux_platform::SettingsOperationsTestAccess::preview(state->model,true);
    return ok&&workerRecovered;
}
#endif
} // namespace mc::gtkui
