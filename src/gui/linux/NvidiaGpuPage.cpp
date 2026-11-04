#include "MagicControl/ActionLog.h"
#include "MagicControl/DisplayContinuity.h"
#include "MagicControl/ControlAudit.h"
#include "Pages.h"
#include "OsdService.h"
#include "../common/OsdMessages.h"
#include "Ui.h"
#include "NumericField.h"
#include "MagicControl/GpuDevice.h"
#include "MagicControl/Logging.h"
#include "../common/DiscreteGpuCard.h"
#include "../common/DeviceHeader.h"
#include "../../platform/common/DiscreteGpuOperation.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>
namespace mc::gtkui {
namespace {
struct NvidiaState : GpuOperations {
    OsdService* osd{};std::uint64_t osdToken{};mc::gui::osd::Message osdNotice;
    explicit NvidiaState(GpuVendor v):GpuOperations(v){}
    Canvas* canvas{};
    struct Controls {std::string key;GtkWidget *field{},*apply{},*reset{};bool dirty{};};std::vector<Controls> controls;
    bool syncing{};guint timer{};
    struct Life {NvidiaState* owner;};std::shared_ptr<Life> life{std::make_shared<Life>(Life{this})};
    ~NvidiaState(){life->owner=nullptr;if(timer)g_source_remove(timer);shutdown();}
};
void start(NvidiaState&,std::string = {},std::optional<int> = {},bool = false);
void availability(NvidiaState& s){s.syncing=true;for(std::size_t i=0;i<s.controls.size();++i){auto& c=s.controls[i];const auto& r=s.readings[i];const bool ready=r.available&&r.powerControl&&!s.applying&&!deviceOperations().interactiveBusy();
    gtk_widget_set_sensitive(c.field,ready&&!ui::laptopPreview());gtk_widget_set_sensitive(c.apply,ready&&!s.busy&&!ui::laptopPreview());gtk_widget_set_sensitive(c.reset,ready&&!s.busy&&gpuPowerDefaultKnown(r)&&!ui::laptopPreview());
    if(gpuPowerRangeKnown(r))gtk_spin_button_set_range(GTK_SPIN_BUTTON(c.field),gpuPowerMinimum(r),gpuPowerMaximum(r));
    const int step=std::max(1,r.powerStep.value_or(1));gtk_spin_button_set_increments(GTK_SPIN_BUTTON(c.field),step,step*10);
    auto* root=gtk_widget_get_root(c.field);auto* focused=root?gtk_root_get_focus(root):nullptr;
    const bool editing=focused&&(focused==c.field||gtk_widget_is_ancestor(focused,c.field));
    if(!c.dirty&&!editing&&gpuPowerKnown(r))gtk_spin_button_set_value(GTK_SPIN_BUTTON(c.field),gpuPowerValue(r));
    else if(!gpuPowerKnown(r))gtk_editable_set_text(GTK_EDITABLE(c.field),"—");
}s.syncing=false;}
void rebuild(NvidiaState& s){bool changed=s.controls.size()!=s.readings.size();if(!changed)for(std::size_t i=0;i<s.controls.size();++i)changed|=s.controls[i].key!=(s.readings[i].uuid.empty()?"unavailable:"+std::to_string(i):s.readings[i].uuid);
    if(!changed)return;
    // Keep child objects alive until Canvas destruction; hide retired controls.
    // Hot-plug is rare and this avoids invalidating Canvas's child references.
    for(auto& c:s.controls)for(auto* h:{c.field,c.apply,c.reset})gtk_widget_set_visible(h,FALSE);s.controls.clear();
    for(std::size_t i=0;i<s.readings.size();++i){NvidiaState::Controls c;c.key=s.readings[i].uuid.empty()?"unavailable:"+std::to_string(i):s.readings[i].uuid;
        const auto id=c.key;c.field=createNumericField(0,100000,1);gtk_spin_button_set_digits(GTK_SPIN_BUTTON(c.field),0);
        c.apply=actionButton("Применить",[&s,id]{for(std::size_t n=0;n<s.controls.size();++n)if(s.controls[n].key==id){gtk_spin_button_update(GTK_SPIN_BUTTON(s.controls[n].field));const auto watts=gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(s.controls[n].field));start(s,s.readings[n].uuid,watts);break;}},false,false);
        c.reset=actionButton("Штатный лимит",[&s,id]{for(std::size_t n=0;n<s.controls.size();++n)if(s.controls[n].key==id){start(s,s.readings[n].uuid,{},true);break;}},false,false);
        g_signal_connect(c.field,"value-changed",G_CALLBACK(+[](GtkSpinButton* field,gpointer data){auto& state=*static_cast<NvidiaState*>(data);if(state.syncing)return;for(auto& c:state.controls)if(c.field==GTK_WIDGET(field))c.dirty=true;}),&s);
        s.canvas->add(c.field);s.canvas->add(c.apply);s.canvas->add(c.reset);s.controls.push_back(c);
    }
}
void start(NvidiaState& s,std::string uuid,std::optional<int> watts,bool reset){
    if(!uuid.empty()&&ui::laptopPreview())return;
    if(s.busy)return;
    if(!uuid.empty()&&s.osd){std::string device;for(const auto& r:s.readings)if(r.uuid==uuid)device=r.name;
        s.osdNotice=mc::gui::osd::gpuPower(uuid,gpuVendorName(s.vendor),device,watts,reset);s.osdToken=s.osd->beginOperation(s.osdNotice);}
    std::optional<std::vector<GpuReading>> preview;
    if(s.vendor==GpuVendor::Nvidia&&ui::nvidiaModelPreview())preview=ui::nvidiaPreviewCard();
    const bool action=!uuid.empty();
    s.submit({action?(reset?GpuOperation::ResetPower:GpuOperation::SetPower):GpuOperation::Read,std::move(uuid),watts},ui::laptopPreview(),std::move(preview));
    availability(s);
}
void finish(NvidiaState& state){
    std::optional<std::vector<GpuReading>> preview;
    if(state.vendor==GpuVendor::Nvidia&&ui::nvidiaModelPreview())preview=ui::nvidiaPreviewCard();
    auto result=state.take(std::move(preview));if(!result)return;
    if(result->action&&result->success)for(auto& c:state.controls)if(c.key==result->uuid)c.dirty=false;
    if(result->action&&state.osd&&state.osdToken){auto notice=state.osdNotice;notice.detail=result->message;notice.error=!result->success;state.osd->completeOperation(state.osdToken,std::move(notice));state.osdToken=0;}
    rebuild(state);availability(state);state.canvas->redraw();
}
}
GtkWidget* createDiscreteGpuPage(GpuVendor vendor,OsdService* osd){auto* state=new NvidiaState(vendor);state->osd=osd;
    auto* canvas=Canvas::create([state](Canvas& c,int width,int){const ui::Scale s{c.dpi()};
        for(std::size_t i=0;i<state->controls.size();++i){const auto g=ui::gpuCardGeometry(width,static_cast<int>(i),s);auto& control=state->controls[i];auto field=g.edit;field.right=g.spin.right;c.place(control.field,field);setNumericArrowWidth(control.field,g.spin.width());c.place(control.apply,g.apply);c.place(control.reset,g.reset);}
        return s(12+static_cast<int>(std::max<std::size_t>(1,state->readings.size()))*(ui::kGpuCardHeight+6));
    },[state](Canvas& c,cairo_t* cr,int width,int height){const ui::Scale s{c.dpi()};fill(cr,{0,0,width,height},theme::kBackground);
        if(state->readings.empty())text(cr,std::string("Читаю данные ")+gpuVendorName(state->vendor)+"…",{s(20),s(20),width,s(60)},Font::Body,theme::kTextSecondary,c.dpi());
        for(std::size_t i=0;i<state->readings.size();++i){const auto& r=state->readings[i];const auto g=ui::gpuCardGeometry(width,static_cast<int>(i),s);fill(cr,g.card,theme::kSurface);text(cr,r.name,g.title,Font::Heading,theme::kTextPrimary,c.dpi());
            const auto metrics=ui::gpuMetrics(r);for(int j=0;j<4;++j){auto box=g.metrics[j];box.bottom=box.top+s(30);text(cr,metrics[j],box,Font::FanMetric,theme::kTextPrimary,c.dpi(),Horizontal::Center);
                box.top=box.bottom;box.bottom=g.metrics[j].bottom;text(cr,ui::gpuMetricCaptions[j],box,Font::Body,theme::kTextSecondary,c.dpi(),Horizontal::Center);}
            const auto details=ui::gpuDetails(r);for(int j=0;j<5;++j)text(cr,details[j],g.details[j],Font::Body,theme::kTextSecondary,c.dpi());
            text(cr,ui::gpuPowerCaption(r),g.powerLabel,Font::Body,theme::kTextSecondary,c.dpi());
            const bool staticPreview=state->vendor==GpuVendor::Nvidia&&ui::nvidiaModelPreview();
            const auto status=ui::translate(state->message.empty()||staticPreview?r.status:state->message);
            text(cr,(ui::laptopPreview()&&!staticPreview?ui::translate("Просмотр · GPU текущего компьютера.")+" ":"")+status+"\n"+r.uuid,g.status,Font::Body,theme::kTextMuted,c.dpi(),Horizontal::Left,Vertical::Top,true);
        }
    });state->canvas=canvas;
    state->bindReady([life=state->life]{
        auto* hold=new std::shared_ptr<NvidiaState::Life>(life);
        g_idle_add_full(G_PRIORITY_DEFAULT,+[](gpointer data)->gboolean{const auto& life=*static_cast<std::shared_ptr<NvidiaState::Life>*>(data);if(life->owner)finish(*life->owner);return G_SOURCE_REMOVE;},hold,+[](gpointer data){delete static_cast<std::shared_ptr<NvidiaState::Life>*>(data);});
    });
    g_object_set_data_full(G_OBJECT(canvas->widget()),"magiccontrol-nvidia",state,[](gpointer p){delete static_cast<NvidiaState*>(p);});
    state->timer=g_timeout_add(1000,+[](gpointer p)->gboolean{auto& s=*static_cast<NvidiaState*>(p);if(gtk_widget_get_mapped(s.canvas->widget())&&!s.busy)start(s);return G_SOURCE_CONTINUE;},state);
    return scrolledPage(canvas);
}
GtkWidget* createNvidiaGpuPage(OsdService* osd){return createDiscreteGpuPage(GpuVendor::Nvidia,osd);}
GtkWidget* createRadeonGpuPage(OsdService* osd){return createDiscreteGpuPage(GpuVendor::Radeon,osd);}

}
