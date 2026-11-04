#include "MagicControl/Overclock.h"
#include "MagicControl/DeviceProfile.h"
#include <array>
#include <cerrno>
#include <fstream>
#include <format>
#ifdef _WIN32
#include <windows.h>
#endif
namespace mc {
namespace {
std::string flag(std::optional<std::uint8_t> x){
    if(!x)return "—";
    if(*x==0)return "0 · нет";
    if(*x==1)return "1 · да";
    return std::format("0x{:02X} · значение не определено",*x);
}
std::string raw(std::optional<std::uint32_t> x){return x?std::to_string(*x)+" · raw": "—";}
#ifdef _WIN32
class ScopedFirmwarePrivilege {
    HANDLE token_{};bool impersonating_{};
public:
    DWORD error{};
    ScopedFirmwarePrivilege(){
        if(!ImpersonateSelf(SecurityImpersonation)){error=GetLastError();return;}
        impersonating_=true;
        if(!OpenThreadToken(GetCurrentThread(),TOKEN_ADJUST_PRIVILEGES|TOKEN_QUERY,TRUE,&token_)){error=GetLastError();return;}
        TOKEN_PRIVILEGES privileges{};privileges.PrivilegeCount=1;
        if(!LookupPrivilegeValueW(nullptr,L"SeSystemEnvironmentPrivilege",&privileges.Privileges[0].Luid)){error=GetLastError();return;}
        privileges.Privileges[0].Attributes=SE_PRIVILEGE_ENABLED;SetLastError(ERROR_SUCCESS);
        if(!AdjustTokenPrivileges(token_,FALSE,&privileges,0,nullptr,nullptr)){error=GetLastError();return;}
        error=GetLastError();
    }
    ~ScopedFirmwarePrivilege(){if(token_)CloseHandle(token_);if(impersonating_)RevertToSelf();}
    bool available()const noexcept{return !error&&token_;}
};
#endif
}
OverclockFirmware readOverclockFirmware(const BoardDescriptor& board){
    OverclockFirmware out;
    if(!overclockObservationSupported(board)){
        out.status="Для этой платы чтение FwVars не подтверждено";return out;
    }
#ifdef _WIN32
    ScopedFirmwarePrivilege privilege;
    if(!privilege.available()){out.status=std::format("UEFI недоступна: права Windows, код {}",privilege.error);return out;}
    constexpr wchar_t guid[]=L"{9f33f85c-13ca-4fd1-9c4a-96217722c593}";
    const wchar_t* names[]{L"OemMagicVariable",L"UniWillVariable"};
    for(const auto* name:names){
        std::array<std::uint8_t,1024> data{};DWORD attrs{};SetLastError(ERROR_SUCCESS);
        const DWORD size=GetFirmwareEnvironmentVariableExW(name,guid,data.data(),DWORD(data.size()),&attrs);
        if(size&&size<=data.size()){out=decodeWujieOverclock(std::span(data).first(size));
            out.source=std::format("UEFI · {} · {} байт · атрибуты 0x{:X}",name==names[0]?"OemMagicVariable":"UniWillVariable",size,attrs);return out;}
        if(size>data.size()){out.status="UEFI: переменная превысила предел чтения";return out;}
        const DWORD error=GetLastError();
        if(error!=ERROR_ENVVAR_NOT_FOUND){out.status=std::format("UEFI: ошибка чтения Windows {}",error);return out;}
    }
    out.status="UEFI: переменные OemMagicVariable и UniWillVariable отсутствуют";
#else
    constexpr const char* paths[]{"/sys/firmware/efi/efivars/OemMagicVariable-9f33f85c-13ca-4fd1-9c4a-96217722c593",
        "/sys/firmware/efi/efivars/UniWillVariable-9f33f85c-13ca-4fd1-9c4a-96217722c593"};
    for(const auto* path:paths){
        errno=0;std::ifstream file(path,std::ios::binary);if(!file){if(errno==ENOENT)continue;out.status=std::format("UEFI: efivarfs недоступна, код {}",errno);return out;}
        std::array<std::uint8_t,1029> data{};file.read(reinterpret_cast<char*>(data.data()),data.size());const auto size=static_cast<std::size_t>(file.gcount());
        if(size<4||size>=data.size()||file.bad()){out.status="UEFI: некорректная длина efivarfs";return out;}
        const auto attrs=std::uint32_t(data[0])|(std::uint32_t(data[1])<<8)|(std::uint32_t(data[2])<<16)|(std::uint32_t(data[3])<<24);
        out=decodeWujieOverclock(std::span(data).subspan(4,size-4));
        out.source=std::format("efivarfs · {} · {} байт · атрибуты 0x{:X}",path==paths[0]?"OemMagicVariable":"UniWillVariable",size-4,attrs);return out;
    }
    out.status="UEFI: efivarfs или OEM-переменная отсутствует";
#endif
    return out;
}
OverclockRows overclockRows(const OverclockFirmware& s){
    OverclockRows rows{{"Источник",s.source.empty()?"—":s.source},{"Состояние",s.status.empty()?"Ожидание чтения":s.status}};
    if(!s.readable)return rows;
    const auto number=[](std::optional<std::uint8_t> value){return value?std::to_string(*value):std::string("—");};
    const auto switched=[](std::optional<std::uint8_t> value){
        if(!value)return std::string("—");
        if(*value==0)return std::string("0 · выкл.");
        if(*value==1)return std::string("1 · вкл.");
        return std::format("0x{:02X} · значение не определено",*value);
    };
    rows.insert(rows.end(),{{"Project ID / версия AP",number(s.project)+" / "+number(s.version)},
        {"CPU · поддержка OEM OC",flag(s.cpuSupport)}, {"Память · поддержка OEM OC",flag(s.memorySupport)},
        {"Память · переключатель FwVars",switched(s.memorySwitch)}, {"Восстановление Overclock",flag(s.recovery)},
        {"Меню OC · поддержка",flag(s.menuSupport)}, {"Меню OC · переключатель",flag(s.menuSwitch)},
        {"CPU частота · тек. / мин. / макс.",raw(s.frequency)+" / "+raw(s.frequencyMinimum)+" / "+raw(s.frequencyMaximum)},
        {"CPU напряжение · тек. / мин. / макс.",raw(s.voltage)+" / "+raw(s.voltageMinimum)+" / "+raw(s.voltageMaximum)},
        {"Единицы и применение", "Не подтверждены; raw не является фактической частотой или напряжением"}});
    return rows;
}
bool OverclockOperations::refresh(std::function<void()> ready){
    if(worker_.busy())return false;
    try{worker_.launch([this]{try{pending_=readOverclockFirmware(readBoardDescriptor());}catch(const std::exception& e){pending_={};pending_.status=e.what();}catch(...){pending_={};pending_.status="Ошибка чтения";}},std::move(ready));return true;}
    catch(const std::exception& e){current_={};current_.status=e.what();return false;}
}
bool OverclockOperations::take(){if(!worker_.takeCompletion())return false;current_=std::move(pending_);return true;}
}
