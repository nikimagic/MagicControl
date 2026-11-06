#include "WinHelpers.h"
#include <tlhelp32.h>
#include <conio.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <bitset>
#include <cstring>
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <exception>
#include <wbemidl.h>
#include <wrl/client.h>

namespace {
using probe::Json;
using probe::WinHandle;
namespace fs=std::filesystem;
constexpr unsigned address=0x741;
constexpr unsigned controlAddresses[]{0x726,0x727,0x734,0x735,0x736,0x741,0x751,
    0x75B,0x75C,0x766,0x767,0x769,0x76A,0x76B,0x783,0x784,0x785,0x78C,0x7C5,0x7C6};
std::atomic<bool> stopping{};
HANDLE cleanupComplete{};
bool interactive{};
struct ConsoleInputMode {
    HANDLE input{GetStdHandle(STD_INPUT_HANDLE)};DWORD original{};bool changed{};
    ConsoleInputMode() {
        if(GetConsoleMode(input,&original))
            changed=SetConsoleMode(input,(original|ENABLE_EXTENDED_FLAGS)&~ENABLE_QUICK_EDIT_MODE)!=FALSE;
    }
    ~ConsoleInputMode(){if(changed)SetConsoleMode(input,original);}
};
BOOL WINAPI controlHandler(DWORD event) {
    if(event!=CTRL_C_EVENT && event!=CTRL_BREAK_EVENT && event!=CTRL_CLOSE_EVENT &&
       event!=CTRL_LOGOFF_EVENT && event!=CTRL_SHUTDOWN_EVENT) return FALSE;
    stopping.store(true);
    if(cleanupComplete) WaitForSingleObject(cleanupComplete,4500);
    return TRUE;
}
std::string utc(bool filename=false) {
    SYSTEMTIME t{}; GetSystemTime(&t); char text[48]{};
    std::snprintf(text,sizeof(text),filename?"%04u%02u%02u-%02u%02u%02u-%03uZ":"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
        t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,t.wMilliseconds);
    return text;
}
std::string hex(unsigned value,int width=2) {
    std::ostringstream text; text<<"0x"<<std::hex<<std::uppercase<<std::setw(width)<<std::setfill('0')<<value;
    return text.str();
}
unsigned masked(unsigned value,unsigned mask,unsigned bits) { return (value&~mask)|(bits&mask); }
struct Cancelled : std::runtime_error { Cancelled():std::runtime_error("Остановлено оператором."){} };
struct MutexLock {
    HANDLE value{};
    explicit MutexLock(HANDLE handle,DWORD timeout=5000):value(handle) {
        const auto result=WaitForSingleObject(value,timeout);
        if(result!=WAIT_OBJECT_0 && result!=WAIT_ABANDONED) throw std::runtime_error("Занят mutex EC/вентиляторов.");
    }
    ~MutexLock() { ReleaseMutex(value); }
};
std::string registryString(const wchar_t* path,const wchar_t* key) {
    wchar_t text[512]{};DWORD bytes=sizeof(text);
    if(RegGetValueW(HKEY_LOCAL_MACHINE,path,key,RRF_RT_REG_SZ|RRF_SUBKEY_WOW6464KEY,nullptr,text,&bytes)!=ERROR_SUCCESS) return {};
    return probe::utf8(text);
}
bool containsWindow(const std::vector<unsigned char>& bytes) {
    const std::array<unsigned char,7> prefix{0x5B,0x80,'E','C','M','G',0};
    if(bytes.size()<36 || std::memcmp(bytes.data(),"DSDT",4)!=0) return false;
    auto found=std::search(bytes.begin(),bytes.end(),prefix.begin(),prefix.end());
    if(found==bytes.end()) return false;
    std::size_t offset=static_cast<std::size_t>(found-bytes.begin())+prefix.size();
    const auto integer=[&]()->std::optional<std::uint64_t> {
        if(offset>=bytes.size()) return {};
        const auto code=bytes[offset++]; if(code==0 || code==1) return code;
        const unsigned count=code==0x0A?1:code==0x0B?2:code==0x0C?4:code==0x0E?8:0;
        if(!count || offset+count>bytes.size()) return {};
        std::uint64_t result{};for(unsigned i=0;i<count;++i) result|=static_cast<std::uint64_t>(bytes[offset++])<<(i*8);
        return result;
    };
    const auto base=integer(),length=integer();
    return base && length && *base==0xFED50000 && *length==4096;
}
bool registryWindow(const std::wstring& path,int depth=0) {
    if(depth>8) return false;
    HKEY key{};if(RegOpenKeyExW(HKEY_LOCAL_MACHINE,path.c_str(),0,KEY_READ|KEY_WOW64_64KEY,&key)!=ERROR_SUCCESS) return false;
    struct Close {HKEY h;~Close(){RegCloseKey(h);}} close{key};
    for(DWORD i=0;;++i) {
        wchar_t name[512]{};DWORD length=512,type{},size{};
        const auto result=RegEnumValueW(key,i,name,&length,nullptr,&type,nullptr,&size);
        if(result==ERROR_NO_MORE_ITEMS) break;
        if((result!=ERROR_SUCCESS && result!=ERROR_MORE_DATA) || type!=REG_BINARY || size<36 || size>4*1024*1024) continue;
        std::vector<unsigned char> bytes(size);
        if(RegQueryValueExW(key,name,nullptr,&type,bytes.data(),&size)==ERROR_SUCCESS) { bytes.resize(size);if(containsWindow(bytes)) return true; }
    }
    for(DWORD i=0;;++i) {
        wchar_t name[512]{};DWORD length=512;
        const auto result=RegEnumKeyExW(key,i,name,&length,nullptr,nullptr,nullptr,nullptr);
        if(result==ERROR_NO_MORE_ITEMS) break;
        if(result==ERROR_SUCCESS && registryWindow(path+L"\\"+name,depth+1)) return true;
    }
    return false;
}
void checkCompetitors() {
    WinHandle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0));
    if(snapshot.value==INVALID_HANDLE_VALUE) throw std::runtime_error(probe::winError());
    PROCESSENTRY32W process{};process.dwSize=sizeof(process);
    if(Process32FirstW(snapshot.value,&process)) do {
        for(const wchar_t* name:{L"MagicControl.exe",L"Fan.exe",L"GCUService.exe",L"DefaultTool.exe",L"AiStoneService.exe"})
            if(_wcsicmp(process.szExeFile,name)==0)
                throw std::runtime_error("Закрой управляющую программу перед тестом: "+probe::utf8(process.szExeFile));
    } while(Process32NextW(snapshot.value,&process));
    SC_HANDLE manager=OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT);
    if(!manager) throw std::runtime_error(probe::winError());
    SC_HANDLE service=OpenServiceW(manager,L"GCUBridge",SERVICE_QUERY_STATUS);
    if(service) {
        SERVICE_STATUS state{};const bool ok=QueryServiceStatus(service,&state)!=FALSE;
        CloseServiceHandle(service);CloseServiceHandle(manager);
        if(!ok || state.dwCurrentState!=SERVICE_STOPPED) throw std::runtime_error("GCUBridge работает или меняет состояние; тест не начат.");
    } else {const auto error=GetLastError();CloseServiceHandle(manager);if(error!=ERROR_SERVICE_DOES_NOT_EXIST) throw std::runtime_error(probe::winError(error));}
}
class Device {
    WinHandle driver,transaction;
    bool romIdTrial{};
public:
    explicit Device(bool allowRomIdTrial=false):romIdTrial(allowRomIdTrial) {
        const wchar_t* path=L"HARDWARE\\DESCRIPTION\\System\\BIOS";
        if(registryString(path,L"BaseBoardProduct")!="WUJIE Series-X5SP4NAG" || registryString(path,L"SystemManufacturer")!="MECHREVO")
            throw std::runtime_error("Тест разрешён только для MECHREVO X5SP4NAG.");
        if(!registryWindow(L"HARDWARE\\ACPI\\DSDT")) throw std::runtime_error("DSDT не подтвердила окно EC 0xFED50000/4096.");
        transaction.value=CreateMutexW(nullptr,FALSE,L"Local\\MagicControl.EcTransactions.v1");
        driver.value=CreateFileW(L"\\\\.\\ACPIDriver",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
        if(!transaction.value || driver.value==INVALID_HANDLE_VALUE) throw std::runtime_error(probe::winError());
        if(read(0x454)!=82 || read(0x456)!=72 || read(0x740)!=26) throw std::runtime_error("Идентификаторы EC не совпали: нужны 82/72/26.");
    }
    unsigned read(unsigned offset) {
        if(offset>=4096) throw std::runtime_error("Чтение за пределами 4 КБ запрещено.");
        MutexLock lock(transaction.value);
        struct Packet {DWORD address,value;} packet{0xFED50000u+offset,0};
        std::array<unsigned char,16> output{};DWORD returned{};
        if(!probe::ioctl(driver.value,0x9C40A490,&packet,sizeof(packet),output.data(),sizeof(output),returned)) throw std::runtime_error(probe::winError());
        if(!returned || returned>output.size()) throw std::runtime_error("Неверная длина ответа EC.");
        return output[0];
    }
    unsigned stable(unsigned offset=address) {
        auto previous=read(offset);
        for(int i=0;i<10;++i) {Sleep(10);const auto current=read(offset);if(current==previous)return current;previous=current;}
        throw std::runtime_error("EC меняется; запись отложена.");
    }
    static bool writeAllowed(unsigned offset,unsigned mask,unsigned bits){
        if(mask>255||bits>255||(bits&~mask))return false;
        return (offset==address&&mask&&(mask&(mask-1))==0&&mask<=128)||
            (offset==0x7C3&&mask==0xff&&(bits==4||bits==7))||
            (offset==0x7B9&&((mask==0x7f&&bits<=100)||mask==0x80))||
            (offset==0x742&&mask==4)||(offset==0x7A6&&mask==0x30&&(bits&0x30)!=0x30);
    }
    static bool romTrialWriteAllowed(unsigned offset,unsigned mask,unsigned bits){
        return offset==0x770&&mask==0xfb&&(bits==0||bits==0xfb);
    }
    unsigned writeMaskedOnce(unsigned offset,unsigned mask,unsigned bits) {
        // Explicit user-authorized RAM-only ROM-ID experiment. Never available
        // to ordinary flag/native-store modes; no Set_ROMID/77E/77F trigger.
        const bool romWrite=romIdTrial&&romTrialWriteAllowed(offset,mask,bits);
        if(!writeAllowed(offset,mask,bits)&&!romWrite)throw std::runtime_error("Запись вне масок изолированного теста запрещена.");
        MutexLock lock(transaction.value);
        const unsigned before=stable(offset),wanted=masked(before,mask,bits);
        if(romWrite&&before!=0xff&&before!=4)throw std::runtime_error("Foreign ROM-ID; no overwrite");
        struct Packet {DWORD address,value;} packet{offset,wanted};
        std::array<unsigned char,16> output{};DWORD returned{};
        if(!probe::ioctl(driver.value,0x9C40A48C,&packet,sizeof(packet),output.data(),sizeof(output),returned)) throw std::runtime_error(probe::winError());
        Sleep(6); // Match the upstream/OEM minimum settling delay after ECRW.
        return wanted;
    }
    unsigned writeBitOnce(unsigned mask,unsigned bits) {return writeMaskedOnce(address,mask,bits);}
};
#include "BatteryFlagTestWindows.h"
#include "BatteryNativeStoreTestWindows.h"
#include "BatteryRomIdTestWindows.h"
using Snapshot=std::array<unsigned char,4096>;
struct RecoveryBit {unsigned bit{},original{};};
std::vector<RecoveryBit> readRecovery(const fs::path& path) {
    std::ifstream input(path);if(!input)throw std::runtime_error("Не удалось открыть restore.txt.");
    std::vector<RecoveryBit> entries;std::string line;unsigned seen{};
    while(std::getline(input,line)) {
        std::istringstream row(line);std::string reg,extra;RecoveryBit entry;
        if(!(row>>reg>>entry.bit>>entry.original) || (row>>extra) || reg!="0x741" || entry.bit>7 || entry.original>1 || (seen&(1u<<entry.bit)))
            throw std::runtime_error("Недопустимый restore.txt.");
        seen|=1u<<entry.bit;entries.push_back(entry);
    }
    if(input.bad() || entries.empty() || entries.size()>2 || (entries.size()==2 && entries.front().bit!=0))
        throw std::runtime_error("Недопустимый порядок восстановления.");
    return entries;
}
class Session {
    Device& ec;
    fs::path directory;
    std::ofstream journal;
    std::ofstream series;
    bool armed{};
    bool contextArmed{};
    unsigned contextOriginal{};
    bool manualContext{};
    unsigned seriesFrames{};
    unsigned mask{},originalBit{};
    std::string phase;
    Snapshot before{};
    Snapshot baseline{};
    ULONGLONG activeSince{};
    void saveRecovery() {
        if(!armed && !contextArmed) {
            std::error_code error;fs::remove(directory/"restore.txt",error);
            if(error)throw std::runtime_error("Не удалось удалить завершённый restore.txt: "+error.message());
            return;
        }
        std::ostringstream text;
        if(contextArmed)text<<"0x741 0 "<<(contextOriginal?1:0)<<'\n';
        if(armed)text<<"0x741 "<<static_cast<unsigned>(std::countr_zero(mask))<<' '<<(originalBit?1:0)<<'\n';
        const auto bytes=text.str();const auto temporary=directory/"restore.tmp";
        {
            WinHandle file(CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr));
            if(file.value==INVALID_HANDLE_VALUE)throw std::runtime_error("Не удалось создать восстановление: "+probe::winError());
            DWORD written{};
            if(!WriteFile(file.value,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr) || written!=bytes.size() || !FlushFileBuffers(file.value))
                throw std::runtime_error("Не удалось сохранить восстановление: "+probe::winError());
        }
        if(!MoveFileExW(temporary.c_str(),(directory/"restore.txt").c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Не удалось заменить восстановление: "+probe::winError());
    }
    void restoreOwned(unsigned ownedMask,unsigned original,const char* kind) {
        const auto current=ec.stable();
        if((current&ownedMask)!=original) {
            eventSafe("restore_requested",{{"kind",kind},{"mask",ownedMask},{"before",current},{"original_bit",original}});
            ec.writeBitOnce(ownedMask,original);
        }
        bool confirmed{};unsigned readback{};
        for(int i=0;i<10;++i) {Sleep(50);readback=ec.read(address);if((readback&ownedMask)==original){confirmed=true;break;}}
        eventSafe("restore_result",{{"kind",kind},{"mask",ownedMask},{"original_bit",original},{"readback",readback},{"confirmed",confirmed}});
        if(!confirmed)throw std::runtime_error("Не восстановился бит "+hex(ownedMask)+"; данные в restore.txt. Следующий бит запрещён.");
    }
public:
    Session(Device& device,const fs::path& root,bool writes,bool pinned=false):ec(device),manualContext(pinned) {
        directory=root/(utc(true)+"-"+std::to_string(GetCurrentProcessId()));fs::create_directories(directory);
        journal.open(directory/"events.jsonl",std::ios::binary);
        if(!journal) throw std::runtime_error("Не удалось создать журнал.");
        series.open(directory/"series.bin",std::ios::binary);
        if(!series)throw std::runtime_error("Не удалось создать серию снимков.");
        event("session_start",{{"address","0x741"},{"settings_writes",writes},{"scope","EC RAM, not firmware flash"},
            {"manual_context",pinned},{"series_frame_bytes",4096},{"build",std::string(__DATE__)+" "+__TIME__}});
        std::cout<<"Логи: "<<probe::utf8(directory.c_str())<<"\n"<<std::flush;
    }
    ~Session() {try{restoreAll();}catch(const std::exception& error){std::cerr<<"ВОССТАНОВЛЕНИЕ НЕ ПОДТВЕРЖДЕНО: "<<error.what()<<"\n";}}
    void event(const char* name,Json value={}) {
        value["event"]=name;value["utc"]=utc();value["phase"]=phase;value["uptime_ms"]=GetTickCount64();
        journal<<value.dump()<<'\n';journal.flush();if(!journal)throw std::runtime_error("Ошибка записи журнала.");
    }
    void eventSafe(const char* name,Json value={}) noexcept {
        try{event(name,std::move(value));}catch(...){std::cerr<<"Ошибка записи журнала при восстановлении.\n";}
    }
    void native(bool allowOwnBit0=false) {
        if((ec.read(0x726)&0x80) || (ec.read(0x727)&0x40) || (ec.read(0x7C6)&4) ||
           (!allowOwnBit0 && (ec.read(address)&1))) throw std::runtime_error("EC не в штатном режиме; сначала выключи RAM/Manual и закрой MagicControl.");
        if(contextArmed && !(ec.read(address)&1))throw std::runtime_error("Прошивка сбросила удерживаемый .0; прекращаю тест без повторного навязывания.");
    }
    Snapshot dump(const std::string& name,const Snapshot* reference=nullptr,bool interruptible=true) {
        Snapshot bytes{};const auto start=utc();
        for(unsigned a=0;a<bytes.size();++a) {if(interruptible && stopping.load())throw Cancelled();bytes[a]=static_cast<unsigned char>(ec.read(a));}
        {
            std::ofstream binary(directory/(name+".bin"),std::ios::binary);binary.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
            if(!binary)throw std::runtime_error("Не удалось сохранить снимок.");
            std::ofstream values(directory/(name+".bytes.csv"));values<<"address,raw_hex,raw_decimal\n";
            for(unsigned a=0;a<bytes.size();++a)values<<hex(a,3)<<','<<hex(bytes[a])<<','<<static_cast<unsigned>(bytes[a])<<'\n';
            if(!values)throw std::runtime_error("Не удалось сохранить значения снимка.");
            std::ofstream csv(directory/(name+".diff.csv"));csv<<"address,before,after,xor\n";
            unsigned changed{};Json changes=Json::array();
            if(reference) for(unsigned a=0;a<bytes.size();++a) if(bytes[a]!=(*reference)[a]) {
                ++changed;csv<<hex(a,3)<<','<<static_cast<unsigned>((*reference)[a])<<','<<static_cast<unsigned>(bytes[a])<<','<<hex(bytes[a]^(*reference)[a])<<'\n';
                changes.push({{"address",hex(a,3)},{"before",(*reference)[a]},{"after",bytes[a]}});
            }
            if(!csv)throw std::runtime_error("Не удалось сохранить различия.");
            event("snapshot",{{"file",name+".bin"},{"started_utc",start},{"bytes",bytes.size()},{"changed_bytes",changed},{"changes",std::move(changes)}});
            std::cout<<"  Снимок "<<name<<": 4096 байт; отличий "<<changed<<"; 0x741="<<hex(bytes[address])<<"\n"<<std::flush;
        }
        return bytes;
    }
    void sample() {
        native(contextArmed || (armed && mask==1));
        Snapshot frame{};const auto started=utc();
        for(unsigned a=0;a<frame.size();++a) {if(stopping.load())throw Cancelled();frame[a]=static_cast<unsigned char>(ec.read(a));}
        const auto frameIndex=seriesFrames;
        series.write(reinterpret_cast<const char*>(frame.data()),frame.size());series.flush();
        if(!series)throw std::runtime_error("Не удалось сохранить серию EC.");
        ++seriesFrames;
        Json controls;for(auto a:controlAddresses)controls[hex(a,3)]=ec.read(a);
        LASTINPUTINFO input{sizeof(input)};const bool inputOk=GetLastInputInfo(&input)!=FALSE;
        SYSTEM_POWER_STATUS power{};const bool powerOk=GetSystemPowerStatus(&power)!=FALSE;
        native(contextArmed || (armed && mask==1));
        event("sample",{{"controls",std::move(controls)},{"cpu_ec_celsius",ec.read(0x43E)},
            {"series_frame",frameIndex},{"series_started_utc",started},{"series_completed_utc",utc()},
            {"windows_idle_ms",inputOk?Json(GetTickCount()-input.dwTime):Json{}},
            {"ac_line_status",powerOk?Json(power.ACLineStatus):Json{}}});
    }
    void initial() {phase="baseline";baseline=dump("baseline");}
    void beginContext() {
        checkCompetitors();native();phase="manual-context";
        const auto old=ec.stable();contextOriginal=old&1;contextArmed=true;
        try{saveRecovery();}catch(...){contextArmed=false;throw;}
        event("context_write_requested",{{"mask",1},{"before",old},{"wanted",old|1}});
        const auto wanted=ec.writeBitOnce(1,1);const auto readback=ec.stable();
        event("context_write_returned",{{"wanted",wanted},{"readback",readback},{"confirmed",(readback&1)!=0}});
        if(!(readback&1))throw std::runtime_error(".0=1 не подтверждён.");
        before=dump("manual-context-after",&baseline);activeSince=GetTickCount64();
        std::cout<<"  Контекст .0=1 подтверждён: "<<hex(readback)<<"\n"<<std::flush;
    }
    void apply(unsigned bit) {
        if(manualContext && (!contextArmed || bit==0))throw std::runtime_error("В ручном контексте .0 удерживается; выбирай .1–.7.");
        checkCompetitors();native(contextArmed);phase="bit-"+std::to_string(bit);
        before=dump(phase+"-before");
        mask=1u<<bit;const auto old=ec.stable();originalBit=old&mask;
        armed=true;try{saveRecovery();}catch(...){armed=false;throw;}
        event("write_requested",{{"mask",mask},{"before",old},{"wanted",old^mask},{"bit",bit}});
        activeSince=GetTickCount64();
        const auto wanted=ec.writeBitOnce(mask,originalBit^mask);
        event("write_returned",{{"wanted",wanted},{"readback",ec.read(address)}});
        std::cout<<"  Запись: "<<hex(old)<<" → "<<hex(wanted)<<"; чтение "<<hex(ec.read(address))<<"\n"<<std::flush;
        dump(phase+"-after",&before);sample();
    }
    void restore() {
        if(!armed)return;
        restoreOwned(mask,originalBit,"tested-bit");armed=false;saveRecovery();
        std::cout<<"  Исходный бит восстановлен; 0x741="<<hex(ec.read(address))<<"\n"<<std::flush;
    }
    void restoreAll() {
        std::string failures;
        try{restore();}catch(const std::exception& error){failures=error.what();}
        if(contextArmed)try{restoreOwned(1,contextOriginal,"manual-context");contextArmed=false;saveRecovery();}
        catch(const std::exception& error){failures+=" ";failures+=error.what();}
        if(!failures.empty())throw std::runtime_error(failures);
    }
    void finishStep() {restore();dump(phase+"-restored",&before,false);native(contextArmed);}
    void finishContext() {phase="manual-context-restore";restoreAll();dump("final-restored",&baseline,false);native();}
    void manualDump() {dump(phase+"-manual-"+utc(true),&before);}
    ULONGLONG ageMs() const {return GetTickCount64()-activeSince;}
    void note(const std::wstring& text) {event("operator_note",{{"text",probe::utf8(text.c_str())}});}
    void recover(const std::vector<RecoveryBit>& entries) {
        phase="recovery";
        for(const auto& entry:entries) {
            if(entries.size()==2 && entry.bit==0){contextArmed=true;contextOriginal=entry.original;}
            else {mask=1u<<entry.bit;originalBit=entry.original?mask:0;armed=true;}
        }
        saveRecovery();restoreAll();dump("recovered",nullptr,false);
    }
    void completed(unsigned testedMask,unsigned requestedMask) {phase="complete";event("session_end",{{"completed",true},
        {"tested_bits_mask",testedMask},{"requested_bits_mask",requestedMask},{"manual_context",manualContext},
        {"series_frames",seriesFrames},{"restoration_complete",!armed && !contextArmed},
        {"all_requested_bits_tested",testedMask==requestedMask},{"all_bits_tested",testedMask==255}});}
};
std::wstring readCommand(Session* session=nullptr,unsigned limitSeconds=90) {
    std::wstring line;const auto start=GetTickCount64();auto nextSample=start;
    bool snapshot10{},snapshot35{};
    for(;;) {
        if(stopping.load())throw Cancelled();
        const auto now=GetTickCount64();
        if(session && now>=nextSample) {session->sample();nextSample=now+1000;}
        if(session && !snapshot10 && now-start>=10000) {session->manualDump();snapshot10=true;}
        if(session && !snapshot35 && now-start>=35000) {session->manualDump();snapshot35=true;}
        if((session?session->ageMs():now-start)>=static_cast<ULONGLONG>(limitSeconds)*1000) {
            std::cout<<"\nВремя ожидания истекло."<<(session?" Восстанавливаю бит; следующий шаг ждёт Enter.\n":" Завершаю.\n");
            if(session)session->event("step_timeout",{{"limit_seconds",limitSeconds},{"elapsed_ms",session->ageMs()}});
            return session?L":timeout":L"q";
        }
        if(!_kbhit()) {Sleep(25);continue;}
        const auto key=_getwch();
        if(key==0 || key==0xE0) {_getwch();continue;}
        if(key==L'\r') {std::cout<<'\n';return line;}
        if(key==L'\b') {if(!line.empty()){line.pop_back();std::cout<<"\b \b";}continue;}
        if(key==27)return L"q";
        if(key>=32 && line.size()<512) {line.push_back(key);std::cout<<probe::utf8(std::wstring(1,key).c_str())<<std::flush;}
    }
}
void observeAutomatically(Session& session,unsigned seconds) {
    session.event("observation_start",{{"seconds",seconds},{"automatic",true}});
    const auto started=GetTickCount64();auto next=started;unsigned printed{};
    while(GetTickCount64()-started<static_cast<ULONGLONG>(seconds)*1000) {
        if(stopping.load())throw Cancelled();
        if(_kbhit()) {
            const auto key=_getwch();
            if(key==0 || key==0xE0) {_getwch();continue;}
            if(key==27 || key==L'q' || key==L'Q')throw Cancelled();
        }
        const auto now=GetTickCount64();
        if(now>=next) {session.sample();next=std::max(next+1000,GetTickCount64());}
        const auto elapsed=static_cast<unsigned>((GetTickCount64()-started)/1000);
        if(elapsed/10>printed) {printed=elapsed/10;std::cout<<"  Наблюдение: "<<elapsed<<" / "<<seconds<<" с\n"<<std::flush;}
        Sleep(25);
    }
    session.event("observation_end",{{"seconds",seconds},{"elapsed_ms",GetTickCount64()-started}});
}
int selfTest() {
    unsigned checks{};
    if(Device::writeAllowed(0x770,0xfb,0)||Device::writeAllowed(0x770,0xfb,0xfb)||
       !Device::romTrialWriteAllowed(0x770,0xfb,0)||!Device::romTrialWriteAllowed(0x770,0xfb,0xfb)||
       Device::romTrialWriteAllowed(0x771,0xfb,0)||Device::romTrialWriteAllowed(0x770,0xff,4)||
       Device::romTrialWriteAllowed(0x770,0xfb,1))throw std::runtime_error("ROM trial allowlist failed");
    ++checks;
    for(unsigned value=0;value<256;++value){
        const auto trial=masked(value,0xfb,0),restored=masked(trial,0xfb,value&0xfb);
        if((trial&4)!=(value&4)||restored!=value)throw std::runtime_error("ROM trial mask/restore failed");
        ++checks;
    }
    for(unsigned old=0;old<256;++old)for(unsigned bit=0;bit<8;++bit) {
        const unsigned mask=1u<<bit,target=masked(old,mask,(old^mask)&mask);
        if(target!=(old^mask) || ((target^old)&~mask) || masked(target,mask,old&mask)!=old)throw std::runtime_error("Mask/restore self-test failed");
        for(unsigned live=0;live<256;++live) {
            const auto restored=masked(live,mask,old&mask);
            if((restored&mask)!=(old&mask) || (restored&~mask)!=(live&~mask))throw std::runtime_error("Live-bit preservation failed");
            ++checks;
        }
    }
    for(unsigned old=0;old<256;++old)for(unsigned bit=1;bit<8;++bit)for(unsigned live=0;live<256;++live) {
        const auto context=old|1u;const auto mask=1u<<bit;
        const auto target=masked(context,mask,context^mask);
        if(!(target&1) || target!=(context^mask))throw std::runtime_error("Pinned context apply failed");
        const auto restoredStep=masked(live|1u,mask,old&mask);
        const auto restoredAll=masked(restoredStep,1,old&1);
        const auto owned=mask|1u;
        if(!(restoredStep&1) || (restoredAll&owned)!=(old&owned) || (restoredAll&~owned)!=(live&~owned))
            throw std::runtime_error("Nested context restoration failed");
        ++checks;
    }
    std::cout<<"Self-test: "<<checks<<" mask/restore checks OK. Hardware access: none.\n";
    return 0;
}
} // namespace
int main(int argc,char** argv) {
    SetConsoleOutputCP(CP_UTF8);SetConsoleCP(CP_UTF8);
#ifdef EC_BIT_SELF_TEST_ONLY
    (void)argc;(void)argv;return selfTest();
#else
    try {
        if(argc>1&&std::string(argv[1])=="--battery-romid-ram")return runBatteryRomIdTest(argc,argv);
        if(argc>1&&std::string(argv[1])=="--battery-native-store")return runBatteryNativeStoreTest(argc,argv);
        if(argc>1&&std::string(argv[1])=="--battery-flags")return runBatteryFlagTest(argc,argv);
        fs::path output,recovery;bool readOnly{},manualContext{},automatic{},startSpecified{},secondsSpecified{};
        unsigned startBit{},lastBit=7,stepSeconds=90;std::optional<unsigned> onlyBit;
        const auto number=[](const std::string& value,unsigned low,unsigned high,const char* name) {
            std::size_t used{};const auto parsed=std::stoul(value,&used);
            if(used!=value.size() || parsed<low || parsed>high)throw std::runtime_error(std::string(name)+": вне допустимого диапазона.");
            return static_cast<unsigned>(parsed);
        };
        for(int i=1;i<argc;++i) {
            const std::string arg=argv[i];
            if(arg=="--self-test")return selfTest();
            if(arg=="--help") {std::cout<<"EcBitTest: isolated bits at EC 0x741.\n--manual-context  pin .0=1 while testing .1..7; restore .0 at exit\n--auto            baseline and each bit without Enter\n--step-seconds N  observation only, 5..90 seconds (auto default 40); NOT an EC timeout setting\n--start-bit N --bit N --read-only --output DIRECTORY --restore DIRECTORY --self-test\n";return 0;}
            if(arg=="--read-only")readOnly=true;
            else if(arg=="--manual-context")manualContext=true;
            else if(arg=="--auto")automatic=true;
            else if(arg=="--output" && i+1<argc)output=fs::path(probe::wide(argv[++i]));
            else if(arg=="--restore" && i+1<argc)recovery=fs::path(probe::wide(argv[++i]));
            else if(arg=="--start-bit" && i+1<argc) {
                startBit=number(argv[++i],0,7,"--start-bit");startSpecified=true;
            }
            else if(arg=="--bit" && i+1<argc)onlyBit=number(argv[++i],0,7,"--bit");
            else if(arg=="--step-seconds" && i+1<argc) {stepSeconds=number(argv[++i],5,90,"--step-seconds");secondsSpecified=true;}
            else throw std::runtime_error("Unknown argument: "+arg);
        }
        if(onlyBit && startSpecified)throw std::runtime_error("Выбери --bit или --start-bit.");
        if(onlyBit)startBit=lastBit=*onlyBit;
        else if(manualContext && !startSpecified)startBit=1;
        if(manualContext && startBit==0)throw std::runtime_error("При --manual-context бит .0 закреплён; проверяются .1–.7.");
        if((readOnly || !recovery.empty()) && (manualContext || automatic || onlyBit || startSpecified || secondsSpecified))
            throw std::runtime_error("--read-only/--restore нельзя совмещать с экспериментом.");
        if(readOnly && !recovery.empty())throw std::runtime_error("--read-only не разрешает восстановительные записи.");
        if(automatic && !secondsSpecified)stepSeconds=40;
        const auto recoveries=recovery.empty()?std::vector<RecoveryBit>{}:readRecovery(recovery/"restore.txt");
        if(output.empty()) {wchar_t path[32768]{};GetModuleFileNameW(nullptr,path,32768);output=fs::path(path).parent_path().parent_path()/L"logs"/L"ec-bit-test";}
        WinHandle single(CreateMutexW(nullptr,FALSE,L"Local\\MagicControl.EcBitTest.v1"));
        WinHandle cpu(CreateMutexW(nullptr,FALSE,L"Local\\Fan_X15Pro_CpuCurveWmi"));
        WinHandle gpu(CreateMutexW(nullptr,FALSE,L"Local\\Fan_X15Pro_GpuCurve"));
        if(!single.value || !cpu.value || !gpu.value)throw std::runtime_error(probe::winError());
        MutexLock own(single.value,0),cpuOwner(cpu.value,0),gpuOwner(gpu.value,0);
        interactive=!readOnly && !automatic;
        ConsoleInputMode consoleInputMode;
        checkCompetitors();Device ec;Session session(ec,output,!readOnly,manualContext);
        const bool recoveringBit0=std::any_of(recoveries.begin(),recoveries.end(),[](const auto& entry){return entry.bit==0;});
        session.native(recoveringBit0);
        WinHandle cleaned(CreateEventW(nullptr,TRUE,FALSE,nullptr));cleanupComplete=cleaned.value;
        if(!cleaned.value || !SetConsoleCtrlHandler(controlHandler,TRUE)) {cleanupComplete=nullptr;throw std::runtime_error(probe::winError());}
        try {
            std::cout<<"EC 0x741: побитовый тест, плата 82/72/26. Только RAM EC, 4096 байт.\n"
                <<"Каждый бит переключается отдельно и восстанавливается.\n";
            if(automatic)std::cout<<"Автопрогон: "<<stepSeconds<<" с на участок; q/Esc/Ctrl+C — остановка и восстановление.\n";
            else std::cout
                <<"Enter — следующий шаг; s — пропустить; q/Esc — выйти.\n"
                <<"После записи: наблюдай, затем введи описание и Enter для восстановления.\n"
                <<"d + Enter — дополнительный снимок. Предел одного шага — 90 секунд.\n\n";
            session.initial();unsigned testedMask{};
            if(!recovery.empty()) {
                session.recover(recoveries);fs::remove(recovery/"restore.txt");
            } else if(!readOnly) {
                session.event("experiment_options",{{"automatic",automatic},{"manual_context",manualContext},{"step_seconds",stepSeconds},
                    {"first_bit",startBit},{"last_bit",lastBit},{"time_unit","observation seconds; no EC timer register written"}});
                if(manualContext)session.beginContext();
                if(automatic) {
                    std::cout<<"\nКонтрольный участок без переключения проверяемых битов.\n"<<std::flush;
                    observeAutomatically(session,stepSeconds);
                }
                const char* names[]{"AP_PRESENT / управление программой","неизвестный","наблюдаемый признак гашения",
                    "ITE reactive (другая клавиатура)","неизвестный","OEM Fan Alert","неизвестный","неизвестный"};
                for(unsigned bit=startBit;bit<=lastBit;++bit) {
                    std::cout<<"\nБит ."<<bit<<", маска "<<hex(1u<<bit)<<", "<<names[bit]<<". "<<std::flush;
                    std::wstring command;
                    if(!automatic) {std::cout<<"Enter — применить: "<<std::flush;command=readCommand(nullptr,600);}
                    while(!command.empty() && command!=L"q" && command!=L"s") {
                        session.note(command);std::cout<<"Enter, s или q: "<<std::flush;command=readCommand(nullptr,600);
                    }
                    if(command==L"q")break;if(command==L"s")continue;
                    Sleep(100);session.apply(bit);
                    if(automatic)observeAutomatically(session,stepSeconds);
                    else {
                        std::cout<<"Наблюдение. Описание + Enter — восстановить: "<<std::flush;
                        command=readCommand(&session,stepSeconds);
                        while(command==L"d") {session.manualDump();std::cout<<"Описание + Enter: "<<std::flush;command=readCommand(&session,stepSeconds);}
                        if(!command.empty() && command!=L"q" && command!=L":timeout")session.note(command);
                    }
                    session.finishStep();testedMask|=1u<<bit;if(command==L"q")break;
                }
                if(manualContext)session.finishContext();
            }
            session.completed(testedMask,readOnly || !recovery.empty()?0u:((255u<<startBit)&((1u<<(lastBit+1))-1)));
        } catch(...) {
            session.eventSafe("session_error",{{"stopping",stopping.load()}});
            try{session.restoreAll();}catch(const std::exception& e){std::cerr<<e.what()<<'\n';}
            SetEvent(cleanupComplete);SetConsoleCtrlHandler(controlHandler,FALSE);cleanupComplete=nullptr;throw;
        }
        SetEvent(cleanupComplete);SetConsoleCtrlHandler(controlHandler,FALSE);cleanupComplete=nullptr;
        std::cout<<"\nЗавершено.\n"<<std::flush;if(interactive){std::cout<<"Нажми Enter для выхода.\n"<<std::flush;readCommand(nullptr,600);}
        return 0;
    } catch(const std::exception& error) {std::cerr<<"\nEcBitTest: "<<error.what()<<'\n';if(interactive && GetConsoleWindow()){std::cerr<<"Enter — закрыть окно.\n";try{readCommand(nullptr,300);}catch(...){}}return 1;}
#endif
}
