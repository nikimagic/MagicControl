#include "Pages.h"
#include "../common/FooterStatus.h"
#include "../common/DeviceHeader.h"
#include "Ui.h"
#include "OsdService.h"
#include "../common/OsdMessages.h"
#include "MagicControl/PortableSettings.h"
#include "../../platform/linux/KeysControlLinux.h"
#include "../common/KeysPolicy.h"
#include <array>
#include <memory>
#include "MagicControl/Logging.h"

namespace mc::gtkui {
namespace {
struct KeysState {
    bool preview{ui::laptopPreview()};
    std::array<GtkWidget*,ui::keyRowCount> off{},on{};
    OsdService* osd{};
    Canvas* canvas{};
    mc::ApplicationPreferences preferences;
    std::string result{"Читаю текущие состояния…"};
    std::string microphoneDetail{"Микрофон по умолчанию · PulseAudio/PipeWire"};
    void setStatus(std::string value){if(value==result)return;result=std::move(value);ui::footerStatuses().publish("keys",result);}
    std::unique_ptr<KeysOperations> operations;guint timer{};
    GDBusConnection* sleepBus{};guint sleepSubscription{};
    struct Life {KeysState* owner;};std::shared_ptr<Life> life{std::make_shared<Life>(Life{this})};
    ~KeysState(){life->owner=nullptr;if(sleepSubscription)g_dbus_connection_signal_unsubscribe(sleepBus,sleepSubscription);if(sleepBus)g_object_unref(sleepBus);operations.reset();if(timer)g_source_remove(timer);}
};
void updateKeysButtons(KeysState& state){
    for(int row=0;row<static_cast<int>(ui::keyRowCount);++row)
        for(auto* button:{state.off[row],state.on[row]})gtk_widget_set_sensitive(button,!state.operations->disabled(row));
}
void presentKeys(KeysState& state,const KeyOperationResult& result){
    for(std::size_t row=0;row<ui::keyRowCount;++row){
        gtk_widget_remove_css_class(state.off[row],"primary");gtk_widget_remove_css_class(state.on[row],"primary");
        if(result.snapshot.value[row]>=0)gtk_widget_add_css_class(result.snapshot.value[row]?state.on[row]:state.off[row],"primary");
    }
    state.microphoneDetail=result.snapshot.detail[ui::microphoneRow];
    if(!result.message.empty())state.setStatus(result.message);
    if(result.command&&state.osd&&result.command->noticeToken){
        auto message=mc::gui::osd::keySwitch(static_cast<int>(result.command->action),result.command->enabled);
        message.detail=result.noticeDetail;message.error=!result.success;
        state.osd->completeOperation(result.command->noticeToken,std::move(message));
    }
    if(result.announceOsdEnabled&&state.osd)state.osd->show({"osd","Экранные уведомления","Включены",mc::gui::osd::Icon::Information});
    updateKeysButtons(state);state.canvas->redraw();
}
void completeKeys(KeysState& state){while(const auto result=state.operations->take())presentKeys(state,*result);}
void pollKeys(KeysState& state,std::optional<std::pair<int,bool>> apply={}){
    if(apply){
        const auto submitted=state.operations->submit(apply->first,apply->second);
        if(submitted.completed)presentKeys(state,*submitted.completed);
        else if(!submitted.message.empty())state.setStatus(submitted.message);
    }else state.operations->refresh();
    updateKeysButtons(state);state.canvas->redraw();
}
const char* details[]{
    "Linux-обработчик блокировки ещё не подключён.","Отключает наши экранные уведомления. Выбор сохраняется в settings.",
    "Меняет основное и дополнительное действие F1–F12.","Управление EC в Linux пока недоступно.",
    "Запуск при подключении адаптера. После изменения нужна перезагрузка.",
    "Linux-обработчик блокировки ещё не подключён.","Адаптер NetworkManager ещё не подключён.",
    "Адаптер BlueZ ещё не подключён.","Адаптер V4L2 ещё не подключён.","Адаптер libinput ещё не подключён.","Микрофон по умолчанию · PulseAudio/PipeWire"
};
void selectOsd(KeysState& state){
    gtk_widget_remove_css_class(state.off[1],"primary");gtk_widget_remove_css_class(state.on[1],"primary");
    gtk_widget_add_css_class(state.preferences.osdDisabled?state.on[1]:state.off[1],"primary");
}

}
GtkWidget* createKeysPage(OsdService* osd){
    auto* state=new KeysState();state->osd=osd;
    if(state->preview)state->result="Системные функции текущего компьютера; EC профиля не используется.";
    auto* canvas=Canvas::create([state](Canvas& c,int width,int height){
        const auto g=ui::keysGeometry(width,height,{c.dpi()});
        for(std::size_t i=0;i<ui::keyRowCount;++i){c.place(state->off[i],g.off[i]);c.place(state->on[i],g.on[i]);}
        return g.extent;
    },[state](Canvas& c,cairo_t* cr,int width,int height){
        const int dpi=c.dpi();const auto g=ui::keysGeometry(width,height,{dpi});
        fill(cr,{0,0,width,height},theme::kBackground);fill(cr,g.quickCard,theme::kSurface);fill(cr,g.devicesCard,theme::kSurface);
        text(cr,"Быстрое переключение",g.quickHeading,Font::ClassicHeading,theme::kTextPrimary,dpi);
        text(cr,"Устройства Linux",g.devicesHeading,Font::ClassicHeading,theme::kTextPrimary,dpi);
        for(std::size_t i=0;i<ui::keyRowCount;++i){
            text(cr,i==0?"Блокировка клавиши Super":ui::keyTitles[i],g.titles[i],Font::KeyTitle,theme::kTextPrimary,dpi);
            text(cr,i==ui::microphoneRow?state->microphoneDetail:details[i],g.details[i],Font::KeyDetail,theme::kTextSecondary,dpi);
        }
        text(cr,state->result,g.result,Font::ClassicBody,theme::kTextSecondary,dpi);
    });
    state->canvas=canvas;
    g_object_set_data_full(G_OBJECT(canvas->widget()),"magiccontrol-keys",state,[](gpointer p){delete static_cast<KeysState*>(p);});
    for(int i=0;i<static_cast<int>(ui::keyRowCount);++i){
        const bool systemMic=i==ui::microphoneRow;
        const auto off=(i==1||i==2||systemMic)?std::function<void()>([state,i]{pollKeys(*state,std::pair{i,false});}):std::function<void()>{};
        const auto on=(i==1||i==2||systemMic)?std::function<void()>([state,i]{pollKeys(*state,std::pair{i,true});}):std::function<void()>{};
        state->off[i]=actionButton("Выкл.",off,false,i==1);
        state->on[i]=actionButton("Вкл.",on,false,i==1);
        canvas->add(state->off[i]);canvas->add(state->on[i]);
    }
    state->operations=linux_platform::createKeysOperations(state->preview,state->preferences,
        [state](const ApplicationPreferences& next){state->preferences=next;if(state->osd)state->osd->setEnabled(!next.osdDisabled);},
        [state](KeyAction action,bool on){return state->osd?state->osd->beginOperation(mc::gui::osd::keySwitch(static_cast<int>(action),on)):0;},
        [life=state->life]{
            auto* delivery=new std::shared_ptr<KeysState::Life>(life);
            g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,+[](gpointer data)->gboolean{
                const auto& life=*static_cast<std::shared_ptr<KeysState::Life>*>(data);
                if(auto* owner=life->owner)completeKeys(*owner);
                return G_SOURCE_REMOVE;
            },delivery,+[](gpointer data){delete static_cast<std::shared_ptr<KeysState::Life>*>(data);});
        });
    g_signal_connect(canvas->widget(),"map",G_CALLBACK(+[](GtkWidget*,gpointer p){auto* state=static_cast<KeysState*>(p);pollKeys(*state);if(!state->timer)state->timer=g_timeout_add(3000,+[](gpointer p)->gboolean{pollKeys(*static_cast<KeysState*>(p));return G_SOURCE_CONTINUE;},state);}),state);
    g_signal_connect(canvas->widget(),"unmap",G_CALLBACK(+[](GtkWidget*,gpointer p){auto* state=static_cast<KeysState*>(p);if(state->timer){g_source_remove(state->timer);state->timer=0;}}),state);
    state->operations->restore("startup");
    GError* busError{};state->sleepBus=g_bus_get_sync(G_BUS_TYPE_SYSTEM,nullptr,&busError);
    if(state->sleepBus)state->sleepSubscription=g_dbus_connection_signal_subscribe(state->sleepBus,"org.freedesktop.login1","org.freedesktop.login1.Manager","PrepareForSleep","/org/freedesktop/login1",nullptr,G_DBUS_SIGNAL_FLAGS_NONE,
        +[](GDBusConnection*,const gchar*,const gchar*,const gchar*,const gchar*,GVariant* args,gpointer owner){auto* s=static_cast<KeysState*>(owner);gboolean sleeping{};g_variant_get(args,"(b)",&sleeping);s->operations->powerSleep(sleeping);selectOsd(*s);s->canvas->redraw();},state,nullptr);
    if(busError){logMessage("keys_restore",std::string("logind notification unavailable: ")+busError->message);g_error_free(busError);}
    selectOsd(*state);auto* page=scrolledPage(canvas);g_object_set_data(G_OBJECT(page),"magiccontrol-keys-owner",state);return page;
}
void suspendKeysPage(GtkWidget* page){if(auto* state=static_cast<KeysState*>(g_object_get_data(G_OBJECT(page),"magiccontrol-keys-owner"))){state->operations->suspend();}}
void restoreKeysPage(GtkWidget* page,const char* reason){if(auto* state=static_cast<KeysState*>(g_object_get_data(G_OBJECT(page),"magiccontrol-keys-owner"))){state->operations->restore(reason);selectOsd(*state);state->canvas->redraw();}}
}
