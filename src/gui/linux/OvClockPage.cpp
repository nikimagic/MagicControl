#include "Pages.h"
#include "Ui.h"
#include "MagicControl/Overclock.h"
#include <memory>

namespace mc::gtkui {
namespace {
struct State {
    Canvas* canvas{};GtkWidget *off{},*on{};OverclockOperations operation;
    struct Life{State* owner;};std::shared_ptr<Life> life{std::make_shared<Life>(Life{this})};
    ~State(){life->owner=nullptr;}
};
void refresh(State& state){
    const auto life=state.life;
    state.operation.refresh([life]{
        auto* hold=new std::shared_ptr<State::Life>(life);
        g_idle_add_full(G_PRIORITY_DEFAULT,+[](gpointer data)->gboolean{
            auto& life=*static_cast<std::shared_ptr<State::Life>*>(data);
            if(auto* state=life->owner){
                if(state->operation.take()){
                    const auto value=state->operation.snapshot().memorySwitch;
                    gtk_button_set_label(GTK_BUTTON(state->off),value&&*value==0?"● Выкл. 0":"Выкл. 0");
                    gtk_button_set_label(GTK_BUTTON(state->on),value&&*value==1?"● Вкл. 1":"Вкл. 1");
                }
                state->canvas->redraw();
            }
            return G_SOURCE_REMOVE;
        },hold,+[](gpointer data){delete static_cast<std::shared_ptr<State::Life>*>(data);});
    });
    state.canvas->redraw();
}
}
GtkWidget* createOvClockPage(){
    auto* state=new State();
    state->canvas=Canvas::create([state](Canvas& c,int width,int){const ui::Scale s{c.dpi()};
        const int x=width-s(252);c.place(state->off,{x,s(51),x+s(108),s(81)});c.place(state->on,{x+s(112),s(51),x+s(220),s(81)});return s(460);
    },[state](Canvas& c,cairo_t* cr,int width,int height){const ui::Scale s{c.dpi()};
        fill(cr,{0,0,width,height},theme::kBackground);fill(cr,{s(12),s(8),width-s(12),s(440)},theme::kSurface);
        text(cr,"OvClock · WUJIE X5SP4NAG · UEFI / FwVars",{s(24),s(13),width-s(24),s(45)},Font::Heading,theme::kTextPrimary,c.dpi());
        text(cr,"Память · только просмотр",{s(24),s(51),width-s(270),s(81)},Font::Body,theme::kTextSecondary,c.dpi());
        const auto rows=overclockRows(state->operation.snapshot());int top=90;
        if(state->operation.busy())text(cr,"Чтение…",{s(24),s(top),width-s(24),s(top+23)},Font::Body,theme::kTextMuted,c.dpi());
        else for(const auto& [key,value]:rows){
            const int split=width*48/100;
            text(cr,key,{s(24),s(top),split,s(top+24)},Font::Body,theme::kTextSecondary,c.dpi());
            text(cr,value,{split,s(top),width-s(24),s(top+24)},Font::Body,theme::kTextPrimary,c.dpi());top+=26;
        }
        text(cr,"Обновление при выборе вкладки · только чтение",{s(24),s(408),width-s(24),s(432)},Font::Body,theme::kTextMuted,c.dpi());
    });
    state->off=actionButton("Выкл. 0",{},false,false);state->on=actionButton("Вкл. 1",{},false,false);
    state->canvas->add(state->off);state->canvas->add(state->on);
    auto* page=scrolledPage(state->canvas);
    g_object_set_data_full(G_OBJECT(page),"magiccontrol-ovclock",state,+[](gpointer data){delete static_cast<State*>(data);});
    return page;
}
void refreshOvClockPage(GtkWidget* page){if(auto* state=static_cast<State*>(g_object_get_data(G_OBJECT(page),"magiccontrol-ovclock")))refresh(*state);}
}
