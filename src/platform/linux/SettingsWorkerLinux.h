#pragma once

#include "MagicControl/DeviceOperation.h"
#include <exception>
#include <functional>
#include <string>
#include <utility>

namespace mc::linux_platform {
struct SettingsWorkerRequest {
    int upper{-1}, restart{-1}, keyboardLevel{-1}, keyboardTimeout{-1};
    bool hasBackend{};
    bool writing() const { return upper>=0 || restart>=0 || keyboardLevel>=0 || keyboardTimeout>=0; }
};
struct SettingsWorkerResult { bool success{}; std::string message; };
struct SettingsWorkerActions {
    std::function<std::string()> battery;
    std::function<SettingsWorkerResult()> lighting;
    std::function<void()> collect;
    std::function<bool()> readbackMatches;
};

// Keep the existing order and partial observations, including continuing to
// collect readback after an operation failed. This does not grant admission.
inline SettingsWorkerResult executeSettingsWorkerLinux(const SettingsWorkerRequest& request,
                                                       const SettingsWorkerActions& actions) {
    SettingsWorkerResult result{request.writing(),{}};
    const auto fail=[&](const std::string& message){
        if(!result.success&&!result.message.empty())result.message+="; ";else result.message.clear();
        result.success=false;result.message+=message;
    };
    if(request.writing()&&!request.hasBackend){
        result.success=false;result.message="Для этой модели нет подтверждённого аппаратного обработчика.";
    }
    if(request.hasBackend&&(request.upper>=0||request.restart>=0))try{
        result.message=actions.battery();
    }catch(const std::exception& e){fail(e.what());}
    catch(...){fail("Неизвестная ошибка операции батареи.");}
    if(request.hasBackend&&(request.keyboardLevel>=0||request.keyboardTimeout>=0))try{
        auto lighting=actions.lighting();
        const bool wasSuccessful=result.success;
        if(!lighting.success)result.success=false;
        // A later successful operation cannot replace an earlier error with
        // a success message. Preserve both errors if both operations failed.
        if(!lighting.message.empty()){
            if(wasSuccessful)result.message=std::move(lighting.message);
            else if(!lighting.success){if(!result.message.empty())result.message+="; ";result.message+=lighting.message;}
        }
    }catch(const std::exception& e){fail(e.what());}
    catch(...){fail("Неизвестная ошибка операции подсветки.");}
    try{actions.collect();}catch(const std::exception& e){fail(e.what());}
    catch(...){fail("Неизвестная ошибка чтения состояния.");}
    if(request.writing()&&result.success)try{
        if(!actions.readbackMatches()){
            result.success=false;result.message="Команда выполнена, но итоговое состояние устройства не подтверждено.";
        }
    }catch(const std::exception& e){fail(e.what());}
    catch(...){fail("Неизвестная ошибка подтверждения состояния.");}
    return result;
}

// A SETs write, including its final observation/readback, is one device
// operation. An already-admitted owner may be passed without re-acquiring it.
inline SettingsWorkerResult executeSettingsResourceLinux(const SettingsWorkerRequest& request,
        const SettingsWorkerActions& actions,std::shared_ptr<DeviceOperation> operation={}) {
    if(!request.writing())return executeSettingsWorkerLinux(request,actions);
    if(operation&&!deviceOperations().owns(operation))return {false,"Недействительное владение операцией устройства."};
    if(!operation)operation=deviceOperations().tryBegin();
    if(!operation)return {false,"Дождитесь завершения текущей операции устройства."};
    return executeSettingsWorkerLinux(request,actions);
}
}
