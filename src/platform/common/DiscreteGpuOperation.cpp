#include "DiscreteGpuOperation.h"
#include "MagicControl/ControlAudit.h"
#include "MagicControl/Logging.h"
#include "MagicControl/GpuResourceControl.h"
#include <exception>
#include <utility>
namespace mc {
namespace {
std::shared_ptr<DeviceOperation> admit(const GpuOperationRequest& r,const GpuOperationContext& c){
    if(!r.writing())return {};
    if(c.viewOnly||c.preview)throw std::runtime_error("Просмотр профиля · управление GPU отключено");
    if(r.providerId.empty())throw std::invalid_argument("GPU: missing provider identity");
    if(r.kind==GpuOperation::SetPower&&!r.value)throw std::invalid_argument("GPU: не задан лимит мощности");
    if(c.reservation&&!deviceOperations().owns(c.reservation))throw std::runtime_error("GPU: invalid operation reservation");
    auto operation=c.reservation?c.reservation:deviceOperations().tryBegin();
    if(!operation)throw std::runtime_error("Дождитесь завершения текущей операции устройства.");
    return operation;
}
DiscreteGpuOperationResult rejected(const GpuOperationRequest& r,std::string message){
    DiscreteGpuOperationResult result;result.action=r.writing();result.uuid=r.providerId;result.message=std::move(message);return result;
}
}
DiscreteGpuOperationResult executeGpuOperation(DiscreteGpu& gpu,const GpuOperationRequest& r,GpuOperationContext c){
    audit::Scope trace("gpu.power","\"uuid\":"+audit::quote(r.providerId)+",\"watts\":"+(r.value?std::to_string(*r.value):"null")+",\"reset\":"+(r.kind==GpuOperation::ResetPower?"true":"false"),c.auditId,r.writing());
    DiscreteGpuOperationResult result;result.action=r.writing();result.uuid=r.providerId;result.preview=c.preview.has_value();
    std::shared_ptr<DeviceOperation> operation;
    try{operation=admit(r,c);}
    catch(const std::exception& e){result.message=e.what();trace.finish("rejected","\"message\":"+audit::quote(result.message));return result;}
    try{if(result.action){applyGpuPowerResource(r.providerId,r.value,r.kind==GpuOperation::ResetPower,
        {[&](const std::string& id,int value){gpu.setPowerLimit(id,value);},[&](const std::string& id){gpu.resetPowerLimit(id);}});
        result.success=true;result.message="Лимит подтверждён чтением драйвера GPU.";}}
    catch(const std::exception& e){result.message=e.what();logMessage("gpu",result.message);}
    catch(...){result.message="GPU provider failed";}
    try{result.readings=c.preview?std::move(*c.preview):gpu.read();}
    catch(const std::exception& e){result.readFailed=true;if(result.success)result.message+=" Обновление показаний GPU не удалось: "+std::string(e.what());else if(result.message.empty())result.message=e.what();GpuReading row;row.name=gpuVendorName(gpu.vendor());row.status=e.what();result.readings.push_back(std::move(row));}
    catch(...){result.readFailed=true;if(result.success)result.message+=" Обновление показаний GPU не удалось.";else if(result.message.empty())result.message="GPU provider failed";GpuReading row;row.status="GPU provider failed";result.readings.push_back(std::move(row));}
    if(result.action)trace.finish(result.success?"readback_confirmed":"failed","\"message\":"+audit::quote(result.message)+",\"refresh_failed\":"+(result.readFailed?"true":"false"));
    return result;
}
bool GpuOperations::submit(GpuOperationRequest request,bool viewOnly,std::optional<std::vector<GpuReading>> preview){
    if(busy)return false;
    busy=true;applying=request.writing();active_=request;pending_.reset();immediate_=false;
    GpuOperationContext context{audit::capture(),viewOnly,std::move(preview),{}};
    try{
        reservation_=admit(request,context);context.reservation=reservation_;
        worker_.launch([this,request,context=std::move(context)]()mutable{pending_=executeGpuOperation(gpu_,request,std::move(context));},ready_,request.writing()?ResourceWorkClass::Interactive:ResourceWorkClass::Observation);
    }catch(const std::exception& e){reservation_.reset();pending_=rejected(request,e.what());immediate_=true;}
    catch(...){reservation_.reset();pending_=rejected(request,"Не удалось запустить обработчик GPU.");immediate_=true;}
    if(immediate_){audit::event("gpu.command_rejected","\"uuid\":"+audit::quote(request.providerId)+",\"message\":"+audit::quote(pending_->message));if(ready_)ready_();}
    return true;
}
std::optional<DiscreteGpuOperationResult> GpuOperations::take(std::optional<std::vector<GpuReading>> currentPreview){
    if(!immediate_&&!worker_.takeCompletion())return {};
    immediate_=false;
    if(worker_.failure()&&!pending_)pending_=rejected(active_,"Ошибка обработчика GPU: состояние не подтверждено.");
    auto result=std::move(pending_);pending_.reset();busy=false;applying=false;reservation_.reset();
    if(result){
        if(currentPreview)readings=std::move(*currentPreview);
        else if(!result->preview)readings=displayCache_.update(std::move(result->readings));
        else readings.clear(); // A completed old preview is not a host GPU observation.
        if(result->action)message=result->message;
        audit::state("GPU.page.presentation",audit::gpuDisplay(readings));
    }
    return result;
}
}
