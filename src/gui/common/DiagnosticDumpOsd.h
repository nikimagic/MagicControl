#pragma once
#include "MagicControl/DiagnosticDump.h"
#include "OsdModel.h"

namespace mc::gui::osd {
// UI data only: notifications never read EC, enumerate devices or inspect disk.
// Use the same lifecycle text and actual result directory on both platforms.
inline Message diagnosticDumpStarted() {
    return {"diagnostic-dump","Сбор данных начался",{},Icon::Information};
}
inline std::string diagnosticDumpFolder(const std::filesystem::path& directory) {
    const auto text=[](const std::filesystem::path& path){
        const auto value=path.generic_u8string();
        return std::string(reinterpret_cast<const char*>(value.data()),value.size());
    };
    // The existing OSD detail area is compact. Keep logs_<board>/ec-dumps/<dump>
    // visible instead of spending its space on the executable's absolute path.
    for(auto it=directory.begin();it!=directory.end();++it)if(text(*it).starts_with("logs_")){
        std::filesystem::path relative;
        for(;it!=directory.end();++it)relative/=*it;
        return text(relative);
    }
    return text(directory);
}
inline Message diagnosticDumpFinished(const DiagnosticDumpResult& result) {
    Message message{"diagnostic-dump","Сбор данных завершён",{},Icon::Information};
    if(!result.saved){
        message.title=result.cancelled?"Сбор данных отменён":"Сбор данных не завершён";
        message.detail="Подробности в INFO";message.error=true;
    }else{
        const auto folder=diagnosticDumpFolder(result.directory);
        message.detail="Папка: "+folder;
        if(result.cancelled){message.title="Сбор данных отменён";message.detail="Частичный пакет: "+folder;message.error=true;}
    }
    if(result.paused&&!result.cancelled){message.title="Проверка БП отложена";message.detail=result.saved?"Папка: "+diagnosticDumpFolder(result.directory):"Подробности в INFO";message.error=false;}
    if(result.hardwareWarning){
        // A FAN cleanup warning must not turn an unsaved/cancelled capture
        // into a completion notification.
        if(result.saved&&!result.cancelled)message.title="Сбор завершён с предупреждением";
        message.detail=result.saved?"Проверьте FANs. Папка: "+diagnosticDumpFolder(result.directory):"Проверьте FANs. Подробности в INFO";
        message.error=true;
    }
    return message;
}
}
