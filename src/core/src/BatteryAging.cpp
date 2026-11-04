#include "MagicControl/BatteryAging.h"
#include "MagicControl/EcAccess.h"
#include "MagicControl/PortableSettings.h"
#include "MagicControl/ActionLog.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <thread>
namespace mc {
namespace {
struct Saved {
    std::string host;unsigned manufacture{},design{},cycles{},date{},history{};
    bool enabled{},pending{};
    std::uint64_t total{};unsigned lastRaw{},appliedDate{};std::uint64_t lastAt{};
};
auto path(){return portableSettingsPath("battery-aging-override.state");}
auto originalPath(){return portableSettingsPath("battery-aging-original.state");}
std::optional<Saved> load(){
    if(!std::filesystem::exists(path()))return {};
    std::ifstream in(path());std::string signature,extra;int version{},enabled{},pending{};Saved s;
    if(!(in>>signature>>version>>std::quoted(s.host)>>s.manufacture>>s.design>>s.cycles>>s.date>>s.history>>enabled>>pending>>s.total>>s.lastRaw>>s.appliedDate>>s.lastAt)||
       (in>>extra)||signature!="MagicControlBatteryAging"||version!=2||s.host.empty()||
       !s.manufacture||!s.design||s.design>65535||s.cycles>65535||s.date>65535||s.history>65535||
       enabled<0||enabled>1||pending<0||pending>1||s.lastRaw>65535||s.appliedDate>65535||s.total<s.history||s.total>1000000000)
        throw std::runtime_error("Некорректен исходный снимок подмены батареи");
    s.enabled=enabled;s.pending=pending;return s;
}
void save(const Saved& s,const std::filesystem::path& target=path()){
    std::ostringstream out;out<<"MagicControlBatteryAging 2\n"<<std::quoted(s.host)<<'\n'
        <<s.manufacture<<' '<<s.design<<' '<<s.cycles<<' '<<s.date<<' '<<s.history<<'\n'
        <<s.enabled<<' '<<s.pending<<'\n'<<s.total<<' '<<s.lastRaw<<' '<<s.appliedDate<<' '<<s.lastAt<<'\n';
    std::string error;if(!savePortableText(target,out.str(),error))throw std::runtime_error(error);
}
void match(const Saved& s,const BatteryAgingReading& r){
    if(s.host!=r.host||s.manufacture!=r.manufacture||s.design!=r.design||r.cycles<s.cycles)
        throw std::runtime_error("Исходный снимок относится к другому устройству или батарее");
}
std::uint64_t nowSeconds(){return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());}
void accumulate(Saved& s,const BatteryAgingReading& r){
    if(s.pending)return; // A crashed transition has an ambiguous raw generation.
    if(s.enabled&&r.date!=s.appliedDate)return; // Lost firmware state is not new exposure.
    if(r.history==s.lastRaw)return;
    const auto delta=r.history>=s.lastRaw?r.history-s.lastRaw:r.history;
    const auto now=nowSeconds(),elapsed=now>=s.lastAt?now-s.lastAt:0;
    if(delta>7*(1+elapsed/3600))throw std::runtime_error("Скачок истории EC: исходные термочасы сохранены, запись отменена");
    if(s.total>std::numeric_limits<std::uint64_t>::max()-delta)throw std::runtime_error("Переполнение истории батареи");
    s.total+=delta;s.lastRaw=r.history;s.lastAt=now;save(s); // Persist changes before any future reset.
}
int bcd(unsigned n){if((n&15)>9||(n>>4)>9)throw std::runtime_error("Некорректные часы EC");return int((n>>4)*10+(n&15));}
unsigned monthDate(const BatteryAgingReading& r){
    int y=2000+bcd(r.yearBcd),m=bcd(r.monthBcd);if(m<1||m>12)throw std::runtime_error("Некорректный месяц EC");
    if(--m==0){m=12;--y;}if(y<1980||y>2107)throw std::runtime_error("Дата EC вне диапазона");
    return unsigned(((y-1980)<<9)|(m<<5)|1);
}
int months(const BatteryAgingReading& r,unsigned date){
    if(!date)date=r.manufacture;
    const auto m=(date>>5)&15;if(m<1||m>12)throw std::runtime_error("Некорректная исходная дата");
    return std::max(0,(2000+bcd(r.yearBcd)-int(1980+(date>>9)))*12+bcd(r.monthBcd)-int(m));
}
BatteryAgingReading read(EcAccess& ec){const auto r=ec.readBatteryAging();if(!r)throw std::runtime_error("Подмена батареи для этого устройства недоступна");return *r;}
void apply(EcAccess& ec,const BatteryAgingReading& before,unsigned date,unsigned history,int age){
    const EcAccess::Transaction transaction(ec);
    const auto current=read(ec);
    if(current.host!=before.host||current.manufacture!=before.manufacture||current.date!=before.date||current.history!=before.history)
        throw std::runtime_error("Показания батареи изменились перед записью");
    bool changed=false;
    const auto byte=[&](unsigned address,unsigned value){ec.writeBatteryAgingByte(static_cast<std::uint16_t>(address),static_cast<std::uint8_t>(value));changed=true;};
    if(current.date!=date){
        if(date){byte(0x7f3,date&255);byte(0x7f2,date>>8);}
        else{byte(0x7f2,0);byte(0x7f3,0);}
    }
    if(current.history!=history){byte(0x9c9,history>>8);byte(0x9ca,history&255);}
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    const auto settled=[&]{const auto r=read(ec);return !(r.service&7)&&!(r.pending&8);};
    if(changed){
        while(!settled()){
            if(std::chrono::steady_clock::now()>=deadline)throw std::runtime_error("EC не завершил предыдущее сохранение батареи");
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
        }
        ec.writeBatteryAgingByte(0x726,1);
    }
    int stable=0;
    do{
        const auto r=read(ec);
        if(r.manufacture!=before.manufacture||r.design!=before.design||r.cycles!=before.cycles||
           (r.service&0x80)!=(before.service&0x80))throw std::runtime_error("Контекст батареи или вентиляторов изменился");
        if(r.date==date&&r.history==history&&r.months==age&&r.appliedMonths==age&&!(r.service&7)&&!(r.pending&8)){
            if(++stable>=3)return;
        }else stable=0;
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
    }while(std::chrono::steady_clock::now()<deadline);
    throw std::runtime_error("Подмена батареи не подтверждена EC");
}
}
BatteryAgingStatus observeBatteryAging(EcAccess& ec){
    // One existing recursive EC gate covers both the file and hardware state.
    // This also serializes GUI/CLI processes, without an inverse local lock.
    BatteryAgingStatus out;
    try{
        const EcAccess::Transaction transaction(ec);
        out.reading=ec.readBatteryAging();out.authoritative=true;if(!out.reading)return out;
        out.sampledAtMs=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
        out.writable=true;auto saved=load();
        if(saved){
            match(*saved,*out.reading);
            accumulate(*saved,*out.reading);out.realHistory=saved->total;
            if(saved->pending)out.error="Незавершённая подмена: выключите её для восстановления";
            else if(saved->enabled?out.reading->date==monthDate(*out.reading)&&out.reading->months==1&&out.reading->appliedMonths==1:out.reading->date==saved->date)
                out.enabled=saved->enabled;
            else out.error="Сохранённая подмена не подтверждена EC";
        }
        else if(!out.reading->date){out.enabled=false;out.realHistory=out.reading->history;}
        else out.error="Не найден исходный снимок подмены батареи";
    }catch(const std::exception& e){out.error=e.what();out.writable=false;}
    return out;
}
void setBatteryAgingOverride(EcAccess& ec,bool enabled,bool lifecycle){
    const EcAccess::Transaction transaction(ec);auto s=load();
    if(lifecycle&&(!s||!s->enabled))return;
    const auto before=read(ec);
    if(!s){
        if(std::filesystem::exists(originalPath()))throw std::runtime_error("Отсутствует рабочая история батареи; исходная резервная копия сохранена");
        if(!enabled){if(before.date)throw std::runtime_error("Исходный снимок отсутствует");return;}
        if(before.date)throw std::runtime_error("Нужен исходный снимок до экспериментальной подмены");
        s=Saved{before.host,before.manufacture,before.design,before.cycles,before.date,before.history,false,false,before.history,before.history,before.date,nowSeconds()};
        save(*s,originalPath());save(*s);
    }
    match(*s,before);
    if(!std::filesystem::exists(originalPath()))save(*s,originalPath());
    accumulate(*s,before);
    if(enabled&&s->pending)throw std::runtime_error("Сначала выключите незавершённую подмену для восстановления");
    const auto previous=*s;
    const unsigned date=enabled?monthDate(before):s->date;
    const unsigned history=enabled?0:static_cast<unsigned>(std::min<std::uint64_t>(65535,s->total));
    s->enabled=enabled;s->pending=true;save(*s); // Backup/intent reaches durable storage BEFORE writes.
    try{
        apply(ec,before,date,history,months(before,date));s->pending=false;s->lastRaw=history;s->appliedDate=date;s->lastAt=nowSeconds();save(*s);
        audit::event("battery_aging_confirmed","\"enabled\":"+std::string(enabled?"true":"false")+",\"history\":"+std::to_string(history));
    }catch(const std::exception& e){
        const std::string failure=e.what();
        try{const auto now=read(ec);apply(ec,now,before.date,before.history,months(before,before.date));save(previous);}
        catch(const std::exception& rollback){throw std::runtime_error(failure+"; восстановление не подтверждено: "+rollback.what());}
        throw std::runtime_error(failure+"; прежние показания восстановлены");
    }
}
}
