#include "MagicControl/PowerInputDump.h"
#include "DiagnosticDumpInternal.h"
#include "MagicControl/BoardIdentity.h"
#include "MagicControl/DeviceProfile.h"
#include "MagicControl/HardwareInventory.h"
#include "MagicControl/Logging.h"
#include "MagicControl/Version.h"
#include "MagicControl/Json.h"
#include <chrono>
#include <cmath>
#include <fstream>
#include <format>
#include <optional>
#include <iterator>
#include <vector>
#include <stdexcept>
namespace mc {
namespace {
using mc::json::Json;
using mc::json::JsonReader;
std::string pathText(const std::filesystem::path& p){auto s=p.generic_u8string();return {reinterpret_cast<const char*>(s.data()),s.size()};}
std::string quote(std::string_view s){std::string out="\"";for(unsigned char c:s){if(c=='"'||c=='\\'){out+='\\';out+=char(c);}else if(c<32)out+=std::format("\\u{:04x}",c);else out+=char(c);}return out+'"';}
void write(const std::filesystem::path& p,const std::string& data){
    auto temporary=p;temporary+=".tmp";
    std::ofstream f(temporary,std::ios::binary|std::ios::trunc);f<<data;f.close();
    if(!f)throw std::runtime_error("Не удалось сохранить данные проверки БП");
    std::string error;if(!replaceFileAtomically(pathText(temporary),pathText(p),error))throw std::runtime_error(error);
}
std::optional<double> numeric(const Json& j){if(const auto* n=std::get_if<double>(&j.value);n&&std::isfinite(*n))return *n;return {};}
std::optional<bool> boolean(const Json& j){if(const auto* b=std::get_if<bool>(&j.value))return *b;return {};}
constexpr const char* labels[]{"01-unplugged","02-charging-on-dc","03-full-on-dc"};
constexpr const char* instructions[]{
    "Этап 1 из 3. Отключите круглый БП и все источники питания USB-C. Подождите 5 секунд. Нажмите «Снять состояние» и не подключайте питание до окончания минутного сбора.",
    "Этап 2 из 3. Нужна реальная зарядка через круглый БП. Если батарея полная, отключите БП и пользуйтесь ноутбуком до заряда ниже 80%, затем подключите круглый БП. Дождитесь положительной мощности зарядки и нажмите «Снять состояние». USB-C питание должно быть отключено.",
    "Этап 3 из 3. Подключите круглый БП. Дождитесь полного заряда 100% и прекращения зарядки батареи. Если действует лимит 85/95%, сначала выберите 100% самостоятельно. Нажмите «Снять состояние» и не отключайте БП до окончания сбора."
};
struct Observation {bool match{};std::string reason;};
Observation inspect(const Json& battery,unsigned stage){
    if(battery.at("power_status").text().find("enumeration incomplete")!=std::string::npos||battery.at("devices").array().size()!=1||battery.at("device_id").text().empty()||battery.at("device_id").text().find("/aggregate")!=std::string::npos)
        return {false,"Не подтверждена одна физическая батарея."};
    if(boolean(battery.at("present"))!=true||boolean(battery.at("dynamic_stale"))!=false)
        return {false,"Свежие показания батареи недоступны."};
    const auto online=boolean(battery.at("online"));const auto watts=numeric(battery.at("watts")),percent=numeric(battery.at("percent"));
    const auto flow=battery.at("flow").text();
    if(!online)return {false,"Состояние внешнего питания неизвестно."};
    if(stage==0)return {!*online,!*online?"":"Внешнее питание ещё подключено. Отключите также USB-C питание."};
    if(!*online)return {false,"Внешнее питание ещё не обнаружено. Подключите круглый БП."};
    if(stage==2){
        if(!percent||*percent<99.5)return {false,"Батарея ещё не заряжена до 100%. Можно отложить этап."};
        if(!watts||std::abs(*watts)>0.05)return {false,"Батарея ещё заряжается или нет подтверждения нулевой мощности. Подождите."};
        return {true,{}};
    }
    if(flow!="charging"||!watts||*watts<=0.1)return {false,"Реальная зарядка ещё не подтверждена. Нужны статус зарядки и положительная мощность."};
    return {true,{}};
}
struct Window {unsigned matches{},other{};std::optional<double> first,last;std::string reason;bool verified()const{return matches>=3&&other==0&&first&&last&&*last-*first>=60000.;}};
Window validateWindow(const std::filesystem::path& path,unsigned stage,std::stop_token token){
    Window w;std::ifstream f(path,std::ios::binary);if(!f){w.reason="Журнал минутного окна не прочитан.";return w;}
    std::string line;unsigned lines{};
    while(!token.stop_requested()&&std::getline(f,line)){
        if(++lines>100000||line.size()>16*1024*1024)throw std::runtime_error("Слишком большой журнал проверки БП");
        const auto j=JsonReader::parse(line);if(j.at("channel").text()!="fast")continue;
        const auto verdict=inspect(j.at("battery"),stage);const auto elapsed=numeric(j.at("elapsed_ms"));
        if(!verdict.match||!elapsed){++w.other;w.reason=verdict.reason.empty()?"Состояние менялось во время сбора.":verdict.reason;}
        else{++w.matches;if(!w.first)w.first=elapsed;w.last=elapsed;}
    }
    if(!w.verified()&&w.reason.empty())w.reason="Нет полной минуты стабильных показаний нужного состояния.";
    return w;
}
void rawRegisters(const std::filesystem::path& directory,std::string_view profile){
    std::ifstream ec(directory/"ec.bin",std::ios::binary),valid(directory/"ec-valid.bin",std::ios::binary);
    std::vector<unsigned char> data,mask;
    if(ec&&valid){data.assign(std::istreambuf_iterator<char>(ec),{});mask.assign(std::istreambuf_iterator<char>(valid),{});}
    std::string rows;
    for(unsigned address:{0x432u,0x433u,0x434u,0x435u,0x43Cu,0x476u,0x49Fu,0x4A2u,0x4A3u,0x4A4u,0x4A5u,0x4A6u,0x4A7u,0x4ABu,0x7A6u,0x7B9u,0x7C3u,0x7CCu,0x7CDu,0x7CEu,0x7D0u}){
        if(!rows.empty())rows+=',';const bool ok=address<data.size()&&address<mask.size()&&mask[address]!=0;
        rows+=std::format(R"({{"address":{},"valid":{},"raw":{}}})",address,ok,ok?std::to_string(data[address]):"null");
    }
    write(directory/"power-input-registers.json",std::format(R"({{"schema":1,"source":"initial ec.bin + ec-valid.bin; no extra hardware read","profile":{},"decoded_connector_verified":false,"hydroc_4xx_fields_are_same_offset_candidates":{},"registers":[{}]}})",quote(profile),profile=="eluktronics-hydroc16g2",rows)+"\n");
}
void checkpoint(const PowerInputDumpSession& s,std::string_view status){
    write(s.directory/"power-input-session.json",std::format(R"({{"schema":1,"version":{},"profile":{},"identity":{},"status":{},"next_stage":{},"stage_count":3,"complete":{},"all_ec_complete":{},"automatic_cross_process_resume":false,"scenario_hardware_writes":false,"background_controllers_unchanged":true,"physical_connector_verified_by_software":false,"owner_round_connector_instruction_required":true,"validation_scope":"Fresh OS primary battery before snapshot and >=60s recorded window; EC snapshot is non-atomic and bracketed, no physical connector inferred from AC alone","attempts":[{}]}})" ,quote(MAGICCONTROL_VERSION_STRING),quote(s.profile),quote(s.identity),quote(status),s.nextStage,s.nextStage==3,s.allEcComplete,s.attempts)+"\n");
}
}
bool powerInputDumpSupported(){const auto id=deviceProfile(readBoardDescriptor()).id;return id=="mechrevo-gm6ir0c"||id=="eluktronics-hydroc16g2";}
DiagnosticDumpResult createPowerInputDump(PowerInputDumpSession& s,const PowerInputPrompt& ask,std::stop_token token,std::shared_ptr<EcAccess> access,std::string displaySnapshot){
    DiagnosticDumpResult result;
    try{
        const auto board=readBoardDescriptor();const auto id=std::string(deviceProfile(board).id);
        if(!powerInputDumpSupported())throw std::runtime_error("Проверка БП доступна только фактическим GM6 и HYDROC.");
        const auto identity=board.manufacturer+" / "+board.baseBoard+" / "+board.productName;
        if(!s.profile.empty()&&(s.profile!=id||s.identity!=identity))throw std::runtime_error("Модель изменилась; серия БП не продолжается.");
        if(s.directory.empty()){
            s.profile=id;s.identity=identity;auto root=currentLogPath().parent_path();if(root.empty())root=defaultLogDirectory();root/="ec-dumps";std::filesystem::create_directories(root);
            const auto name=std::format("power-input-{:%Y%m%d-%H%M%S}",std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));
            for(unsigned i=0;i<1000;++i){auto p=root/(name+"-"+std::to_string(i));if(std::filesystem::create_directory(p)){s.directory=p;break;}}
            if(s.directory.empty())throw std::runtime_error("Не удалось создать папку проверки БП");
            write(s.directory/"README.txt","Проверка БП: 1)без питания;2)реальная зарядка через круглый БП;3)круглый БП при100%/почти0Вт.\nКаждый принятый этап содержит обычный read-only EC/firmware/минутный дамп.\nНеверные/прерванные попытки сохраняются. Полнота ОС и EC отмечена отдельно.\nНикаких автоматических FANs/порогов/нагрузки/EC-записей. Используйте круглый штекер согласно подсказке; состояние ОС не доказывает тип входа. Фоновые контроллеры работают как раньше.\nПри отсрочке повторите кнопку «Снять дамп» и выберите проверку БП в этом процессе. После перезапуска начинается новая серия; старые данные сохраняются.\nПришлите всю эту папку вместе с logs_<плата>.\n");
        }
        result.directory=s.directory;checkpoint(s,"waiting");std::string reason;
        while(s.nextStage<3&&!token.stop_requested()){
            if(!ask(instructions[s.nextStage],reason))break;
            if(token.stop_requested())break;
            ++s.attempt;checkpoint(s,"preflight");
            auto provider=diagnostics::createFastDiagnosticWorker();auto frame=provider->read(token,std::chrono::seconds(10));
            const auto pre=inspect(JsonReader::parse(frame.batteryJson.empty()?"null":frame.batteryJson),s.nextStage);
            write(s.directory/("preflight-"+std::to_string(s.attempt)+".json"),std::format(R"({{"stage":{},"match":{},"reason":{},"owner_acknowledged_instructions":true,"battery":{}}})",s.nextStage,pre.match,quote(pre.reason),frame.batteryJson.empty()?"null":frame.batteryJson)+"\n");
            if(!pre.match){reason=pre.reason;checkpoint(s,"waiting");continue;}
            checkpoint(s,"capturing");
            const auto captured=createDiagnosticDump(access,token,{},std::make_shared<std::mutex>(),DiagnosticDumpMode::ReadOnly,{},s.displaySnapshot.empty()?displaySnapshot:s.displaySnapshot,s.directory/labels[s.nextStage]);
            Window window;
            if(captured.saved&&!captured.cancelled)window=validateWindow(captured.directory/"sensors.jsonl",s.nextStage,token);
            const bool verified=captured.saved&&!captured.cancelled&&!token.stop_requested()&&window.verified();
            auto record=std::format(R"({{"stage":{},"attempt":{},"directory":{},"saved":{},"cancelled":{},"state_verified":{},"ec_complete":{},"matching_samples":{},"nonmatching_samples":{},"observed_span_ms":{},"reason":{}}})",s.nextStage,s.attempt,quote(pathText(captured.directory)),captured.saved,captured.cancelled,verified,captured.ecComplete,window.matches,window.other,window.first&&window.last?std::to_string(*window.last-*window.first):"null",quote(window.reason.empty()?captured.message:window.reason));
            if(!s.attempts.empty())s.attempts+=',';s.attempts+=record;
            if(!captured.directory.empty()){
                rawRegisters(captured.directory,s.profile);
                write(captured.directory/"power-input-validation.json",record+"\n");
            }
            if(verified){s.allEcComplete&=captured.ecComplete;++s.nextStage;reason="Предыдущий этап сохранён.";}
            else reason=window.reason.empty()?"Состояние не подтверждено. Данные попытки сохранены; можно повторить или отложить.":window.reason;
            checkpoint(s,verified&&s.nextStage==3?"complete":"waiting");
        }
        result.saved=true;result.cancelled=s.nextStage<3;result.paused=!token.stop_requested()&&s.nextStage<3;result.ecComplete=s.nextStage==3&&s.allEcComplete;
        checkpoint(s,s.nextStage==3?"complete":"paused");
        result.message=s.nextStage==3?(s.allEcComplete?"Три состояния БП собраны. Папка: ":"Три состояния ОС собраны; EC неполный. Папка: "):"Проверка БП отложена; предыдущие этапы сохранены. Папка: ";
        result.message+=pathText(s.directory);logMessage("diagnostic_dump",result.message);
    }catch(const std::exception& e){result.directory=s.directory;result.message=e.what();result.cancelled=token.stop_requested();if(!s.directory.empty())try{checkpoint(s,"error");}catch(...){} }
    return result;
}
}
