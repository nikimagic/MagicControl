#pragma once
#include "Localization.h"
#include "MagicControl/PowerInputDump.h"
#include <string>
namespace mc::gui {
inline constexpr char powerInputChoiceText[]="Выберите сбор данных: обычный дамп или проверка круглого БП в трёх состояниях. Проверка БП только читает данные. Этап можно отложить и продолжить этой кнопкой до выхода из программы.";
inline std::string diagnosticPromptText(const std::string& instructions,const std::string& reason){
    auto text=ui::translate(instructions);if(!reason.empty())text+="\n\n"+ui::translate(reason);
    return text+"\n\n"+ui::translate("Настройки не меняются. Можно отложить этап и продолжить кнопкой «Снять дамп» до выхода из программы.");
}

}
