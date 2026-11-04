#include "AboutPanel.h"
#include "Ui.h"
#include "../common/DeviceHeader.h"
#include <string>
#include <algorithm>
#include <cstring>
#include <array>

namespace mc::gtkui {
namespace {
constexpr char wallet[]="UQBm4jj-N5u15h9E-JM61bZBYcl84m7JRfR0VRwX3IMUiFfu";
constexpr char github[]="https://github.com/nikimagic/MagicControl";
constexpr char donationWebsite[]="https://gram.org/";
constexpr char icon[]="com.mechrevo.magiccontrol";
GtkWidget* centred(const char* value,bool bold=false) {
    auto* label=gtk_label_new(value);gtk_label_set_xalign(GTK_LABEL(label),0.5f);gtk_label_set_justify(GTK_LABEL(label),GTK_JUSTIFY_CENTER);
    if(bold){gtk_widget_add_css_class(label,"about-heading");
        // Keep plain source text for localization; native attributes retain
        // the same bold face without passing markup through the translator.
        auto* attributes=pango_attr_list_new();pango_attr_list_insert(attributes,pango_attr_weight_new(PANGO_WEIGHT_BOLD));
        gtk_label_set_attributes(GTK_LABEL(label),attributes);pango_attr_list_unref(attributes);}
    return label;
}
void license(GtkWindow* parent) {
    GError* error{};GBytes* bytes=g_resources_lookup_data("/com/mechrevo/magiccontrol/license.txt",G_RESOURCE_LOOKUP_FLAGS_NONE,&error);
    if(!bytes){g_clear_error(&error);return;}
    auto* window=GTK_WINDOW(gtk_window_new());setWindowTitle(window,"MagicControl — Лицензия");
    gtk_widget_add_css_class(GTK_WIDGET(window),"license-window");
    gtk_window_set_icon_name(window,icon);gtk_window_set_transient_for(window,parent);gtk_window_set_modal(window,TRUE);gtk_window_set_destroy_with_parent(window,TRUE);
    gtk_window_set_default_size(window,650,500);
    auto* header=gtk_header_bar_new();auto* title=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);
    auto* logo=gtk_image_new_from_resource("/com/mechrevo/magiccontrol/icons/512x512/apps/com.mechrevo.magiccontrol.png");
    gtk_image_set_pixel_size(GTK_IMAGE(logo),20);gtk_box_append(GTK_BOX(title),logo);
    gtk_box_append(GTK_BOX(title),centred("MagicControl — Лицензия"));gtk_header_bar_set_title_widget(GTK_HEADER_BAR(header),title);
    gtk_window_set_titlebar(window,header);
    // Keep one continuous surface; inset the text itself so the shared scrollbar
    // stays flush with the window edge, just like the main tabs.
    auto* root=gtk_box_new(GTK_ORIENTATION_VERTICAL,12);gtk_widget_set_margin_bottom(root,12);
    auto* view=gtk_text_view_new();gtk_text_view_set_editable(GTK_TEXT_VIEW(view),FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view),GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(view),16);gtk_text_view_set_right_margin(GTK_TEXT_VIEW(view),20);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(view),12);gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(view),12);
    gsize size{};const auto* data=static_cast<const char*>(g_bytes_get_data(bytes,&size));
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)),data,static_cast<int>(size));g_bytes_unref(bytes);
    gtk_box_append(GTK_BOX(root),scrolledWidget(view));
    auto* close=actionButton("Закрыть",[window]{gtk_window_destroy(window);});gtk_widget_set_size_request(close,100,30);
    gtk_widget_set_halign(close,GTK_ALIGN_END);gtk_widget_set_margin_end(close,12);gtk_box_append(GTK_BOX(root),close);
    auto* keys=gtk_event_controller_key_new();g_signal_connect(keys,"key-pressed",G_CALLBACK(+[](GtkEventControllerKey*,guint key,guint,GdkModifierType,gpointer p)->gboolean{
        if(key!=GDK_KEY_Escape)return FALSE;
        gtk_window_destroy(GTK_WINDOW(p));return TRUE;
    }),window);gtk_widget_add_controller(GTK_WIDGET(window),keys);
    gtk_window_set_child(window,root);applyWidgetLanguage(GTK_WIDGET(window));gtk_window_present(window);
}
struct State {
    GtkWindow* parent{};GtkWidget* root{};std::array<GtkWidget*,4> dots{},choices{};std::function<void(int)> changed;
    GtkWidget* pages{};GtkWidget* modelHeading{};GtkWidget* modelsButton{};GtkWidget* headingArrow{};
    GtkWidget* modelViewport{};GtkWidget* modelList{};
    GdkTexture* qrTexture{};GtkWidget* copyHint{};
    std::array<GtkWidget*,ui::kSupportedLaptops.size()> modelChoices{},modelDots{};
    std::array<GtkWidget*,ui::kSupportedLaptops.size()> modelStatuses{};
    ~State(){if(qrTexture)g_object_unref(qrTexture);}
};
struct PanelGeometries {ui::LaptopMenuGeometry models;ui::LanguageMenuGeometry main;};
PanelGeometries panelGeometries(const State& state){
    int longest{},compactLongest{},lineHeight{},statusWidth{};
    for(int i=0;i<static_cast<int>(ui::kSupportedLaptops.size());++i){
        const int nameWidth=textWidth(std::string(ui::kSupportedLaptops[i].name),Font::ClassicBody,96);
        longest=std::max(longest,nameWidth);
        if(i<static_cast<int>(ui::kCompactMenuReferenceModels))compactLongest=std::max(compactLongest,nameWidth);
        statusWidth=std::max(statusWidth,textWidth(ui::laptopSupportCaption(i,true),Font::KeyDetail,96));}
    auto* measured=gtk_widget_create_pango_layout(state.root,"Ag");pango_layout_get_pixel_size(measured,nullptr,&lineHeight);g_object_unref(measured);
    const int parentWidth=gtk_widget_get_width(GTK_WIDGET(state.parent));
    const int parentHeight=gtk_widget_get_height(GTK_WIDGET(state.parent));
    const int availableWidth=parentWidth>16?parentWidth-16:1200;
    const int titleWidth=textWidth(ui::kLaptopsCaption,Font::ClassicHeading,96),footerWidth=textWidth(ui::kLaptopPreviewNote,Font::KeyDetail,96);
    return {ui::laptopMenuGeometry(static_cast<int>(ui::kSupportedLaptops.size()),longest,titleWidth,footerWidth,
        lineHeight,availableWidth,{},statusWidth,parentHeight>96?parentHeight-mc::gui::kTitleBarHeight-16:600),
        ui::languageMenuGeometry(ui::menuWidthFromText(compactLongest,titleWidth,footerWidth,availableWidth))};
}
void resizePanel(State& state){
    const auto panels=panelGeometries(state);const auto& geometry=panels.models;
    const bool models=std::string_view(gtk_stack_get_visible_child_name(GTK_STACK(state.pages)))=="laptops";
    gtk_widget_set_size_request(state.root,models?geometry.width:panels.main.width,-1);
    gtk_widget_set_size_request(state.modelHeading,-1,geometry.title.height());
    gtk_widget_set_size_request(state.headingArrow,geometry.backArrow.width(),geometry.backArrow.height());
    gtk_widget_set_size_request(state.modelsButton,-1,panels.main.models.height());
    if(state.modelViewport)gtk_widget_set_size_request(state.modelViewport,-1,geometry.viewport.height());
    if(state.modelList)gtk_widget_set_margin_end(state.modelList,geometry.rowsExtent>geometry.viewport.height()?20:8);
    for(int i=0;i<static_cast<int>(state.modelChoices.size());++i){
        if(state.modelChoices[i])gtk_widget_set_size_request(state.modelChoices[i],geometry.viewport.width(),geometry.rowHeight);
        if(state.modelStatuses[i])gtk_widget_set_size_request(state.modelStatuses[i],geometry.statusWidth,-1);}
}
}
void refreshAboutPanel(GtkWidget* panel){
    if(!panel)return;
    if(auto* state=static_cast<State*>(g_object_get_data(G_OBJECT(panel),"about-panel"))){applyWidgetLanguage(panel);resizePanel(*state);gtk_widget_queue_resize(panel);}
}
GtkWidget* createAboutPanel(GtkWindow* parent,int selected,std::function<void(int)> changed,std::function<void(int)> modelChanged) {
    auto* state=new State();state->parent=parent;state->changed=std::move(changed);
    auto* root=gtk_box_new(GTK_ORIENTATION_VERTICAL,6);state->root=root;gtk_widget_add_css_class(root,"language-menu");
    gtk_widget_set_hexpand(root,FALSE);
    gtk_widget_set_halign(root,GTK_ALIGN_START);gtk_widget_set_valign(root,GTK_ALIGN_START);
    g_object_set_data_full(G_OBJECT(root),"about-panel",state,[](gpointer p){delete static_cast<State*>(p);});
    auto* mainBox=gtk_box_new(GTK_ORIENTATION_VERTICAL,6);
    auto* modelPage=gtk_box_new(GTK_ORIENTATION_VERTICAL,0);
    state->pages=gtk_stack_new();
    gtk_stack_set_hhomogeneous(GTK_STACK(state->pages),FALSE);gtk_stack_set_vhomogeneous(GTK_STACK(state->pages),FALSE);
    gtk_stack_set_transition_type(GTK_STACK(state->pages),GTK_STACK_TRANSITION_TYPE_NONE);
    gtk_stack_add_named(GTK_STACK(state->pages),mainBox,"main");gtk_stack_add_named(GTK_STACK(state->pages),modelPage,"laptops");
    gtk_stack_set_visible_child_name(GTK_STACK(state->pages),"main");gtk_box_append(GTK_BOX(root),state->pages);
    g_signal_connect(root,"notify::visible",G_CALLBACK(+[](GObject*,GParamSpec*,gpointer p){
        auto* state=static_cast<State*>(p);
        if(!gtk_widget_get_visible(state->root))gtk_stack_set_visible_child_name(GTK_STACK(state->pages),"main");
        else resizePanel(*state);
    }),state);
    auto* grid=gtk_grid_new();gtk_grid_set_column_homogeneous(GTK_GRID(grid),TRUE);gtk_widget_set_hexpand(grid,TRUE);
    gtk_widget_set_margin_start(grid,8);gtk_widget_set_margin_end(grid,8);gtk_widget_set_margin_top(grid,6);
    for(int i=0;i<4;++i){
        auto* choice=actionButton("",[state,i]{for(int n=0;n<4;++n){gtk_label_set_text(GTK_LABEL(state->dots[n]),i==n?"•":"");
                if(i==n)gtk_widget_add_css_class(state->choices[n],"selected-language");else gtk_widget_remove_css_class(state->choices[n],"selected-language");}
            state->changed(i);gtk_widget_set_visible(state->root,FALSE);});
        state->choices[i]=choice;gtk_widget_add_css_class(choice,"credit-button");
        if(i==selected)gtk_widget_add_css_class(choice,"selected-language");
        gtk_widget_set_size_request(choice,92,30);
        gtk_widget_set_hexpand(choice,TRUE);
        auto* content=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,2);state->dots[i]=gtk_label_new(i==selected?"•":"");
        gtk_widget_set_size_request(state->dots[i],14,-1);gtk_box_append(GTK_BOX(content),state->dots[i]);
        auto* label=gtk_label_new(gui::ui::kLanguageOptions[i].label.data());gtk_label_set_xalign(GTK_LABEL(label),0);
        gtk_box_append(GTK_BOX(content),label);gtk_button_set_child(GTK_BUTTON(choice),content);
        gtk_grid_attach(GTK_GRID(grid),choice,i%2,i/2,1,1);
    }
    gtk_box_append(GTK_BOX(mainBox),grid);
    const auto separator=[](GtkWidget* box,int margin=10){auto* line=gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);gtk_widget_set_margin_start(line,margin);gtk_widget_set_margin_end(line,margin);gtk_box_append(GTK_BOX(box),line);};
    auto* modelList=gtk_box_new(GTK_ORIENTATION_VERTICAL,0);state->modelList=modelList;
    gtk_widget_set_margin_start(modelList,8);gtk_widget_set_margin_end(modelList,8);
    state->modelHeading=actionButton("",[state]{
        gtk_stack_set_visible_child_name(GTK_STACK(state->pages),"main");resizePanel(*state);gtk_widget_grab_focus(state->modelsButton);
    });
    auto* headingContent=gtk_overlay_new();gtk_overlay_set_child(GTK_OVERLAY(headingContent),centred(ui::kLaptopsCaption,true));
    auto* headingArrow=centred(ui::kLaptopsBackArrow,true);gtk_widget_set_halign(headingArrow,GTK_ALIGN_START);
    state->headingArrow=headingArrow;
    gtk_widget_set_margin_start(headingArrow,4);gtk_widget_set_can_target(headingArrow,FALSE);
    gtk_overlay_add_overlay(GTK_OVERLAY(headingContent),headingArrow);
    gtk_button_set_child(GTK_BUTTON(state->modelHeading),headingContent);
    gtk_widget_add_css_class(state->modelHeading,"credit-button");
    gtk_widget_set_margin_start(state->modelHeading,16);gtk_widget_set_margin_end(state->modelHeading,16);
    gtk_widget_set_margin_top(state->modelHeading,6);gtk_widget_set_margin_bottom(state->modelHeading,1);
    gtk_box_append(GTK_BOX(modelPage),state->modelHeading);separator(modelPage,16);
    state->modelViewport=scrolledWidget(modelList);gtk_widget_set_vexpand(state->modelViewport,FALSE);
    gtk_widget_set_margin_top(state->modelViewport,4);gtk_box_append(GTK_BOX(modelPage),state->modelViewport);
    state->modelsButton=actionButton("",[state]{
        gtk_stack_set_visible_child_name(GTK_STACK(state->pages),"laptops");resizePanel(*state);gtk_widget_grab_focus(state->modelHeading);
    });
    gtk_button_set_child(GTK_BUTTON(state->modelsButton),centred(ui::kLaptopsForwardCaption));
    gtk_widget_add_css_class(state->modelsButton,"credit-button");
    gtk_widget_set_margin_start(state->modelsButton,8);gtk_widget_set_margin_end(state->modelsButton,8);
    separator(mainBox);gtk_box_append(GTK_BOX(mainBox),state->modelsButton);separator(mainBox);
    const int detected=ui::selectedLaptopIndex();
    for(const int i:ui::kLaptopMenuOrder){
        auto* choice=actionButton("",[state,modelChanged,i]{if(modelChanged)modelChanged(i);for(int j=0;j<static_cast<int>(ui::kSupportedLaptops.size());++j){
            gtk_label_set_text(GTK_LABEL(state->modelDots[j]),j==ui::selectedLaptopIndex()?"•":"");
            if(j==ui::selectedLaptopIndex())gtk_widget_add_css_class(state->modelChoices[j],"selected-language");else gtk_widget_remove_css_class(state->modelChoices[j],"selected-language");}
            gtk_widget_set_visible(state->root,FALSE);});
        state->modelChoices[i]=choice;
        gtk_widget_add_css_class(choice,"credit-button");if(i==detected)gtk_widget_add_css_class(choice,"selected-language");
        auto* content=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6);gtk_widget_set_margin_start(content,4);gtk_widget_set_margin_end(content,8);
        state->modelDots[i]=gtk_label_new(i==detected?"•":"");gtk_widget_set_size_request(state->modelDots[i],14,-1);
        gtk_box_append(GTK_BOX(content),state->modelDots[i]);
        auto* label=gtk_label_new(ui::kSupportedLaptops[i].name.data());gtk_label_set_xalign(GTK_LABEL(label),0);
        gtk_label_set_wrap(GTK_LABEL(label),FALSE);gtk_label_set_single_line_mode(GTK_LABEL(label),TRUE);
        gtk_label_set_ellipsize(GTK_LABEL(label),PANGO_ELLIPSIZE_END);gtk_widget_set_hexpand(label,TRUE);
        gtk_box_append(GTK_BOX(content),label);
        auto* status=gtk_label_new(ui::laptopSupportCaption(i,true));state->modelStatuses[i]=status;
        gtk_label_set_xalign(GTK_LABEL(status),1);gtk_label_set_single_line_mode(GTK_LABEL(status),TRUE);
        gtk_label_set_ellipsize(GTK_LABEL(status),PANGO_ELLIPSIZE_END);gtk_widget_add_css_class(status,"about-detail");
        gtk_box_append(GTK_BOX(content),status);gtk_button_set_child(GTK_BUTTON(choice),content);
        gtk_box_append(GTK_BOX(modelList),choice);
    }
    // Keep the Russian source for the common language adapter; using the
    // already-translated measurement string loses later language switches.
    auto* modelNote=centred(ui::kLaptopPreviewNote);gtk_label_set_xalign(GTK_LABEL(modelNote),0);gtk_widget_add_css_class(modelNote,"about-detail");
    gtk_label_set_ellipsize(GTK_LABEL(modelNote),PANGO_ELLIPSIZE_END);
    gtk_widget_set_margin_start(modelNote,8);gtk_widget_set_margin_end(modelNote,8);gtk_widget_set_margin_top(modelNote,8);gtk_widget_set_margin_bottom(modelNote,4);
    gtk_box_append(GTK_BOX(modelPage),modelNote);
    auto* copyright=actionButton("",[state]{gtk_widget_set_visible(state->root,FALSE);license(state->parent);});
    gtk_widget_add_css_class(copyright,"credit-button");auto* rights=gtk_box_new(GTK_ORIENTATION_VERTICAL,0);
    gtk_box_append(GTK_BOX(rights),centred("© 2026 NikiMagic",true));
    auto* reserved=centred("All rights reserved.");gtk_widget_add_css_class(reserved,"about-secondary");gtk_box_append(GTK_BOX(rights),reserved);
    gtk_button_set_child(GTK_BUTTON(copyright),rights);gtk_box_append(GTK_BOX(mainBox),copyright);
    auto* link=actionButton("GitHub · github.com",[state]{auto* launcher=gtk_uri_launcher_new(github);gtk_uri_launcher_launch(launcher,state->parent,nullptr,nullptr,nullptr);g_object_unref(launcher);});
    gtk_widget_add_css_class(link,"credit-button");gtk_widget_add_css_class(link,"about-link");gtk_box_append(GTK_BOX(mainBox),link);
    separator(mainBox);gtk_box_append(GTK_BOX(mainBox),centred("Донаты",true));
    // Fixed 180-DIP QR, independent of the PNG's 360-pixel intrinsic size.
    auto* qr=gtk_drawing_area_new();gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(qr),180);
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(qr),180);gtk_widget_set_halign(qr,GTK_ALIGN_CENTER);
    GError* qrError{};GBytes* qrBytes=g_resources_lookup_data("/com/mechrevo/magiccontrol/donation-wallet.png",G_RESOURCE_LOOKUP_FLAGS_NONE,&qrError);
    g_clear_error(&qrError);cairo_surface_t* qrSurface{};
    if(qrBytes){GError* textureError{};state->qrTexture=gdk_texture_new_from_bytes(qrBytes,&textureError);g_clear_error(&textureError);
        gsize size{};const auto* data=static_cast<const unsigned char*>(g_bytes_get_data(qrBytes,&size));
        struct Reader {const unsigned char* data;gsize size,position;};Reader reader{data,size,0};
        qrSurface=cairo_image_surface_create_from_png_stream(+[](void* p,unsigned char* destination,unsigned length)->cairo_status_t{
            auto& r=*static_cast<Reader*>(p);if(length>r.size-r.position)return CAIRO_STATUS_READ_ERROR;
            std::memcpy(destination,r.data+r.position,length);r.position+=length;return CAIRO_STATUS_SUCCESS;
        },&reader);g_bytes_unref(qrBytes);
        if(cairo_surface_status(qrSurface)!=CAIRO_STATUS_SUCCESS){cairo_surface_destroy(qrSurface);qrSurface=nullptr;}
    }
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(qr),+[](GtkDrawingArea*,cairo_t* cr,int width,int height,gpointer p){
        if(!p)return;
        auto* pixels=static_cast<cairo_surface_t*>(p);const int side=std::min(width,height);
        cairo_save(cr);cairo_translate(cr,(width-side)/2.0,(height-side)/2.0);
        cairo_scale(cr,side/static_cast<double>(cairo_image_surface_get_width(pixels)),side/static_cast<double>(cairo_image_surface_get_height(pixels)));
        cairo_set_source_surface(cr,pixels,0,0);cairo_pattern_set_filter(cairo_get_source(cr),CAIRO_FILTER_NEAREST);cairo_paint(cr);cairo_restore(cr);
    },qrSurface,+[](gpointer p){if(p)cairo_surface_destroy(static_cast<cairo_surface_t*>(p));});
    auto* qrCopy=actionButton("",[state]{if(state->qrTexture){
        gdk_clipboard_set_texture(gdk_display_get_clipboard(gtk_widget_get_display(state->root)),state->qrTexture);
        gtk_label_set_text(GTK_LABEL(state->copyHint),"QR-код скопирован");gtk_widget_add_css_class(state->copyHint,"donation-copied");}});
    gtk_widget_add_css_class(qrCopy,"credit-button");gtk_widget_set_halign(qrCopy,GTK_ALIGN_CENTER);
    gtk_widget_set_cursor_from_name(qrCopy,"pointer");gtk_widget_set_sensitive(qrCopy,state->qrTexture!=nullptr);
    setWidgetTooltip(qrCopy,"Скопировать QR-код как изображение");
    setWidgetAccessibleLabel(qrCopy,"Скопировать QR-код как изображение");
    gtk_button_set_child(GTK_BUTTON(qrCopy),qr);gtk_box_append(GTK_BOX(mainBox),qrCopy);
    auto* copy=actionButton("",[state]{gdk_clipboard_set_text(gdk_display_get_clipboard(gtk_widget_get_display(state->root)),wallet);
        gtk_label_set_text(GTK_LABEL(state->copyHint),"Адрес скопирован");gtk_widget_add_css_class(state->copyHint,"donation-copied");});
    gtk_widget_add_css_class(copy,"credit-button");std::string address(wallet);address.insert(24,"\n");
    gtk_button_set_child(GTK_BUTTON(copy),centred(address.c_str()));gtk_box_append(GTK_BOX(mainBox),copy);
    auto* hint=centred("Нажмите, чтобы скопировать");state->copyHint=hint;gtk_widget_add_css_class(hint,"about-detail");gtk_widget_set_margin_bottom(hint,6);gtk_box_append(GTK_BOX(mainBox),hint);
    auto* donationLink=actionButton("gram.org",[state]{auto* launcher=gtk_uri_launcher_new(donationWebsite);gtk_uri_launcher_launch(launcher,state->parent,nullptr,nullptr,nullptr);g_object_unref(launcher);});
    gtk_widget_add_css_class(donationLink,"credit-button");gtk_widget_add_css_class(donationLink,"about-link");
    gtk_widget_set_margin_bottom(donationLink,6);gtk_box_append(GTK_BOX(mainBox),donationLink);
    refreshAboutPanel(root);
    return root;
}
}
