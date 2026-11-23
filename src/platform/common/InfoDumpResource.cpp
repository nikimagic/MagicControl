#include "MagicControl/InfoDumpResource.h"
#include "MagicControl/InfoInventoryResource.h"
#include "MagicControl/InfoResourceControl.h"
#include <array>
#include <stdexcept>
#include "MagicControl/ActionLog.h"
#include "MagicControl/Logging.h"
#include "MagicControl/OperationMessages.h"
#include <chrono>
#include <format>

namespace mc {
namespace {
using Routes=ResourceControlTable<InfoDumpResourceRequest,InfoDumpResourceServices,InfoDumpResourceResult>;
InfoDumpResourceResult ec(const InfoDumpResourceRequest& request,const InfoDumpResourceServices& services){
    if(!services.ec)throw std::invalid_argument("INFO: EC collector unavailable");
    return {services.ec(request),{}};
}
InfoDumpResourceResult powerInput(const InfoDumpResourceRequest& request,const InfoDumpResourceServices& services){
    if(!services.powerInput)throw std::invalid_argument("INFO: power-input collector unavailable");
    return {services.powerInput(request),{}};
}
InfoDumpResourceResult bios(const InfoDumpResourceRequest& request,const InfoDumpResourceServices& services){
    if(!services.bios)throw std::invalid_argument("INFO: BIOS collector unavailable");
    return {services.bios(request),{}};
}
InfoDumpResourceResult vbios(const InfoDumpResourceRequest& request,const InfoDumpResourceServices& services){
    if(!services.vbios)throw std::invalid_argument("INFO: VBIOS collector unavailable");
    auto result=services.vbios(request);result.mode=request.vbiosMode;
    result.saved=result.requestedImageSaved();return {result,std::move(result)};
}
const Routes& routes(){
    static const auto result=[] {
        const auto e=infoResourceControlKey(InfoResourceOperation::EcDump),b=infoResourceControlKey(InfoResourceOperation::BiosDump),v=infoResourceControlKey(InfoResourceOperation::VbiosDump),p=infoResourceControlKey(InfoResourceOperation::PowerInputDump);
        const std::array<Routes::Binding,4> entries{{{e.kind,e.instance,e.operation,ec},{b.kind,b.instance,b.operation,bios},{v.kind,v.instance,v.operation,vbios},{p.kind,p.instance,p.operation,powerInput}}};
        return Routes(entries);
    }();
    return result;
}
InfoResourceOperation resourceOperation(InfoDumpKind kind){
    switch(kind){
    case InfoDumpKind::Ec:return InfoResourceOperation::EcDump;
    case InfoDumpKind::PowerInput:return InfoResourceOperation::PowerInputDump;
    case InfoDumpKind::Bios:return InfoResourceOperation::BiosDump;
    case InfoDumpKind::Vbios:return InfoResourceOperation::VbiosDump;
    }
    throw std::invalid_argument("INFO: unknown dump operation");
}
const InfoDumpResourceServices& productionServices(){
    static const InfoDumpResourceServices services{
        [](const auto& r){return createDiagnosticDump(r.access,r.cancellation,r.maximumProbe,r.hardwareGate,r.mode,r.firmwareDefault,r.displaySnapshot,r.outputRoot);},
        [](const auto& r){return createBiosRomDump(r.cancellation,r.progress,r.firmwarePause);},
        [](const auto& r){if(!r.powerSession)throw std::runtime_error("Power input diagnostic session unavailable");
            return createPowerInputDump(*r.powerSession,r.prompt,r.cancellation,r.access,r.displaySnapshot);},
        [](const auto& r){return createVbiosRomDump(r.cancellation,r.progress,r.pci,r.outputRoot,r.firmwarePause,r.vbiosMode);}
    };
    return services;
}
}
InfoDumpResourceResult executeInfoDumpResource(const InfoDumpResourceRequest& request,const InfoDumpResourceServices* services){
    return routes().execute(infoResourceControlKey(resourceOperation(request.kind)),request,services?*services:productionServices());
}
InfoInventoryResult executeInfoInventoryResource(std::stop_token token,
        const std::function<HardwareInventory(std::stop_token)>& collect,
        const std::function<bool(const HardwareInventory&,std::string&)>& save){
    InfoInventoryResult result;
    try{
        if(!token.stop_requested()){
            if(collect){auto observation=telemetryGate().tryObservation();if(!observation)throw std::runtime_error("INFO inventory unavailable while a firmware capture is active");result.inventory=collect(token);}
            else result.inventory=collectInfoInventoryResource(token);
        }
        result.cancelled=token.stop_requested();
        if(!result.cancelled){
            result.saved=save?save(result.inventory,result.error):InventoryCache::save(result.inventory,result.error);
            if(!result.saved&&result.error.empty())result.error="Не удалось сохранить снимок сведений";
        }
    }catch(const std::exception& e){result.error=e.what();}
    catch(...){result.error="Неизвестная ошибка сбора сведений";}
    result.cancelled=token.stop_requested();
    return result;
}
HardwareInventory collectInfoInventoryResource(std::stop_token cancellation,const IHardwareProvider* provider){
    auto observation=telemetryGate().tryObservation();
    if(!observation)throw std::runtime_error("INFO inventory unavailable while a firmware capture is active");
    using InventoryRoutes=ResourceControlTable<std::stop_token,const IHardwareProvider*,HardwareInventory>;
    static const auto inventory=[] {
        const auto key=infoResourceControlKey(InfoResourceOperation::Inventory);
        const std::array<InventoryRoutes::Binding,1> bindings{{{key.kind,key.instance,key.operation,
            [](const std::stop_token& token,const IHardwareProvider* const& source){return source->collect(token);}}}};
        return InventoryRoutes(bindings);
    }();
    const auto* selected=provider?provider:&defaultHardwareProvider();
    return inventory.execute(infoResourceControlKey(InfoResourceOperation::Inventory),cancellation,selected);
}

namespace {
const char* operationName(InfoResourceOperation operation)noexcept{
    switch(operation){
    case InfoResourceOperation::Inventory:return "info.inventory";
    case InfoResourceOperation::EcDump:return "info.ec_dump";
    case InfoResourceOperation::BiosDump:return "info.bios_dump";
    case InfoResourceOperation::VbiosDump:return "info.vbios_dump";
    case InfoResourceOperation::PowerInputDump:return "info.power_input_dump";
    case InfoResourceOperation::PawnIoPrepare:return "info.pawnio_prepare";
    case InfoResourceOperation::LenovoEnergyPrepare:return "info.lenovo_energy_prepare";
    }
    return "info.invalid";
}
}
InfoOperation::InfoOperation(InfoOperationRequest request,InfoOperationServices services)
    :request_(std::move(request)),services_(std::move(services)),messages_(OperationMessages::create(request_.auditId)){
    if(!request_.dump.hardwareGate)request_.dump.hardwareGate=std::make_shared<std::mutex>();
}
std::shared_ptr<InfoOperation> InfoOperation::start(InfoOperationRequest request,InfoOperationServices services){
    auto job=std::shared_ptr<InfoOperation>(new InfoOperation(std::move(request),std::move(services)));
    job->messages_->append("Сбор данных начался.");
    if(job->dumping()&&!job->firmwareDump()&&job->request_.dump.mode==DiagnosticDumpMode::ReadOnly)
        job->messages_->append("Диагностика только для чтения; пробы вентиляторов отключены.");
    if(job->firmwareDump())try{job->request_.dump.firmwarePause=telemetryGate().requestFirmwareCapture(job->cancellation_.get_token());}
    catch(const std::exception& e){job->fail(e.what());return job;}
    // The worker owns the job and all device/session capabilities, never a page.
    // Closing a window cancels without joining a potentially unbounded WMI call.
    try{submitDetachedResource([job]{
        try{job->run();}catch(const std::exception& e){job->fail(e.what());}
        catch(...){job->fail("Неизвестная ошибка INFO");}
    },job->firmwareDump()?ResourceWorkClass::Interactive:ResourceWorkClass::Observation);}
    catch(const std::exception& e){job->fail(e.what());}
    return job;
}
void InfoOperation::run(){
    {std::lock_guard lock(resultGate_);if(done())return;workerStarted_=true;}
    OperationMessages::Binding capture(*messages_);
    audit::Scope trace(operationName(operation()),{},request_.auditId);
    const auto started=std::chrono::steady_clock::now();
    try{
        const auto token=cancellation_.get_token();
        if(token.stop_requested())result_.message="Операция отменена.";
        else if(inventory()){
            auto collected=executeInfoInventoryResource(token,services_.collect,services_.save);
            result_.inventory=std::move(collected.inventory);result_.success=collected.saved&&!collected.cancelled;
            result_.message=result_.success?"Сведения обновлены и сохранены: "+result_.inventory.readAt+". Обновление — только по кнопке.":
                collected.cancelled?"Сбор сведений отменён.":"Операция не завершена: "+collected.error;
        }else if(operation()==InfoResourceOperation::PawnIoPrepare||operation()==InfoResourceOperation::LenovoEnergyPrepare){
            if(!services_.prepare||!services_.launch)throw std::runtime_error("Установщик драйвера недоступен в этой ОС");
            installer_=services_.prepare(token);
            if(!token.stop_requested()){
                trace.finish("prepared");
                phase_.store(Phase::AwaitingUi,std::memory_order_release);
                if(token.stop_requested())cancel();
                return;
            }
        }else{
            auto dump=request_.dump;
            switch(operation()){
            case InfoResourceOperation::EcDump:dump.kind=InfoDumpKind::Ec;break;
            case InfoResourceOperation::BiosDump:dump.kind=InfoDumpKind::Bios;break;
            case InfoResourceOperation::VbiosDump:dump.kind=InfoDumpKind::Vbios;break;
            case InfoResourceOperation::PowerInputDump:dump.kind=InfoDumpKind::PowerInput;break;
            default:throw std::invalid_argument("INFO: unknown dump operation");
            }
            dump.cancellation=token;dump.progress=&progress_;dump.powerSession=request_.powerSession.get();
            dump.prompt=[this,token](const auto& instruction,const auto& reason){return prompts.ask(instruction,reason,token);};
            auto collected=services_.dump?services_.dump(dump):executeInfoDumpResource(dump);
            result_.dump=std::move(collected.report);result_.vbios=std::move(collected.vbios);
            if(operation()==InfoResourceOperation::VbiosDump){
                result_.vbios.mode=dump.vbiosMode;
                result_.dump.saved=result_.vbios.requestedImageSaved();
            }
            result_.message=result_.dump.message;result_.success=result_.dump.saved;
        }
    }catch(const std::exception& e){result_.success=false;result_.message="Операция не завершена: "+std::string(e.what());}
    catch(...){result_.success=false;result_.message="Операция не завершена: неизвестная ошибка INFO";}
    finish(); // Published result is now immutable, including cancellation policy.
    trace.finish(result_.cancelled?"cancelled":operation()==InfoResourceOperation::VbiosDump?result_.vbios.outcome():
        result_.success?(result_.dump.hardwareWarning?"completed_with_warning":"completed"):"failed",
        "\"message\":"+audit::quote(result_.message)+",\"saved\":"+(result_.success?"true":"false")+
        ",\"hardware_warning\":"+(result_.dump.hardwareWarning?"true":"false")+
        ",\"hardware_warning_message\":"+audit::quote(result_.dump.hardwareWarningMessage)+
        ",\"vbios_saved\":"+(result_.vbios.vbiosSaved?"true":"false")+",\"eeprom_saved\":"+(result_.vbios.eepromSaved?"true":"false"));
    try{if(loggingEnabled())logMessage("bios",std::format("{}: success={} sections={} rows={} elapsed_ms={:.2f}",
        operationName(operation()),result_.success,result_.inventory.sections.size(),result_.inventory.rowCount(),
        std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count()));}catch(...){}
}
void InfoOperation::fail(std::string error){
    complete(false,"Операция не завершена: "+std::move(error));
}
void InfoOperation::finish(){
    const std::lock_guard lock(resultGate_);
    if(done())return;
    publish();
}
bool InfoOperation::complete(bool success,std::string message){
    const std::lock_guard lock(resultGate_);
    if(done())return false;
    result_.success=success;result_.message=std::move(message);publish();return true;
}
void InfoOperation::publish(){
    result_.cancelled=cancellation_.stop_requested()||result_.dump.cancelled||result_.vbios.cancelled;
    if(result_.cancelled&&inventory())result_.success=false;
    if(result_.cancelled&&result_.message.empty())result_.message="Операция отменена.";
    // OSD consumes the same report as the operation window, including failures
    // thrown before a collector could return its own DiagnosticDumpResult.
    if(dumping()){
        result_.dump.cancelled=result_.cancelled;
        if(result_.dump.message.empty())result_.dump.message=result_.message;
    }
    audit::related(request_.auditId,"info.result","\"operation\":"+audit::quote(operationName(operation()))+
        ",\"saved\":"+(result_.success?"true":"false")+",\"cancelled\":"+(result_.cancelled?"true":"false")+
        ",\"message\":"+audit::quote(result_.message));
    messages_->finish(result_.message,result_.success,result_.cancelled,result_.dump.hardwareWarningMessage);
    request_.dump.firmwarePause.reset();
    phase_.store(Phase::Finished,std::memory_order_release);
}
bool InfoOperation::done()const noexcept{return phase_.load(std::memory_order_acquire)==Phase::Finished;}
bool InfoOperation::installing()const noexcept{return phase_.load(std::memory_order_acquire)==Phase::Installing;}
const InfoOperationResult* InfoOperation::poll(){
    auto phase=Phase::AwaitingUi;
    if(phase_.compare_exchange_strong(phase,Phase::Installing,std::memory_order_acq_rel)){
        if(cancellation_.stop_requested()){finish();return &result_;}
        OperationMessages::Binding capture(*messages_);
        audit::Scope launch(operation()==InfoResourceOperation::LenovoEnergyPrepare?"info.lenovo_energy_launch":"info.pawnio_launch",{},request_.auditId);
        const auto self=shared_from_this();
        try{services_.launch(installer_,messages_,[self](bool success,std::string message){
            OperationMessages::Binding capture(*self->messages_);
            audit::related(self->request_.auditId,self->operation()==InfoResourceOperation::LenovoEnergyPrepare?"info.lenovo_energy_exit":"info.pawnio_exit",
                "\"success\":"+std::string(success?"true":"false")+",\"message\":"+audit::quote(message));
            // A late/duplicate callback or an exception after synchronous
            // completion cannot overwrite data already handed to the GUI.
            if(!self->complete(success,std::move(message)))
                audit::related(self->request_.auditId,"info.duplicate_completion_ignored");
        });launch.finish("launched");}
        catch(const std::exception& e){if(done())launch.finish("completed_before_launch_return");else fail(e.what());}
        catch(...){if(done())launch.finish("completed_before_launch_return");else fail("Неизвестная ошибка установки PawnIO");}
    }
    return done()?&result_:nullptr;
}
void InfoOperation::releaseHardware(){cancel();if(dumping()){std::lock_guard cleanup(*request_.dump.hardwareGate);}}
void InfoOperation::cancel(){
    cancellation_.request_stop();
    if(firmwareDump()){
        std::lock_guard lock(resultGate_);
        if(!workerStarted_&&!done()){
            result_.success=false;result_.message="Операция отменена.";publish();return;
        }
    }
    auto phase=Phase::AwaitingUi;
    // No UI pump is guaranteed after the page closes. A prepared-but-unlaunched
    // installer must still finish its transcript and must never launch later.
    if(phase_.compare_exchange_strong(phase,Phase::Running,std::memory_order_acq_rel))finish();
}
}
