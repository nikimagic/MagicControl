// Pure GUI transcript regression; real saved events may be replayed offline.
// No OS, GUI, driver, vendor process or hardware API is linked or invoked.
#include "MagicControl/OperationMessages.h"
#include "Localization.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
void check(bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);}
int main(int argc,char** argv){try{
    struct Case {const char* group;const char* version;const char* expected;};
    unsigned action=12000;
    for(const Case c:{Case{"InsydeH2offt","6.76","Выбран BIOS-ридер Insyde H2OFFT 6.76."},
        {"AmiAfu","5.17","Выбран BIOS-ридер AMI AFU 5.17."},
        {"IntelFpt","16.1","Выбран BIOS-ридер Intel FPT 16.1."},
        {"IntelFpt19","19.0","Выбран BIOS-ридер Intel FPT 19.0."},
        {"FutureReader","1.0","Выбран BIOS-ридер."},
        {"InsydeH2offt","","Выбран BIOS-ридер Insyde H2OFFT."}}){
        auto m=mc::OperationMessages::create(++action);
        mc::OperationMessages::publish("bios_dump",std::string("\"event\":\"reader_selected\",\"details\":{\"runtime_group\":\"")+c.group+"\",\"tool_version\":\""+c.version+"\"}",action);
        std::size_t cursor{};const auto rows=m->read(cursor);
        check(rows.size()==1&&rows[0].text==c.expected,"Actual reader caption/version or neutral fallback incorrect");
    }
    for(const auto version:{std::string("5.867.0"),std::string("5.999.1"),std::string{}}){
        auto m=mc::OperationMessages::create(++action);
        mc::OperationMessages::publish("vbios_dump","\"event\":\"tool_verified\",\"details\":{\"version\":\""+version+"\"}",action);
        std::size_t cursor{};const auto rows=m->read(cursor);
        const auto expected=version.empty()?"Утилита NVFlash проверена; версия не сообщена.":"Утилита NVFlash "+version+" проверена.";
        check(rows.size()==1&&rows[0].text==expected,"NVFlash version must come from verified tool event, including future versions");
    }
    const std::string label="Выбран BIOS-ридер Insyde H2OFFT 6.76.";
    {
        auto m=mc::OperationMessages::create(++action);
        const auto stage=[&](const std::string& text){mc::OperationMessages::publish("diagnostic_dump","\"event\":\"stage\",\"stage\":\"ec_read\",\"message\":\""+text+"\"",action);};
        stage("Чтение EC: 256 / 8192 байт.");stage("Чтение EC: 256 / 8192 байт.");
        mc::OperationMessages::publish("diagnostic_dump","\"message\":\"EC progress valid_bytes=512 raw address=0xc200\"",action);
        mc::OperationMessages::publish("diagnostic_dump","\"event\":\"stage\",\"message\":\"wrong action\"",action+1);
        stage("Чтение EC: 512 / 8192 байт; прошло 2 с.");
        std::size_t cursor{};const auto rows=m->read(cursor);
        check(rows.size()==2&&rows[0].text=="Чтение EC: 256 / 8192 байт."&&rows[1].text.find("512 / 8192")!=std::string::npos,
            "EC stages show real progress once, keep raw rows hidden and isolate actions");
        m->finish("cancelled",false,true);
        static_cast<void>(m->read(cursor));stage("late collector stage");
        check(m->read(cursor).empty(),"late EC stage cannot overwrite a finished transcript");
    }
    for(unsigned language=0;language<4;++language){mc::gui::ui::setLanguage(language);
        const auto translated=mc::gui::ui::translate(label);
        check(translated.find("Insyde H2OFFT 6.76")!=std::string::npos&&translated.find("Intel FPT")==std::string::npos,"Reader identity changed by localization");
        if(language)check(translated!=label,"Reader caption did not translate");
        const std::string nv="Утилита NVFlash 5.867.0 проверена.";const auto nvText=mc::gui::ui::translate(nv);
        check(nvText.find("5.867.0")!=std::string::npos,"NVFlash version lost in translation");
        if(language)check(nvText!=nv,"NVFlash caption did not translate");
        for(const std::string stage:{"Подготовка диагностического пакета EC.","Чтение EC: 256 / 8192 байт.",
                "Чтение EC: 512 / 8192 байт; прошло 2 с.","Наблюдение датчиков: прошло 20 с; минимум 60 с.",
                "Проверка и восстановление прежнего режима вентиляторов.",
                "Дамп EC: только чтение; активные пробы отключены для этой модели."}){
            const auto translatedStage=mc::gui::ui::translate(stage);
            check(!translatedStage.empty()&&(!language||translatedStage!=stage),"EC stage did not translate");
            if(stage.find("8192")!=std::string::npos)check(translatedStage.find("8192")!=std::string::npos,"EC byte count lost in translation");
        }
    }
    if(argc==2){std::ifstream file(argv[1]);check(bool(file),"Saved BIOS events missing");
        auto m=mc::OperationMessages::create(++action);std::string line;
        while(std::getline(file,line))if(line.find("\"reader_selected\"")!=std::string::npos){
            const auto begin=line.find('{');const auto end=line.rfind('}');check(begin!=std::string::npos&&end>begin,"Event JSON missing");
            mc::OperationMessages::publish("bios_dump",line.substr(begin+1,end-begin-1),action);
        }
        std::size_t cursor{};const auto rows=m->read(cursor);check(rows.size()==1&&rows[0].text==label,"Actual Honor event replay mislabels reader");
        std::cout<<"Honor replay: "<<rows[0].text<<'\n';
    }
    std::cout<<"PASS BIOS reader identities, EC progress filtering/action routing, unknown fallback and four locales; offline only\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
