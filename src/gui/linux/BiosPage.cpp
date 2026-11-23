#include "MagicControl/ActionLog.h"
#include "Pages.h"
#include "OsdService.h"
#include "../common/DiagnosticDumpOsd.h"
#include "../common/DiagnosticPrompts.h"
#include "MagicControl/PowerInputDump.h"
#include "MagicControl/InfoResourceControl.h"
#include "MagicControl/OperationMessages.h"
#include "Ui.h"
#include "../common/DeviceHeader.h"
#include "../common/BiosActions.h"
#include "../common/InventoryPresentation.h"

#include <array>
#include <string>
#include <vector>
#include <memory>
#include "MagicControl/DiagnosticDump.h"
#include "MagicControl/VbiosRomDump.h"
#include "MagicControl/Logging.h"

#include "MagicControl/BiosSupport.h"
#include "MagicControl/HardwareInventory.h"
#include "MagicControl/RomIdHistory.h"

namespace mc::gtkui {
namespace {
struct BiosState {
    OsdService* osd{};
    mc::BiosSupportLinks links{mc::biosSupportLinks(ui::presentationBoard())};
    mc::HardwareInventory inventory;
    const bool uniwillIdentity=uniwillIdentityHistoryProfile(deviceProfile(readBoardDescriptor()));
    mc::HardwareInventory information;
    Canvas* header{};
    Canvas* contents{};
    GtkWidget* scroller{};
    std::array<GtkWidget*,ui::kBiosActionCount> buttons{};
    std::shared_ptr<InfoOperation> job;
    std::shared_ptr<PowerInputDumpSession> powerSession;
    guint timer{};std::uint64_t shownProgress{};
    std::array<bool,ui::kBiosActionCount> enabled{};
    std::string snapshot;
    std::string update{ui::kPawnIoLinuxNote};
    std::vector<ui::InventoryCardGeometry> cards;
    int cachedWidth{-1}, cachedDpi{}, extent{};
    unsigned cachedLanguage{~0u};std::string cachedSystemFont;
    ~BiosState(){if(job)job->releaseHardware();if(timer)g_source_remove(timer);}
};
void refreshVbiosButton(BiosState& state){
    const bool active=state.job&&state.job->operation()==InfoResourceOperation::VbiosDump;
    gtk_button_set_label(GTK_BUTTON(state.buttons[7]),ui::translate(active?"Отмена VBIOS":ui::kVbiosRomDumpCaption).c_str());
    gtk_widget_set_sensitive(state.buttons[7],active||(!state.job&&ui::biosDeviceActionsAllowed()&&vbiosRomDumpAvailable()));
    gtk_widget_set_tooltip_text(state.buttons[7],ui::translate(ui::kVbiosRomDumpNote).c_str());
}
gboolean pollInfo(gpointer data){
    auto* s=static_cast<BiosState*>(data);const auto job=s->job;
    if(!job){s->timer=0;return G_SOURCE_REMOVE;}
    if(const auto prompt=job->prompts.take()){
        job->messages()->append(mc::gui::diagnosticPromptText(prompt->instructions,prompt->reason));
        const auto parent=GTK_WINDOW(gtk_widget_get_root(s->header->widget()));auto session=s->powerSession;g_object_ref(parent);
        const auto choice=showChoiceDialog(parent,mc::gui::diagnosticPromptText(prompt->instructions,prompt->reason),{"Снять состояние","Отложить"});
        const bool accepted=choice==0&&gtk_widget_get_visible(GTK_WIDGET(parent));
        if(accepted&&session){try{session->displaySnapshot=captureDisplayEvidence(GTK_WIDGET(parent));}catch(...){session->displaySnapshot.clear();}}
        g_object_unref(parent);job->prompts.reply(accepted);return G_SOURCE_CONTINUE;
    }
    const auto* result=job->poll();
    if(!result){
        if(job->firmwareDump()){
            const auto n=job->progress();if(n!=s->shownProgress){s->shownProgress=n;s->update="NVIDIA ROM: прочитано "+std::to_string(n)+" байт.";gtk_widget_queue_resize(s->header->widget());s->header->redraw();}
        }
        return G_SOURCE_CONTINUE;
    }
    if(job->inventory()){
        s->snapshot=result->message;
        if(result->success){s->inventory=result->inventory;s->information=ui::informationInventory(s->inventory,s->uniwillIdentity);s->cachedWidth=-1;}
        for(int i=0;i<ui::kBiosActionCount;++i)gtk_widget_set_sensitive(s->buttons[i],s->enabled[i]);
        gtk_widget_queue_resize(s->contents->widget());s->contents->redraw();
    }else{
        s->update=result->message;
        if(s->osd&&!job->firmwareDump())s->osd->show(mc::gui::osd::diagnosticDumpFinished(result->dump));
        gtk_widget_set_sensitive(s->buttons[0],TRUE);gtk_widget_set_sensitive(s->buttons[5],ui::biosDeviceActionsAllowed());
    }
    s->job.reset();s->timer=0;refreshVbiosButton(*s);
    gtk_widget_queue_resize(s->header->widget());s->header->redraw();return G_SOURCE_REMOVE;
}
void startInfo(BiosState& state,InfoOperationRequest request,const char* title){
    request.auditId=audit::capture();const auto job=InfoOperation::start(std::move(request));state.job=job;state.shownProgress=0;
    showOperationDialog(GTK_WINDOW(gtk_widget_get_root(state.header->widget())),title,job->messages(),
        [weak=std::weak_ptr<InfoOperation>(job)]{if(auto p=weak.lock())p->cancel();});
    if(job->inventory())for(int i=0;i<ui::kBiosActionCount;++i){state.enabled[i]=gtk_widget_get_sensitive(state.buttons[i]);gtk_widget_set_sensitive(state.buttons[i],FALSE);}
    else{gtk_widget_set_sensitive(state.buttons[0],FALSE);gtk_widget_set_sensitive(state.buttons[5],FALSE);}
    refreshVbiosButton(state);state.timer=g_timeout_add(50,pollInfo,&state);
    gtk_widget_queue_resize(state.header->widget());state.header->redraw();
}
ui::BiosHeaderGeometry headerGeometry(BiosState& state,Canvas& c,int width,int height) {
    const ui::Scale s{c.dpi()};
    const int inner=width-s(48);
    std::array<int,5> actionWidths{};
    for(std::size_t i=0;i<actionWidths.size();++i)
        actionWidths[i]=textWidth(gtk_button_get_label(GTK_BUTTON(state.buttons[ui::kBiosPrimaryActions[i]])),Font::ClassicBody,c.dpi());
    return ui::biosHeaderGeometry(width,height,
        textHeight(state.snapshot,Font::ClassicBody,inner,c.dpi()),
        textHeight(state.update,Font::ClassicBody,inner,c.dpi()),s,actionWidths);
}
void openUrl(BiosState& state,const wchar_t* value) {
    std::string url;
    for(;*value;++value)url.push_back(static_cast<char>(*value));
    GtkUriLauncher* launcher=gtk_uri_launcher_new(url.c_str());
    gtk_uri_launcher_launch(launcher,GTK_WINDOW(gtk_widget_get_root(state.header->widget())),
                            nullptr,nullptr,nullptr);
    g_object_unref(launcher);
}
int inventoryLayout(BiosState& state,Canvas& c,int width) {
    const ui::Scale s{c.dpi()};
    const int cardWidth=std::max(1,width-s(12));
    const std::string systemFont=ui::selectedFontFamily(static_cast<ui::Language>(ui::languageIndex()));
    if(state.cachedWidth==cardWidth && state.cachedDpi==c.dpi() &&
       state.cachedLanguage==ui::languageIndex() && state.cachedSystemFont==systemFont)return state.extent;
    state.cachedWidth=cardWidth;state.cachedDpi=c.dpi();state.cachedLanguage=ui::languageIndex();
    state.cachedSystemFont=systemFont;state.cards.clear();
    const int inner=cardWidth-s(24),column=inner*28/100;
    int y=0;
    for(const auto& section:state.information.sections) {
        std::vector<int> rows;
        for(const auto& row:section.rows)
            rows.push_back(std::max(textHeight(row.name,Font::ClassicBody,column-s(12),c.dpi()),
                textHeight(row.value,Font::ClassicBody,inner-column,c.dpi())));
        auto card=ui::inventoryCardGeometry(cardWidth,y,
            textHeight(section.title,Font::ClassicHeading,inner,c.dpi()),rows,s);
        y=card.bounds.bottom+s(6);state.cards.push_back(std::move(card));
    }
    state.extent=std::max(0,y-s(6));return state.extent;
}
}

void refreshBiosProfile(GtkWidget* page) {
    auto* state = static_cast<BiosState*>(g_object_get_data(G_OBJECT(page), "magiccontrol-bios"));
    if (!state) return;
    state->links = mc::biosSupportLinks(ui::presentationBoard());
    gtk_button_set_label(GTK_BUTTON(state->buttons[2]), state->links.supportCaption);
    gtk_widget_set_sensitive(state->buttons[1], FALSE);
    gtk_widget_set_sensitive(state->buttons[2], state->links.supportUrl != nullptr);
    gtk_widget_set_sensitive(state->buttons[5],!state->job&&ui::biosDeviceActionsAllowed());
    refreshVbiosButton(*state);
    state->update = ui::kPawnIoLinuxNote;
    if (ui::laptopPreview()) state->update = "Просмотр: ссылки выбранной модели; сведения ниже относятся к текущему компьютеру. " + state->update;
    gtk_widget_queue_resize(state->header->widget()); state->header->redraw();
}

GtkWidget* createBiosPage(OsdService* osd) {
    auto* state=new BiosState();state->osd=osd;
    if(auto cached=InventoryCache::load()){
        state->inventory=std::move(*cached);state->information=ui::informationInventory(state->inventory,state->uniwillIdentity);
        state->snapshot="Сохранённые сведения от "+state->inventory.readAt+". Обновление — только по кнопке.";
    }else state->snapshot="Нажмите «Обновить сведения», чтобы прочитать данные ноутбука.";
    state->contents=Canvas::create(
        [state](Canvas& c,int width,int){return inventoryLayout(*state,c,width);},
        [state](Canvas& c,cairo_t* cr,int width,int height){
            fill(cr,{0,0,width,height},theme::kBackground);inventoryLayout(*state,c,width);
            for(std::size_t i=0;i<state->cards.size();++i) {
                const auto& g=state->cards[i];const auto& section=state->information.sections[i];
                fill(cr,g.bounds,theme::kSurface);
                text(cr,section.title,g.heading,Font::ClassicHeading,theme::kTextPrimary,
                     c.dpi(),Horizontal::Left,Vertical::Top,true);
                for(std::size_t j=0;j<section.rows.size();++j) {
                    text(cr,section.rows[j].name,g.captions[j],Font::ClassicBody,theme::kTextSecondary,
                         c.dpi(),Horizontal::Left,Vertical::Top,true);
                    text(cr,section.rows[j].value,g.values[j],Font::ClassicBody,theme::kTextPrimary,
                         c.dpi(),Horizontal::Left,Vertical::Top,true);
                }
            }
        });
    state->scroller=scrolledPage(state->contents);
    state->header=Canvas::create(
        [state](Canvas& c,int width,int height){
            refreshVbiosButton(*state);
            const ui::Scale s{c.dpi()};
            const auto g=headerGeometry(*state,c,width,height);
            for(std::size_t i=0;i<ui::kBiosActionCount;++i)c.place(state->buttons[i],g.buttons[i]);
            auto viewport=g.inventory;viewport.right+=s(12);
            c.place(state->scroller,viewport);
            return 0;
        },
        [state](Canvas& c,cairo_t* cr,int width,int height){
            const auto g=headerGeometry(*state,c,width,height);
            fill(cr,{0,0,width,height},theme::kBackground);fill(cr,g.card,theme::kSurface);
            text(cr,ui::kInformationCaption,g.heading,Font::ClassicHeading,theme::kTextPrimary,c.dpi());
            text(cr,state->snapshot,g.snapshot,Font::ClassicBody,theme::kTextSecondary,
                 c.dpi(),Horizontal::Left,Vertical::Top,true);
            text(cr,state->update,g.update,Font::ClassicBody,theme::kTextSecondary,
                 c.dpi(),Horizontal::Left,Vertical::Top,true);
        },false);
    g_object_set_data_full(G_OBJECT(state->header->widget()),"magiccontrol-bios",state,
        [](gpointer p){delete static_cast<BiosState*>(p);});
    state->buttons[0]=actionButton("Обновить сведения",[state]{
        if(state->job)return;
        InfoOperationRequest request;request.operation=InfoResourceOperation::Inventory;
        startInfo(*state,std::move(request),"MagicControl · Сведения");
    });
    state->buttons[1]=actionButton(ui::kPawnIoInstallCaption,[]{},false,false);
    state->buttons[2]=actionButton(state->links.supportCaption,[state]{if(state->links.supportUrl)openUrl(*state,state->links.supportUrl);},false,state->links.supportUrl!=nullptr);
    state->buttons[3]=actionButton("Nahimic · обновления",[state]{openUrl(*state,mc::kNahimicUrl);});
    state->buttons[4]=actionButton("Копировать отчёт",[state]{
        const auto report=ui::informationReport(state->inventory,"\n",state->uniwillIdentity);
        gdk_clipboard_set_text(gdk_display_get_clipboard(gtk_widget_get_display(state->header->widget())),report.c_str());
    });
    state->buttons[5]=actionButton(ui::kDiagnosticDumpCaption,[state]{
        if(state->job||!ui::biosDeviceActionsAllowed())return;
        bool guided=false;
        if(powerInputDumpSupported()){
            const auto choice=showChoiceDialog(GTK_WINDOW(gtk_widget_get_root(state->header->widget())),ui::translate(mc::gui::powerInputChoiceText),{"Обычный дамп","Проверка БП","Отмена"});
            if(choice<0||choice==2)return;guided=choice==1;
            if(guided&&(!state->powerSession||state->powerSession->nextStage==3))state->powerSession=std::make_shared<PowerInputDumpSession>();
        }
        const auto dumpMode=guided?DiagnosticDumpMode::ReadOnly:ui::biosFanProbesAllowed()?DiagnosticDumpMode::WithFanProbes:DiagnosticDumpMode::ReadOnly;
        // GDK belongs to this GTK callback, never to the background dump worker.
        // This records the actual desktop, independently of model preview.
        std::string displaySnapshot;
        try{displaySnapshot=captureDisplayEvidence(state->header->widget());}catch(const std::exception& error){mc::logMessage("diagnostic_dump",std::string("GDK display snapshot failed: ")+error.what());}
        state->update=dumpMode==DiagnosticDumpMode::ReadOnly?"Дамп и минимум 60 сек датчиков без проб вентиляторов в logs_<плата>/ec-dumps…":"Дамп и минимум 60 сек датчиков с доступными пробами Max по 30 сек в logs_<плата>/ec-dumps…";
        InfoOperationRequest request;request.operation=guided?InfoResourceOperation::PowerInputDump:InfoResourceOperation::EcDump;
        request.dump.mode=dumpMode;request.dump.displaySnapshot=std::move(displaySnapshot);request.powerSession=state->powerSession;
        startInfo(*state,std::move(request),"MagicControl · Дамп EC");
        if(state->osd)state->osd->show(mc::gui::osd::diagnosticDumpStarted());
    },false,ui::biosDeviceActionsAllowed());
    state->buttons[6]=actionButton(ui::kBiosRomDumpCaption,[]{},false,false);
    gtk_widget_set_tooltip_text(state->buttons[6],ui::translate("Системный BIOS: совместимые ридеры подключены в Windows. Linux SPI backend пока не подключён.").c_str());
    state->buttons[7]=actionButton(ui::kVbiosRomDumpCaption,[state]{
        if(state->job){if(state->job->operation()==InfoResourceOperation::VbiosDump)state->job->cancel();return;}
        if(!ui::biosDeviceActionsAllowed()||!vbiosRomDumpAvailable())return;
        const auto choice=showChoiceDialog(GTK_WINDOW(gtk_widget_get_root(state->header->widget())),
            ui::translate(ui::kVbiosDumpChoice),{ui::translate(ui::kVbiosRomDumpCaption),ui::translate(ui::kEepromDumpCaption),ui::translate("Отмена")});
        if(choice!=0&&choice!=1)return;
        const auto mode=choice==1?VbiosDumpMode::EntireEeprom:VbiosDumpMode::Image;
        std::string target;
        try{const auto adapters=vbiosDumpAdapters();
            if(adapters.size()>1){std::vector<std::string> choices;for(const auto& a:adapters)choices.push_back(a.name+" · "+vbiosAdapterPci(a));
                const auto selected=showChoiceDialog(GTK_WINDOW(gtk_widget_get_root(state->header->widget())),ui::translate("Выберите NVIDIA для дампа VBIOS"),choices);
                if(selected<0||static_cast<std::size_t>(selected)>=adapters.size())return;target=vbiosAdapterPci(adapters[static_cast<std::size_t>(selected)]);
            }else if(adapters.size()==1)target=vbiosAdapterPci(adapters.front());
        }catch(const std::exception& e){state->update=e.what();gtk_widget_queue_resize(state->header->widget());state->header->redraw();return;}
        state->update=choice==1?"NVIDIA: полный EEPROM. Проверяю ёмкость перед чтением.":"NVIDIA: обычный VBIOS, два чтения.";
        InfoOperationRequest request;request.operation=InfoResourceOperation::VbiosDump;request.dump.pci=std::move(target);request.dump.vbiosMode=mode;
        startInfo(*state,std::move(request),choice==1?"MagicControl · EEPROM":"MagicControl · VBIOS");
    },false,ui::biosDeviceActionsAllowed()&&vbiosRomDumpAvailable());
    gtk_widget_set_tooltip_text(state->buttons[7],ui::translate(ui::kVbiosRomDumpNote).c_str());
    for(const int i:ui::kBiosActionTabOrder)state->header->add(state->buttons[i]);
    state->header->add(state->scroller);
    gtk_widget_set_vexpand(state->header->widget(),TRUE);
    refreshBiosProfile(state->header->widget());
    return state->header->widget();
}
}  // namespace mc::gtkui
