#pragma once
// Included after the shared EcBitTest Device. No separate transport/driver.
struct BatteryRecovery {unsigned offset,mask,original,ours;};
class BatteryFlagSession {
    Device& ec;fs::path folder;std::ofstream journal;
    std::vector<BatteryRecovery> changes;
    bool active{},romIdTrial{};
    bool romAccepted(){const auto value=ec.read(0x770);return value==0xff||(romIdTrial&&value==4);}
    void restoreIndex(std::size_t index){
        const auto r=changes.at(index);
        if(ec.read(0x454)!=82||ec.read(0x456)!=72||ec.read(0x740)!=26||!romAccepted())
            throw std::runtime_error("Identity/ROM mismatch during restoration");
        const auto now=ec.stable(r.offset)&r.mask;
        if(now!=r.original){
            if(now!=r.ours)throw std::runtime_error("Чужое изменение маски; восстановление не перезаписывает его.");
            ec.writeMaskedOnce(r.offset,r.mask,r.original);
        }
        if((ec.stable(r.offset)&r.mask)!=r.original)throw std::runtime_error("Readback восстановления не совпал.");
        event("restore_confirmed",{{"address",r.offset},{"mask",r.mask},{"value",r.original}});
        changes.erase(changes.begin()+static_cast<std::ptrdiff_t>(index));persist();active=!changes.empty();
    }
    void persist(){
        std::ofstream f(folder/"battery-restore.txt",std::ios::trunc);
        for(const auto& r:changes)f<<r.offset<<' '<<r.mask<<' '<<r.original<<' '<<r.ours<<'\n';
        f.flush();if(!f)throw std::runtime_error("Не удалось сохранить восстановление; запись отменена.");
    }
public:
    BatteryFlagSession(Device& d,fs::path p,bool allowRomIdTrial=false):ec(d),folder(std::move(p)),romIdTrial(allowRomIdTrial){
        fs::create_directories(folder);journal.open(folder/"battery-events.jsonl",std::ios::binary);
        if(!journal)throw std::runtime_error("Не удалось открыть журнал.");
        std::cout<<"Battery logs: "<<probe::utf8(folder.c_str())<<'\n'<<std::flush;
    }
    ~BatteryFlagSession(){if(active)try{restore(0);}catch(const std::exception& e){std::cerr<<"RESTORE UNKNOWN: "<<e.what()<<'\n';}}
    void event(const char* name,Json value={}){
        value["event"]=name;value["utc"]=utc();value["uptime_ms"]=GetTickCount64();
        journal<<value.dump()<<'\n';journal.flush();if(!journal)throw std::runtime_error("Ошибка журнала.");
    }
    std::size_t checkpoint()const{return changes.size();}
    void recover(const fs::path& source){
        std::ifstream f(source/"battery-restore.txt");if(!f)throw std::runtime_error("Не найден battery-restore.txt");
        BatteryRecovery r{};while(f>>r.offset>>r.mask>>r.original>>r.ours){
            const bool romRecovery=romIdTrial&&r.offset==0x770&&r.mask==0xfb&&r.original==0xfb&&r.ours==0;
            if(changes.size()>=8||r.offset==address||
               (romIdTrial?!romRecovery:(!Device::writeAllowed(r.offset,r.mask,r.original)||!Device::writeAllowed(r.offset,r.mask,r.ours))))
                throw std::runtime_error("Недопустимое восстановление батареи.");
            changes.push_back(r);
        }
        if(!f.eof())throw std::runtime_error("Повреждён файл восстановления.");
        active=true;persist();restore(0);sample("recovery-complete");
    }
    void guard(bool requireContext=true){
        if(stopping.load())throw Cancelled();
        checkCompetitors();
        if(ec.read(0x454)!=82||ec.read(0x456)!=72||ec.read(0x740)!=26||!romAccepted())
            throw std::runtime_error("Identity/ROM изменились; тест остановлен.");
        SYSTEM_POWER_STATUS power{};
        const auto ready=ec.read(0x490);
        if(!GetSystemPowerStatus(&power)||power.ACLineStatus!=1||!(ready&1)||!(ready&4))
            throw std::runtime_error("Нужны подключённое питание и готовые данные батареи.");
        if(requireContext&&(ec.read(0x7C3)!=7||(ec.read(0x7B9)&0x7f)!=10))
            throw std::runtime_error("Контекст platform7/upper10 изменился; повторных записей нет.");
        const auto temp=ec.read(0x4A2)|(ec.read(0x4A3)<<8);
        if(temp<2731||temp>3181)throw std::runtime_error("Температура батареи вне 0..45C; тест остановлен.");
    }
    Json sample(const std::string& phase){
        Json row{{"phase",phase}};
        for(unsigned a:{0x432u,0x433u,0x434u,0x435u,0x436u,0x437u,0x438u,0x439u,0x490u,0x497u,0x4ABu,
            0x742u,0x7B9u,0x7C3u,0x770u,0x7D0u,0x7A6u,0x7C6u,0x7CCu,0x726u,0x727u,0x741u,0x751u,0x7C5u})
            row[hex(a,3)]=ec.read(a);
        const auto val=[&](unsigned a){return static_cast<unsigned>(*row.find(hex(a,3))->number());};
        const auto state=val(0x432)|(val(0x433)<<8),current=val(0x434)|(val(0x435)<<8),voltage=val(0x438)|(val(0x439)<<8);
        const int direction=(state&3)==2?1:(state&3)==1?-1:0;
        row["state_raw"]=state;row["current_mA"]=current;row["charge_state"]=(state&3)==2;
        row["watts"]=direction*current*voltage/1000000.0;
        std::cout<<phase<<" SOC="<<val(0x4AB)<<" I="<<current<<"mA state="<<state
            <<" 742="<<hex(val(0x742))<<" 7B9="<<hex(val(0x7B9))<<" 7A6="<<hex(val(0x7A6))
            <<" platform="<<val(0x7C3)<<" ROM="<<hex(val(0x770))<<'\n'<<std::flush;
        event("sample",row);return row;
    }
    void dump(const std::string& name){
        std::array<unsigned char,4096> bytes{};
        for(unsigned a=0;a<bytes.size();++a)bytes[a]=static_cast<unsigned char>(ec.read(a));
        std::ofstream f(folder/(name+".bin"),std::ios::binary);f.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
        if(!f)throw std::runtime_error("Не удалось сохранить снимок EC RAM.");
        event("ec_ram_dump",{{"name",name},{"bytes",4096},{"atomic",false}});
    }
    void change(unsigned a,unsigned mask,unsigned bits){
        const auto original=ec.stable(a)&mask,ours=bits&mask;
        if(original==ours){event("write_noop",{{"address",a},{"mask",mask},{"value",ours}});return;}
        changes.push_back({a,mask,original,ours});active=true;persist();
        event("write_requested",{{"address",a},{"mask",mask},{"before",original},{"requested",ours}});
        const auto wanted=ec.writeMaskedOnce(a,mask,ours);
        event("write_observed",{{"address",a},{"wanted",wanted},{"immediate",ec.read(a)}});
    }
    void restore(std::size_t checkpoint){
        while(changes.size()>checkpoint)restoreIndex(changes.size()-1);
        active=!changes.empty();
    }
    void restoreMask(unsigned offset,unsigned mask){
        for(std::size_t i=changes.size();i>0;--i)if(changes[i-1].offset==offset&&changes[i-1].mask==mask){restoreIndex(i-1);return;}
    }
    void observe(const std::string& phase,unsigned seconds){
        const auto start=GetTickCount64();
        for(unsigned i=0;i<seconds;++i){
            guard();sample(phase);
            const auto until=start+(i+1)*1000;
            while(GetTickCount64()<until){if(stopping.load())throw Cancelled();Sleep(25);}
        }
    }
};
inline int runBatteryFlagTest(int argc,char** argv){
    fs::path output,recovery;bool apply=false,readOnly=false;
    for(int i=2;i<argc;++i){const std::string arg=argv[i];
        if(arg=="--apply")apply=true;else if(arg=="--read-only")readOnly=true;
        else if(arg=="--output"&&i+1<argc)output=fs::path(probe::wide(argv[++i]));
        else if(arg=="--restore"&&i+1<argc)recovery=fs::path(probe::wide(argv[++i]));
        else throw std::runtime_error("Battery mode: --apply or --read-only, --output DIR");}
    if(output.empty()||static_cast<int>(apply)+static_cast<int>(readOnly)+static_cast<int>(!recovery.empty())!=1)
        throw std::runtime_error("Нужны --output и ровно один --apply/--read-only/--restore DIR");
    checkCompetitors();Device ec;
    WinHandle completed(CreateEventW(nullptr,TRUE,FALSE,nullptr));cleanupComplete=completed.value;
    SetConsoleCtrlHandler(controlHandler,TRUE);
    struct Finish{~Finish(){if(cleanupComplete)SetEvent(cleanupComplete);SetConsoleCtrlHandler(controlHandler,FALSE);cleanupComplete=nullptr;}} finish;
    BatteryFlagSession session(ec,output/(utc(true)+"-"+std::to_string(GetCurrentProcessId())));
    session.event("session_start",{{"mode","battery-flags"},{"apply",apply},{"platform",7},{"upper",10},{"step_seconds",10},{"rom_writes",false}});
    if(!recovery.empty()){session.recover(recovery);return 0;}
    session.sample("initial");session.dump("initial-ec-ram");
    if(readOnly){session.event("read_only_complete");return 0;}
    try{
        session.guard(false);
        if(ec.read(0x4AB)<=10||ec.read(0x4AB)>98)throw std::runtime_error("SOC вне11..98, тест неинформативен.");
        if((ec.read(0x7B9)&0x7f)>100)throw std::runtime_error("Неизвестный исходный upper.");
        const auto platform=ec.read(0x7C3);if(platform!=4&&platform!=7)throw std::runtime_error("Неизвестная платформа.");
        if((ec.read(0x7A6)&0x30)==0x30)throw std::runtime_error("Неизвестный штатный charging profile.");
        session.change(0x7B9,0x7f,10);session.change(0x7C3,0xff,7);
        session.observe("baseline-platform7-upper10",10);
        if((ec.read(0x432)&3)!=2||(ec.read(0x434)|(ec.read(0x435)<<8))<100)
            throw std::runtime_error("На platform7/upper10 нет зарядного тока >=100мА для сравнения.");
        const auto checkpoint=session.checkpoint();
        struct Step{const char* name;unsigned address,mask,bits;};
        const Step steps[]={{"gate742-bit2",0x742,4,4},{"reached7B9-bit7",0x7B9,0x80,0x80},
            {"profile-balanced",0x7A6,0x30,0x10},{"profile-stationary",0x7A6,0x30,0x20}};
        for(const auto& step:steps){
            if(step.address==0x7A6&&!(ec.read(0x78E)&8)){
                session.event("step_skipped",{{"phase",step.name},{"reason","native charging profiles not advertised"}});continue;
            }
            session.guard();session.change(step.address,step.mask,step.bits);
            session.observe(step.name,10);session.restore(checkpoint);
            session.dump(std::string(step.name)+"-restored");session.observe("between-steps",10);
        }
        session.guard();session.change(0x742,4,4);session.change(0x7B9,0x80,0x80);
        session.observe("gate-and-reached",10);session.restore(checkpoint);
        session.restore(0);session.dump("final-ec-ram");session.sample("final-restored");
        session.event("complete",{{"restored",true}});return 0;
    }catch(...){session.event("abort",{{"reason","exception or operator stop"}});session.restore(0);session.event("abort_restored");throw;}
}
