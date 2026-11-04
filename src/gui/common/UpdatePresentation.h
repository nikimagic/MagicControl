#pragma once
#include "Localization.h"
#include "MagicControl/AppUpdate.h"
#include "MagicControl/Version.h"
namespace mc::gui::ui {
// Semantic data is translated only on the native UI thread, including progress
// and a language change while the window is open. No updater worker sees GUI.
inline std::string updateMessage(const update::OperationState& state){
    if(!state.releaseMessage)return translate(state.message);
    const auto& release=state.release;
    auto channel=release.preview?translate(" (предварительная)"):std::string{};
    if(!release.localArchive.empty())channel+=translate(release.githubUnavailable?" (локальный пакет; GitHub недоступен)":" (локальный пакет)");
    return release.available?formatMessage("Доступна версия {version}{channel}.\nНажмите «Обновить». Настройки и кривые сохранятся.",{{"version",release.version},{"channel",channel}}):
        formatMessage("Установлена актуальная версия {version}.",{{"version",MAGICCONTROL_VERSION_STRING}});
}
}
