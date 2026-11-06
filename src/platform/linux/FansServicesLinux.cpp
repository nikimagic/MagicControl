#include "FansServicesLinux.h"
#include "MagicControl/FanControlLimits.h"
#include "MagicControl/DeviceProfile.h"
#include <utility>
#include "MagicControl/ControlAudit.h"
#include "MagicControl/Logging.h"
#include <cstdio>

namespace mc::linux_platform {
std::string executeFanResource(const FanWriteRequest& request){return request.operation==FanResourceOperation::Online?
    "Синхронизация вентиляторов недоступна: Linux EC-транспорт не подключён.":"Управление вентиляторами недоступно: Linux EC-транспорт не подключён.";}
MonitoringSnapshot FansOperationsLinux::readMonitoring() {
    MonitoringSnapshot sample;
    try{if(reader_)sample=reader_->read();}catch(...){}
    return sample;
}
MonitoringSnapshot FansOperationsLinux::presentMonitoring(MonitoringSnapshot sample) {
    return displayCache_.update(std::move(sample));
}
bool FansOperationsLinux::loadProfiles(std::string_view profile,FanCurveProfiles& curves,std::string& error) {
    for(const auto* p:deviceProfiles())if(p->id==profile)for(int ch=0;ch<2;++ch)curves.setActivePointCount(ch,p->fanPointCounts[ch]);
    const bool loaded=loadFanCurveProfiles(portableSettingsPath(std::string(gmFanCurveFile(profile))),curves,error);
    for(const auto* p:deviceProfiles())if(p->id==profile)for(int ch=0;ch<2;++ch)curves.setActivePointCount(ch,p->fanPointCounts[ch]);return loaded;
}
bool FansOperationsLinux::saveProfiles(std::string_view profile,const FanCurveProfiles& curves,std::string& error) {
    return saveFanCurveProfiles(portableSettingsPath(std::string(gmFanCurveFile(profile))),curves,error);
}
FansOperationsLinux::FansOperationsLinux(BoardDescriptor board,bool viewing,std::function<MonitoringSnapshot()> source)
    :resourceBoard(std::move(board)),preview(viewing),monitoringSource(std::move(source)),reader_(viewing?nullptr:std::make_unique<MonitoringReader>(nullptr,false,true)){for(int ch=0;ch<2;++ch)profiles.setActivePointCount(ch,deviceProfile(resourceBoard).fanPointCounts[ch]);if(preview)profiles.linked=deviceProfile(resourceBoard).id=="lenovo-legion-kwcn";}
bool FansOperationsLinux::poll(std::function<void()> ready){
    if(preview||worker_.busy())return false;
    try{worker_.launch([this]{try{pending_=monitoringSource?monitoringSource():readMonitoring();}catch(...){pending_={};}},std::move(ready),ResourceWorkClass::Telemetry);return true;}
    catch(const std::exception& error){logMessage("fans",std::string("Observation start failed: ")+error.what());return false;}
}
std::optional<bool> FansOperationsLinux::take(){
    if(!worker_.takeCompletion())return {};
    if(worker_.telemetrySkipped())return {};
    auto shown=presentMonitoring(std::move(pending_));const bool changed=monitoring!=shown;monitoring=std::move(shown);
    resourceState.update(resourceBoard,monitoring);const ResourcePresentationView resources(resourceState);
    auto presentation=audit::presentation(monitoring);presentation.pop_back();
    const auto used=[&](unsigned channel,std::string_view metric){return resources.measurement(ResourceKind::Fan,channel,metric)!=nullptr;};
    presentation+=",\"resource_state\":"+audit::resourceStateSummary(resourceState)+",\"resource_state_accepted\":"+(resources.available()?"true":"false")+
        ",\"fan_resource_used\":[{\"speed\":"+(used(0,"speed")?"true":"false")+",\"duty\":"+(used(0,"duty")?"true":"false")+"},{\"speed\":"+(used(1,"speed")?"true":"false")+",\"duty\":"+(used(1,"duty")?"true":"false")+"}]}";
    audit::state("FANs.presentation",presentation,this);
    if(changed&&!loggedModels_){std::printf("FANs startup models: CPU=%s GPU=%s\n",monitoring.processorModels[0].c_str(),monitoring.processorModels[1].c_str());loggedModels_=true;}
    return changed;
}
void FansOperationsLinux::normalizeEditorDraft(int channel){
    if(profiles.activePointCount(channel)!=16)return;
    if(profiles.ecManual)fanInput::normalizeDraft(profiles.ecManualCurve);
    else if(profiles.currentPreset(channel)==FanCurvePreset::Manual)fanInput::normalizeDraft(profiles.curves[static_cast<std::size_t>(FanCurvePreset::Manual)][profiles.linked?0:channel]);
}
std::string FansOperationsLinux::loadDraft(){
    if(preview){profiles.selected.fill(FanCurvePreset::Default);return {};}
    std::string error;const bool loaded=loadProfiles(deviceProfile(resourceBoard).id,profiles,error);
    for(auto& selected:profiles.selected)if(selected==FanCurvePreset::Min)selected=FanCurvePreset::Manual;
    return !loaded&&!error.empty()?"Не удалось загрузить кривые: "+error:std::string{};
}
std::string FansOperationsLinux::saveDraft(){
    if(preview)return {};std::string error;
    return saveProfiles(deviceProfile(resourceBoard).id,profiles,error)?"Настройки сохранены. Управление вентиляторами недоступно: Linux EC-транспорт не подключён.":"Не удалось сохранить настройки: "+error;
}
std::string FansOperationsLinux::requestPreset(int channel,FanCurvePreset preset){
    if(preview){if(channel<0||channel>1)return "Просмотр: неверный канал";profiles.ecManual=false;
        if(preset==FanCurvePreset::Default||preset==FanCurvePreset::Max)profiles.selected.fill(preset);else profiles.select(channel,preset);
        return "Просмотр профиля · аппаратное управление отключено";}
    return executeFanResource(FanWriteRequest{preset==FanCurvePreset::Default?FanResourceOperation::Default:preset==FanCurvePreset::Max?FanResourceOperation::Maximum:FanResourceOperation::Manual});}
std::string FansOperationsLinux::requestLink(){if(preview){profiles.linked=!profiles.linked;return "Просмотр профиля · аппаратное управление отключено";}return executeFanResource(FanWriteRequest{FanResourceOperation::Online});}

}
