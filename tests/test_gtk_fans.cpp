#include <gtk/gtk.h>
#include <cstdio>
#include <cstring>
#include "../src/gui/linux/Pages.h"
#include "../src/gui/linux/Ui.h"
#include "../src/gui/common/DeviceHeader.h"
#include "MagicControl/BoardIdentity.h"
#include "MagicControl/FanControlLimits.h"

namespace {
struct Counts { int temperature{},duty{},defaults{},invalid{},narrowArrows{}; };
void inspect(GtkWidget* widget,Counts& counts) {
    if(GTK_IS_LABEL(widget)&&gtk_widget_get_mapped(widget)){
        const auto* label=gtk_label_get_text(GTK_LABEL(widget));
        if((std::strcmp(label,"‹")==0||std::strcmp(label,"›")==0)&&gtk_widget_get_width(widget)<=0)++counts.narrowArrows;
    }
    if(GTK_IS_SPIN_BUTTON(widget)){
        double minimum{},maximum{};gtk_spin_button_get_range(GTK_SPIN_BUTTON(widget),&minimum,&maximum);
        if(minimum==mc::fanInput::temperatureMinimum&&maximum==mc::fanInput::temperatureMaximum)++counts.temperature;
        else if(minimum==mc::fanInput::percentMinimum&&maximum==mc::fanInput::percentMaximum)++counts.duty;
        else ++counts.invalid;
    }
    if(GTK_IS_BUTTON(widget)){
        const auto* label=gtk_button_get_label(GTK_BUTTON(widget));
        if(label&&std::strcmp(label,"Default")==0)++counts.defaults;
    }
    for(auto* child=gtk_widget_get_first_child(widget);child;child=gtk_widget_get_next_sibling(child))inspect(child,counts);
}
}
int main(){
    if(!gtk_init_check()){std::puts("GTK display unavailable: skipped");return 77;}
    g_log_set_always_fatal(static_cast<GLogLevelFlags>(G_LOG_LEVEL_ERROR|G_LOG_LEVEL_CRITICAL));
    const int actual=mc::gui::ui::currentLaptop(mc::readBoardDescriptor());
    int failures=0;
    for(const int candidate:{0,1,3}){
        // Always preview a different model: mapping the widget cannot start
        // hardware polling or apply/save curves on this test machine.
        const int preview=candidate==actual?(candidate+1)%3:candidate;
        mc::gui::ui::laptopSelection.store(preview);
        auto* window=gtk_window_new();g_object_ref_sink(window);
        mc::gtkui::installUiCss(window);
        auto* page=mc::gtkui::createFansPage();gtk_window_set_child(GTK_WINDOW(window),page);
        gtk_window_set_default_size(GTK_WINDOW(window),1400,1000);gtk_window_present(GTK_WINDOW(window));
        const auto deadline=g_get_monotonic_time()+300000;
        while(g_get_monotonic_time()<deadline){while(g_main_context_iteration(nullptr,FALSE)){}g_usleep(1000);}
        Counts counts;inspect(page,counts);
        const bool ok=mc::gui::ui::laptopPreview()&&counts.temperature==16&&counts.duty==16&&counts.invalid==0&&counts.defaults==2&&counts.narrowArrows==0;
        std::printf("GTK FANs preview=%d temperature=%d duty=%d default=%d invalid=%d narrow_arrows=%d\n",preview,counts.temperature,counts.duty,counts.defaults,counts.invalid,counts.narrowArrows);
        if(!ok)++failures;
        gtk_window_destroy(GTK_WINDOW(window));g_object_unref(window);
        while(g_main_context_iteration(nullptr,FALSE)){}
    }
    mc::gui::ui::laptopSelection.store(-1);
    return failures?1:0;
}
