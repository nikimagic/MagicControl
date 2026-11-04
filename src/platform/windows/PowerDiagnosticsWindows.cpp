#include "MagicControl/ActionLog.h"
#include "PowerDiagnosticsWindows.h"
#include "MagicControl/EcAccess.h"
#include "MagicControl/EcRegister.h"
#include "MagicControl/Logging.h"
#include "MagicControl/Monitoring.h"
#include "MagicControl/PortableSettings.h"
#include "MagicControl/BoardIdentity.h"
#include "MagicControl/DeviceProfile.h"
#include "MagicControl/DeviceOperation.h"
#include <powrprof.h>
#include <powersetting.h>
#include <commctrl.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <format>
#include <fstream>
#include <locale>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <vector>
#include <iterator>

namespace mc::windows {
namespace {
std::atomic<int> effectiveMode{-1}, requestedPreset{-1}, confirmedPreset{-1};
std::atomic<unsigned> samplesRemaining{};
std::atomic<ULONGLONG> burstGeneration{};
// After an action/power event capture ten samples at1Hz. UI polling and
// control cadence stay independent; events/errors are still written at once.
constexpr unsigned kBurstSamples=10;
constexpr ULONGLONG kSampleIntervalMs=1000;
std::string quote(std::string_view value) {
    std::string result{"\""};
    for (const unsigned char ch : value) {
        switch (ch) {
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default: if (ch < 0x20) result += std::format("\\u{:04x}", ch); else result += static_cast<char>(ch);
        }
    }
    return result + '"';
}
std::string number(std::optional<double> value) {
    return value && std::isfinite(*value) ? std::format("{:.3f}", *value) : "null";
}
} // namespace

std::string lenovoPowerDiagnostic(const MonitoringSnapshot& sample){
    const auto integer=[](auto v){return v?std::to_string(*v):std::string("null");};
    const auto bit=[](auto v,unsigned mask){return !v?std::string("null"):(*v&mask)?std::string("true"):std::string("false");};
    const auto boolean=[](std::optional<bool> v){return !v?std::string("null"):*v?std::string("true"):std::string("false");};
    std::string result="{";
    if(const auto& p=sample.intelPackagePower){
        const auto hex=[](auto v){return v?quote(std::format("0x{:X}",*v)):std::string("null");};
        result+=std::format("\"msr_sampled_at_ms\":{},\"mmio_sampled_at_ms\":{},\"msr_units_raw\":{},\"msr_610_raw\":{},\"msr_613_cumulative_raw\":{},\"msr_pl1_w\":{},\"msr_pl2_w\":{},\"msr_pl1_enabled\":{},\"msr_pl2_enabled\":{},\"msr_pl1_window_s\":{},\"msr_pl2_window_s\":{},\"mmio_59a0_raw\":{},\"mmio_59a4_raw\":{},\"cltp_mmio_w\":{},\"cstp_mmio_w\":{},\"mmio_pl1_enabled\":{},\"mmio_pl2_enabled\":{},\"mmio_pl1_clamp\":{},\"mmio_shared_window_s\":{},\"intel_status\":{}",
            p->msrSampledAtMs,p->mmioSampledAtMs,hex(p->unitsRaw),hex(p->msrLimitRaw),hex(p->msrPerformanceRaw),number(p->msrLimit1Watts),number(p->msrLimit2Watts),boolean(p->msrLimit1Enabled),boolean(p->msrLimit2Enabled),number(p->msrWindow1Seconds),number(p->msrWindow2Seconds),hex(p->mmioLimit1Raw),hex(p->mmioLimit2Raw),number(p->mmioLimit1Watts),number(p->mmioLimit2Watts),boolean(p->mmioLimit1Enabled),boolean(p->mmioLimit2Enabled),boolean(p->mmioLimit1Clamp),number(p->mmioWindow1Seconds),quote(p->status));
    }
    if(const auto& e=sample.lenovoPowerEcDiagnostic){
        if(result.size()>1)result+=',';
        std::optional<unsigned> acty;
        if(e->c4a3&&e->c439&&e->c41d&&*e->c4a3!=0xff&&*e->c439!=0xff&&*e->c41d!=0xff){
            const bool adpt=(*e->c4a3&0x80)!=0,acmo=(*e->c4a3&0x08)!=0,acps=(*e->c41d&0x80)!=0,c170=(*e->c439&0x02)!=0;
            acty=!adpt?3:!acmo?2:c170?5:acps?4:1;
        }
        result+=std::format("\"ec_started_at_ms\":{},\"ec_sampled_at_ms\":{},\"ec_c5e0\":{},\"ec_c5e1\":{},\"ec_c5e3\":{},\"ec_c5fb\":{},\"ec_c5ff\":{},\"ec_c41d\":{},\"candidate_f9fb_bit7\":{},\"candidate_exmf_bit4\":{},\"candidate_ctmd_bit0\":{},\"ec_alias_status\":{}",
            e->startedAtMs,e->sampledAtMs,integer(e->c5e0),integer(e->c5e1),integer(e->c5e3),integer(e->c5fb),integer(e->c5ff),integer(e->c41d),bit(e->c5fb,0x80),bit(e->c5ff,0x10),bit(e->c41d,0x01),quote(e->status));
        result+=std::format(",\"adapter_ec_c4a3\":{},\"adapter_ec_c439\":{},\"candidate_acty\":{},\"adapter_ec_c22f_class\":{},\"adapter_ec_c2a7\":{},\"adapter_ec_c2de\":{},\"adapter_ec_c2df\":{},\"adapter_ec_c450\":{},\"adapter_ec_c451\":{},\"adapter_ec_c93f\":{},\"adapter_ec_c930\":{},\"adapter_ec_c931\":{},\"adapter_ec_c932\":{},\"adapter_ec_c933\":{}",
            integer(e->c4a3),integer(e->c439),integer(acty),integer(e->c22f),integer(e->c2a7),integer(e->c2de),integer(e->c2df),integer(e->c450),integer(e->c451),integer(e->c93f),integer(e->c930),integer(e->c931),integer(e->c932),integer(e->c933));
        // Raw, individually twice-read stable candidate aliases. F01602.0
        // is a distinct MMIO gate and cannot be inferred from Windows AC.
        // This word combines stable bytes but is not an atomic word sample.
        const std::optional<unsigned> c364word=e->c364&&e->c365?std::optional<unsigned>(unsigned(*e->c364)|(unsigned(*e->c365)<<8)):std::nullopt;
        std::optional<int> c4d0signed;
        if(e->c4d0&&e->c4d1){const auto raw=unsigned(*e->c4d0)|(unsigned(*e->c4d1)<<8);c4d0signed=raw<0x8000?static_cast<int>(raw):static_cast<int>(raw)-0x10000;}
        result+=std::format(",\"adapter_ec_c2a5\":{},\"adapter_ec_c29a\":{},\"adapter_ec_c364\":{},\"adapter_ec_c365\":{},\"adapter_ec_c364_word_from_stable_bytes\":{},\"adapter_ec_c304\":{},\"adapter_ec_c305\":{},\"adapter_ec_c3b6\":{},\"adapter_ec_c3b8\":{},\"adapter_ec_c4ae\":{},\"adapter_ec_c30c\":{},\"adapter_ec_c3fe\":{},\"adapter_ec_c4be\":{},\"adapter_ec_c4ce\":{},\"adapter_ec_c200\":{},\"adapter_ec_c2c9\":{},\"adapter_ec_c4c1\":{},\"adapter_hw_f01602_bit0\":null",
            integer(e->c2a5),integer(e->c29a),integer(e->c364),integer(e->c365),integer(c364word),integer(e->c304),integer(e->c305),integer(e->c3b6),integer(e->c3b8),integer(e->c4ae),integer(e->c30c),integer(e->c3fe),integer(e->c4be),integer(e->c4ce),integer(e->c200),integer(e->c2c9),integer(e->c4c1));
        result+=std::format(",\"adapter_ec_c4d0\":{},\"adapter_ec_c4d1\":{},\"adapter_ec_c4d0_signed_from_stable_bytes\":{}",
            integer(e->c4d0),integer(e->c4d1),integer(c4d0signed));
        // Independent candidate words, not measured watts; individually stable
        // bytes do not make either word or the full snapshot atomic.
        const auto word=[](std::optional<std::uint8_t> lo,std::optional<std::uint8_t> hi)->std::optional<unsigned>{
            return lo&&hi?std::optional<unsigned>(unsigned(*lo)|(unsigned(*hi)<<8)):std::nullopt;
        };
        result+=std::format(",\"adapter_ec_c2a2\":{},\"adapter_ec_c2a3\":{},\"adapter_ec_c2a2_word_from_stable_bytes\":{},\"adapter_ec_c452\":{},\"adapter_ec_c453\":{},\"adapter_ec_c452_word_from_stable_bytes\":{},\"adapter_ec_c2bb\":{},\"adapter_ec_c43e\":{}",
            integer(e->c2a2),integer(e->c2a3),integer(word(e->c2a2,e->c2a3)),integer(e->c452),integer(e->c453),integer(word(e->c452,e->c453)),integer(e->c2bb),integer(e->c43e));
    }
    return result+'}';
}

namespace {
std::string utf8(const std::wstring& value) {
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(size), '\0');
    if (size) WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
    return result;
}
std::string guid(const GUID& value) {
    return std::format("{:08x}-{:04x}-{:04x}-{:02x}{:02x}-{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}",
        value.Data1, value.Data2, value.Data3, value.Data4[0], value.Data4[1], value.Data4[2],
        value.Data4[3], value.Data4[4], value.Data4[5], value.Data4[6], value.Data4[7]);
}
const char* modeName(int mode) {
    constexpr const char* names[]{"battery_saver", "better_battery", "balanced", "high_performance",
                                  "max_performance", "game_mode", "mixed_reality"};
    return mode >= 0 && mode < 7 ? names[mode] : "unknown";
}
const char* presetName(int preset) {
    return preset == 0 ? "office" : preset == 1 ? "balanced" : preset == 2 ? "turbo" : "unknown";
}
struct Journal {
    void write(std::string_view kind, const std::string& fields) noexcept {
        if (!anyLoggingEnabled()) return;
        try { recordLog("power", std::format("\"uptime_ms\":{},\"event\":{},{}", GetTickCount64(), quote(kind), fields)); }
        catch (...) {}
    }
};
Journal& journal() { static Journal value; return value; }
void startBurst(std::string_view reason) {
    if (!loggingEnabled()) return;
    samplesRemaining.store(kBurstSamples);
    burstGeneration.fetch_add(1);
    journal().write("burst_start", std::format("\"samples\":{},\"interval_ms\":{},\"reason\":{}",kBurstSamples,kSampleIntervalMs,quote(reason)));
}
std::string powerStatus(std::optional<int>* percent=nullptr) {
    SYSTEM_POWER_STATUS status{};
    if (!GetSystemPowerStatus(&status)) return std::format("{{\"error\":{}}}", GetLastError());
    if(percent&&status.BatteryLifePercent<=100)*percent=status.BatteryLifePercent;
    return std::format("{{\"ac_line_status\":{},\"battery_flags\":{},\"battery_percent\":{},\"system_saver\":{}}}",
        status.ACLineStatus, status.BatteryFlag, status.BatteryLifePercent, status.SystemStatusFlag);
}
std::string windowsPolicy() {
    GUID* scheme{};
    const auto error = PowerGetActiveScheme(nullptr, &scheme);
    if (error != ERROR_SUCCESS || !scheme) return std::format("{{\"error\":{}}}", error);
    struct Release { GUID* value; ~Release() { LocalFree(value); } } release{scheme};
    const auto batteryPolicy=readBatteryPowerPolicy(scheme);
    std::string result = std::format("{{\"scheme_guid\":{},\"scheme_name\":{},\"effective_mode\":{},\"effective_mode_name\":{},\"base_scheme_cpu_policy\":{{",
        quote(batteryPolicy.schemeGuid),batteryPolicy.schemeName.empty()?"null":quote(batteryPolicy.schemeName),
        effectiveMode.load(), quote(modeName(effectiveMode.load())));
    struct Setting { const char* name; const GUID* id; };
    const Setting settings[]{
        {"minimum_percent", &GUID_PROCESSOR_THROTTLE_MINIMUM}, {"maximum_percent", &GUID_PROCESSOR_THROTTLE_MAXIMUM},
        {"boost_mode", &GUID_PROCESSOR_PERF_BOOST_MODE},
        {"energy_performance_preference", &GUID_PROCESSOR_PERF_ENERGY_PERFORMANCE_PREFERENCE},
        {"frequency_limit_mhz", &GUID_PROCESSOR_FREQUENCY_LIMIT}};
    bool first = true;
    for (const auto& setting : settings) {
        PowerPolicySetting values;
        if(IsEqualGUID(*setting.id,GUID_PROCESSOR_PERF_ENERGY_PERFORMANCE_PREFERENCE))values=batteryPolicy.epp;
        else if(IsEqualGUID(*setting.id,GUID_PROCESSOR_FREQUENCY_LIMIT))values=batteryPolicy.frequencyMHz;
        else{
            DWORD ac{},dc{};
            values.acError=PowerReadACValueIndex(nullptr,scheme,&GUID_PROCESSOR_SETTINGS_SUBGROUP,setting.id,&ac);
            values.dcError=PowerReadDCValueIndex(nullptr,scheme,&GUID_PROCESSOR_SETTINGS_SUBGROUP,setting.id,&dc);
            if(!values.acError)values.ac=ac;if(!values.dcError)values.dc=dc;
        }
        if (!first) result += ',';
        first = false;
        result += std::format("{}:{{\"ac\":{},\"dc\":{},\"ac_error\":{},\"dc_error\":{}}}", quote(setting.name),
            values.ac?std::to_string(*values.ac):"null",values.dc?std::to_string(*values.dc):"null",values.acError,values.dcError);
    }
    return result + "}}";
}
struct EcSnapshot {
    std::array<std::optional<std::uint8_t>, 30> values;
    std::string status, errors;
};
constexpr std::array<std::uint16_t, 30> addresses{
    ec::kPerformanceMode, ec::kPowerLimitSustained, ec::kPowerLimitShort, ec::kPowerLimitPeak,
    ec::kAdapterClass, ec::kPowerInput, ec::kTypeCStatus, ec::kTypeCPowerA, ec::kTypeCPowerB,
    ec::kBatteryChargeControl, ec::kCustomMode, ec::kControl727, ec::kAppPresence,
    ec::kFanControlRespective, ec::kRamTableCommit, ec::kCpuTemperature,
    0x734, 0x735, 0x736, 0x730, 0x731, 0x732, 0x7A7, 0x7A8, 0x7A9,
    0x742, 0x7C3, 0x770, 0x7A6, 0x7D0};
EcSnapshot readEc(EcAccess* access) {
    EcSnapshot result;
    if (!access) { result.status = deviceProfile(readBoardDescriptor()).id=="mechrevo-gm6ir0c"?
        "not_applicable_x5_map; see gm6ir0c channel":"unavailable"; return result; }
    if (!access->boardVerified()) { result.status = "board_unverified"; return result; }
    if (deviceOperations().passiveEcReadsPaused()) { result.status = "paused_for_ec_snapshot"; return result; }
    if (access->observationsPaused()) { result.status = "paused_for_command"; return result; }
    result.status = "ok";
    // Existing process-wide gate, also used by SETs and the fan controller.
    const EcAccess::Transaction transaction(*access);
    if (deviceOperations().passiveEcReadsPaused()) { result.status = "paused_for_ec_snapshot"; return result; }
    for (std::size_t i = 0; i < addresses.size(); ++i) {
        try { result.values[i] = access->readStable(addresses[i]); }
        catch (const std::exception& error) {
            result.status = "partial";
            if (!result.errors.empty()) result.errors += "; ";
            result.errors += std::format("0x{:03X}: {}", addresses[i], error.what());
        }
    }
    return result;
}
std::string ecJson(const EcSnapshot& snapshot) {
    std::string result = std::format("{{\"status\":{},\"errors\":{},\"raw\":{{", quote(snapshot.status), quote(snapshot.errors));
    for (std::size_t i = 0; i < addresses.size(); ++i) {
        if (i) result += ',';
        result += quote(std::format("0x{:03X}", addresses[i])) + ':' +
            (snapshot.values[i] ? std::to_string(*snapshot.values[i]) : "null");
    }
    const auto raw = snapshot.values[0];
    int observed = -1;
    if (raw) {
        const auto preset = *raw & ec::kPerformancePresetMask;
        observed = preset == 0x80 ? 0 : preset == 0 ? 1 : preset == 0x10 ? 2 : -1;
    }
    const int requested = requestedPreset.load(), confirmed = confirmedPreset.load();
    result += std::format("}},\"observed_preset\":{},\"last_requested_preset\":{},\"last_confirmed_preset\":{},\"matches_last_confirmed\":{},\"fan_boost\":{}}}",
        quote(presetName(observed)), requested < 0 ? "null" : quote(presetName(requested)),
        confirmed < 0 ? "null" : quote(presetName(confirmed)),
        raw && confirmed >= 0 ? (observed == confirmed ? "true" : "false") : "null",
        raw ? ((*raw & ec::kPerformanceBoostBit) ? "true" : "false") : "null");
    return result;
}
void WINAPI effectiveChanged(EFFECTIVE_POWER_MODE mode, void*) {
    const int previous = effectiveMode.exchange(static_cast<int>(mode));
    try {
        if (previous != static_cast<int>(mode)) {
            journal().write("effective_power_mode", std::format("\"mode\":{},\"name\":{}", static_cast<int>(mode), quote(modeName(mode))));
            if (previous >= 0) startBurst("effective_power_mode");
        }
    }
    catch (...) {}
}
std::string nativeControlState(HWND parent){
    struct Capture{std::string json{"["};unsigned count{};} capture;
    if(!IsWindow(parent))return "[]";
    EnumChildWindows(parent,+[](HWND w,LPARAM raw)->BOOL{
        auto& c=*reinterpret_cast<Capture*>(raw);wchar_t type[64]{};GetClassNameW(w,type,64);
        const bool button=!_wcsicmp(type,L"Button"),edit=!_wcsicmp(type,L"Edit"),combo=!_wcsicmp(type,L"ComboBox");
        if(!button&&!edit&&!combo)return TRUE;
        if(edit&&(GetWindowLongPtrW(w,GWL_STYLE)&ES_PASSWORD))return TRUE;
        if(c.count>=256)return FALSE;
        const int length=GetWindowTextLengthW(w);std::wstring text(std::min(length,4096)+1,0);GetWindowTextW(w,text.data(),static_cast<int>(text.size()));text.resize(wcslen(text.c_str()));
        if(c.count++)c.json+=',';
        c.json+=std::format("{{\"id\":{},\"class\":{},\"text\":{},\"text_truncated\":{},\"enabled\":{},\"visible\":{}",GetDlgCtrlID(w),quote(utf8(type)),quote(utf8(text.c_str())),length>4096,IsWindowEnabled(w)!=FALSE,IsWindowVisible(w)!=FALSE);
        if(button)c.json+=",\"native_check\":"+std::to_string(SendMessageW(w,BM_GETCHECK,0,0))+",\"selected\":"+(GetPropW(w,L"MC.Selected")?"true":"false");
        c.json+='}';return TRUE;
    },reinterpret_cast<LPARAM>(&capture));
    return capture.json+"]";
}
struct NativeDispatch{HWND window;UINT message;WPARAM wp;LPARAM lp;audit::Id action;std::unique_ptr<audit::Scope> scope;};
thread_local std::vector<NativeDispatch> nativeDispatch;
LRESULT CALLBACK buttonMessage(int code, WPARAM wParam, LPARAM lParam) {
    if(code>=0&&loggingEnabled())try{
        const auto& m=*reinterpret_cast<const CWPSTRUCT*>(lParam);std::string kind,fields;HWND control{};
        if(m.message==WM_COMMAND&&m.lParam){
            control=reinterpret_cast<HWND>(m.lParam);wchar_t type[64]{},caption[4097]{},parent[128]{};
            GetClassNameW(control,type,64);GetWindowTextW(control,caption,4097);GetClassNameW(m.hwnd,parent,128);
            const auto notification=HIWORD(m.wParam);
            if(!_wcsicmp(type,L"Button")&&notification==BN_CLICKED)kind="button_clicked";
            else if(!_wcsicmp(type,L"Edit")&&notification==EN_KILLFOCUS&&!(GetWindowLongPtrW(control,GWL_STYLE)&ES_PASSWORD))kind="numeric_or_text_committed";
            else if(!_wcsicmp(type,L"ComboBox")&&notification==CBN_SELCHANGE)kind="selection_changed";
            if(!kind.empty())fields=std::format("\"control_id\":{},\"caption\":{},\"parent_class\":{},\"enabled\":{},\"controls_before\":{}",LOWORD(m.wParam),quote(utf8(caption)),quote(utf8(parent)),IsWindowEnabled(control)!=FALSE,nativeControlState(m.hwnd));
        }else if(m.message==WM_NOTIFY&&m.lParam){
            const auto* h=reinterpret_cast<const NMHDR*>(m.lParam);
            if(h->code==UDN_DELTAPOS){const auto* d=reinterpret_cast<const NMUPDOWN*>(h);kind="numeric_step";
                fields=std::format("\"control_id\":{},\"position\":{},\"delta\":{},\"controls_before\":{}",h->idFrom,d->iPos,d->iDelta,nativeControlState(m.hwnd));}
        }else if(m.message==WM_HSCROLL&&m.lParam){
            control=reinterpret_cast<HWND>(m.lParam);kind="slider_changed";fields=std::format("\"control_id\":{},\"value\":{}",GetDlgCtrlID(control),SendMessageW(control,TBM_GETPOS,0,0));
        }
        if(!kind.empty()){
            const auto id=audit::input("windows.native",kind,fields);
            if(nativeDispatch.size()<64)nativeDispatch.push_back({m.hwnd,m.message,m.wParam,m.lParam,id,std::make_unique<audit::Scope>("windows.ui_dispatch","",id)});
            // Preserve the existing sampling policy. Newly audited edit/combo/
            // slider messages must not add hardware polling just for the log.
            if(kind=="button_clicked")startBurst("application_button");
            else if(kind=="numeric_step")startBurst("numeric_step");
        }
    }catch(...){}
    return CallNextHookEx(nullptr,code,wParam,lParam);
}
LRESULT CALLBACK buttonReturned(int code,WPARAM wp,LPARAM lp){
    if(code>=0)try{
        const auto& m=*reinterpret_cast<const CWPRETSTRUCT*>(lp);
        for(auto i=nativeDispatch.rbegin();i!=nativeDispatch.rend();++i)if(i->window==m.hwnd&&i->message==m.message&&i->wp==m.wParam&&i->lp==m.lParam){
            i->scope->finish("ui_dispatch_returned","\"controls_after\":"+nativeControlState(m.hwnd)+",\"hardware_confirmed\":false");
            nativeDispatch.erase(std::next(i).base());break;
        }
    }catch(...){}
    return CallNextHookEx(nullptr,code,wp,lp);
}

} // namespace

BatteryPowerPolicy readBatteryPowerPolicy(const GUID* activeScheme){
    BatteryPowerPolicy result;result.applicable=true;
    GUID* owned{};
    if(!activeScheme){
        const auto error=PowerGetActiveScheme(nullptr,&owned);
        if(error||!owned){result.status=std::format("PowerGetActiveScheme error={}",error);return result;}
        activeScheme=owned;
    }
    struct Release{GUID* value;~Release(){if(value)LocalFree(value);}} release{owned};
    result.schemeGuid=guid(*activeScheme);
    std::array<wchar_t,512> name{};DWORD bytes=sizeof(name);
    if(PowerReadFriendlyName(nullptr,activeScheme,nullptr,nullptr,reinterpret_cast<PUCHAR>(name.data()),&bytes)==ERROR_SUCCESS)
        result.schemeName=utf8(name.data());
    const auto read=[&](const GUID& id,bool epp){
        PowerPolicySetting value;DWORD ac{},dc{};
        value.acError=PowerReadACValueIndex(nullptr,activeScheme,&GUID_PROCESSOR_SETTINGS_SUBGROUP,&id,&ac);
        value.dcError=PowerReadDCValueIndex(nullptr,activeScheme,&GUID_PROCESSOR_SETTINGS_SUBGROUP,&id,&dc);
        if(!value.acError){if(!epp||ac<=100)value.ac=ac;else value.acError=ERROR_INVALID_DATA;}
        if(!value.dcError){if(!epp||dc<=100)value.dc=dc;else value.dcError=ERROR_INVALID_DATA;}
        return value;
    };
    result.frequencyMHz=read(GUID_PROCESSOR_FREQUENCY_LIMIT,false);
    result.epp=read(GUID_PROCESSOR_PERF_ENERGY_PERFORMANCE_PREFERENCE,true);
    result.status=(result.frequencyMHz.ac&&result.frequencyMHz.dc&&result.epp.ac&&result.epp.dc)?"ok":"partial";
    return result;
}

struct PowerDiagnostics::Impl {
    std::vector<HPOWERNOTIFY> notifications;
    void* effectiveHandle{};
    HHOOK buttons{},buttonsAfter{};
    explicit Impl(HWND window) {
        // Both are operational notifications, also needed with logging off.
        if (window) for (const auto* id : {&GUID_ACDC_POWER_SOURCE, &GUID_SESSION_DISPLAY_STATUS}) {
            if (const auto handle = RegisterPowerSettingNotification(window, id, DEVICE_NOTIFY_WINDOW_HANDLE)) notifications.push_back(handle);
            else journal().write("registration_error", std::format("\"guid\":{},\"error\":{}", quote(guid(*id)), GetLastError()));
        }
        buttons=SetWindowsHookExW(WH_CALLWNDPROC,buttonMessage,nullptr,GetCurrentThreadId());
        buttonsAfter=SetWindowsHookExW(WH_CALLWNDPROCRET,buttonReturned,nullptr,GetCurrentThreadId());
        if (loggingEnabled()&&!buttons) journal().write("registration_error", std::format("\"api\":\"button_events\",\"error\":{}", GetLastError()));
        journal().write("session_start", std::format("\"pid\":{},\"nominal_interval_ms\":1000,\"notes\":{}",
            GetCurrentProcessId(), quote("Read-only. EC 0x783..0x785 are custom overrides, zero is not a measured CPU watt limit. Windows base-scheme AC/DC policy is configuration, not applied SMU telemetry. USB-C bytes are raw OEM values; a PD voltage/current contract is not verified.")));
        if (window) for (const auto* id : {&GUID_ACTIVE_POWERSCHEME, &GUID_POWER_SAVING_STATUS}) {
            if (const auto handle = RegisterPowerSettingNotification(window, id, DEVICE_NOTIFY_WINDOW_HANDLE)) notifications.push_back(handle);
            else journal().write("registration_error", std::format("\"guid\":{},\"error\":{}", quote(guid(*id)), GetLastError()));
        }
        const auto error = PowerRegisterForEffectivePowerModeNotifications(EFFECTIVE_POWER_MODE_V2, effectiveChanged, nullptr, &effectiveHandle);
        if (FAILED(error)) journal().write("registration_error", std::format("\"api\":\"effective_power_mode\",\"hresult\":{}", error));
    }
    ~Impl() {
        if (buttons) UnhookWindowsHookEx(buttons);
        if(buttonsAfter)UnhookWindowsHookEx(buttonsAfter);
        if (effectiveHandle) static_cast<void>(PowerUnregisterFromEffectivePowerModeNotifications(effectiveHandle));
        for (const auto handle : notifications) UnregisterPowerSettingNotification(handle);
        journal().write("notifications_stopped", "\"reason\":\"application_exit\"");
    }
};
PowerDiagnostics::PowerDiagnostics(HWND window) : impl_(std::make_unique<Impl>(window)) {}
PowerDiagnostics::~PowerDiagnostics() = default;
std::filesystem::path PowerDiagnostics::path() const { return currentLogPath(); }
void PowerDiagnostics::broadcast(WPARAM event, LPARAM data) noexcept {
    if (!anyLoggingEnabled()) return;
    try {
        std::string fields = std::format("\"message\":{},\"windows_power\":{}", event, powerStatus());
        if (event == PBT_POWERSETTINGCHANGE && data) {
            const auto* change = reinterpret_cast<const POWERBROADCAST_SETTING*>(data);
            fields += ",\"setting_guid\":" + quote(guid(change->PowerSetting));
            if (change->DataLength == sizeof(DWORD)) {
                DWORD value{}; std::memcpy(&value, change->Data, sizeof(value));
                fields += ",\"value\":" + std::to_string(value);
            } else if (change->DataLength == sizeof(GUID)) {
                GUID value{}; std::memcpy(&value, change->Data, sizeof(value));
                fields += ",\"value_guid\":" + quote(guid(value));
            }
        }
        journal().write("windows_power_event", fields);
        if (event == PBT_POWERSETTINGCHANGE || event == PBT_APMPOWERSTATUSCHANGE ||
            event == PBT_APMRESUMEAUTOMATIC || event == PBT_APMRESUMESUSPEND) startBurst("windows_power_event");
    } catch (...) {}
}
void recordPowerSample(EcAccess* access, const MonitoringSnapshot& sample) noexcept {
    if (!anyLoggingEnabled()) return;
    try {
        const auto started = GetTickCount64();
        const auto policy = windowsPolicy();
        std::optional<int> systemPercent;
        const auto status = powerStatus(&systemPercent);
        const auto snapshot = readEc(access);
        const auto ecState = ecJson(snapshot);
        // Multiple monitor clients can publish concurrently. Serialize comparison
        // and publication of their shared baselines after the hardware read.
        static std::mutex sampleGate;
        std::lock_guard sampleLock(sampleGate);
        // Continuous battery/power evidence is independent of the bounded CPU/GPU
        // burst below. Include every available raw power/control field; CPU
        // temperature alone is telemetry, not a reason to emit flag changes.
        static std::string previousBatterySignature, previousBatteryHealth;
        static std::optional<int> previousBatteryPercent;
        static EcSnapshot previousBatteryEc;
        static bool batteryBaseline{};
        static bool hadEcAccess{};
        static std::optional<ULONGLONG> missingEcSince;
        // A command/snapshot pause is a gap in observation, not an EC transition.
        // Keep each last known byte across gaps and partial reads; compare only
        // two available values. Report real read errors separately from raw deltas.
        const bool paused=snapshot.status.starts_with("paused_");
        bool missingGrace=false;
        if(!paused){
            if(access){hadEcAccess=true;missingEcSince.reset();}
            else if(hadEcAccess){
                const auto now=GetTickCount64();
                if(!missingEcSince)missingEcSince=now;
                missingGrace=now-*missingEcSince<5000;
            }
        }
        if(!paused&&!missingGrace)previousBatteryHealth=snapshot.status+snapshot.errors;
        const std::string signature=policy+status+previousBatteryHealth;
        std::string delta;
        bool firstEcValue=false;
        for(std::size_t i=0;i<addresses.size();++i){
            if(addresses[i]==ec::kCpuTemperature)continue;
            const auto raw=[](std::optional<std::uint8_t> value){return value?std::format("0x{:02X}",*value):std::string("unknown");};
            if(paused||!snapshot.values[i])continue;
            if(!previousBatteryEc.values[i])firstEcValue=true;
            else if(snapshot.values[i]!=previousBatteryEc.values[i]){
                if(!delta.empty())delta+=';';
                delta+=std::format("0x{:03X}:{}>{}",addresses[i],raw(previousBatteryEc.values[i]),raw(snapshot.values[i]));
            }
            previousBatteryEc.values[i]=snapshot.values[i];
        }
        const bool socChanged=systemPercent!=previousBatteryPercent;
        if(!batteryBaseline||socChanged||firstEcValue||!delta.empty()||signature!=previousBatterySignature){
            const auto reason=!batteryBaseline?"initial":socChanged?"soc_change":firstEcValue?"ec_baseline":"flags_or_policy_change";
            recordLog("battery_power",std::format("\"event\":\"snapshot\",\"reason\":{},\"previous_percent\":{},\"windows_power\":{},\"windows_policy\":{},\"ec\":{},\"raw_changes\":{}",
                quote(reason),previousBatteryPercent?std::to_string(*previousBatteryPercent):"null",status,policy,ecState,quote(delta)));
            recordLog("battery",std::format("\"event\":\"power_flags\",\"reason\":{},\"windows_power\":{},\"effective_mode\":{},\"ec_status\":{},\"raw_changes\":{}",
                quote(reason),status,effectiveMode.load(),quote(snapshot.status),quote(delta)));
            previousBatterySignature=signature;previousBatteryPercent=systemPercent;batteryBaseline=true;
        }
        if(!loggingEnabled())return;
        static std::string previousPolicy, previousEc;
        std::string ecSignature;
        if (snapshot.status == "ok") for (std::size_t i = 0; i < 10; ++i)
            ecSignature += snapshot.values[i] ? std::to_string(*snapshot.values[i]) + ',' : "null,";
        const bool changed = policy != previousPolicy || (!ecSignature.empty() && ecSignature != previousEc);
        if (changed) {
            journal().write("power_state_change", "\"windows_policy\":" + policy + ",\"windows_power\":" + status + ",\"ec\":" + ecState);
            previousPolicy = policy;
            if (!ecSignature.empty()) previousEc = ecSignature;
            // EC settling during an existing series does not extend it forever.
            if (!samplesRemaining.load()) startBurst("observed_policy_or_ec_change");
        }
        static ULONGLONG lastSampleAt{};
        const auto now = GetTickCount64();
        const auto pending=samplesRemaining.load();
        const bool lenovo=sample.intelPackagePower.has_value()||sample.lenovoPowerEcDiagnostic.has_value();
        // Keep a sparse idle baseline, but capture the requested CPU/GPU
        // interaction at 10 s when the NVIDIA GPU is under substantial load.
        const auto gpuWatts=sample.processors[1].powerWatts;
        static std::optional<bool> previousGpuLoaded;
        const auto gpuLoaded=gpuWatts?std::optional<bool>(*gpuWatts>=(previousGpuLoaded?(*previousGpuLoaded?95.:105.):100.)):std::nullopt;
        const bool gpuTransition=gpuLoaded&&(!previousGpuLoaded||*previousGpuLoaded!=*gpuLoaded);
        if(gpuLoaded)previousGpuLoaded=gpuLoaded;
        const auto interval=pending?kSampleIntervalMs:gpuLoaded.value_or(false)?10000ULL:60000ULL;
        const bool immediateTransition=lenovo&&!pending&&gpuTransition&&(!lastSampleAt||now-lastSampleAt>=5000);
        if ((!pending&&!lenovo) || (!immediateTransition&&lastSampleAt && now - lastSampleAt < interval)) return;
        lastSampleAt = now;
        const auto remaining = pending?samplesRemaining.fetch_sub(1)-1:0;
        const auto& cpu = sample.processors[0]; const auto& gpu = sample.processors[1];
        const auto& battery = sample.battery;
        const auto batteryAges=[&]{
            const auto now=batteryMonotonicMs();std::string ages="[";
            for(std::size_t i=0;i<battery.dynamicAtMs.size();++i){if(i)ages+=',';const auto at=battery.dynamicAtMs[i];ages+=at&&now>=at?std::to_string(now-at):"null";}
            return ages+"]";
        };
        const char* flow = battery.flow == BatteryFlow::Charging ? "charging" : battery.flow == BatteryFlow::Discharging ? "discharging" : battery.flow == BatteryFlow::Idle ? "idle" : "unknown";
        const auto devices=[&](const auto& items){
            std::string json="[";
            for(const auto& item:items){if(json.size()>1)json+=',';
                json+=std::format("{{\"celsius\":{},\"model\":{},\"serial\":{},\"source\":{},\"status\":{}}}",
                    number(item.celsius),quote(item.model),quote(item.serial),quote(item.source),quote(item.status));}
            return json+"]";
        };
        auto fields=std::format(
            "\"read_duration_ms\":{},\"processor_sample_uptime_ms\":{},\"windows_power\":{},\"effective_mode\":{},\"ec\":{},\"application_ec_write_count\":{},"
            "\"cpu\":{{\"celsius\":{},\"tctl_celsius\":{},\"driver_celsius\":{},\"usage_percent\":{},\"clock_mhz\":{},\"watts\":{},\"temperature_source\":{},\"power_source\":{},\"fan_rpm\":{},\"fan_pwm\":{}}},"
            "\"gpu\":{{\"celsius\":{},\"usage_percent\":{},\"clock_mhz\":{},\"watts\":{},\"fan_rpm\":{},\"fan_pwm\":{}}},"
            "\"system_power_watts\":{},\"system_power_source\":{},\"system_power_stale\":{},\"system_consumption_watts\":{},\"apu_package_power_watts\":{},\"apu_package_power_source\":{},\"soc_celsius\":{},"
            "\"battery\":{{\"online\":{},\"percent\":{},\"flow\":{},\"watts\":{},\"volts\":{},\"milliamps\":{},\"celsius\":{},\"temperature_raw\":{},\"temperature_source\":{},\"temperature_status\":{},\"source\":{},\"dynamic_stale\":{},\"dynamic_age_ms_percent_watts_temperature_voltage_current\":{},\"power_status\":{}}},\"memory\":{},\"disks\":{},\"monitoring_status\":{}",
            GetTickCount64() - started, sample.processorSampledAtMs, status, effectiveMode.load(), ecState, access ? std::to_string(access->writeCount()) : "null",
            number(cpu.celsius), number(cpu.tctlCelsius), number(cpu.driverCelsius), number(cpu.usagePercent), number(cpu.clockMhz), number(cpu.powerWatts), quote(cpu.temperatureSource), quote(cpu.powerSource), number(cpu.fanRpm), number(cpu.fanPwm),
            number(gpu.celsius), number(gpu.usagePercent), number(gpu.clockMhz), number(gpu.powerWatts), number(gpu.fanRpm), number(gpu.fanPwm),
            number(sample.systemPowerWatts), quote(sample.systemPowerSource), sample.systemPowerStale ? "true" : "false",
            number(sample.systemConsumptionWatts),
            number(sample.apuPackagePowerWatts), quote(sample.apuPackagePowerSource), number(sample.socCelsius),
            !battery.onlineKnown ? "null" : battery.online ? "true" : "false", number(battery.percent), quote(flow), number(battery.watts), number(battery.volts), number(battery.milliamps), number(battery.celsius),
            battery.temperatureRaw?std::to_string(*battery.temperatureRaw):"null",quote(battery.temperatureSource),quote(battery.temperatureStatus),quote(battery.source),
            battery.dynamicStale,batteryAges(),quote(battery.powerStatus),
            devices(sample.memory),devices(sample.disks),quote(sample.processorStatus));
        if(lenovo)fields+=",\"lenovo_power_diagnostic\":"+lenovoPowerDiagnostic(sample);
        journal().write("sample",fields);
        if (pending&&!remaining) journal().write("burst_complete", "\"remaining\":0");
    } catch (const std::exception& error) { journal().write("sample_error", "\"error\":" + quote(error.what())); }
      catch (...) {}
}
void recordPowerAction(std::string_view action, std::string_view phase, int requested, std::string_view detail) noexcept {
    if (!loggingEnabled()) return;
    try {
        if (action == "performance" && phase == "requested") requestedPreset.store(requested);
        if (action == "performance" && phase == "confirmed") confirmedPreset.store(requested);
        journal().write("application_action", std::format("\"action\":{},\"phase\":{},\"requested\":{},\"detail\":{}",
            quote(action), quote(phase), requested, quote(detail)));
        if (phase == "requested" || phase == "confirmed" || phase == "failed" ||
            (action=="typed_ec_state"&&(phase=="observed"||phase=="read_failed"))) startBurst("application_action");
    } catch (...) {}
}
bool powerDiagnosticBurstActive() noexcept { return loggingEnabled() && samplesRemaining.load() != 0; }
ULONGLONG powerDiagnosticBurstGeneration() noexcept { return burstGeneration.load(); }
} // namespace mc::windows
