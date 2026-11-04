#include <gtk/gtk.h>
#include <cstdio>
#include <filesystem>

#include <fontconfig/fontconfig.h>
#include <pango/pangocairo.h>
#include <pango/pangofc-fontmap.h>

#include "MagicControl/PortableSettings.h"
#include "MagicControl/KeysLifecycle.h"
#include "MagicControl/HardwareInventory.h"
#include "MagicControl/BoardIdentity.h"
#include "MagicControl/DeviceProfile.h"
#include "MagicControl/Logging.h"
#include "MagicControl/RomIdHistory.h"
#include "MainWindow.h"
#include "Ui.h"
#include "MagicControl/AppUpdate.h"
#include "../../platform/linux/SettingsServicesLinux.h"
#include "../../platform/common/DiagnosticDumpInternal.h"

namespace {
bool registerPortableFont() {
    auto* fontMap=pango_cairo_font_map_get_default();
    if(!PANGO_IS_FC_FONT_MAP(fontMap)) {
        g_warning("Portable language fonts unavailable: Pango fontconfig backend is not active");
        return false;
    }
    const auto directory=mc::portableSettingsPath("startup.log").parent_path()/"fonts";
    // An application-local collection prevents Pango from substituting an
    // installed desktop font for one of our four language variants.
    FcConfig* config=FcConfigCreate();
    if(!config) {
        g_warning("Portable language fonts unavailable: Fontconfig has no active configuration");
        return false;
    }
    bool loaded=true;
    for(const auto* name:{"NotoSansSC.ttf","NotoSansTC.ttf"}) {
        const auto path=directory/name;
        const bool added=std::filesystem::is_regular_file(path)&&
            FcConfigAppFontAddFile(config,reinterpret_cast<const FcChar8*>(path.string().c_str()));
        if(!added)g_warning("Could not load portable UI font: %s",path.string().c_str());
        loaded=loaded&&added;
    }
    {
        FcConfigBuildFonts(config);
        if(loaded){FcConfigSetCurrent(config);pango_fc_font_map_set_config(PANGO_FC_FONT_MAP(fontMap),config);}
        pango_fc_font_map_config_changed(PANGO_FC_FONT_MAP(fontMap));
        if(loaded)g_print("Portable UI fonts registered: Noto Sans SC / Noto Sans TC\n");
    }
    FcConfigDestroy(config);
    return loaded;
}
}

int main(int argc,char** argv) {
    if(auto result=mc::diagnostics::runFastDiagnosticHelper(std::vector<std::string>(argv,argv+argc)))return *result;
    if(auto result=mc::update::runHelper(std::vector<std::string>(argv,argv+argc)))return *result;
    const auto startupSettings=mc::linux_platform::prepareStartupSettings();
    if(startupSettings.portableError){g_printerr("Settings migration: %s\n",startupSettings.portableError->c_str());return EXIT_FAILURE;}
    if(startupSettings.keysError)g_printerr("KEYs migration: %s\n",startupSettings.keysError->c_str());
    mc::ApplicationPreferences preferences;
    std::string error;
    mc::linux_platform::loadOrCreateApplicationSettings(preferences,error);
    mc::gui::ui::setLanguage(preferences.language);
    g_set_print_handler(+[](const gchar* message){mc::logMessage("linux_stdout",message);});
    g_set_printerr_handler(+[](const gchar* message){mc::logMessage("linux_stderr",message);});
    g_log_set_default_handler(+[](const gchar* domain,GLogLevelFlags,const gchar* message,gpointer){
        mc::logMessage(domain?domain:"glib",message?message:"");
    },nullptr);
    g_log_set_writer_func(+[](GLogLevelFlags,const GLogField* fields,gsize count,gpointer)->GLogWriterOutput{
        for(gsize i=0;i<count;++i)if(g_strcmp0(fields[i].key,"MESSAGE")==0&&fields[i].value) {
            const auto* message=static_cast<const char*>(fields[i].value);
            mc::logMessage("gtk",fields[i].length<0?std::string_view(message):std::string_view(message,fields[i].length));
        }
        return G_LOG_WRITER_HANDLED;
    },nullptr,nullptr);
    struct Startup {mc::ApplicationPreferences preferences;std::string error;std::shared_ptr<void> controlLease;} startup{preferences,error,{}};
    GtkApplication* application=gtk_application_new("com.mechrevo.magiccontrol",G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(application,"startup",G_CALLBACK(+[](GApplication* app,gpointer data){
        auto* startup=static_cast<Startup*>(data);
        auto* preferences=&startup->preferences;
        try{startup->controlLease=mc::linux_platform::acquireApplicationControlLease();}
        catch(const std::exception& failure){g_warning("Application control unavailable: %s",failure.what());g_application_quit(app);return;}
        static_cast<void>(mc::initializeLogging(preferences->allLogs,{},preferences->batteryLog));
        if(!startup->error.empty())g_warning("Cannot load/create app-settings.ini: %s",startup->error.c_str());
        g_print("MagicControl Linux startup\n");
        const bool fontLoaded=registerPortableFont();
        g_object_set_data(G_OBJECT(app),"mc-fonts-ready",GINT_TO_POINTER(fontLoaded));
        if(!fontLoaded){g_warning("Bundled UI fonts missing or invalid; refusing system font fallback");return;}
        mc::gtkui::setEmbeddedFontEnabled(true);
        g_print("UI font: configured=embedded-only active=%s\n",
            mc::gui::ui::selectedFontFamily(static_cast<mc::gui::ui::Language>(preferences->language)));
    }),&startup);
    g_application_add_main_option(G_APPLICATION(application),"osd-preview",0,G_OPTION_FLAG_NONE,G_OPTION_ARG_NONE,"Preview the OSD without changing hardware",nullptr);
    g_signal_connect(application,"handle-local-options",G_CALLBACK(+[](GApplication* app,GVariantDict* options,gpointer)->gint{
        if(g_variant_dict_contains(options,"osd-preview"))g_object_set_data(G_OBJECT(app),"magiccontrol-osd-preview",GINT_TO_POINTER(1));
        return -1;
    }),nullptr);
    g_signal_connect(application,"activate",G_CALLBACK(+[](GtkApplication* app,gpointer){
        if(!g_object_get_data(G_OBJECT(app),"mc-fonts-ready")){g_application_quit(G_APPLICATION(app));return;}
        mc::gtkui::createMainWindow(app);
    }),nullptr);
    const int status=g_application_run(G_APPLICATION(application),argc,argv);
    g_object_unref(application);
    return status;
}
