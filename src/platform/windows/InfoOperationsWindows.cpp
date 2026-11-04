#include "InfoOperationsWindows.h"
#include "FanControlWindows.h"
#include "MagicControl/PawnIoInstall.h"
#include "MagicControl/OperationMessages.h"
#include <stdexcept>

namespace mc::windows {
MaximumFanProbe makeInfoMaximumFanProbe(std::shared_ptr<FanController> controller){
    if(!controller)return {};
    return [controller](std::stop_token token){return controller->measureMaximum(token).get();};
}
FirmwareFanDefault makeInfoFirmwareFanDefault(std::shared_ptr<FanController> controller){
    if(!controller)return {};
    return [controller]{return controller->prepareDiagnostic();};
}
InfoOperationServices infoOperationServices(InfoResourceOperation operation){
    if(operation==InfoResourceOperation::LenovoEnergyPrepare)return {.prepare=dependencies::prepareLenovoEnergyInstaller,.launch=
        [](const auto& installer,const auto& messages,InfoOperationServices::Completion complete){
            dependencies::launchLenovoEnergyInstaller(installer,[complete=std::move(complete)](unsigned code){
                try{const auto status=dependencies::lenovoEnergyDriverStatus();complete(status.installed,
                    "Установщик Lenovo Energy завершён, код "+std::to_string(code)+(status.installed?". Драйвер обнаружен; перезапустите приложение.":". Драйвер не обнаружен."));}
                catch(const std::exception& e){complete(false,e.what());}});
            if(!messages->done())messages->append("Открыт штатный установщик Lenovo Energy.");
        }};
    return {.prepare=dependencies::preparePawnIoInstaller,.launch=
        [](const auto& installer,const auto& messages,InfoOperationServices::Completion complete){
            const auto action=dependencies::pawnIoSetupAction();
            if(action==dependencies::PawnIoSetupAction::UnknownVersion)
                throw std::runtime_error("Версия PawnIO неизвестна; запуск установщика отменён");
            if(action==dependencies::PawnIoSetupAction::Current){complete(true,"Драйвер PawnIO уже установлен.");return;}
            dependencies::launchPawnIoInstaller(installer,[complete=std::move(complete)](unsigned code){
                bool installed=false;std::string message;
                try{installed=dependencies::pawnIoSetupAction()==dependencies::PawnIoSetupAction::Current;
                    message="Установщик PawnIO завершён, код "+std::to_string(code)+(installed?
                        ". Установленная версия подтверждена. Перезапустите MagicControl.":". Установка нужной версии не подтверждена.");
                }catch(const std::exception& e){message=e.what();}
                complete(installed,std::move(message));
            });
            if(!messages->done())messages->append("Мастер установки PawnIO открыт. Завершите установку и перезапустите MagicControl.");
        }};
}
}
