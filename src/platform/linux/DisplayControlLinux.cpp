#include "DisplayControlLinux.h"
#include "MagicControl/Json.h"
#include "MagicControl/Logging.h"
#include <gio/gio.h>
#include <cmath>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <optional>
namespace mc::linux_platform {
namespace {
std::string run(const std::vector<std::string>& args){
    gchar* binary=g_find_program_in_path(args.front().c_str());
    if(!binary)throw std::runtime_error("Для Sc установите "+args.front()+" в текущей графической сессии.");
    std::vector<const char*> argv{binary};for(std::size_t i=1;i<args.size();++i)argv.push_back(args[i].c_str());argv.push_back(nullptr);
    auto* launcher=g_subprocess_launcher_new(static_cast<GSubprocessFlags>(G_SUBPROCESS_FLAGS_STDOUT_PIPE|G_SUBPROCESS_FLAGS_STDERR_PIPE));
    g_subprocess_launcher_setenv(launcher,"LC_ALL","C",TRUE);GError* error{};
    auto* process=g_subprocess_launcher_spawnv(launcher,argv.data(),&error);g_object_unref(launcher);g_free(binary);
    if(!process){std::string message=error?error->message:"Display helper failed";g_clear_error(&error);throw std::runtime_error(message);}
    auto* context=g_main_context_new();g_main_context_push_thread_default(context);
    struct Call {GSubprocess* process;GCancellable* cancel;gchar* out{};gchar* errors{};GError* error{};bool done{},timeout{};} call{process,g_cancellable_new()};
    auto* deadline=g_timeout_source_new(10000);
    g_source_set_callback(deadline,+[](gpointer p)->gboolean{auto& c=*static_cast<Call*>(p);c.timeout=true;g_cancellable_cancel(c.cancel);g_subprocess_force_exit(c.process);return G_SOURCE_REMOVE;},&call,nullptr);g_source_attach(deadline,context);
    g_subprocess_communicate_utf8_async(process,nullptr,call.cancel,+[](GObject* object,GAsyncResult* result,gpointer p){auto& c=*static_cast<Call*>(p);g_subprocess_communicate_utf8_finish(G_SUBPROCESS(object),result,&c.out,&c.errors,&c.error);c.done=true;},&call);
    while(!call.done)g_main_context_iteration(context,TRUE);
    g_source_destroy(deadline);g_source_unref(deadline);g_main_context_pop_thread_default(context);g_main_context_unref(context);
    const bool ok=!call.error&&!call.timeout&&g_subprocess_get_successful(process);
    std::string output=call.out?call.out:"",failure=call.error?call.error->message:call.errors?call.errors:"Display helper failed";
    g_clear_error(&call.error);g_free(call.out);g_free(call.errors);g_object_unref(call.cancel);g_object_unref(process);
    if(call.timeout)throw std::runtime_error("Sc: системный дисплейный сервис не ответил за10сек; результат не подтверждён.");
    if(!ok)throw std::runtime_error(failure);if(output.size()>1024*1024)throw std::runtime_error("Display response too large");return output;
}
struct Panel {int id{},rotation{};std::string name;};
Panel kscreenPanel(){
    const auto config=json::JsonReader::parse(run({"kscreen-doctor","--json"}));std::optional<Panel> panel;
    for(const auto& output:config.at("outputs").array()){
        if(output.at("type").number()!=7||!output.at("connected").flag()||output.at("currentModeId").text().empty())continue;
        const auto& enabled=output.at("enabled");if(std::holds_alternative<bool>(enabled.value)&&!enabled.flag())continue;
        const double id=output.at("id").number(),rotation=output.at("rotation").number();
        if(id<0||id>100000||id!=std::floor(id)||rotation<0||rotation>128||rotation!=std::floor(rotation))throw std::runtime_error("KScreen сообщил неверную identity/ориентацию панели.");
        if(panel)throw std::runtime_error("Sc: несколько встроенных панелей; выбор неоднозначен.");
        panel=Panel{static_cast<int>(id),static_cast<int>(rotation),output.at("name").text()};
    }
    if(!panel||panel->name.empty())throw std::runtime_error("Sc: активная встроенная панель не найдена KScreen.");return *panel;
}
Panel xrandrPanel(){
    const auto response=run({"xrandr","--query"});std::istringstream rows(response);std::string line;std::optional<Panel> panel;
    const std::regex active(R"(^((?:eDP|LVDS|DSI)[A-Za-z0-9_.:-]*) connected(?: primary)? [0-9]+x[0-9]+[+-][0-9]+[+-][0-9]+(?: (normal|left|inverted|right))? \()");
    while(std::getline(rows,line)){std::smatch match;if(!std::regex_search(line,match,active))continue;
        if(panel)throw std::runtime_error("Sc: несколько встроенных панелей XRandR.");
        const auto rotation=match[2].str();panel=Panel{0,rotation=="left"?2:rotation=="inverted"?4:rotation=="right"?8:1,match[1].str()};}
    if(!panel)throw std::runtime_error("Sc: активная встроенная eDP/LVDS/DSI панель не найдена XRandR.");return *panel;
}
}
std::string toggleBuiltInDisplayRotation(){
    const char* wayland=g_getenv("WAYLAND_DISPLAY");const char* type=g_getenv("XDG_SESSION_TYPE");
    const bool isWayland=(wayland&&*wayland)||g_strcmp0(type,"wayland")==0;
    const char* desktop=g_getenv("XDG_CURRENT_DESKTOP");
    if(isWayland&&(!desktop||(!g_strrstr(desktop,"KDE")&&!g_strrstr(desktop,"Plasma"))))throw std::runtime_error("Sc: для этой Wayland-сессии не подключён штатный display adapter; поддерживается KDE/KScreen. XWayland не используется.");
    const bool kde=isWayland;
    const auto before=kde?kscreenPanel():xrandrPanel();int wanted{};const char* rotation{};int degrees{};
    switch(before.rotation){case 1:wanted=4;rotation="inverted";degrees=180;break;case 4:wanted=1;rotation=kde?"none":"normal";degrees=0;break;case 2:wanted=8;rotation="right";degrees=270;break;case 8:wanted=2;rotation="left";degrees=90;break;default:throw std::runtime_error("Sc: отражённая/неизвестная ориентация; изменение отменено.");}
    logMessage("display_rotation","Sc before="+std::to_string(before.rotation)+" target="+std::to_string(wanted)+" panel="+before.name+" backend="+(kde?"KScreen":"XRandR"));
    if(kde)run({"kscreen-doctor","output."+std::to_string(before.id)+".rotation."+rotation});else run({"xrandr","--output",before.name,"--rotate",rotation});
    for(int attempt=0;attempt<6;++attempt){const auto after=kde?kscreenPanel():xrandrPanel();
        if(after.name!=before.name||(kde&&after.id!=before.id))throw std::runtime_error("Sc: identity панели изменилась при переключении.");
        if(after.rotation==wanted)return "Ориентация "+std::to_string(degrees)+"° · подтверждено Linux · "+(kde?std::string("KScreen"):std::string("XRandR"));g_usleep(50000);}
    throw std::runtime_error("Linux не подтвердил поворот встроенного экрана повторным чтением.");
}
}
