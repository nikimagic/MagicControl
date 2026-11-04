#include "SettingsServicesLinux.h"
#include "MagicControl/LaptopLightingResource.h"
#include "MagicControl/LaptopChargeResource.h"
#include "MagicControl/PortableSettings.h"
#include "MagicControl/CustomPresets.h"
#include "MagicControl/BoardIdentity.h"
#include "MagicControl/DeviceProfile.h"
#include "MagicControl/KeysLifecycle.h"
#include "MagicControl/RomIdHistory.h"
#include "MagicControl/BatteryUiPolicy.h"
#include "MagicControl/SettingsResourceControl.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <unistd.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <cerrno>
#include <cstring>

namespace mc::linux_platform {
std::shared_ptr<void> acquireApplicationControlLease(){
    // Keep the inode for the whole application lifetime; never unlink a held
    // lease (a recreated file would allow two independent owners).
    struct Lease {int fd{-1};~Lease(){if(fd>=0){flock(fd,LOCK_UN);close(fd);}}};
    auto lease=std::make_shared<Lease>();
    constexpr const char* path="/tmp/magiccontrol.application-control.v1.lock";
    auto& fd=lease->fd;
    fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);
    if(fd<0&&errno==ENOENT){
        fd=open(path,O_RDWR|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0644);
        if(fd>=0&&fchmod(fd,0644))throw std::runtime_error("Cannot publish application control lease: "+std::string(std::strerror(errno)));
        if(fd<0&&errno==EEXIST)fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);
    }
    if(fd<0)throw std::runtime_error("Cannot open application control lease: "+std::string(std::strerror(errno)));
    struct stat state{};
    if(fstat(fd,&state)||!S_ISREG(state.st_mode)||state.st_nlink!=1)throw std::runtime_error("Invalid application control lease file");
    if(flock(fd,LOCK_EX|LOCK_NB)){
        const auto error=errno;
        if(error==EWOULDBLOCK||error==EAGAIN)throw std::runtime_error("GUI or another CLI control command is active; close it before applying SSH control");
        throw std::runtime_error("Cannot acquire application control lease: "+std::string(std::strerror(error)));
    }
    return lease;
}
bool batteryControlsAvailable(const LaptopControl* control,const LaptopControlSnapshot& snapshot) noexcept {
    return snapshot.chargeWritable&&nativeBatteryPolicySupported(control);
}
std::string readHostnameFallback() {
    char hostname[256]{};
    return gethostname(hostname,sizeof(hostname)-1)==0&&hostname[0]?hostname:"";
}
bool hasAnyOnlinePowerSupply() {
    std::error_code error;
    for(const auto& supply:std::filesystem::directory_iterator("/sys/class/power_supply",error)) {
        std::ifstream status(supply.path()/"online");int online{};
        if(status>>online && online==1)return true;
    }
    return false;
}
StartupSettingsPreparation prepareStartupSettings() {
    StartupSettingsPreparation result;
    try{migratePortableSettings();}
    catch(const std::exception& error){result.portableError=error.what();return result;}
    recordStartupRomId(); // Also records launches that activate an existing instance.
    try{migrateKeysPreferences(deviceProfile(readBoardDescriptor()).id);}
    catch(const std::exception& error){result.keysError=error.what();}
    return result;
}
int loadSelectedCustomPreset(std::string_view profile) {
    ensureCustomPresets(profile);
    return loadCustomPresetLibrary(profile).selected;
}
void loadOrCreateApplicationSettings(ApplicationPreferences& preferences,std::string& error) {
    const auto path=portableSettingsPath("app-settings.ini");
    if(!loadApplicationPreferences(path,preferences,error)&&error.empty())
        static_cast<void>(saveApplicationPreferences(path,preferences,error));
}
bool saveApplicationLanguage(unsigned language,std::string& error) {
    ApplicationPreferences preferences;
    const auto path=portableSettingsPath("app-settings.ini");
    if(!loadApplicationPreferences(path,preferences,error)&&!error.empty())return false;
    preferences.language=language;
    return saveApplicationPreferences(path,preferences,error);
}
void collectSettingsReadings(MonitoringReader& reader,LaptopControl* control,
                             BatteryReading& battery,BatteryReading& osBattery,
                             EcBatteryReading& ecBattery,BatteryPowerPolicy& policy,
                             LaptopControlSnapshot& snapshot) {
    // Update outputs in the original order so a failed later read retains the
    // same partial observation and exception handling as the existing worker.
    BatteryReading osObservation;
    battery=reader.readBatteryOnly(&osObservation);
    osBattery=std::move(osObservation);
    ecBattery=reader.readEcBatteryOnly();
    policy=reader.readBatteryPowerPolicy();
    if(control)snapshot=control->read();
}
bool settingsReadbackMatches(const LaptopControlSnapshot& snapshot,int upper,int restart,int level,int timeout) {
    return snapshot.connected && (upper<0||snapshot.chargeEnd==upper) &&
        (restart<0||snapshot.chargeStart==restart) && lightingReadbackMatches(snapshot,LightingWriteRequest::nativeUpdate(level,timeout));
}
SettingsWorkerResult executeSettingsOperation(MonitoringReader& reader,LaptopControl* control,
                                              SettingsWorkerRequest request,SettingsReadingOutputs output) {
    // Backend presence is derived here rather than trusted from the caller.
    request.hasBackend=control!=nullptr;
    return executeSettingsWorkerLinux(request,{
        [&]{return mc::executeBatteryResource(*control,BatteryWriteRequest::native(request.upper,request.restart));},
        [&]{const auto result=mc::executeLightingResource(*control,LightingWriteRequest::nativeUpdate(request.keyboardLevel,request.keyboardTimeout));return SettingsWorkerResult{result.success,result.message};},
        [&]{collectSettingsReadings(reader,control,output.battery,output.osBattery,output.ecBattery,output.policy,output.snapshot);},
        [&]{return settingsReadbackMatches(output.snapshot,request.upper,request.restart,request.keyboardLevel,request.keyboardTimeout);}
    });
}
}

#include "SettingsFailureLinux.h"
#include "MagicControl/ControlAudit.h"
#include "MagicControl/Logging.h"
namespace mc::linux_platform {
SettingsOperationsLinux::SettingsOperationsLinux(BoardDescriptor board,bool viewing)
    :pageBoard(std::move(board)),preview(viewing),brightnessMaximum(deviceProfile(pageBoard).keyboardBrightnessMaximum),batteryView(initialBatteryCardView(deviceProfile(pageBoard).id)),reader(viewing?nullptr:std::make_unique<MonitoringReader>()),oem(viewing?nullptr:createLaptopControl()){if(!preview&&!deviceProfile(pageBoard).id.empty())try{selectedCustomPreset=loadSelectedCustomPreset(deviceProfile(pageBoard).id);}catch(const std::exception& e){logMessage("custom_presets",e.what());}}
bool SettingsOperationsLinux::chargeAvailable()const noexcept{return batteryControlsAvailable(oem.get(),oemState);}
bool SettingsOperationsLinux::submit(SettingsWorkerRequest request,std::function<void()> ready){
    if(preview){immediate_=SettingsPageResult{};immediate_->message="Просмотр профиля · аппаратное управление отключено";return false;}
    if(observationEpoch.asleep())return false;
    if(worker_.busy()){if(request.writing())audit::event("command_rejected","\"surface\":\"SETs\",\"reason\":\"busy\"");return false;}
    request.hasBackend=static_cast<bool>(oem);pending_={};pending_.epoch=observationEpoch.capture();
    using namespace mc::gui;
    if(request.upper>=0)pending_.notice={"battery","Лимит зарядки "+std::to_string(request.upper)+"%","Применяю…",osd::Icon::Battery,request.upper};
    else if(request.restart>=0)pending_.notice={"battery","Батарея","Применяю нижний порог…",osd::Icon::Battery};
    else if(request.keyboardLevel>=0)pending_.notice=osd::lighting(LightingZone::Keyboard,request.keyboardLevel?std::to_string(request.keyboardLevel)+" / "+std::to_string(brightnessMaximum):"Выкл.",request.keyboardLevel,brightnessMaximum);
    else if(request.keyboardTimeout>=0)pending_.notice=osd::lighting(LightingZone::Keyboard,"Авто Выкл");
    if(request.writing()&&beginNotice_)pending_.token=beginNotice_(pending_.notice);
    try{worker_.launch([this,request,id=audit::capture()]{
        if(testWorkerFault==1)throw 42;
        if(testWorkerFault==2){pending_.success=true;throw 42;}
        if(!reader)throw std::logic_error("settings reader unavailable");
        audit::Scope trace("settings.apply","\"upper\":"+std::to_string(request.upper)+",\"keyboard_level\":"+std::to_string(request.keyboardLevel)+",\"keyboard_timeout\":"+std::to_string(request.keyboardTimeout)+",\"restart\":"+std::to_string(request.restart),id,request.writing());
        const auto outcome=executeSettingsOperation(*reader,oem.get(),request,{pending_.battery,pending_.osBattery,pending_.ecBattery,pending_.policy,pending_.oem});
        pending_.success=outcome.success;pending_.message=outcome.message;
        if(request.writing())trace.finish(pending_.success?"completed_with_observation":"failed","\"message\":"+audit::quote(pending_.message)+",\"observed\":"+audit::laptop(pending_.oem));
    },std::move(ready),request.writing()?ResourceWorkClass::Interactive:ResourceWorkClass::Observation);}
    catch(...){SettingsPageResult result;result.startFailed=true;result.message="Не удалось запустить обработчик SETs.";result.token=pending_.token;result.notice=pending_.notice;immediate_=std::move(result);return false;}
    return true;
}
std::optional<SettingsPageResult> SettingsOperationsLinux::take(){
    if(immediate_){auto result=std::move(immediate_);immediate_.reset();return result;}
    if(!worker_.takeCompletion())return {};
    SettingsPageResult result;result.token=pending_.token;result.notice=pending_.notice;result.success=pending_.success;result.message=pending_.message;
    if(worker_.failure()){
        result.workerEscaped=true;result.success=false;result.message="Ошибка SETs: состояние не подтверждено.";batteryResources.clear();
        invalidateSettingsAfterWorkerFailure(oemDisplayCache,oemState,reading,osBattery,ecBattery);return result;
    }
    if(!observationEpoch.current(pending_.epoch)){result.superseded=true;result.success=false;result.message="Состояние изменилось во время сна; требуется повторное чтение.";result.retry=!observationEpoch.asleep();return result;}
    reading=std::move(pending_.battery);batteryResources.update(pageBoard,reading);
    oemState=oemDisplayCache.update(std::move(pending_.oem));osBattery=std::move(pending_.osBattery);ecBattery=ecDisplayCache.update(std::move(pending_.ecBattery));batteryPolicy=std::move(pending_.policy);
    if(oem&&result.message.empty())result.message=oemState.chargeWritable&&!chargeAvailable()?"Управление зарядкой для этого backend не подключено.":oemState.status;
    result.accepted=true;return result;
}
bool SettingsOperationsLinux::transitionSleep(bool asleep){const bool was=observationEpoch.asleep();observationEpoch.transition(asleep);if(was!=asleep)batteryResources.clear();return was&&!asleep&&!busy();}
std::string SettingsOperationsLinux::selectBatteryView(BatteryCardView selected){
    batteryView=selected;std::string error;
    if(preview)return error;
    rememberBatteryView(deviceProfile(pageBoard).id,static_cast<int>(selected),error);
    logMessage("battery","Card view selected="+std::to_string(static_cast<int>(selected))+"; Linux EC writes=0");return error;
}

}
