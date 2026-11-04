#pragma once
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>
#include <chrono>
#include <algorithm>
#include <cstdint>
#include <set>
#include "MagicControl/Json.h"

namespace mc {
// Presentation-only transcript. No hardware/file access and no dependence on
// log rotation or AllLogs. Workers publish data; GUI adapters poll it on UI thread.
class OperationMessages:public std::enable_shared_from_this<OperationMessages>{
public:
    enum class Tone { Information, Error, Success };
    struct Entry { std::string text;Tone tone{Tone::Information}; };
private:
    std::mutex gate_;std::vector<Entry> messages_;std::set<std::string> seen_;bool done_{};
    std::uint64_t action_{};
    static inline std::mutex registryGate_;
    static inline std::vector<std::weak_ptr<OperationMessages>> registry_;
    static inline thread_local OperationMessages* active_{};
    static std::string concise(std::string text){
        for(const auto prefix:{"BIOS: ","Операция не завершена: ","Сбор сведений не завершён: ",
                "Дамп не завершён: VBIOS не подтверждён, EEPROM не подтверждён. ",
                "Частично: VBIOS сохранён, EEPROM не подтверждён. ",
                "Частично: VBIOS не подтверждён, EEPROM сохранён. "})
            if(text.starts_with(prefix))text.erase(0,std::string_view(prefix).size());
        for(const auto marker:{" Логи и partial:"," Файлы:"," Пакет:",". Пакет:"})
            if(const auto at=text.find(marker);at!=std::string::npos)text.resize(at);
        while(!text.empty()&&(text.back()=='\n'||text.back()=='\r'||text.back()==' '))text.pop_back();
        return text;
    }
    void accept(std::string_view channel,std::string_view fields){
        // Explicit allowlist: UI snapshots, EC byte exchanges, metadata rows and
        // vendor progress remain in the full disk logs, never in the user window.
        if(channel!="bios_dump"&&channel!="vbios_dump"&&channel!="vendor_stderr"&&channel!="vendor_stdout"&&channel!="diagnostic_dump"&&channel!="power_input_dump")return;
        try{const auto body=mc::json::JsonReader::parse("{"+std::string(fields)+"}");
            const auto event=body.at("event").text();
            if(channel=="diagnostic_dump"&&event=="stage"){
                // Reviewed collector stages only; raw EC/telemetry stays on disk.
                // recordLog delivers this even when disk logging is disabled.
                append(body.at("message").text());return;
            }
            if(channel=="bios_dump"){
                const auto& details=body.at("details");
                if(event=="precheck"&&details.at("actual_profile").text().empty())
                    append("Модель не распознана; проверяется фактическое оборудование.");
                if(event=="reader_selected"){
                    // Presentation follows the collector's actual selection.
                    // Never treat every non-AMI/new runtime as Intel FPT.
                    const auto group=details.at("runtime_group").text();
                    std::string reader;
                    if(group=="AmiAfu")reader="AMI AFU";
                    else if(group=="IntelFpt"||group=="IntelFpt19")reader="Intel FPT";
                    else if(group=="InsydeH2offt")reader="Insyde H2OFFT";
                    if(reader.empty())append("Выбран BIOS-ридер.");
                    else{const auto version=details.at("tool_version").text();
                        append("Выбран BIOS-ридер "+reader+(version.empty()?"":" "+version)+".");}
                    return;
                }
                if(event=="vendor_started"&&details.at("operation").text()=="save"){append("Чтение дампа BIOS началось.");return;}
            }
            if(channel=="vbios_dump"){
                const auto& details=body.at("details");
                // Only user-relevant stages. Full paths, hashes, stdout and
                // repeated preflights remain in the disk log; append deduplicates.
                if(event=="capture_started"){append("Подготовка дампов VBIOS и EEPROM.");return;}
                if(event=="actual_platform"&&details.at("profile").text().empty()){append("Модель не распознана; проверяется фактическая видеокарта.");return;}
                if(event=="target_selected"){append("Видеокарта: "+details.at("name").text()+".");return;}
                if(event=="tool_verified"){
                    const auto version=details.at("version").text();
                    append(version.empty()?"Утилита NVFlash проверена; версия не сообщена.":"Утилита NVFlash "+version+" проверена.");return;
                }
                if(event=="driver_collision"){
                    append("Обнаружена служба NVFlash.");
                    append(details.at("state").number()==1?"Служба NVFlash уже остановлена.":"Служба NVFlash ещё активна.");return;
                }
                if(event=="driver_image_verified"){append("Файл оставшегося драйвера NVFlash проверен.");return;}
                if(event=="driver_image_absent"){append("Файл оставшегося драйвера NVFlash отсутствует.");return;}
                if(event=="driver_residue_cleanup_requested"){append("Удаляется остановленная запись службы NVFlash.");return;}
                if(event=="driver_residue_removed"){append("Остановленная запись службы NVFlash удалена.");return;}
                if(event=="driver_cleanup_checked"){append("Проверка очистки после NVFlash завершена.");return;}
                if(event=="driver_preflight"){append("Проверка готовности NVFlash завершена.");return;}
                if(event=="vendor_started"){
                    const auto operation=details.at("operation").text();
                    if(operation=="list")append("Поиск видеокарты в NVFlash.");
                    else if(operation=="check")append("Определяется микросхема EEPROM.");
                    else if(operation=="save_vbios")append("Чтение дампа VBIOS началось.");
                    else if(operation=="save_entire")append("Чтение полного EEPROM началось.");
                    else if(operation=="compare")append("Сверка полного EEPROM с видеокартой.");
                    return;
                }
                if(event=="capacity_probe_started"){append("Проверяется доступность полного EEPROM.");return;}
                if(event=="eeprom_capacity"){
                    if(details.at("capacity").at("admitted").flag())append("Ёмкость EEPROM проверена.");
                    return;
                }
                if(event=="capacity_probe_failed")return; // The following entire_refused owns this error.
                if(event=="pass_file"&&details.at("bytes").number()>0&&details.at("exit_code").number()==0){
                    const auto name=details.at("mode").text()=="vbios"?"VBIOS":"EEPROM";
                    append(std::string(name)+": чтение "+std::to_string(static_cast<int>(details.at("pass").number()))+" сохранено.");return;
                }
                if(event=="mode_verified"){
                    append(details.at("mode").text()=="vbios"?"VBIOS: два чтения совпали, дамп проверен.":"EEPROM: два чтения совпали, дамп проверен.",Tone::Success);return;
                }
                if(event=="mode_failed"){
                    const auto name=details.at("mode").text()=="vbios"?"VBIOS":"EEPROM";
                    append(std::string(name)+": "+details.at("error").text(),Tone::Error);return;
                }
                if(event=="entire_refused"){
                    append("EEPROM: "+details.at("reason").text(),Tone::Error);
                    if(details.at("ordinary_vbios_preserved").flag())
                        append("Проверенный обычный VBIOS сохранён независимо от полного EEPROM.",Tone::Success);
                    return;
                }
            }
            if(event=="cancel_pending"){append("Отмена запрошена. Ожидание окончания текущего чтения.");return;}
            if(event=="failed"||event=="capture_failed"||event=="vendor_process_pending"){
                auto error=body.at("details").at("error").text();if(error.empty())error=body.at("error").text();
                if(error.empty()&&event=="vendor_process_pending")error="Чтение ещё выполняется. Ожидайте завершения процесса.";
                if(!error.empty())append(concise(error),Tone::Error);return;
            }
            const auto message=body.at("message").text();
            if(channel=="vendor_stderr"||channel=="vendor_stdout"){
                // Show exceptional reader output, not banners, percentages or
                // repeated 'Operation Failed' trailers. Full raw files stay intact.
                std::size_t begin{};while(begin<message.size()){
                    const auto end=message.find_first_of("\r\n",begin);auto line=message.substr(begin,end==std::string::npos?end:end-begin);
                    const auto error=line.find("Error ");const auto upper=line.find("ERROR:");
                    if(error!=std::string::npos)append(line.substr(error),Tone::Error);
                    else if(upper!=std::string::npos)append(line.substr(upper),Tone::Error);
                    else if(line.find("Warning:")!=std::string::npos)append(line.substr(line.find("Warning:")));
                    if(end==std::string::npos)break;begin=end+1;
                }return;
            }
            if(message.find("Подключите")!=std::string::npos||message.find("Отключите")!=std::string::npos||message.find("подключите")!=std::string::npos||message.find("отключите")!=std::string::npos)append(concise(message));
            else if(message.find("ВНИМАНИЕ")!=std::string::npos||message.find("не удалось")!=std::string::npos||message.find("failed")!=std::string::npos)append(concise(message),Tone::Error);
        }catch(...){} // Unknown technical records are not user-facing prose.
    }
public:
    struct Binding {
        OperationMessages* previous;
        explicit Binding(OperationMessages& value):previous(active_){active_=&value;}
        ~Binding(){active_=previous;}
        Binding(const Binding&)=delete;
    };
    static std::shared_ptr<OperationMessages> create(std::uint64_t action=0){
        auto p=std::make_shared<OperationMessages>();p->action_=action;
        std::lock_guard lock(registryGate_);
        std::erase_if(registry_,[](const auto& w){return w.expired();});registry_.push_back(p);return p;
    }
    void append(std::string text,Tone tone=Tone::Information){
        text=concise(std::move(text));if(text.empty())return;
        auto key=text;while(!key.empty()&&key.back()=='.')key.pop_back();
        std::lock_guard lock(gate_);if(seen_.insert(key).second)messages_.push_back({std::move(text),tone});
    }
    void finish(std::string text,bool success,bool cancelled=false,std::string warning={}){
        if(cancelled)append("Сбор данных отменён.");
        else if(success){
            if(warning.empty())append("Сбор данных завершён успешно.",Tone::Success);
            else append("Данные сохранены.",Tone::Success);
        }
        else {
            auto error=concise(std::move(text));const auto split=error.find("; EEPROM: ");
            if(error.starts_with("VBIOS: ")&&split!=std::string::npos){
                append(error.substr(0,split),Tone::Error);append(error.substr(split+2),Tone::Error);
            }else append(std::move(error),Tone::Error);
            append("Сбор данных не завершён.",Tone::Error);
        }
        if(!warning.empty()){
            append(std::move(warning),Tone::Error);
            if(success&&!cancelled)append("Сбор данных завершён с предупреждением.");
        }
        std::lock_guard lock(gate_);done_=true;
    }
    bool done(){std::lock_guard lock(gate_);return done_;}
    std::vector<Entry> read(std::size_t& cursor,std::size_t maximum=512){
        std::lock_guard lock(gate_);const auto end=std::min(messages_.size(),cursor+maximum);
        std::vector<Entry> result(messages_.begin()+std::min(cursor,messages_.size()),messages_.begin()+end);cursor=end;return result;
    }
    static bool capturing()noexcept{return active_!=nullptr;}
    static void publish(std::string_view channel,std::string_view fields,std::uint64_t action)noexcept{
        try{
            std::vector<std::shared_ptr<OperationMessages>> targets;
            {std::lock_guard lock(registryGate_);for(const auto& w:registry_)if(auto p=w.lock())
                if(p.get()==active_||(action&&p->action_==action))targets.push_back(std::move(p));}
            for(const auto& p:targets)if(!p->done())p->accept(channel,fields);
        }catch(...){} // Display failures never change operation outcomes.
    }
};
}
