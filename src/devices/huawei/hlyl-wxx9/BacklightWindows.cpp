#include "BacklightWindows.h"
#include "I2EcWindows.h"
#include "MagicControl/BoardIdentity.h"
#include "MagicControl/DeviceProfile.h"
#include "MagicControl/Logging.h"
#include "../../../protocols/huawei/Hwmi.h"
#include <windows.h>
#include <array>
#include <format>
#include <stdexcept>
#include <string>
#include <optional>
#include <vector>
#include <type_traits>
#include <sstream>
#include <iomanip>

namespace mc::windows {
namespace {
// Derived from the user's HLYL-WXX9_KeyboardLight_WORKING source, physically
// rechecked 18.10.2026. These are volatile I2EC runtime fields, not EC flash.
constexpr std::uint16_t levelAddress=0x02F4,shadowAddress=0x03FC,dutyAddress=0x1804;
struct Rule {std::uint16_t address;std::uint8_t mask,expected;};
constexpr std::array<Rule,17> hardwareRules{{
    {0x1612,0xff,0x00},{0x1626,0xff,0x80},{0x1611,0xff,0x40},{0x1620,0xff,0x40},
    {0x1632,0xff,0x40},{0x1641,0xff,0x40},{0x1800,0xff,0x01},{0x1801,0xff,0xc8},
    {0x180a,0x04,0x00},{0x180b,0x0f,0x0b},{0x180c,0xff,0x3a},{0x180d,0xff,0x00},
    {0x182d,0xff,0x01},{0x182e,0xff,0x00},{0x1840,0x10,0x10},{0x1843,0xff,0xc8},{0x1849,0x04,0x00}}};
constexpr std::array<Rule,6> safetyRules{{
    {0x042c,1,0},{0x045c,0xff,0xff},{0x180f,4,0},{0x1823,2,2},{0x031a,1,1},{0x0400,0xff,0x10}}};
void requireBoard(){
    if(deviceProfile(readBoardDescriptor()).id!="huawei-hlyl-wxx9")throw std::runtime_error("I2EC: требуется HLYL-WXX9-PCB");
    wchar_t bios[128]{};DWORD size=sizeof(bios);
    if(RegGetValueW(HKEY_LOCAL_MACHINE,L"HARDWARE\\DESCRIPTION\\System\\BIOS",L"BIOSVersion",RRF_RT_REG_SZ,nullptr,bios,&size)!=ERROR_SUCCESS||std::wstring(bios)!=L"1.09")
        throw std::runtime_error("I2EC: поддерживается только проверенный BIOS1.09");
}
struct LegacyState {bool known{},stopped{};std::string status;};
LegacyState legacyState(){
    const auto manager=OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT);
    if(!manager)return {false,false,"HuaweiKeyboardLight: SCM Win32="+std::to_string(GetLastError())};
    const auto service=OpenServiceW(manager,L"HuaweiKeyboardLight",SERVICE_QUERY_STATUS);
    const auto error=GetLastError();CloseServiceHandle(manager);
    if(!service){if(error==ERROR_SERVICE_DOES_NOT_EXIST)return {true,true,"HuaweiKeyboardLight: not installed"};
        return {false,false,"HuaweiKeyboardLight: query access Win32="+std::to_string(error)};}
    SERVICE_STATUS status{};const bool okay=QueryServiceStatus(service,&status)!=FALSE;const auto queryError=GetLastError();CloseServiceHandle(service);
    if(!okay)return {false,false,"HuaweiKeyboardLight: status Win32="+std::to_string(queryError)};
    return {true,status.dwCurrentState==SERVICE_STOPPED,"HuaweiKeyboardLight: state="+std::to_string(status.dwCurrentState)};
}
void requireLegacyStopped(){
    const auto state=legacyState();if(!state.known||!state.stopped)throw std::runtime_error(state.status+"; I2EC write unavailable");
}
struct Snapshot {
    std::uint8_t level{},shadow{},duty{};
    std::uint16_t timeoutTicks{},elapsedTicks{};
    std::uint8_t delayedRestore{},aux{};
    std::array<std::uint8_t,7> safety{};
    std::array<std::uint8_t,17> hardware{};
    friend bool operator==(const Snapshot&,const Snapshot&)=default;
};
bool policyEqual(const Snapshot& a,const Snapshot& b){
    // Elapsed time, native fade PWM and delayed-resume counters are firmware
    // observations. Only selected policy/context must remain ours.
    return a.level==b.level&&(a.shadow&0xc0)==(b.shadow&0xc0)&&a.timeoutTicks==b.timeoutTicks&&a.safety==b.safety&&a.hardware==b.hardware;
}
struct Observation {std::uint16_t address{};std::optional<unsigned> raw;std::uint64_t at{};std::string error;};
class Session : public HlylI2EcSession {
public:
    bool record{};std::vector<Observation> observations;
    std::uint8_t read(std::uint16_t address){
        try{const auto value=HlylI2EcSession::read(address);if(record)observations.push_back({address,value,GetTickCount64(),{}});return value;}
        catch(const std::exception& error){if(record)observations.push_back({address,{},GetTickCount64(),error.what()});throw;}
    }
    std::uint16_t word(std::uint16_t high,std::uint16_t low){
        for(int attempt=0;attempt<3;++attempt){const auto first=read(high),least=read(low),second=read(high);
            if(first==second)return static_cast<std::uint16_t>((unsigned(first)<<8)|least);}
        throw std::runtime_error("I2EC: unstable keyboard timer/counter word");
    }
    void write(std::uint16_t address,std::uint8_t value){
        writeBacklight(address,value,address==dutyAddress);
    }
    std::array<std::uint8_t,7> safety(){
        std::array<std::uint8_t,7> values{};
        for(size_t i=0;i<safetyRules.size();++i)values[i]=read(safetyRules[i].address)&safetyRules[i].mask;
        // Original BIOS file1647C/164A3/164D9 gates PWM2 with042C.bit0,
        // 031A.bit0,0400 and GPIOC6. A1/C0/E2/G1 OUTPUT LEVELS belong to
        // other firmware paths (e.g.file1A12D/1A16D); they are not power
        // permission checks for keyboard PWM. Do not freeze them or write them.
        // Their pin CONFIGURATION remains checked in hardwareRules above.
        values[6]=read(0x1603)&0x40;
        return values;
    }
    Snapshot snapshot(){
        Snapshot value;value.level=read(levelAddress);value.shadow=read(shadowAddress);value.duty=read(dutyAddress);
        value.timeoutTicks=word(0x0318,0x0319);value.elapsedTicks=word(0x02f8,0x02f9);
        value.delayedRestore=read(0x02f7);value.aux=read(0x02f5);value.safety=safety();
        for(size_t i=0;i<hardwareRules.size();++i)value.hardware[i]=read(hardwareRules[i].address)&hardwareRules[i].mask;
        return value;
    }
    void mirror(bool required){
        bool low=false,high=false;
        for(int i=0;i<128&&!(low&&high);++i){if(read(0x1661)&4)high=true;else low=true;}
        if(required&&!(low&&high))throw std::runtime_error("I2EC: физический PWM2 не подтверждён");
    }
};
template<class Action> auto checkedSession(Action action){
    Session session;
    try{
        if constexpr(std::is_void_v<std::invoke_result_t<Action,Session&>>){action(session);session.finish();}
        else{auto result=action(session);session.finish();return result;}
    }catch(const HlylI2EcFinishError&){throw;}
    catch(const std::exception& error){
        const std::string reason=error.what();
        try{session.finish();}catch(const std::exception& exit){throw std::runtime_error(reason+"; "+exit.what());}
        throw;
    }
}
std::uint8_t duty(int level){return static_cast<std::uint8_t>(level==0?0:level==1?0x32:0x64);}
std::string classify(const Snapshot& value){
    for(size_t i=0;i<hardwareRules.size();++i)if(value.hardware[i]!=hardwareRules[i].expected)throw std::runtime_error("I2EC: PWM-конфигурация отличается от проверенной");
    for(size_t i=0;i<safetyRules.size();++i)if(value.safety[i]!=safetyRules[i].expected)throw std::runtime_error("I2EC: питание/guard не допускают управление");
    if(value.safety[6]||value.level>2)throw std::runtime_error("I2EC: keyboard selected level/power guard invalid");
    // Resume may retain HIGH in the owned shadow bits while runtime PWM is
    // off. Neighbouring03FC[5:0] are not brightness permissions or our state.
    if(value.level==0&&(value.shadow&0xc0)==0x80&&value.duty==0)return "resume_retained_high";
    if((value.shadow&0xc0)!=(value.level<<6))throw std::runtime_error("I2EC: selected level and owned shadow disagree");
    const auto full=duty(value.level);
    if(value.duty==full)return "steady";
    // Exact BIOS1.09 file164D9/16623: non-zero seconds*10 timeout,
    // elapsed>=timeout, and duty decremented by period/10 (200/10=20).
    // Do not accept arbitrary darkness/duty or extend the LOW-resume signature.
    if(value.level==0||!value.timeoutTicks||value.timeoutTicks>36000||value.timeoutTicks%10||value.elapsedTicks<value.timeoutTicks)
        throw std::runtime_error("I2EC: effective PWM differs outside a proven native idle context");
    if(value.duty==0)return "native_idle_off";
    if(value.elapsedTicks<=unsigned(value.timeoutTicks)+10&&value.duty<full&&(full-value.duty)%20==0)return "native_idle_fade";
    throw std::runtime_error("I2EC: effective PWM is not a reviewed native fade step");
}
void unchanged(Session& session,const Snapshot& before){
    requireLegacyStopped();
    if(session.safety()!=before.safety)throw std::runtime_error("I2EC: изменилось состояние питания/guard");
}
std::string phaseStatus(const Snapshot& value,const std::string& phase){
    // Raw elapsed counter is diagnostic evidence, not changing UI text on
    // every poll. Only selected/effective phase changes need a card refresh.
    auto status=std::format("Honor white keyboard: selected={} actual PWM={}/200; phase={}; timeout={} raw ticks",value.level,value.duty,phase,value.timeoutTicks);
    if(value.level&&value.timeoutTicks&&value.elapsedTicks>unsigned(value.timeoutTicks)+10&&value.duty==duty(value.level))
        status+="; native idle window elapsed; activity counter was not reset";
    return status;
}
}
HlylBacklightState readHlylBacklight(){
    requireBoard();const auto legacy=legacyState();
    return checkedSession([&](Session& session){const auto value=session.snapshot();const auto phase=classify(value);
        return HlylBacklightState{value.level,session.writable&&legacy.known&&legacy.stopped,value.duty,200,phase,
            phaseStatus(value,phase)+"; "+legacy.status};});
}
HlylFanDiagnostic readHlylFanDiagnostic(){
    requireBoard();
    // No XRAM write operation is exposed. The production setter allowlist
    // remains limited to the three backlight addresses.
    return checkedSession([](Session& session){return HlylFanDiagnostic{session.read(0x042c),session.read(0x1801),session.read(0x1808),session.read(0x1809),session.read(0x0532),session.read(0x053c)};});
}
void setHlylBacklight(int level){
    if(level<0||level>2)throw std::invalid_argument("I2EC: доступны OFF/LOW/HIGH");
    requireBoard();requireLegacyStopped();
    try{
        const auto confirmed=checkedSession([&](Session& session){
            if(!session.writable)throw std::runtime_error("I2EC: интерфейс только для чтения");
            auto before=session.snapshot();(void)classify(before);Sleep(75);
            const auto recent=session.snapshot();const auto recentPhase=classify(recent);
            if(!policyEqual(before,recent)||
                (before.duty!=recent.duty&&(recent.duty>before.duty||(recentPhase!="native_idle_fade"&&recentPhase!="native_idle_off"))))
                throw std::runtime_error("I2EC: keyboard policy/context changed; write cancelled");
            before=recent;
            // Source validity is independent of the requested target. The
            // already-reviewed retained-HIGH resume state also admits OFF/LOW.
            session.mirror(before.duty!=0);
            const auto shadow=static_cast<std::uint8_t>((before.shadow&0x3f)|(level<<6));
            if(before.level==level&&(before.shadow&0xc0)==(shadow&0xc0)&&before.duty==duty(level))return phaseStatus(before,recentPhase)+"; unchanged";
            bool attempted=false;
            try{
                unchanged(session,before);attempted=true;
                session.write(levelAddress,static_cast<std::uint8_t>(level));unchanged(session,before);
                session.write(shadowAddress,shadow);unchanged(session,before);
                session.write(dutyAddress,duty(level));unchanged(session,before);Sleep(500);
                const auto after=session.snapshot();const auto phase=classify(after);
                if(after.level!=level||(after.shadow&0xc0)!=(shadow&0xc0)||after.timeoutTicks!=before.timeoutTicks||after.safety!=before.safety||after.hardware!=before.hardware)
                    throw std::runtime_error("I2EC: selected brightness/context not confirmed");
                // Firmware may legitimately fade the newly selected level
                // again. Do not reset its activity counter or invent a timer.
                session.mirror(after.duty!=0);
                return phaseStatus(after,phase)+"; selected policy readback verified; fan/flash writes=0";
            }catch(const std::exception& failure){
                const std::string reason=failure.what();std::string rollback="no target write attempted";
                if(attempted)try{
                    const auto current=session.snapshot();
                    if(current.safety!=before.safety||current.hardware!=before.hardware||current.timeoutTicks!=before.timeoutTicks)
                        throw std::runtime_error("power/configuration/timeout changed");
                    auto restore=[&](std::uint16_t address,std::uint8_t saved,std::uint8_t target){const auto value=session.read(address);
                        const auto mask=address==shadowAddress?0xc0:0xff;
                        if((value&mask)==(saved&mask))return;
                        if((value&mask)!=(target&mask))throw std::runtime_error("current value is not owned by this operation");
                        unchanged(session,before);session.write(address,saved);};
                    restore(dutyAddress,before.duty,duty(level));restore(shadowAddress,before.shadow,shadow);restore(levelAddress,before.level,static_cast<std::uint8_t>(level));
                    const auto restored=session.snapshot();(void)classify(restored);
                    if(!policyEqual(restored,before)||restored.duty!=before.duty)throw std::runtime_error("rollback snapshot mismatch");
                    rollback="previous owned registers read back; no optical claim";
                }catch(const std::exception& error){rollback=std::string("rollback NOT confirmed; actual state unknown: ")+error.what();}
                throw std::runtime_error(reason+"; "+rollback);
            }
        });
        // checkedSession has confirmed PNPCFG exit, including its no-op path.
        logMessage("huawei_i2ec","Backlight confirmed; PNPCFG exit verified; "+confirmed);
    }catch(const std::exception& error){logMessage("huawei_i2ec",std::string("Backlight failed: ")+error.what());throw;}
}
namespace {
std::string jsonString(const std::string& value){
    std::ostringstream out;out<<'"';
    for(const unsigned char c:value){
        if(c=='"'||c=='\\')out<<'\\'<<char(c);
        else if(c=='\n')out<<"\\n";else if(c=='\r')out<<"\\r";else if(c=='\t')out<<"\\t";
        else if(c<0x20)out<<"\\u"<<std::hex<<std::setfill('0')<<std::setw(4)<<unsigned(c)<<std::dec;
        else out<<char(c);
    }
    out<<'"';return out.str();
}
}
std::string readHlylKeyboardDiagnosticJson(){
    const auto legacy=legacyState();std::vector<Observation> observations;std::optional<Snapshot> value;
    std::string phase="unknown",status;bool exitConfirmed=false,identity=false,writable=false;
    try{
        if(wcsstr(GetCommandLineW(),L"--no-ec"))throw std::runtime_error("I2EC diagnostic disabled by --no-ec");
        requireBoard();
        checkedSession([&](Session& session){identity=true;writable=session.writable;session.record=true;
            try{value=session.snapshot();phase=classify(*value);status=phaseStatus(*value,phase);}
            catch(const std::exception& error){status=error.what();}
            // These are fixed reviewed identity/mirror fields, not a SFR sweep.
            for(const auto address:{0x2000,0x2001,0x200d,0x1661})try{(void)session.read(static_cast<std::uint16_t>(address));}catch(const std::exception& error){status+="; ";status+=error.what();}
            observations=std::move(session.observations);
        });exitConfirmed=true;
    }catch(const std::exception& error){status+="; ";status+=error.what();}
    std::string timeoutStatus="not read",timeoutRaw="null";std::optional<unsigned> timeoutSeconds;
    if(identity)try{
        // HWMI is queried only AFTER the configuration-port session is closed.
        if(!exitConfirmed)throw std::runtime_error("HWMI skipped after uncertain PNPCFG exit");
        auto session=huawei::openSession();const auto data=session->call(huawei::command(huawei::getKeyboardTimeout));
        timeoutRaw="[";for(const auto byte:data){if(timeoutRaw.size()>1)timeoutRaw+=',';timeoutRaw+=std::to_string(byte);}timeoutRaw+=']';
        huawei::success(data,3);const unsigned seconds=data[1]+(unsigned(data[2])<<8);
        if(seconds>3600)throw std::runtime_error("invalid GKBT seconds="+std::to_string(seconds));
        timeoutSeconds=seconds;timeoutStatus="HWMI0x1206 status0; units=seconds";
    }catch(const std::exception& error){timeoutStatus=std::string("HWMI0x1206: ")+error.what();}
    std::string rows="[";for(const auto& row:observations){if(rows.size()>1)rows+=',';
        rows+=std::format("{{\"address\":{},\"raw\":{},\"sampled_uptime_ms\":{},\"error\":{}}}",row.address,row.raw?std::to_string(*row.raw):"null",row.at,jsonString(row.error));}rows+=']';
    std::string guards="[";
    const auto appendGuard=[&](const Rule& rule){
        const Observation* last=nullptr;for(const auto& row:observations)if(row.address==rule.address)last=&row;
        if(guards.size()>1)guards+=',';
        guards+=std::format("{{\"address\":{},\"mask\":{},\"expected\":{},\"raw\":{},\"passed\":{}}}",rule.address,rule.mask,rule.expected,
            last&&last->raw?std::to_string(*last->raw):"null",last&&last->raw?((*last->raw&rule.mask)==rule.expected?"true":"false"):"null");
    };
    for(const auto& rule:hardwareRules)appendGuard(rule);for(const auto& rule:safetyRules)appendGuard(rule);appendGuard({0x1603,0x40,0});guards+=']';
    return std::format("{{\"schema\":1,\"model\":\"HLYL-WXX9-PCB\",\"bios\":\"1.09\",\"read_only\":true,\"target_writes\":0,\"sfr_sweep\":false,\"identity_verified\":{},\"pnp_config_exit_confirmed\":{},\"interface_writable\":{},\"legacy_service_known\":{},\"legacy_service_stopped\":{},\"legacy_service_status\":{},\"phase\":{},\"status\":{},\"selected_level\":{},\"shadow_raw\":{},\"actual_pwm_raw\":{},\"period\":{},\"timeout_ticks\":{},\"elapsed_ticks\":{},\"gkbt_seconds\":{},\"gkbt_status\":{},\"gkbt_raw\":{},\"observations\":{},\"guards\":{},\"runtime_proof\":\"PawnIOLib/LpcIO SHA pinned by active transport before load\",\"mirror_note\":\"1661.bit2 is one electrical observation, not optical proof; samples are non-atomic\"}}\n",
        identity,exitConfirmed,writable,legacy.known,legacy.stopped,jsonString(legacy.status),jsonString(phase),jsonString(status),
        value?std::to_string(value->level):"null",value?std::to_string(value->shadow):"null",value?std::to_string(value->duty):"null",value?std::to_string(value->hardware[7]):"null",
        value?std::to_string(value->timeoutTicks):"null",value?std::to_string(value->elapsedTicks):"null",timeoutSeconds?std::to_string(*timeoutSeconds):"null",jsonString(timeoutStatus),timeoutRaw,rows,guards);
}
}
