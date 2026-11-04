#include "MagicControl/ActionLog.h"
#include "MagicControl/BatteryIdentityHistory.h"
#include "MagicControl/DisplayRotationControl.h"
#include "../../platform/linux/DisplayControlLinux.h"
#include "../../platform/linux/DisplayHotkeyLinux.h"
#include "../common/FooterStatus.h"
#include "MainWindow.h"
#include "AboutPanel.h"
#include "UpdateWindow.h"
#include "Pages.h"
#include "Ui.h"
#include "OsdService.h"
#include "TrayIcon.h"
#include "../common/OsdMessages.h"
#include "../common/DeviceHeader.h"

#include <array>
#include <cmath>
#include <string>
#include <memory>

#include "MagicControl/Version.h"
#include "MagicControl/PortableSettings.h"
#include "../../platform/linux/SettingsServicesLinux.h"
#include "MagicControl/Logging.h"
#include "MagicControl/Overclock.h"
#include "MagicControl/DeviceProfile.h"
#include "MagicControl/NvidiaGpu.h"
#include "MagicControl/GpuDevice.h"

namespace mc::gtkui {
namespace {
constexpr auto& tabTitles=ui::kHeaderTabTitlesUtf8;
constexpr const char* pageNames[]{"fans","sets","keys","bios","nvidia","radeon","ovclock"};
struct WindowState {
    GtkWindow* window{};
    GtkWidget* headerIcon{};
    GtkWidget* stack{};
    Canvas* header{};
    Canvas* status{};
    GtkWidget* versionButton{};
    GtkOverlay* overlay{};
    GtkWidget* brand{};
    GtkWidget* languageMenu{};
    std::array<GtkWidget*,4> languageOptions{};
    std::array<GtkWidget*,4> languageDots{};
    ui::DeviceHeader device;
    int brandDpi{},manufacturerWidth{},modelWidth{},selectedLanguage{};
    int hostnameBrandWidth{};
    std::array<GtkWidget*,mc::gui::kTabCount> tabs{};
    GtkWidget* overclockPage{};bool overclockBoard{},overclockAllowed{};
    std::array<GtkWidget*,3> captions{};
    std::unique_ptr<OsdService> osd;
    std::unique_ptr<TrayIcon> tray;
    std::unique_ptr<mc::DisplayRotationOperations> rotation;
    std::unique_ptr<mc::linux_platform::DisplayRotationHotkey> rotationHotkey;
    struct Life {WindowState* owner;};std::shared_ptr<Life> life{std::make_shared<Life>(Life{this})};
    bool hiddenToTray{},restoreMaximized{},exiting{};
    bool huawei{};
    bool nvidia{},radeon{};guint deviceTimer{};
    std::array<GtkWidget*,3> realPages{};
    int active{};
    ~WindowState(){life->owner=nullptr;rotationHotkey.reset();rotation.reset();ui::footerStatuses().observe({});if(deviceTimer)g_source_remove(deviceTimer);for(auto* page:realPages)if(page)g_object_unref(page);}
};
void initializeRotation(WindowState& state){
    state.rotation=std::make_unique<mc::DisplayRotationOperations>(mc::linux_platform::toggleBuiltInDisplayRotation,
        [&state](std::uint64_t at){return state.osd?state.osd->beginOperation({"display-rotation","Поворот экрана","Поворачиваю на180°…",mc::gui::osd::Icon::Display,{},false,true,at}):0;},
        [life=state.life]{auto* hold=new std::shared_ptr<WindowState::Life>(life);
            g_idle_add_full(G_PRIORITY_DEFAULT,+[](gpointer p)->gboolean{auto& life=*static_cast<std::shared_ptr<WindowState::Life>*>(p);if(auto* s=life->owner){
                auto result=s->rotation->take();if(result&&!s->exiting){if(s->osd)s->osd->completeOperation(result->token,{"display-rotation","Поворот экрана",result->detail,mc::gui::osd::Icon::Display,{},!result->success,false,result->receivedAt});
                    mc::logMessage("display_rotation",std::string(result->success?"confirmed: ":"failed: ")+result->detail);s->rotation->continueQueued();}}
                return G_SOURCE_REMOVE;},hold,+[](gpointer p){delete static_cast<std::shared_ptr<WindowState::Life>*>(p);});});
    state.rotationHotkey=std::make_unique<mc::linux_platform::DisplayRotationHotkey>([&state](std::uint64_t at){if(!state.exiting)state.rotation->request(at);},[](std::string text){mc::logMessage("display_rotation",text);});
}
std::array<int,mc::gui::kTabCount> tabWidths(int dpi){auto values=ui::tabTextWidths(dpi);for(int i=4;i<mc::gui::kTabCount;++i)values[i]=textWidth(tabTitles[i],Font::TabActive,dpi);return values;}
struct LanguageDotData { WindowState* state{}; std::size_t index{}; };
constexpr const char* kReferenceDeviceModel = "Wujie 15X Pro";
Font manufacturerFont() {
    // Match the Windows wordmark. In WSL the hostname stands in for DMI data;
    // a known MECHREVO hostname is split into the same brand/model slots.
    return Font::Brand;
}
bool splitMechrevoHostname(const WindowState& state) {
    return state.device.model.empty() && state.device.manufacturer.rfind("MECHREVO-",0)==0;
}
void openFromTray(WindowState& state) {
    if(state.exiting)return;
    if(state.hiddenToTray&&state.restoreMaximized)gtk_window_maximize(state.window);
    state.hiddenToTray=false;
    gtk_window_unminimize(state.window);
    gtk_window_present(state.window);
}
bool hover(GtkWidget* widget) {
    if(GTK_IS_MENU_BUTTON(widget))if(auto* child=gtk_widget_get_first_child(widget))
        return (gtk_widget_get_state_flags(child)&GTK_STATE_FLAG_PRELIGHT)!=0;
    return (gtk_widget_get_state_flags(widget)&GTK_STATE_FLAG_PRELIGHT)!=0;
}
int brandWidth(WindowState& state,int dpi) {
    if(state.brandDpi!=dpi) {
        state.brandDpi=dpi;
        const Font font=manufacturerFont();
        state.hostnameBrandWidth=0;
        if(splitMechrevoHostname(state)) {
            state.hostnameBrandWidth=ui::brandTextWidth(dpi);
            state.manufacturerWidth=state.hostnameBrandWidth;
            // Windows sizes this dropdown using the full DMI model. Reserve
            // the same model slot for the shorter WSL hostname suffix.
            state.modelWidth=textWidth(kReferenceDeviceModel,Font::Body,dpi);
        } else {
            state.manufacturerWidth=state.device.manufacturer=="MECHREVO"
                ?ui::brandTextWidth(dpi):textWidth(state.device.manufacturer,font,dpi);
            state.modelWidth=textWidth(state.device.model,Font::Body,dpi);
        }
    }
    const ui::Scale s{dpi};
    const bool hasModelSlot=!state.device.model.empty()||splitMechrevoHostname(state);
    return s(32)+state.manufacturerWidth+(hasModelSlot?s(8)+state.modelWidth:0);
}
Rect brandMenuRect(const WindowState& state,Rect bounds,int dpi) {
    const ui::Scale scale{dpi};const auto g=ui::deviceBrandGeometry(bounds,scale,state.manufacturerWidth,state.modelWidth);
    const int inset=std::max(0,bounds.right-g.arrowX-scale(4));bounds.left=std::max(bounds.left,g.manufacturer.left-inset);return bounds;
}
void watchHover(GtkWidget* widget,Canvas* canvas) {
    g_signal_connect(widget,"state-flags-changed",G_CALLBACK(+[](GtkWidget*,GtkStateFlags,gpointer p){
        static_cast<Canvas*>(p)->redraw();
    }),canvas);
}
GtkWidget* chromeButton(const char* accessible,std::function<void()> callback) {
    GtkWidget* button=actionButton("",std::move(callback));
    gtk_widget_add_css_class(button,"chrome");
    setWidgetAccessibleLabel(button,accessible);
    return button;
}
void windowGlyph(cairo_t* cr,Rect r,int index,bool maximized,Color color,int dpi) {
    const ui::Scale s{dpi};
    const int size=std::max(5,ui::fontHeight(Font::Glyph,dpi)-s(2));
    const int x=(r.left+r.right-size)/2,y=(r.top+r.bottom-size)/2;
    const auto stroke=[&](int x1,int y1,int x2,int y2){line(cr,x1,y1,x2,y2,color,s(1));};
    if(index==0) stroke(x,y+size/2,x+size,y+size/2);
    else if(index==2) {
        stroke(x,y,x+size,y+size);stroke(x+size,y,x,y+size);
    } else {
        const int shift=maximized?s(3):0;
        if(maximized) {
            stroke(x+shift,y,x+size,y);stroke(x+size,y,x+size,y+size-shift);
            stroke(x+size-shift,y+size-shift,x+size,y+size-shift);
        }
        const int top=y+shift,right=x+size-shift,bottom=y+size;
        stroke(x,top,right,top);stroke(right,top,right,bottom);
        stroke(right,bottom,x,bottom);stroke(x,bottom,x,top);
    }
}
gboolean report(gpointer data) {
    auto* window=GTK_WIDGET(data);
    if(!gtk_widget_get_realized(window))return G_SOURCE_CONTINUE;
    GdkSurface* surface=gtk_native_get_surface(GTK_NATIVE(window));
    const double scale=displayScale(window);const int dpi=displayDpi(window);
    GdkMonitor* monitor=gdk_display_get_monitor_at_surface(gtk_widget_get_display(window),surface);
    GdkRectangle geometry{};if(monitor)gdk_monitor_get_geometry(monitor,&geometry);
    GskRenderer* renderer=gtk_native_get_renderer(GTK_NATIVE(window));
    g_print("MagicControl GTK: shared-layout window=%dx%d logical, %ldx%ld device-px; scale=%.2f dpi=%d; monitor=%dx%d logical; renderer=%s\n",
        gtk_widget_get_width(window),gtk_widget_get_height(window),
        std::lround(gtk_widget_get_width(window)*scale),std::lround(gtk_widget_get_height(window)*scale),
        scale,dpi,geometry.width,geometry.height,renderer?G_OBJECT_TYPE_NAME(renderer):"unknown");
    return G_SOURCE_REMOVE;
}
void resizePressed(GtkGestureClick* gesture,int,double,double,gpointer data) {
    auto* window=GTK_WINDOW(data);if(gtk_window_is_maximized(window))return;
    GdkEvent* event=gtk_event_controller_get_current_event(GTK_EVENT_CONTROLLER(gesture));
    if(!event)return;
    double x{},y{};
    if(!gdk_event_get_position(event,&x,&y))return;
    const int w=gtk_widget_get_width(GTK_WIDGET(window)),h=gtk_widget_get_height(GTK_WIDGET(window));
    const int border=mc::gui::kResizeBorder;
    const bool left=x<border,right=x>=w-border,top=y<border,bottom=y>=h-border;
    if(!left&&!right&&!top&&!bottom)return;
    const GdkSurfaceEdge edge=top?(left?GDK_SURFACE_EDGE_NORTH_WEST:right?GDK_SURFACE_EDGE_NORTH_EAST:GDK_SURFACE_EDGE_NORTH):
        bottom?(left?GDK_SURFACE_EDGE_SOUTH_WEST:right?GDK_SURFACE_EDGE_SOUTH_EAST:GDK_SURFACE_EDGE_SOUTH):
        left?GDK_SURFACE_EDGE_WEST:GDK_SURFACE_EDGE_EAST;
    GdkSurface* surface=gtk_native_get_surface(GTK_NATIVE(window));
    gdk_toplevel_begin_resize(GDK_TOPLEVEL(surface),edge,gdk_event_get_device(event),
        gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture)),x,y,gdk_event_get_time(event));
    gtk_gesture_set_state(GTK_GESTURE(gesture),GTK_EVENT_SEQUENCE_CLAIMED);
}
}

void createMainWindow(GtkApplication* application) {
    if(GList* windows=gtk_application_get_windows(application)){
        auto* existing=GTK_WINDOW(windows->data);
        if(auto* current=static_cast<WindowState*>(g_object_get_data(G_OBJECT(existing),"magiccontrol-window")))openFromTray(*current);
        else gtk_window_present(existing);
        return;
    }
    auto* state=new WindowState();state->selectedLanguage=static_cast<int>(ui::languageIndex());
    state->nvidia=nvidiaGpuPresent();state->radeon=radeonDiscretePresent();
    const auto board=mc::readBoardDescriptor();
    state->overclockBoard=mc::overclockObservationSupported(board);state->overclockAllowed=state->overclockBoard;
    mc::beginBatteryIdentityHistory(false);
    state->huawei=deviceProfile(board).id=="huawei-hlyl-wxx9";
    state->device=ui::deviceHeader(board);
    if(board.manufacturer.empty()&&board.productName.empty()) {
        const auto hostname=linux_platform::readHostnameFallback();
        if(!hostname.empty()) {
            state->device={hostname,{}};
            g_print("Device header: hostname=%s; Linux DMI unavailable\n",hostname.c_str());
        }
    } else g_print("Device header: manufacturer=%s model=%s; startup DMI\n",state->device.manufacturer.c_str(),state->device.model.c_str());
    state->window=GTK_WINDOW(gtk_application_window_new(application));
    setWindowTitle(state->window,"MagicControl");
    gtk_icon_theme_add_resource_path(gtk_icon_theme_get_for_display(gtk_widget_get_display(GTK_WIDGET(state->window))),
        "/com/mechrevo/magiccontrol/icons");
    gtk_window_set_default_icon_name("com.mechrevo.magiccontrol");
    gtk_window_set_icon_name(state->window,"com.mechrevo.magiccontrol");
    state->headerIcon=gtk_image_new_from_resource(
        "/com/mechrevo/magiccontrol/icons/512x512/apps/com.mechrevo.magiccontrol.png");
    gtk_image_set_pixel_size(GTK_IMAGE(state->headerIcon),mc::gui::kHeaderIconSize);
    gtk_widget_set_can_target(state->headerIcon,FALSE);
    // Match the frameless Windows host. GtkWindow's titlebar adds a theme
    // minimum height and a shadow resize gutter outside the shared geometry.
    gtk_window_set_decorated(state->window,FALSE);
    installUiCss();
    gtk_window_set_default_size(state->window,mc::gui::kDefaultOuterWidth,mc::gui::kDefaultOuterHeight);
    const double scale=displayScale();
    gtk_widget_set_size_request(GTK_WIDGET(state->window),
        static_cast<int>(std::ceil(mc::gui::kMinWidth/scale)),
        static_cast<int>(std::ceil(mc::gui::kMinHeight/scale)));
    g_object_set_data_full(G_OBJECT(state->window),"magiccontrol-window",state,
        [](gpointer p){delete static_cast<WindowState*>(p);});

    GtkWidget* root=gtk_box_new(GTK_ORIENTATION_VERTICAL,0);
    GtkWidget* overlay=gtk_overlay_new();state->overlay=GTK_OVERLAY(overlay);gtk_overlay_set_child(state->overlay,root);
    gtk_window_set_child(state->window,overlay);
    state->osd=std::make_unique<OsdService>(state->window,state->overlay);
    g_signal_connect(state->window,"close-request",G_CALLBACK(+[](GtkWindow*,gpointer data)->gboolean{
        auto& state=*static_cast<WindowState*>(data);
        if(!state.exiting&&state.tray&&state.tray->available()){
            state.restoreMaximized=gtk_window_is_maximized(state.window);
            state.hiddenToTray=true;
            gtk_widget_set_visible(GTK_WIDGET(state.window),FALSE);
            g_print("Window hidden to tray; notifications retained\n");
            return TRUE;
        }
        state.exiting=true;state.osd->stop();return FALSE;
    }),state);
    state->tray=std::make_unique<TrayIcon>([state]{openFromTray(*state);},[state]{
        state->exiting=true;gtk_window_close(state->window);
    },[state](bool available){
        if(!available&&state->hiddenToTray)openFromTray(*state);
    });
    state->stack=gtk_stack_new();gtk_widget_set_hexpand(state->stack,TRUE);gtk_widget_set_vexpand(state->stack,TRUE);
    gtk_stack_set_hhomogeneous(GTK_STACK(state->stack),FALSE);gtk_stack_set_vhomogeneous(GTK_STACK(state->stack),FALSE);
    gtk_stack_set_transition_type(GTK_STACK(state->stack),GTK_STACK_TRANSITION_TYPE_NONE);
    gtk_stack_add_named(GTK_STACK(state->stack),createFansPage(state->osd.get()),"fans");
    gtk_stack_add_named(GTK_STACK(state->stack),createSettingsPage(state->osd.get()),"sets");
    gtk_stack_add_named(GTK_STACK(state->stack),createKeysPage(state->osd.get()),"keys");
    gtk_stack_add_named(GTK_STACK(state->stack),createBiosPage(state->osd.get()),"bios");
    gtk_stack_add_named(GTK_STACK(state->stack),createNvidiaGpuPage(state->osd.get()),"nvidia");
    gtk_stack_add_named(GTK_STACK(state->stack),createRadeonGpuPage(state->osd.get()),"radeon");
    state->overclockPage=createOvClockPage();gtk_stack_add_named(GTK_STACK(state->stack),state->overclockPage,"ovclock");
    gtk_box_append(GTK_BOX(root),state->stack);

    ui::footerStatuses().clear();
    state->status=Canvas::create([state](Canvas& c,int width,int height){
            const ui::Scale s{c.dpi()};const auto right=width-s(mc::gui::kContentPadding);
            c.place(state->versionButton,{right-textWidth("MagicControl " MAGICCONTROL_VERSION_STRING,Font::Body,c.dpi()),0,right,height});return 0;},
        [state](Canvas& c,cairo_t* cr,int width,int height){
            const ui::Scale s{c.dpi()};fill(cr,{0,0,width,height},theme::kTitleBar);
            fill(cr,{0,0,width,1},theme::kDivider);
            const std::string brand="MagicControl " MAGICCONTROL_VERSION_STRING;
            const int brandWidth=textWidth(brand,Font::Body,c.dpi());
            // One shaped string and one gradient: no per-character clipping/ellipsis.
            gradientText(cr,brand,{0,0,width-s(mc::gui::kContentPadding),height},Font::Body,
                mc::gui::rgb(166,92,255),mc::gui::rgb(40,210,222),c.dpi());
            const int powerRight=width-s(mc::gui::kContentPadding)-brandWidth-s(16);
            const std::string power="Потребление: — Вт";const int powerWidth=textWidth(power,Font::Body,c.dpi());
            text(cr,power,{powerRight-powerWidth,0,powerRight,height},Font::Body,theme::kBatteryDischarging,c.dpi());
            auto status=ui::footerStatuses().text(ui::footerSource(state->active));
            if(ui::laptopPreview())status="Просмотр профиля · аппаратное управление отключено";
            else if(status.empty())status=state->huawei?"Huawei · huawei-wmi sysfs":"EC · Linux-транспорт не подключён";
            text(cr,status,{s(mc::gui::kContentPadding),0,powerRight-powerWidth-s(12),height},
                 Font::Body,theme::kTextMuted,c.dpi());
        },false,mc::gui::kStatusBarHeight);
    struct FooterTarget {
        GWeakRef widget;
        explicit FooterTarget(GtkWidget* target){g_weak_ref_init(&widget,G_OBJECT(target));}
        ~FooterTarget(){g_weak_ref_clear(&widget);}
    };
    auto footerTarget=std::make_shared<FooterTarget>(state->status->widget());
    ui::footerStatuses().observe([footerTarget]{
        auto* object=g_weak_ref_get(&footerTarget->widget);if(!object)return;
        g_main_context_invoke_full(nullptr,G_PRIORITY_DEFAULT,+[](gpointer data)->gboolean{
            gtk_widget_queue_draw(GTK_WIDGET(data));return G_SOURCE_REMOVE;
        },object,+[](gpointer data){g_object_unref(data);});
    });
    state->versionButton=chromeButton("Проверить обновления",[state]{
        showUpdateWindow(state->window,[state]{state->exiting=true;gtk_window_close(state->window);});
    });
    gtk_button_set_child(GTK_BUTTON(state->versionButton),gtk_label_new(""));
    gtk_widget_set_cursor_from_name(state->versionButton,"pointer");
    setWidgetTooltip(state->versionButton,"Проверить обновления MagicControl");
    state->status->add(state->versionButton);
    gtk_box_append(GTK_BOX(root),state->status->widget());

    state->header=Canvas::create([state](Canvas& c,int width,int height){
        const auto g=ui::headerGeometry(width,height,tabWidths(c.dpi()),{c.dpi()},brandWidth(*state,c.dpi()),mc::gui::kTabCount,ui::visibleGpuTabs(state->nvidia,state->radeon,state->overclockAllowed));
        c.place(state->headerIcon,g.icon);
        const auto menu=brandMenuRect(*state,g.brand,c.dpi());c.place(state->brand,menu);
        if(state->languageMenu)gtk_widget_set_margin_start(state->languageMenu,static_cast<int>(menu.left/c.scale()));
        for(int i=0;i<mc::gui::kTabCount;++i)if(ui::visibleGpuTabs(state->nvidia,state->radeon,state->overclockAllowed)[i])c.place(state->tabs[i],g.tabs[i]);
        for(int i=0;i<3;++i)c.place(state->captions[i],g.captionButtons[i]);
        return height;
    },[state](Canvas& c,cairo_t* cr,int width,int height){
        const int dpi=c.dpi();const ui::Scale s{dpi};
        const auto g=ui::headerGeometry(width,height,tabWidths(dpi),s,brandWidth(*state,dpi),mc::gui::kTabCount,ui::visibleGpuTabs(state->nvidia,state->radeon,state->overclockAllowed));
        fill(cr,{0,0,width,height},theme::kTitleBar);fill(cr,{0,height-1,width,height},theme::kDivider);
        if(hover(state->brand))fill(cr,brandMenuRect(*state,g.brand,dpi),theme::kHoverOverlay);
        if(g.brand.width()>0) {
            const Font brandFont=manufacturerFont();
            const auto device=ui::deviceBrandGeometry(g.brand,s,state->manufacturerWidth,state->modelWidth,
                ui::textMetrics(brandFont,dpi));
            if(state->hostnameBrandWidth>0) {
                const Rect prefix{device.manufacturer.left,device.manufacturer.top,
                    device.manufacturer.left+state->hostnameBrandWidth,device.manufacturer.bottom};
                wordmark(cr,prefix,dpi,"MECHREVO",Font::Brand);
                text(cr,state->device.manufacturer.substr(8),
                    {device.manufacturer.right,device.model.top,device.model.right,device.model.bottom},
                    Font::Body,theme::kTextSecondary,dpi,
                    Horizontal::Left,Vertical::Top);
            } else wordmark(cr,device.manufacturer,dpi,state->device.manufacturer,brandFont);
            text(cr,state->device.model,device.model,Font::Body,theme::kTextSecondary,dpi,Horizontal::Left,Vertical::Top);
            const int x=device.arrowX,y=device.arrowY,half=s(4);
            cairo_set_source_rgb(cr,(theme::kTextSecondary&255)/255.0,((theme::kTextSecondary>>8)&255)/255.0,
                                    ((theme::kTextSecondary>>16)&255)/255.0);
            cairo_move_to(cr,x-half,y-half/2);cairo_line_to(cr,x+half,y-half/2);cairo_line_to(cr,x,y+half);cairo_close_path(cr);cairo_fill(cr);
        }
        for(int i=0;i<mc::gui::kTabCount;++i)if(ui::visibleGpuTabs(state->nvidia,state->radeon,state->overclockAllowed)[i]) {
            const bool active=state->active==i;
            if(hover(state->tabs[i])&&!active)fill(cr,g.tabs[i],theme::kHoverOverlay);
            text(cr,tabTitles[i],g.tabs[i],active?Font::TabActive:Font::Tab,
                 active?theme::kTextPrimary:theme::kTextSecondary,dpi,Horizontal::Center);
            if(active)fill(cr,{g.tabs[i].left,height-s(mc::gui::kTabUnderlineHeight),g.tabs[i].right,height},theme::kAccent);
        }
        for(int i=0;i<3;++i) {
            if(hover(state->captions[i]))fill(cr,g.captionButtons[i],i==2?theme::kCloseHover:theme::kHoverOverlay);
            windowGlyph(cr,g.captionButtons[i],i,gtk_window_is_maximized(state->window),
                hover(state->captions[i])?theme::kTextPrimary:theme::kTextSecondary,dpi);
        }
    },false,mc::gui::kTitleBarHeight);
    state->header->add(state->headerIcon);
    state->languageMenu=createAboutPanel(state->window,state->selectedLanguage,[state](int language){
        std::string error;
        if(!mc::linux_platform::saveApplicationLanguage(static_cast<unsigned>(language),error)){mc::logMessage("language",error);return;}
        state->selectedLanguage=language;ui::setLanguage(static_cast<unsigned>(language));
        state->brandDpi=0;refreshUiLanguage(state->window);
        refreshAboutPanel(state->languageMenu);
        // Real pages can be detached during model preview. Keep their source
        // captions and shaping current without recreating controls or scroll.
        for(auto* page:state->realPages)if(page)applyWidgetLanguage(page);
        if(state->tray)state->tray->refreshLanguage();
        if(state->osd)state->osd->refreshLanguage();
        gtk_widget_queue_resize(GTK_WIDGET(state->window));gtk_widget_queue_draw(GTK_WIDGET(state->window));
    },[state](int model){
        audit::input("gtk.model_menu","model_selected","\"requested_index\":"+std::to_string(model)+",\"previous_index\":"+std::to_string(ui::selectedLaptopIndex()));
        if(model<0||model>=static_cast<int>(ui::kSupportedLaptops.size())||model==ui::selectedLaptopIndex())return;
        const auto board=readBoardDescriptor();const bool wasPreview=ui::laptopPreview(),preview=model!=ui::currentLaptop(board);
        suspendKeysPage(gtk_stack_get_child_by_name(GTK_STACK(state->stack),"keys"));
        state->osd->hide(); // Do not replay a notification from another model view.
        for(int i=0;i<3;++i){auto* page=gtk_stack_get_child_by_name(GTK_STACK(state->stack),pageNames[i]);
            if(!wasPreview&&preview)state->realPages[i]=GTK_WIDGET(g_object_ref(page));
            gtk_stack_remove(GTK_STACK(state->stack),page);}
        ui::laptopSelection.store(model);
        state->overclockAllowed=state->overclockBoard&&!preview;
        ui::footerStatuses().clear();
        const bool gpuVisible=ui::showNvidiaTab(state->nvidia);gtk_widget_set_visible(state->tabs[4],gpuVisible);
        gtk_widget_set_visible(state->tabs[5],ui::showRadeonTab(state->radeon));
        gtk_widget_set_visible(state->tabs[6],state->overclockAllowed);
        if(preview&&state->active==6)state->active=0;
        if((!gpuVisible&&state->active==4)||(!ui::showRadeonTab(state->radeon)&&state->active==5))state->active=0;
        if(preview){gtk_stack_add_named(GTK_STACK(state->stack),createFansPage(state->osd.get()),"fans");gtk_stack_add_named(GTK_STACK(state->stack),createSettingsPage(state->osd.get()),"sets");gtk_stack_add_named(GTK_STACK(state->stack),createKeysPage(state->osd.get()),"keys");}
        else{for(int i=0;i<3;++i){gtk_stack_add_named(GTK_STACK(state->stack),state->realPages[i],pageNames[i]);g_object_unref(state->realPages[i]);state->realPages[i]=nullptr;}}
        refreshBiosProfile(gtk_stack_get_child_by_name(GTK_STACK(state->stack),"bios"));
        restoreKeysPage(gtk_stack_get_child_by_name(GTK_STACK(state->stack),"keys"),"profile_changed");
        state->device=ui::deviceHeader(ui::presentationBoard());if(preview)state->device.model="Просмотр · "+state->device.model;
        setWidgetAccessibleLabel(state->brand,state->device.label().c_str());
        state->brandDpi=0;gtk_widget_queue_resize(state->header->widget());state->header->redraw();
        gtk_stack_set_visible_child_name(GTK_STACK(state->stack),pageNames[state->active]);
        const auto name=std::string(ui::kSupportedLaptops[model].name);
        const auto detail=preview?"Просмотр профиля · аппаратное управление отключено":"Профиль текущего ноутбука";
        state->osd->show({"device-profile",preview?"Просмотр ноутбука":"Ноутбук",name,mc::gui::osd::Icon::Information});
    });
    gtk_widget_set_margin_top(state->languageMenu,mc::gui::kTitleBarHeight);
    gtk_widget_set_visible(state->languageMenu,FALSE);
    gtk_overlay_add_overlay(state->overlay,state->languageMenu);
    auto* menuKeys=gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(menuKeys,GTK_PHASE_CAPTURE);
    g_signal_connect(menuKeys,"key-pressed",G_CALLBACK(+[](GtkEventControllerKey*,guint key,guint,GdkModifierType,gpointer p)->gboolean{
        auto* state=static_cast<WindowState*>(p);
        if(key!=GDK_KEY_Escape||!gtk_widget_get_visible(state->languageMenu))return FALSE;
        gtk_widget_set_visible(state->languageMenu,FALSE);return TRUE;
    }),state);gtk_widget_add_controller(GTK_WIDGET(state->window),menuKeys);
    GtkGesture* dismiss=gtk_gesture_click_new();gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(dismiss),0);
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(dismiss),GTK_PHASE_CAPTURE);
    g_signal_connect(dismiss,"pressed",G_CALLBACK(+[](GtkGestureClick* gesture,int,double x,double y,gpointer data){
        auto* state=static_cast<WindowState*>(data);
        if(!gtk_widget_get_visible(state->languageMenu))return;
        GtkWidget* picked=gtk_widget_pick(GTK_WIDGET(state->overlay),x,y,GTK_PICK_DEFAULT);
        for(GtkWidget* child=picked;child&&child!=GTK_WIDGET(state->overlay);child=gtk_widget_get_parent(child))
            if(child==state->languageMenu||child==state->brand)return;
        gtk_widget_set_visible(state->languageMenu,FALSE);
        static_cast<void>(gesture);
    }),state);
    gtk_widget_add_controller(GTK_WIDGET(state->overlay),GTK_EVENT_CONTROLLER(dismiss));
    state->brand=chromeButton(state->device.label().c_str(),[state]{
        gtk_widget_set_visible(state->languageMenu,!gtk_widget_get_visible(state->languageMenu));
    });
    gtk_button_set_child(GTK_BUTTON(state->brand),gtk_label_new(""));
    state->header->add(state->brand);watchHover(state->brand,state->header);
    for(int i=0;i<mc::gui::kTabCount;++i) {
        state->tabs[i]=chromeButton(tabTitles[i],[state,i]{
            audit::event("tab_selected","\"tab\":"+std::to_string(i)+",\"previous_tab\":"+std::to_string(state->active));state->active=i;gtk_stack_set_visible_child_name(GTK_STACK(state->stack),pageNames[i]);state->header->redraw();state->status->redraw();
            if(i==6&&state->overclockAllowed)refreshOvClockPage(state->overclockPage);
        });
        state->header->add(state->tabs[i]);watchHover(state->tabs[i],state->header);
    }
    gtk_widget_set_visible(state->tabs[4],ui::showNvidiaTab(state->nvidia));gtk_widget_set_visible(state->tabs[5],ui::showRadeonTab(state->radeon));
    gtk_widget_set_visible(state->tabs[6],state->overclockAllowed);
    state->deviceTimer=g_timeout_add(5000,+[](gpointer data)->gboolean{auto& s=*static_cast<WindowState*>(data);const bool present=nvidiaGpuPresent(),radeon=radeonDiscretePresent();
        if(present!=s.nvidia||radeon!=s.radeon){s.nvidia=present;s.radeon=radeon;const bool visible=ui::showNvidiaTab(present),rv=ui::showRadeonTab(radeon);gtk_widget_set_visible(s.tabs[4],visible);gtk_widget_set_visible(s.tabs[5],rv);if((!visible&&s.active==4)||(!rv&&s.active==5)){s.active=0;gtk_stack_set_visible_child_name(GTK_STACK(s.stack),"fans");}gtk_widget_queue_resize(s.header->widget());s.header->redraw();}
        return G_SOURCE_CONTINUE;},state);
    const char* captions[]{"Свернуть","Развернуть","Закрыть"};
    for(int i=0;i<3;++i) {
        state->captions[i]=chromeButton(captions[i],[state,i]{
            if(i==0)gtk_window_minimize(state->window);
            else if(i==2){gtk_window_close(state->window);return;}
            else if(gtk_window_is_maximized(state->window))gtk_window_unmaximize(state->window);
            else gtk_window_maximize(state->window);
            state->header->redraw();
        });
        state->header->add(state->captions[i]);watchHover(state->captions[i],state->header);
    }
    GtkWidget* handle=gtk_window_handle_new();gtk_window_handle_set_child(GTK_WINDOW_HANDLE(handle),state->header->widget());
    gtk_widget_add_css_class(handle,"magiccontrol-titlebar");
    gtk_box_prepend(GTK_BOX(root),handle);
    GtkGesture* resize=gtk_gesture_click_new();gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(resize),GDK_BUTTON_PRIMARY);
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(resize),GTK_PHASE_CAPTURE);
    g_signal_connect(resize,"pressed",G_CALLBACK(resizePressed),state->window);
    gtk_widget_add_controller(GTK_WIDGET(state->window),GTK_EVENT_CONTROLLER(resize));
    initializeRotation(*state);
    GtkEventController* locks=gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(locks,GTK_PHASE_CAPTURE);
    g_signal_connect(locks,"key-released",G_CALLBACK(+[](GtkEventControllerKey* controller,guint key,guint,GdkModifierType,gpointer data){
        auto* window=static_cast<WindowState*>(data);
        if(key==GDK_KEY_RotateWindows){
            if(!window->exiting&&window->rotation&&(!window->rotationHotkey||!window->rotationHotkey->bound()))window->rotation->request(mc::gui::osd::now());
            return;
        }
        GdkDevice* device=gtk_event_controller_get_current_event_device(GTK_EVENT_CONTROLLER(controller));
        if(!device)return;
        mc::SystemEvent event;event.receivedAt=mc::gui::osd::now();
        if(key==GDK_KEY_Caps_Lock){event.kind=mc::SystemEventKind::CapsLock;event.value=gdk_device_get_caps_lock_state(device);}
        else if(key==GDK_KEY_Num_Lock){event.kind=mc::SystemEventKind::NumLock;event.value=gdk_device_get_num_lock_state(device);}
        else if(key==GDK_KEY_Scroll_Lock){event.kind=mc::SystemEventKind::ScrollLock;event.value=gdk_device_get_scroll_lock_state(device);}
        else return;
        window->osd->show(mc::gui::osd::system(event,"Linux"));
    }),state);
    gtk_widget_add_controller(GTK_WIDGET(state->window),locks);
    g_signal_connect(state->window,"map",G_CALLBACK(+[](GtkWidget* w,gpointer){
        auto* current=static_cast<WindowState*>(g_object_get_data(G_OBJECT(w),"magiccontrol-window"));
        if(current){current->brandDpi=0;refreshAboutPanel(current->languageMenu);}
        g_timeout_add_full(G_PRIORITY_DEFAULT,250,report,g_object_ref(w),g_object_unref);
    }),nullptr);
    // Starting the application must not arm a numeric field for wheel edits.
    gtk_window_set_focus(state->window,state->tabs[0]);
    refreshUiLanguage(state->window);
    gtk_window_present(state->window);
    if(g_object_get_data(G_OBJECT(application),"magiccontrol-osd-preview"))
        state->osd->show(mc::gui::osd::keyboard(2,"Предпросмотр уведомления · параметры не меняются",mc::gui::osd::now()));
}
}  // namespace mc::gtkui
