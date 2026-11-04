#include "ApplicationPreferencesWindows.h"
#include "NativeBatteryWindows.h"
#include "ConfigurationRestoreWindows.h"
#include "../../devices/minisforum/hpbsd/PowerControlWindows.h"
#include "../../devices/lenovo/legion-kwcn/ControlWindows.h"
#include "../../devices/mechrevo/gm6ir0c/ControlWindows.h"
#include "MagicControl/BoardIdentity.h"
#include "MagicControl/DeviceProfile.h"
#include "MagicControl/KeyboardBacklight.h"
#include "MagicControl/KeysLifecycle.h"
#include <exception>
#include <stdexcept>

namespace mc::windows {
void readKeysApplicationPreferences(ApplicationPreferences& preferences){
    KeysPreferences keys;std::string error;
    if(loadKeysPreferences(keysPreferencesPath(),keys,error)){
        preferences.osdDisabled=keys[KeyAction::Osd].value_or(false);
    }else if(!error.empty())throw std::runtime_error(error);
}
ApplicationPreferencesBootstrap loadApplicationPreferencesForStartup(ApplicationPreferences& preferences){
    ApplicationPreferencesBootstrap result;
    try{migrateKeysPreferences(deviceProfile(readBoardDescriptor()).id);}catch(const std::exception& e){result.keysError=e.what();}
    const auto path=portableSettingsPath("app-settings.ini");
    if(!loadApplicationPreferences(path,preferences,result.applicationError)&&result.applicationError.empty())
        static_cast<void>(saveApplicationPreferences(path,preferences,result.applicationError));
    try{readKeysApplicationPreferences(preferences);}catch(const std::exception& e){result.keysError+=std::string(" ")+e.what();}
    return result;
}
bool readStoredApplicationPreferences(ApplicationPreferences& preferences,std::string& error){
    return loadApplicationPreferences(portableSettingsPath("app-settings.ini"),preferences,error);
}
std::filesystem::path applicationPreferencesPath(){return portableSettingsPath("app-settings.ini");}
void saveApplicationPreferencesIfChanged(const std::filesystem::path& path,const ApplicationPreferences& previous,const ApplicationPreferences& next){
    const bool changed=next.embeddedFont!=previous.embeddedFont||next.allLogs!=previous.allLogs||
        next.batteryLog!=previous.batteryLog||next.language!=previous.language;
    std::string error;
    if(changed&&!saveApplicationPreferences(path,next,error))throw std::runtime_error(error);
}
void suspendKeyboardForPower(){
    suspendHpbsdPower();
    suspendLegionControl();
    setKeyboardLightingSuspended(true);
    if(auto gm6=gm6Controller())gm6->suspendKeyboard();
}
bool resumeKeyboardForPower(){
    requestHpbsdConfigurationRestore("resume");
    requestLegionConfigurationRestore("power_resume");
    setKeyboardLightingSuspended(false);
    return static_cast<bool>(gm6Controller());
}
void restoreNativeBatteryForPower(const std::string& reason){
    if(auto battery=nativeBatteryController())battery->restore(reason);
    else if(auto voltage=nativeBatteryVoltageController())voltage->restore(reason);
}
bool restoreDeviceConfiguration(const std::string& reason,ConfigurationRestore* configuration){
    requestHpbsdConfigurationRestore(reason);
    requestLegionConfigurationRestore(reason);
    auto gm6=gm6Controller();
    if(!configuration&&(!gm6||deviceProfile(readBoardDescriptor()).id!="mechrevo-gm6ir0c")){
        if(gm6)gm6->restoreKeyboard(reason);
        return false;
    }
    if(gm6){gm6->restoreKeyboard(reason);return true;}
    if(configuration)configuration->request(reason);
    return false;
}
}
