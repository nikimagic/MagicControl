#include "MagicControl/ResourceControl.h"
#include "ControlWindows.h"
#include "FactoryCurves.h"
#include "../../eluktronics/hydroc-16-g2/DiagnosticsWindows.h"
#include "../../../protocols/uniwill/hydroc16g2/LiquidCooling.h"
#include "../gm5hg0a/FactoryCurves.h"
#include "../../../protocols/uniwill/gm6ir0c/BatteryDecode.h"
#include "../../../protocols/uniwill/gm6ir0c/BatteryCurrentDiagnostics.h"
#include "MagicControl/SensorDecoders.h"
#include "MagicControl/BoardIdentity.h"
#include "MagicControl/DeviceProfile.h"
#include "MagicControl/EcAccess.h"
#include "MagicControl/Logging.h"
#include "MagicControl/ActionLog.h"
#include "MagicControl/DeviceOperation.h"
#include "MagicControl/FanCurve.h"
#include "MagicControl/NvidiaGpu.h"
#include "MagicControl/PortableSettings.h"
#include "MagicControl/Monitoring.h"
#include "IntelTelemetryWindows.h"
#include "PawnTelemetryWindows.h"
#include "PowerDiagnosticsWindows.h"
#include "UniwillControlOwnerWindows.h"
#include "BackendWindows.h"
#include "../../../platform/windows/uniwill-acpi/ReviewedDriverWindows.h"
#include "LightingOwnershipWindows.h"
#include "WindowsDevices.h"
#include <thread>
#include <condition_variable>
#include <map>
#include <cmath>
#include <windows.h>
#include <tlhelp32.h>
#include <algorithm>
#include <format>
#include <fstream>
#include <sstream>
#include <locale>
#include <functional>
#include <mutex>
#include <vector>
#include <deque>
#include <atomic>

namespace mc::windows {
namespace {
std::string activeOem(bool nativeProfile=false) {
    const HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(snapshot==INVALID_HANDLE_VALUE)throw std::runtime_error("Не удалось проверить службы OEM");
    PROCESSENTRY32W entry{sizeof(entry)};std::string found;
    if(!Process32FirstW(snapshot,&entry)){CloseHandle(snapshot);throw std::runtime_error("Не удалось прочитать список процессов OEM");}
    do {
        if(!_wcsicmp(entry.szExeFile,L"GCUService.exe")||!_wcsicmp(entry.szExeFile,L"AiStoneService.exe")||
            (nativeProfile&&!_wcsicmp(entry.szExeFile,L"GCUBridge.exe"))) {
            const char* name=!_wcsicmp(entry.szExeFile,L"GCUService.exe")?"GCUService.exe":!_wcsicmp(entry.szExeFile,L"GCUBridge.exe")?"GCUBridge.exe":"AiStoneService.exe";
            found=std::string(name)+" (PID "+std::to_string(entry.th32ProcessID)+")";break;}
    }while(Process32NextW(snapshot,&entry));
    const auto enumerationError=GetLastError();
    if(nativeProfile&&found.empty()&&enumerationError!=ERROR_NO_MORE_FILES){CloseHandle(snapshot);throw std::runtime_error("Не удалось завершить проверку процессов OEM native profile");}
    CloseHandle(snapshot);return found;
}
struct OwnershipChanged:std::runtime_error {using std::runtime_error::runtime_error;};
bool transientKeyboardRead(const std::exception& error){
    if(dynamic_cast<const WindowsEcNotReady*>(&error)||dynamic_cast<const EcObservationBusy*>(&error))return true;
    if(const auto* io=dynamic_cast<const EcIoError*>(&error))switch(io->nativeCode()){
        case ERROR_NOT_READY:case ERROR_DEVICE_NOT_CONNECTED:case ERROR_BUSY:case ERROR_SEM_TIMEOUT:return true;
        default:break;
    }
    return false;
}
struct Change {std::uint16_t address;std::uint8_t before,after,mask;};
// Roll back only our fields if they still contain our value. No blind old dump.
struct Journal {
    EcAccess& ec;std::vector<Change> changes;bool done{};
    std::function<void()> admission;
    void put(std::uint16_t a,int value,std::uint8_t mask=255) {
        if(admission)admission();
        const auto before=ec.readStable(a);
        const auto after=static_cast<std::uint8_t>((before&~mask)|(value&mask));
        if(before==after)return;
        changes.push_back({a,before,after,mask});
        if(mask==255)ec.writeVerified(a,after);else (void)ec.updateBitsVerified(a,mask,after);
    }
    std::string rollback(const std::function<void()>& beforeWrite={}) {
        if(done)return {};done=true;std::string errors;
        const auto failure=[&](const std::string& message){if(!errors.empty())errors+="; ";errors+=message;logMessage("gm6ir0c",message);};
        for(auto i=changes.rbegin();i!=changes.rend();++i)try{
            const auto actual=ec.readStable(i->address)&i->mask;
            if(actual==(i->before&i->mask))continue; // Already original, not lost ownership.
            if(actual!=(i->after&i->mask)){failure(std::format("Rollback skipped at0x{:03X}: external EC change",i->address));continue;}
            if(beforeWrite)beforeWrite();
            (void)ec.updateBitsVerified(i->address,i->mask,i->before);
        }catch(const std::exception& e){failure(std::format("Rollback failed at0x{:03X}: {}",i->address,e.what()));}
        return errors;
    }
    ~Journal(){if(!done)try{(void)rollback();}catch(...){logMessage("gm6ir0c","Rollback failure could not be reported");}}
};
constexpr std::array<int,3> economyPower{10,15,20};
void validatePower(const std::array<int,3>& values) {
    if(values==std::array<int,3>{})return; // Return only PL overrides to firmware.
    const auto id=deviceProfile(readBoardDescriptor()).id;const bool hydroc=hydrocProfile(id);
    const bool amd=gmAmdProfile(id);
    for(int i=0;i<3;++i)if(values[i]<(amd?5:gm6::powerMinimum[i])||values[i]>(hydroc?(i==2?510:255):amd?gmAmdPowerMaximum(id,i):gm6::powerMaximum[i]))
        throw std::runtime_error("PL1 10–140; PL2 10–165; PL4 10–250 Вт");
    if(values[0]>values[1]||values[1]>values[2])throw std::runtime_error("Нужно PL1 ≤ PL2 ≤ PL4");
}
void validate(const gm6::Curves& curves) {
    for(const auto& c:curves)for(int i=0;i<16;++i) {
        if(c[i].up<0||c[i].up>255||c[i].down<0||c[i].down>255||c[i].duty<0||c[i].duty>100)
            throw std::runtime_error("Температура 0–255, PWM 0–100%; 255 — служебная граница OEM");
        if(i&& (c[i].up<c[i-1].up||c[i].down<c[i-1].down||c[i].duty<c[i-1].duty))
            throw std::runtime_error("Пороги и PWM должны идти по возрастанию");
    }
}
std::optional<gm6::Curves> readRamCurves(EcAccess& ec){
    gm6::Curves curves{};bool valid=true;
    for(int ch=0;ch<2;++ch){
        const auto raw=ec.readBlock(gm6::table[ch],48);
        valid=valid&&raw[15]==255;
        for(int i=0;i<16;++i)valid=valid&&raw[32+i]<=200;
        for(int i=0;i<16;++i)curves[ch][i]={i?raw[i-1]:0,i<15?raw[17+i]:255,raw[32+i]/2};
    }
    if(valid)return curves;return {};
}
using CustomPreset=mc::CustomPreset;
constexpr std::array quickSettings{DeviceSetting::WindowsLocked,DeviceSetting::FnLocked,DeviceSetting::UsbCharging,DeviceSetting::AcRecovery};
// Desired commands, never a dump of arbitrary live EC values. A native profile
// owns its FAN profile; an explicit FAN command is a later overlay.
struct Configuration {
    int profile{-1},fan{-1}; // Legacy full profile; fan0 Default,1 Manual,2 Max,3 online.
    int stockCpu{-1}; // CPU-only Office/Gaming/Turbo, never a FAN replay.
    bool resetPower{},respective{},linked{};
    std::array<bool,2> controlled{};
    std::array<int,3> power{};
    gm6::Curves profileCurves{},fanCurves{};
    bool customCurvesSaved{},customPowerSaved{};
    CustomPreset custom;
};
constexpr auto configurationFile="gm6ir0c-configuration.txt";
Configuration loadConfiguration(){
    const auto path=settingsReadPath(portableSettingsPath(configurationFile));Configuration c;
    if(!std::filesystem::exists(path))return c;
    if(std::filesystem::file_size(path)>16384)throw std::runtime_error("GM6 configuration: oversized file");
    std::ifstream in(path);in.imbue(std::locale::classic());
    std::string header,id;int version{},reset{},split{},linked{},cpu{},gpu{};
    if(!(in>>header>>version>>id)||header!="MagicControlConfiguration"||(version!=1&&version!=2)||id!="mechrevo-gm6ir0c"||
       !(in>>c.profile>>c.fan>>reset>>split>>linked>>cpu>>gpu>>c.power[0]>>c.power[1]>>c.power[2])||
       c.profile< -1||c.profile>4||c.fan< -1||c.fan>3||reset<0||reset>1||split<0||split>1||
       linked<0||linked>1||cpu<0||cpu>1||gpu<0||gpu>1)
        throw std::runtime_error("GM6 configuration: invalid header/selection");
    c.resetPower=reset;c.respective=split;c.linked=linked;c.controlled={cpu!=0,gpu!=0};
    if(version==2&&(!(in>>c.stockCpu)||c.stockCpu< -1||c.stockCpu>2||(c.stockCpu>=0&&c.profile>=0)))
        throw std::runtime_error("GM6 configuration: invalid CPU-only selection");
    for(auto* curves:{&c.profileCurves,&c.fanCurves})for(auto& channel:*curves)for(auto& p:channel)
        if(!(in>>p.up>>p.down>>p.duty))throw std::runtime_error("GM6 configuration: incomplete curves");
    int customCurves{},customPower{},customSplit{};
    if(!(in>>customCurves>>customPower>>customSplit>>c.custom.power[0]>>c.custom.power[1]>>c.custom.power[2])||
       customCurves<0||customCurves>1||customPower<0||customPower>1||customSplit<0||customSplit>1)
        throw std::runtime_error("GM6 configuration: invalid Custom draft");
    c.customCurvesSaved=customCurves;c.customPowerSaved=customPower;c.custom.respective=customSplit;
    for(auto& channel:c.custom.curves)for(auto& p:channel)
        if(!(in>>p.up>>p.down>>p.duty))throw std::runtime_error("GM6 configuration: incomplete Custom draft");
    std::string extra;if(in>>extra||!in.eof())throw std::runtime_error("GM6 configuration: trailing data/read error");
    if(c.profile==3||c.profile==4){validatePower(c.power);validate(c.profileCurves);}
    if(c.fan==1||c.fan==3)validate(c.fanCurves);
    if(c.customCurvesSaved)validate(c.custom.curves);
    if(c.customPowerSaved)validatePower(c.custom.power);
    if(c.fan==3&&!c.controlled[0]&&!c.controlled[1])throw std::runtime_error("GM6 configuration: online channel absent");
    return c;
}
void saveConfiguration(const Configuration& c){
    std::ostringstream out;out.imbue(std::locale::classic());
    out<<"MagicControlConfiguration 2 mechrevo-gm6ir0c\n"<<c.profile<<' '<<c.fan<<' '<<c.resetPower<<' '
       <<c.respective<<' '<<c.linked<<' '<<c.controlled[0]<<' '<<c.controlled[1]<<' '
       <<c.power[0]<<' '<<c.power[1]<<' '<<c.power[2]<<'\n'<<c.stockCpu<<'\n';
    for(const auto* curves:{&c.profileCurves,&c.fanCurves})for(const auto& channel:*curves)for(const auto& p:channel)
        out<<p.up<<' '<<p.down<<' '<<p.duty<<'\n';
    out<<c.customCurvesSaved<<' '<<c.customPowerSaved<<' '<<c.custom.respective<<' '
       <<c.custom.power[0]<<' '<<c.custom.power[1]<<' '<<c.custom.power[2]<<'\n';
    for(const auto& channel:c.custom.curves)for(const auto& p:channel)out<<p.up<<' '<<p.down<<' '<<p.duty<<'\n';
    std::string error;if(!savePortableText(portableSettingsPath(configurationFile),out.str(),error))
        throw std::runtime_error("GM6: настройка применена, но не сохранена: "+error);
}
using CustomPresets=std::array<std::optional<CustomPreset>,3>;
void requireCustomPreset(int slot){
    if(!ramFanControlProfile(deviceProfile(readBoardDescriptor()).id)||slot<0||slot>=3)throw std::runtime_error("Custom presets: unsupported model/slot");
}
CustomPresets loadCustomPresets(){return loadCustomPresetLibrary(deviceProfile(readBoardDescriptor()).id).slots;}
std::optional<std::array<int,3>> powerDraft(std::string& error) {
    error.clear();
    try{
        const auto path=settingsReadPath(portableSettingsPath(std::string(gmPowerDraftFile(deviceProfile(readBoardDescriptor()).id))));std::ifstream input(path);
        if(!input){if(std::filesystem::exists(path))error="Не удалось открыть черновик CPU PL";return {};}
        if(std::filesystem::file_size(path)>4096)throw std::runtime_error("Размер черновика CPU PL недопустим");
        std::array<int,3> values{};if(!(input>>values[0]>>values[1]>>values[2]))throw std::runtime_error("Неверный формат черновика CPU PL");
        input>>std::ws;if(input.bad()||!input.eof())throw std::runtime_error("Лишние/непрочитанные данные черновика CPU PL");
        validatePower(values);return values;
    }catch(const std::exception& failure){error=failure.what();return {};}
}
void putTables(Journal& j,const gm6::Curves& curves,bool respective=true) {
    validate(curves);
    j.put(gm6::commit,0,4);
    // OEM FanControlRespective: 0x7C5.7, NOT X5's other split flag.
    j.put(gm6::respective,respective?128:0,128);
    for(int ch=0;ch<2;++ch)for(int i=0;i<16;++i) {
        const auto base=gm6::table[ch];
        // Logical point0 is the CSV graph anchor/initial duty, not an extra
        // visible EC marker. OEM raw Up starts with the NEXT transition.
        j.put(static_cast<std::uint16_t>(base+i),i<15?curves[ch][i+1].up:255);
        if(i<15)j.put(static_cast<std::uint16_t>(base+16+i+1),curves[ch][i].down); // preserve Down[0]
        j.put(static_cast<std::uint16_t>(base+32+i),curves[ch][i].duty*2);
    }
    j.put(0x741,1,1); // OEM Set_APExistToEC; explicit table application only
}
}
struct Gm6Controller::Impl {
    std::atomic<bool> lppPresenceOwned{};
    std::optional<std::uint8_t> gm5Project;
    std::mutex gate;std::unique_ptr<EcAccess> ec;std::string lastLog,lastCpuLog;bool readbackConfirmed{},fanResponseConfirmed{};
    std::string lastReadError;ULONGLONG lastTelemetryLogMs{},lastBurstGeneration{};unsigned burstDetailsRemaining{};
    struct OwnedField {std::uint8_t mask,before,expected;};
    using FanFields=std::map<std::uint16_t,OwnedField>;
    FanFields ownedFanFields;
    std::string ownedFanMode;
    std::uint64_t fanCommandGeneration{};
    std::map<std::uint16_t,OwnedField> onlineFields;
    gm6::Curves onlineCurves{},onlineTemplate{};
    std::array<std::vector<FanCurvePoint>,2> onlinePoints;
    std::array<bool,2> onlineControlled{};
    std::array<int,2> onlineLevels{},onlineLast{-1,-1};
    bool online{},onlineLinked{},needsRestore{},onlineProtection{},probeActive{};std::string onlineStatus;
    std::uint64_t onlineGeneration{};
    using Clock=std::chrono::steady_clock;
    struct NativeRequest {std::uint64_t receivedAt{},epoch{};Clock::time_point queued;std::function<void(Gm6NativeProfileResult)> complete;};
    std::mutex nativeQueueGate;
    std::atomic<std::uint64_t> nativeEpoch{};
    std::atomic<bool> nativeSubscription{},nativeArmPending{};
    std::atomic<std::uint64_t> nativeArmGeneration{};
    bool nativeArmed{};
    std::deque<NativeRequest> nativeRequests;
    Clock::time_point nextNativeCycle{};
    std::optional<gm6::Curves> nativeCustomCurves;
    bool nativeCustomRespective{};
    std::optional<std::array<int,3>> nativeCustomLimits;
    // Economy is a temporary software intent implemented by EC Custom, never
    // a fifth native profile. Do not capture its overrides into the user's Custom.
    bool transientEconomy{};
    bool automaticPower{};int stockCpuIntent{-1};std::uint64_t customPresetRevision{};
    Gm6Controller* owner{};
    bool configurationPending{},restoringConfiguration{},configurationWaitingForAc{};
    bool keyboardRestoreConfirmed{};
    unsigned configurationAttempts{};
    Clock::time_point configurationNext{},configurationDeadline{};
    std::string configurationReason;
    std::string nativeProfileStatus;
    bool hasNativeRequests(){std::lock_guard lock(nativeQueueGate);return !nativeRequests.empty();}
    bool keyboardRestorePending{},keyboardSuspended{};
    unsigned keyboardRestoreAttempts{};
    Clock::time_point keyboardRestoreDeadline{},keyboardRestoreNext{};
    std::string keyboardRestoreReason,keyboardRestoreStatus;
    std::condition_variable wake;mc::ResourceActor worker;
    explicit Impl(Gm6Controller* controller,bool):owner(controller),worker([this](std::stop_token token){watchOnline(token);}){}
    ~Impl(){worker.request_stop();wake.notify_all();if(worker.joinable())worker.join();
        std::lock_guard lock(gate);try{stopOnline();}catch(const std::exception& e){logMessage("gm6ir0c",std::string("Online exit restore not confirmed: ")+e.what());}}
    bool persistentModel()const{return deviceProfile(readBoardDescriptor()).id=="mechrevo-gm6ir0c";}
    // Caller owns the device gate and EC transaction. No fallback or write is
    // allowed if Turbo capability or the current CPU-mode bits are unknown.
    void requireGm5TurboBase(){
        if(!(ec->readStable(0x49F)&2))throw std::runtime_error("GM5: Turbo не подтверждён прошивкой; Custom не применён");
        const auto mode=ec->readStable(gm6::mode)&0xB0;
        if(mode!=0&&mode!=0x10&&mode!=0x80&&mode!=0xA0)throw std::runtime_error("GM5: неизвестная CPU-политика; Custom не применён");
    }
    void cancelConfigurationRestore(){if(!restoringConfiguration){configurationPending=false;configurationWaitingForAc=false;}}
    void remember(const std::function<void(Configuration&)>& change){
        if(restoringConfiguration||!persistentModel())return;
        cancelConfigurationRestore();
        auto desired=loadConfiguration();change(desired);saveConfiguration(desired);
        logMessage("configuration",std::format("GM6 saved profile={} fan={} auto_power={}",desired.profile,desired.fan,desired.resetPower));
    }
    void restoreConfigurationTick(std::unique_lock<std::mutex>& lock){
        if(configurationWaitingForAc&&!keyboardSuspended){
            SYSTEM_POWER_STATUS supply{};
            if(GetSystemPowerStatus(&supply)&&supply.ACLineStatus==1){
                configurationWaitingForAc=false;configurationPending=true;configurationAttempts=0;
                configurationNext={};configurationDeadline=Clock::now()+std::chrono::seconds(30);
            }
        }
        if(!configurationPending||keyboardSuspended||probeActive||Clock::now()<configurationNext)return;
        if(Clock::now()>=configurationDeadline){configurationPending=false;nativeProfileStatus="GM6: срок восстановления конфигурации истёк";
            logMessage("configuration",nativeProfileStatus);return;}
        if(!nativeArmed){
            try{const auto active=activeOem(true);if(!active.empty())throw std::runtime_error("Управление занято OEM: "+active);}
            catch(const std::exception& error){configurationPending=false;nativeProfileStatus=error.what();logMessage("configuration",nativeProfileStatus);}
            return; // Provider subscription may finish after the first maintenance tick.
        }
        auto operation=deviceOperations().tryBegin(DeviceOperationKind::Background);if(!operation)return;
        const auto epoch=nativeEpoch.load();bool mayHaveWritten=false;
        configurationPending=false;++configurationAttempts;restoringConfiguration=true;
        try{
            const auto desired=loadConfiguration();
            if(desired.customCurvesSaved){nativeCustomCurves=desired.custom.curves;nativeCustomRespective=desired.custom.respective;}
            if(desired.customPowerSaved)nativeCustomLimits=desired.custom.power;
            // Admit all identities/owners before leaving the lock. Actual setters
            // repeat their own checks and journal every write/readback.
            nativeAdmission();
            if((desired.profile>=3||desired.stockCpu>=0)&&!desired.resetPower&&desired.power!=std::array<int,3>{}){
                SYSTEM_POWER_STATUS supply{};
                if(!GetSystemPowerStatus(&supply)||supply.ACLineStatus!=1){
                    configurationWaitingForAc=true;restoringConfiguration=false;
                    nativeProfileStatus="GM6: сохранённый Custom/Economy ожидает питания от сети";
                    logMessage("configuration",nativeProfileStatus);return;
                }
            }
            const auto check=[&]{if(epoch!=nativeEpoch.load()||keyboardSuspended)throw std::runtime_error("GM6 restore superseded by power/close epoch");};
            if(desired.profile>=0){
                check();mayHaveWritten=true;Gm6NativeProfileResult result;
                const auto profile=desired.profile==4?gm6::NativeProfile::Custom:static_cast<gm6::NativeProfile>(desired.profile);
                applyNativeProfile(result,epoch,profile,
                    desired.profile>=3?std::optional(desired.resetPower?std::array<int,3>{}:desired.power):std::nullopt,false,desired.profile==4,
                    desired.profile>=3?std::optional(desired.profileCurves):std::nullopt,desired.respective);
            }
            // Public FAN setters are the same paths used by the buttons. Release
            // only the model mutex; keep the shared operation reservation.
            const auto invoke=[&](auto action){check();mayHaveWritten=true;lock.unlock();try{action();}catch(...){lock.lock();throw;}lock.lock();check();};
            if(desired.fan==0)invoke([&]{owner->setAutomatic();});
            else if(desired.fan==1)invoke([&]{owner->setCurves(desired.fanCurves);});
            else if(desired.fan==2)invoke([&]{owner->setBoost(true);});
            else if(desired.fan==3)invoke([&]{owner->setOnlineCurves(desired.fanCurves,desired.controlled,desired.linked);});
            if(desired.stockCpu>=0&&!desired.resetPower)invoke([&]{owner->setCpuPreset(desired.stockCpu);});
            if(desired.resetPower)invoke([&]{owner->resetCpuPower();});
            // Reapplying Custom with Auto must not replace its separate draft
            // with the temporary zero overrides used by this restore.
            if(desired.customCurvesSaved){nativeCustomCurves=desired.custom.curves;nativeCustomRespective=desired.custom.respective;}
            if(desired.customPowerSaved)nativeCustomLimits=desired.custom.power;
            // KEYs restore is independent of native APU/FAN configuration.
            logMessage("configuration",std::format("GM6 restored reason={} profile={} fan={} auto_power={} EC_readback=true physical_response=unmeasured",configurationReason,desired.profile,desired.fan,desired.resetPower));
        }catch(const std::exception& error){
            configurationPending=!mayHaveWritten&&transientKeyboardRead(error)&&configurationAttempts<5&&Clock::now()<configurationDeadline;
            configurationNext=Clock::now()+std::chrono::seconds(2);
            nativeProfileStatus="GM6: восстановление конфигурации не завершено: "+std::string(error.what());
            logMessage("configuration",std::format("GM6 restore failed reason={} attempt={} retry={} write_may_have_started={} error={}",configurationReason,configurationAttempts,configurationPending,mayHaveWritten,error.what()));
        }
        restoringConfiguration=false;
    }
    static MonitoringSnapshot temperatures(){
        NvidiaGpu gpu;MonitoringSnapshot sample;
        if(gmAmdProfile(deviceProfile(readBoardDescriptor()).id)){
            static thread_local PawnTelemetryReader cpu(true,false);cpu.readTemperatures(sample);
        }else{IntelTelemetryReader cpu;cpu.read(sample);}
        readNvidiaMonitoring(gpu,sample);return sample;
    }
    static void requireTemperatures(const MonitoringSnapshot& sample){
        if(!sample.processors[0].celsius||!sample.processors[1].celsius||
            GetTickCount64()-sample.processorSampledAtMs>5000)
            throw std::runtime_error("GM6IR0C: для Manual online нужны свежие CPU и NVIDIA температуры обоих каналов");
        for(const auto& p:sample.processors)if(!std::isfinite(*p.celsius)||*p.celsius<0||*p.celsius>125)
            throw std::runtime_error("GM6IR0C: недействительная температура; online отменён");
    }
    void captureOnline(){
        onlineFields.clear();
        const auto capture=[&](std::uint16_t a,std::uint8_t mask){const auto value=ec->readStable(a);onlineFields.emplace(a,OwnedField{mask,value,value});};
        for(auto [a,mask]:{std::pair{gm6::commit,4},std::pair{gm6::respective,128},std::pair{gm6::custom,128},std::pair{gm6::light,64},std::pair{gm6::mode,240},std::pair{std::uint16_t{0x741},1}})capture(a,static_cast<std::uint8_t>(mask));
        for(auto base:gm6::table)for(int i=0;i<48;++i)if(i!=16)capture(static_cast<std::uint16_t>(base+i),255);
    }
    void confirmOnline(const Journal& journal){
        // Every journal write has already passed readback. Store the exact
        // expected bits without a second read that could fail after commit.
        for(const auto& change:journal.changes)if(auto it=onlineFields.find(change.address);it!=onlineFields.end())
            it->second.expected=static_cast<std::uint8_t>((it->second.expected&~change.mask)|(change.after&change.mask));
    }
    void checkOnline(){
        for(const auto& [address,field]:onlineFields)if((ec->readStable(address)&field.mask)!=(field.expected&field.mask))
            throw OwnershipChanged("GM6IR0C: владение RAM/флагами не подтверждено; online остановлен");
    }
    FanFields fanFields(bool tables){
        FanFields fields;
        const auto capture=[&](std::uint16_t address,std::uint8_t mask){const auto value=ec->readStable(address);fields.emplace(address,OwnedField{mask,value,value});};
        for(auto [a,mask]:{std::pair{gm6::commit,4},std::pair{gm6::respective,128},std::pair{gm6::custom,128},std::pair{gm6::light,64},std::pair{gm6::mode,240},std::pair{std::uint16_t{0x741},1}})capture(a,static_cast<std::uint8_t>(mask));
        if(tables)for(auto base:gm6::table)for(int i=0;i<48;++i)if(i!=16)capture(static_cast<std::uint16_t>(base+i),255);
        return fields;
    }
    bool matchesFan(const FanFields& fields){
        for(const auto& [address,field]:fields)if((ec->readStable(address)&field.mask)!=(field.expected&field.mask))return false;
        return true;
    }
    void automatic(){
        guard(DeviceFeature::Fans,true);
        try{stopOnline();}catch(const OwnershipChanged&){online=false;onlineFields.clear();logMessage("gm6ir0c","Explicit Default after lost online ownership; old snapshot is not restored");}
        EcAccess::Transaction transaction(*ec);Journal j{*ec};j.put(gm6::commit,0,4);
        j.put(gm6::custom,0,128);j.put(gm6::light,0,64);j.put(gm6::mode,0,64);
        j.done=true;readbackConfirmed=true;needsRestore=false;onlineStatus="GM6IR0C: штатное управление EC";
        ownedFanFields.clear();ownedFanMode.clear();
        logMessage("gm6ir0c","Default: RAM/custom/override/boost disabled and verified; tables, mode and power overrides preserved");
    }
    void stopOnline(const std::function<void()>& admission={}){
        // Invalidate in-flight temperature reads before changing ownership,
        // including an unsuccessful stop or a new session from Default.
        ++onlineGeneration;
        if(onlineFields.empty()){online=false;return;}
        online=false;needsRestore=true;guard(DeviceFeature::Fans,true);EcAccess::Transaction transaction(*ec);
        checkOnline(); // A mismatch/read error preserves the snapshot and needsRestore.

        const bool oldRam=(onlineFields.at(gm6::commit).before&4)!=0;
        Journal journal{*ec};journal.admission=admission;
        try{
        journal.put(gm6::commit,0,4);
        // An inactive baseline can contain zeros. Leave our tables inactive;
        // never wipe tables back to zeros just to return firmware ownership.
        for(const auto& [address,field]:onlineFields)if(address!=gm6::commit&&(oldRam||address<0xF00))journal.put(address,field.before,field.mask);
        journal.put(gm6::commit,onlineFields.at(gm6::commit).before,4);journal.done=true;
        }catch(const std::exception& error){
            const auto rollback=journal.rollback(admission);
            throw std::runtime_error(std::string(error.what())+(rollback.empty()?"":"; online restore rollback: "+rollback));
        }
        onlineFields.clear();needsRestore=false;onlineStatus="GM6IR0C: Manual online завершён; исходное управление возвращено";
        logMessage("gm6ir0c",onlineStatus);
    }
    void driveOnline(const MonitoringSnapshot& sample){
        requireTemperatures(sample);guard(DeviceFeature::Fans,true);EcAccess::Transaction transaction(*ec);checkOnline();
        auto targets=onlineLast;bool changed=false;
        const bool protection=*sample.processors[0].celsius>=85||*sample.processors[1].celsius>=85;
        changed=protection!=onlineProtection;
        for(int ch=0;ch<2;++ch)if(onlineControlled[ch]){
            const int source=onlineLinked?0:ch;
            const double temperature=onlineLinked?std::max(*sample.processors[0].celsius,*sample.processors[1].celsius):*sample.processors[ch].celsius;
            onlineLevels[ch]=fanCurveLevel(onlinePoints[source],onlineLevels[ch],temperature);
            targets[ch]=protection?100:onlinePoints[source][onlineLevels[ch]].duty;
            changed|=targets[ch]!=onlineLast[ch];
        }
        if(protection)for(int ch=0;ch<2;++ch){targets[ch]=100;changed|=onlineLast[ch]!=100;}
        if(!changed)return;
        auto tables=onlineTemplate;
        for(int ch=0;ch<2;++ch)if(onlineControlled[ch]||protection)for(auto& point:tables[ch])point.duty=targets[ch];
        Journal journal{*ec};putTables(journal,tables,true);journal.put(gm6::custom,128,128);journal.put(gm6::light,64,64);journal.put(gm6::mode,0,240);journal.put(gm6::commit,4,4);journal.done=true;
        confirmOnline(journal);onlineLast=targets;onlineProtection=protection;readbackConfirmed=true;
        onlineStatus=protection?"GM6IR0C: Manual online · защита85°C →100% обоим":"GM6IR0C: Manual online · Intel DTS/NVIDIA · readback RAM подтверждён";
        logMessage("gm6ir0c",std::format("ONLINE CPU/GPU temperature={}/{} target={}/{} linked={}; native RAM duty, PL untouched",*sample.processors[0].celsius,*sample.processors[1].celsius,targets[0],targets[1],onlineLinked));
    }
    void watchOnline(std::stop_token token){
        std::unique_lock lock(gate);
        Clock::time_point nextMaintenance{};
        Clock::time_point nativeArmNext{},nativeArmDeadline{};
        std::uint64_t armGeneration=~std::uint64_t{};unsigned armAttempts{};
        while(!token.stop_requested()){
            wake.wait_for(lock,std::chrono::seconds(1),[&]{return token.stop_requested()||(nativeArmPending.load()&&!keyboardSuspended&&!probeActive)||hasNativeRequests();});if(token.stop_requested())break;
            if(nativeArmPending.load()&&!keyboardSuspended&&!probeActive){
                if(armGeneration!=nativeArmGeneration.load()){
                    armGeneration=nativeArmGeneration.load();armAttempts=0;nativeArmNext={};nativeArmDeadline=Clock::now()+std::chrono::seconds(30);}
                if(auto operation=Clock::now()>=nativeArmNext?deviceOperations().tryBegin():nullptr){
                    const auto epoch=nativeEpoch.load();
                    bool retryAllowed{};++armAttempts;
                    try{
                        nativeAdmission(false);EcAccess::Transaction transaction(*ec);Journal journal{*ec};
                        try{
                            journal.admission=[&]{if(epoch!=nativeEpoch.load())throw std::runtime_error("GM6 native hotkey: stale power/close epoch");};
                            journal.put(0x741,1,1);
                            if(!(ec->readStable(0x741)&1))throw std::runtime_error("GM6 native hotkey: APP presence readback mismatch");
                            journal.done=true;nativeArmed=true;
                            nativeProfileStatus="Native hotkey готов · APP741.0 readback; профиль не изменён";
                            logMessage("gm6ir0c","native_hotkey armed after successful AcpiTest_EventULong subscription: APP741.0=1 readback; profile/FAN/PL/GPU/battery unchanged");
                        }catch(const std::exception& error){
                            retryAllowed=journal.changes.empty()&&transientKeyboardRead(error);
                            const auto rollback=journal.rollback([&]{if(epoch!=nativeEpoch.load())throw std::runtime_error("GM6 native hotkey rollback: power/close epoch changed");nativeAdmission(false);});
                            throw std::runtime_error(std::string(error.what())+(rollback.empty()?"":"; rollback: "+rollback));
                        }
                    }catch(const std::exception& error){nativeArmed=false;
                        retryAllowed=retryAllowed||transientKeyboardRead(error); // guard/open is pre-write; post-write wrapper is not transient.
                        nativeProfileStatus=std::string("Native hotkey не готов: ")+error.what();logMessage("gm6ir0c",nativeProfileStatus);}
                    const bool retry=!nativeArmed&&retryAllowed&&armAttempts<5&&Clock::now()<nativeArmDeadline&&epoch==nativeEpoch.load();
                    nativeArmPending.store(retry);if(retry)nativeArmNext=Clock::now()+std::chrono::seconds(2);
                }else{
                    wake.wait_for(lock,std::chrono::milliseconds(100),[&]{return token.stop_requested();});
                    if(token.stop_requested())break;
                    if(Clock::now()<nextMaintenance)continue;
                }
            }
            if(hasNativeRequests()&&!nativeArmPending.load()){
                NativeRequest request;
                {std::lock_guard queueLock(nativeQueueGate);if(nativeRequests.empty())continue;request=nativeRequests.front();}
                const bool expired=Clock::now()-request.queued>std::chrono::seconds(10);
                auto operation=Clock::now()>=nextNativeCycle?deviceOperations().tryBegin():nullptr;
                if(!operation&&!expired&&request.epoch==nativeEpoch.load()&&!keyboardSuspended&&!probeActive){
                    wake.wait_for(lock,std::chrono::milliseconds(100),[&]{return token.stop_requested();});
                    if(token.stop_requested())break;
                    if(Clock::now()<nextMaintenance)continue;
                    // Busy command admission never starves the existing
                    // one-second online cooling/lifecycle maintenance.
                }else{
                {std::lock_guard queueLock(nativeQueueGate);if(nativeRequests.empty()||nativeRequests.front().epoch!=request.epoch||nativeRequests.front().receivedAt!=request.receivedAt)continue;nativeRequests.pop_front();}
                Gm6NativeProfileResult result;result.receivedAt=request.receivedAt;
                try{
                    if(request.epoch!=nativeEpoch.load())throw std::runtime_error("GM6 native profile: устаревшее нажатие отменено");
                    if(keyboardSuspended||probeActive)throw std::runtime_error("GM6 native profile: сон/диагностика; нажатие отменено");
                    if(!operation)throw std::runtime_error("GM6 native profile: очередь занята более10с; нажатие отменено");
                    applyNativeProfile(result,request.epoch);
                }catch(const std::exception& error){result.status=error.what();nativeProfileStatus=result.status;
                    logMessage("gm6ir0c","native_profile failed: "+result.status);}
                nextNativeCycle=Clock::now()+std::chrono::milliseconds(200);
                operation.reset();lock.unlock();
                try{if(request.complete)request.complete(std::move(result));}catch(...){logMessage("gm6ir0c","native_profile completion callback failed");}
                lock.lock();continue;
                }
            }
            if(Clock::now()<nextMaintenance){wake.wait_for(lock,std::chrono::milliseconds(100),[&]{return token.stop_requested();});continue;}
            nextMaintenance=Clock::now()+std::chrono::seconds(1);
            restoreKeyboardTick();
            restoreConfigurationTick(lock);
            if(!online||probeActive)continue;
            const auto generation=onlineGeneration;
            lock.unlock();MonitoringSnapshot sample;std::string failure;
            try{sample=temperatures();requireTemperatures(sample);}catch(const std::exception& e){failure=e.what();}
            // A new Default/Manual/online command may have completed while
            // telemetry ran outside gate. Its request cannot be driven or
            // rolled back because of an old session's sample/failure.
            lock.lock();if(!online||probeActive||generation!=onlineGeneration)continue;
            try{if(!failure.empty())throw std::runtime_error(failure);driveOnline(sample);}
            catch(const std::exception& e){const std::string error=e.what();
                try{stopOnline();onlineStatus=error+"; исходное управление возвращено";}
                catch(const std::exception& restore){needsRestore=!onlineFields.empty();onlineStatus=error+"; возврат: "+restore.what();online=false;}
                logMessage("gm6ir0c",onlineStatus);
            }
        }
    }
    gm6::NativeProfileFlags nativeFlags(){
        const auto sample=[&]{return gm6::NativeProfileFlags{ec->readStable(gm6::mode),ec->readStable(gm6::custom),ec->readStable(gm6::light),ec->readStable(gm6::commit)};};
        auto first=sample();for(int n=0;n<3;++n){const auto second=sample();if(first==second)return second;first=second;}
        throw std::runtime_error("GM6 native profile: согласованные режимные флаги не получены");
    }
    void nativeAdmission(bool power=true){
        guard(DeviceFeature::Fans,true);if(power)guard(DeviceFeature::PowerLimits,true);
        // Exact archived running service is GCUBridge.exe. Keep this stronger
        // admission local to native hotkey, not the unrelated battery helper.
        const auto owner=activeOem(true);if(!owner.empty())throw std::runtime_error("GM6 native profile занят OEM: "+owner);
    }
    gm6::Curves customCurves(std::string& source){
        if(nativeCustomCurves){source="session explicit Manual/online intent";return *nativeCustomCurves;}
        FanCurveProfiles profiles;std::string error;
        if(!loadFanCurveProfiles(portableSettingsPath(std::string(gmFanCurveFile(deviceProfile(readBoardDescriptor()).id))),profiles,error))
            throw std::runtime_error("Custom: сохранённая EcCurve недоступна: "+error);
        gm6::Curves curves{};
        for(int ch=0;ch<2;++ch)for(int i=0;i<16;++i){const auto& table=profiles.ecManualCurve;
            const int next=i<15?table[i+1].temperature:255;
            curves[ch][i]={table[i].temperature,next==255?255:std::max(0,next-3),table[i].duty};}
        validate(curves);source="portable fan-curves.csv EcCurve";return curves;
    }
    void applyNativeProfile(Gm6NativeProfileResult& result,std::uint64_t epoch,
            std::optional<gm6::NativeProfile> selected={},
            std::optional<std::array<int,3>> explicitLimits={},bool saveDraft=false,bool economy=false,
            std::optional<gm6::Curves> explicitCurves={},std::optional<bool> explicitRespective={}){
        audit::Scope trace("gm6.native_profile","\"source\":"+audit::quote(selected?"software_or_restore":"physical_mode_cycle")+",\"selected\":"+(selected?audit::quote(gm6::nativeProfileName(*selected)):"null")+",\"epoch\":"+std::to_string(epoch)+",\"economy\":"+(economy?"true":"false")+",\"requested_limits\":"+(explicitLimits?("["+std::to_string((*explicitLimits)[0])+","+std::to_string((*explicitLimits)[1])+","+std::to_string((*explicitLimits)[2])+"]"):"null"));
        cancelConfigurationRestore();
        // Shared physical-cycle and explicit software command. Caller owns gate
        // and DeviceOperation reservation. No battery/GPU/NVRAM/voltage setters.
        if(keyboardSuspended||probeActive)
            throw std::runtime_error("GM6 native profile: сон/диагностика; команда отменена");
        if(epoch!=nativeEpoch.load())throw std::runtime_error("GM6 native profile: устаревшая команда отменена");
        if(l16Profile(deviceProfile(readBoardDescriptor()).id)||hydrocProfile(deviceProfile(readBoardDescriptor()).id))
            throw std::runtime_error("L16: OEM mode-cycle factory curves are not recovered; use FANs and CPU-only SETs controls");
        nativeAdmission();
        if(!nativeArmed)throw std::runtime_error("GM6 native hotkey: APP presence init/readback ещё не подтверждены");
        EcAccess::Transaction transaction(*ec);
        if(!(ec->readStable(0x741)&1)){nativeArmed=false;throw std::runtime_error("GM6 native hotkey: APP presence изменён внешне; цикл отменён");}
        const auto before=nativeFlags();const auto current=gm6::nativeProfile(before);result.observed=current;
        if(!current)throw std::runtime_error("GM6 native profile: смешанные/неизвестные флаги; цикл не угадывается");
        const bool amd=gmAmdProfile(deviceProfile(readBoardDescriptor()).id);
        SYSTEM_POWER_STATUS supply{};
        const bool acConfirmed=GetSystemPowerStatus(&supply)&&supply.ACLineStatus==1;
        // Physical GM6 cycling must not get stuck at a Custom rejected by the
        // unchanged AC guard. Skip that step, never apply stored PL on DC.
        const bool skipDcCustom=!selected&&!amd&&!acConfirmed&&
            gm6::nextNativeProfile(*current)==gm6::NativeProfile::Custom;
        auto target=selected.value_or(gm6::nextNativeProfile(*current,amd||acConfirmed));
        if(skipDcCustom)logMessage("gm6ir0c","native_profile_cycle_skip profile=Custom reason=AC_not_confirmed next=Office saved_Custom_unchanged=true");
        if(!selected&&amd&&!(ec->readStable(0x49F)&2))
            target=*current==gm6::NativeProfile::Office?gm6::NativeProfile::Gaming:gm6::NativeProfile::Office;
        result.requested=target;
        const bool custom=target==gm6::NativeProfile::Custom;
        const auto activeId=deviceProfile(readBoardDescriptor()).id;
        const bool requestedTurbo=custom&&!economy&&customBaseSupported(activeId)&&selectedCustomBase(activeId)==1;
        const bool gm5TurboCustom=requestedTurbo&&gm5Profile(activeId);
        if(gm5TurboCustom)requireGm5TurboBase();
        std::string tableSource,powerSource="OEM native zero overrides";
        std::array<int,3> limits{};
        // Preserve the actual CPU request when leaving our explicit Custom.
        // Online uses its editable intent, never a constant-duty RAM snapshot.
        if(*current==gm6::NativeProfile::Custom&&nativeCustomCurves&&!transientEconomy&&!automaticPower&&stockCpuIntent<0&&!restoringConfiguration){
            std::array<int,3> old{};for(int i=0;i<3;++i)old[i]=ec->readStable(gm6::power[i]);
            if(amd&&(before.light&128))old[2]*=2;
            validatePower(old);nativeCustomLimits=old;nativeCustomRespective=(ec->readStable(gm6::respective)&128)!=0;
        }
        gm6::Curves curves{};
        if(custom){
            if(explicitCurves){curves=*explicitCurves;tableSource="saved Custom preset";validate(curves);}
            else curves=customCurves(tableSource);
            if(explicitLimits){limits=*explicitLimits;powerSource=economy?"software Economy PL":"software Custom PL";}
            else if(nativeCustomLimits){limits=*nativeCustomLimits;powerSource="session explicit Custom PL";}
            else{std::string error;const auto draft=powerDraft(error);if(!error.empty())throw std::runtime_error("Custom: "+error);
                if(draft){limits=*draft;powerSource=gmPowerDraftFile(deviceProfile(readBoardDescriptor()).id);}
                else{limits=cpuPresetValues(1);powerSource="OEM default Custom: fresh Gaming PL defaults";}}
            validatePower(limits);
            if(limits!=std::array<int,3>{}){
                SYSTEM_POWER_STATUS supply{};
                if(!GetSystemPowerStatus(&supply)||supply.ACLineStatus!=1)throw std::runtime_error("Custom PL: требуется подтверждённое питание от сети");
                if(!amd&&(before.light&128))throw std::runtime_error("Custom PL: масштаб PL4 Double не подтверждён");
            }
        }else{
            curves=amd?gm5::factoryCurves[static_cast<int>(target)]:gm6::factoryCurves[static_cast<int>(target)];
            tableSource=std::string(amd?"GM5 OEM5.52.50.0 DefaultFanTable_":"exact GM6 OEM DefaultFanTable_")+std::string(gm6::nativeProfileName(target));validate(curves);
        }
        auto encodedLimits=limits;
        if(amd&&(before.light&128)){
            if(limits[2]%2)throw std::runtime_error("AMD FPPT Double: Custom требует чётные Вт");
            encodedLimits[2]/=2;
        }
        logMessage("gm6ir0c",std::format("native_profile requested={} previous={} tables=[{}] PL=[{}] physical_response=unmeasured",
            gm6::nativeProfileName(target),gm6::nativeProfileName(*current),tableSource,powerSource));
        logMessage("gm6ir0c",std::format("native_profile source={} target={} shared_apply=true",
            economy?"software_economy":selected?(saveDraft?"software_custom":"software_preset"):"physical_cycle",gm6::nativeProfileName(target)));
        // Explicit native command supersedes the old host loop. A read failure
        // aborts; a known external ownership change is not blindly rolled back.
        if(epoch!=nativeEpoch.load())throw std::runtime_error("GM6 native profile: power/close epoch changed before apply");
        const auto nativeRestoreAdmission=[&]{
            if(epoch!=nativeEpoch.load())throw std::runtime_error("GM6 native online restore: power/close epoch changed");
            nativeAdmission();
        };
        try{stopOnline(nativeRestoreAdmission);}catch(const OwnershipChanged&){online=false;onlineFields.clear();needsRestore=false;}
        ++fanCommandGeneration;ownedFanFields.clear();ownedFanMode.clear();
        Journal journal{*ec};
        journal.admission=[&]{if(epoch!=nativeEpoch.load())throw std::runtime_error("GM6 native profile: power/close epoch changed during apply");};
        try{
            // All three archived exact OEM defaults use Respective=false.
            // Custom CSV EcCurve is common; explicit session intent retains
            // its freshly captured physical split flag.
            const bool respective=explicitRespective.value_or(custom&&nativeCustomCurves?nativeCustomRespective:false);
            putTables(journal,curves,respective);
            journal.put(gm6::custom,custom?128:0,128);
            journal.put(gm6::light,custom?64:0,64);
            journal.put(gm6::mode,requestedTurbo?0x10:gm6::nativeModeBits(target),0xF0); // Existing native profile transition; preserve low bits.
            for(int i=0;i<3;++i)journal.put(gm6::power[i],encodedLimits[i]);
            nativeAdmission(); // Recheck external owner before activation.
            journal.put(gm6::commit,4,4);
            const auto after=nativeFlags();result.observed=gm6::nativeProfile(after);
            if(result.observed!=target||(after.mode&64)||!(after.commit&4)||!(ec->readStable(0x741)&1))
                throw std::runtime_error("GM6 native profile: final mode/LED/commit/APP readback mismatch");
            for(int i=0;i<3;++i)if(ec->readStable(gm6::power[i])!=encodedLimits[i])throw std::runtime_error("GM6 native profile: final PL readback mismatch");
            if(((ec->readStable(gm6::respective)&128)!=0)!=respective)throw std::runtime_error("GM6 native profile: final split readback mismatch");
            for(int ch=0;ch<2;++ch){const auto raw=ec->readBlock(gm6::table[ch],48);
                for(int i=0;i<16;++i)if(raw[i]!=(i<15?curves[ch][i+1].up:255)||raw[32+i]!=curves[ch][i].duty*2||
                    (i<15&&raw[17+i]!=curves[ch][i].down))throw std::runtime_error("GM6 native profile: final RAM table readback mismatch");}
            if(saveDraft){std::string error;
                if(!savePortableText(portableSettingsPath(std::string(gmPowerDraftFile(deviceProfile(readBoardDescriptor()).id))),
                        std::format("{} {} {}\n",limits[0],limits[1],limits[2]),error))
                    throw std::runtime_error("Custom: черновик CPU PL не сохранён: "+error);
            }
            journal.done=true;readbackConfirmed=true;result.readbackConfirmed=true;needsRestore=false;
            if(custom&&!economy){nativeCustomCurves=curves;nativeCustomLimits=limits;nativeCustomRespective=respective;}
            transientEconomy=economy;
            if(custom&&!economy)++customPresetRevision;
            automaticPower=false;
            stockCpuIntent=-1;
            setCustomPresetActive(deviceProfile(readBoardDescriptor()).id,custom&&!economy);
            onlineStatus.clear();
            nativeProfileStatus=std::format("{} · EC readback подтверждён; индикация/исполнение физически не измерены",gm6::nativeProfileName(target));
            if(skipDcCustom)nativeProfileStatus+=" · Custom пропущен: требуется питание от сети; сохранённые настройки не изменены";
            if(economy)nativeProfileStatus="Economy · EC Custom/FAN Manual · PL10/15/20 readback; Custom draft preserved; physical response unmeasured";
            result.status=nativeProfileStatus;
            remember([&](Configuration& c){c.profile=economy?4:static_cast<int>(target);c.stockCpu=-1;c.fan=-1;c.resetPower=false;
                c.power=limits;c.profileCurves=curves;c.respective=respective;
                if(custom&&!economy){c.customCurvesSaved=c.customPowerSaved=true;c.custom={limits,curves,respective};}});
            logMessage("gm6ir0c",std::format("native_profile readback={} mode751=0x{:02X} custom726={} light727={} commit={} PL={}/{}/{} GPU_writes=0 battery_writes=0 physical_response=unmeasured",
                gm6::nativeProfileName(target),after.mode,(after.custom&128)!=0,(after.light&64)!=0,(after.commit&4)!=0,limits[0],limits[1],limits[2]));
            trace.finish("readback_confirmed","\"profile\":"+audit::quote(gm6::nativeProfileName(target))+",\"mode_751\":"+std::to_string(after.mode)+",\"custom_726\":"+std::to_string(after.custom)+",\"light_727\":"+std::to_string(after.light)+",\"commit\":"+std::to_string(after.commit)+",\"limits\":["+std::to_string(limits[0])+","+std::to_string(limits[1])+","+std::to_string(limits[2])+"]");
        }catch(const std::exception& error){
            const auto rollback=journal.rollback([&]{
                if(epoch!=nativeEpoch.load())throw std::runtime_error("native profile rollback postponed: power/close epoch changed");
                nativeAdmission();});readbackConfirmed=false;
            needsRestore=!rollback.empty();
            trace.fail(std::string(error.what())+"; rollback="+rollback);
            throw std::runtime_error(std::string(error.what())+(rollback.empty()?"; собственные изменения возвращены":"; rollback: "+rollback));
        }
    }
    void open() {
        if(ec)return;
        if(hydrocProfile(deviceProfile(readBoardDescriptor()).id)){verifyHydrocControlIdentity();std::string proof;verifyHydrocDriver(proof);logMessage("hydroc",proof);}
        if(gm5Profile(deviceProfile(readBoardDescriptor()).id)){
            std::string evidence;reviewed_uniwill::verifyGm5Driver(evidence);
            logMessage("gm5hg0a",evidence);
        }
        if(l16Profile(deviceProfile(readBoardDescriptor()).id)){
            std::string evidence;reviewed_uniwill::verifyL16Driver(evidence);logMessage("l16",evidence);
        }
        auto candidate=std::make_unique<EcAccess>(createWindowsAcpiDriverBackend());
        const auto& check=candidate->verifyBoard();
        logMessage("gm6ir0c",describeBoardCheck(check));
        if(!check.verified)throw std::runtime_error(check.error);
        ec=std::move(candidate);
    }
    void requireLiquidCoolingPolicyAdmission(){
        namespace lpp=hydroc16g2::lpp;
        if(!(ec->readStable(lpp::capability)&lpp::capabilityMask))throw std::runtime_error("LPP: native LC capability absent");
        if(ec->readStable(lpp::presence)&lpp::presenceMask)throw std::runtime_error("LPP: native LC presence already set outside this session; ownership refused");
        if(online||needsRestore||probeActive||(ec->readStable(gm6::commit)&4)||(ec->readStable(gm6::custom)&128)||
            (ec->readStable(gm6::light)&64)||(ec->readStable(gm6::mode)&64))
            throw std::runtime_error("LPP native Auto requires existing FAN Default; Manual/online/Max are preserved, not reset");
        for(const auto address:gm6::power)if(ec->readStable(address))throw std::runtime_error("LPP native Auto requires existing CPU Auto; manual PL overrides are preserved");
    }
    void guard(DeviceFeature feature,bool writing,bool revokeIdentity=true) {
        open();
        const auto& p=deviceProfile(readBoardDescriptor());
        if(hydrocProfile(p.id)){
            verifyHydrocControlIdentity();
            if(ec->readStable(0x7C3)!=13)throw std::runtime_error("HYDROC: platform13 changed; no control write");
            if(writing){std::string proof;verifyHydrocDriver(proof);const auto owner=uniwillControlOwner();if(!owner.empty())throw std::runtime_error("HYDROC owned by OEM: "+owner);
                if((feature==DeviceFeature::Fans||feature==DeviceFeature::PowerLimits)&&(ec->readStable(0x7C8)&0x20)&&(ec->readStable(0x7C7)&2))throw std::runtime_error("HYDROC: connected liquid cooling owns FAN/PL policy");}
        }
        if(l16Profile(p.id)){
            reviewed_uniwill::verifyL16EcIdentity([&](auto a){return ec->readStable(a);});
            if(writing){std::string evidence;reviewed_uniwill::verifyL16Driver(evidence);}
            else reviewed_uniwill::verifyL16Identity();
        }
        if(!ramFanControlProfile(p.id)||!gmRamProject(p.id,ec->readStable(gm6::project))||!(ec->readStable(gm6::capability)&64)) {
            // Wake admission still rejects every writer. A transient read
            // during the explicit keyboard power phase must not permanently
            // poison the shared EC verification for unrelated FANs/APU.
            if(revokeIdentity&&!keyboardSuspended&&!keyboardRestorePending)ec->invalidateBoardVerification();
            throw std::runtime_error("GM6IR0C: идентификация или RamFan1.5 не совпадает");
        }
        if(gm5Profile(p.id)){
            const auto project=ec->readStable(gm6::project);
            if(gm5Project&&*gm5Project!=project)throw std::runtime_error("GM5 ProjectID changed; restart required");
            gm5Project=project;
        }
        ec->ensureCapability(feature,p.protocolId);
        if(writing){const auto blocker=activeOem();if(!blocker.empty())throw std::runtime_error("Управление занято OEM: "+blocker+". Остановите эту службу перед применением.");}
    }
    void guardKeyboardRestore(bool writing){
        open();
        if(l16Profile(deviceProfile(readBoardDescriptor()).id)){
            if(writing){std::string evidence;reviewed_uniwill::verifyL16Driver(evidence);}
            else reviewed_uniwill::verifyL16Identity();
            reviewed_uniwill::verifyL16EcIdentity([&](auto a){return ec->readStable(a);});
        }
        if(!ramFanControlProfile(deviceProfile(readBoardDescriptor()).id)||
            !gmRamProject(deviceProfile(readBoardDescriptor()).id,ec->readStable(gm6::project))||!(ec->readStable(gm6::capability)&64))
            throw std::runtime_error("GM6 keyboard restore: текущая exact identity/RAM1.5 не подтверждены");
        ec->ensureCapability(DeviceFeature::Backlight,deviceProfile(readBoardDescriptor()).protocolId);
        if(writing)if(const auto owner=uniwillControlOwner();!owner.empty())
            throw std::runtime_error("GM6 keyboard restore: управление занято "+owner);
    }
    static void knownKeyboard(const KeyboardBacklightState& actual){
        if(!actual.supported||actual.model!=SingleZoneModel::Gm6)
            throw std::runtime_error("GM6 keyboard restore: RGB capability/model не подтверждены");
        if(((actual.keyboard>>5)&7)>4||actual.breathing>4)
            throw std::runtime_error("GM6 keyboard restore: неизвестное состояние яркости/пульсации");
        for(const auto raw:actual.color)if(raw>50)
            throw std::runtime_error("GM6 keyboard restore: неизвестные RGB уровни EC");
        if((actual.effect&0x80)&&actual.breathing)
            throw std::runtime_error("GM6 keyboard restore: противоречивые rainbow/pulse flags");
    }
    // Called only by the existing worker with gate held. No idle enforcement:
    // one explicit lifecycle request, bounded retries before any possible write.
    void restoreKeyboardTick(){
        if(!keyboardRestorePending||keyboardSuspended||probeActive||Clock::now()<keyboardRestoreNext)return;
        auto operation=deviceOperations().tryBegin(DeviceOperationKind::Background);
        if(!operation)return;
        if(keyboardRestoreAttempts&&Clock::now()>=keyboardRestoreDeadline){
            // End a previously deferred resume as unknown, rather than leave
            // an active cycle indefinitely paused without a terminal outcome.
            KeyboardLightingPause expired("GM6 keyboard restore deadline expired");
            keyboardRestorePending=false;keyboardRestoreStatus="GM6 keyboard restore: срок ожидания истёк; примените подсветку явно";
            logMessage("backlight_restore",keyboardRestoreStatus);return;
        }
        if(!keyboardRestoreAttempts)keyboardRestoreDeadline=Clock::now()+std::chrono::seconds(30);
        ++keyboardRestoreAttempts;keyboardRestorePending=false;
        bool writeMayHaveStarted{};
        std::optional<KeyboardLightingPause> pause;
        try{
            pause.emplace("GM6 static keyboard restore: "+keyboardRestoreReason);
            // Ownership is checked BEFORE even a transient open failure, so a
            // queued resume never waits for OEM to disappear and then takes over.
            ensureLightingWritable();
            guardKeyboardRestore(false);EcAccess::Transaction transaction(*ec);
            auto actual=readKeyboardBacklight(*ec);knownKeyboard(actual);
            DevicePreferences saved;std::string error;
            const auto path=portableSettingsPath(std::string(gmSettings(deviceProfile(readBoardDescriptor()).id)));
            const bool exists=std::filesystem::exists(settingsReadPath(path));
            if(exists&&!loadDevicePreferences(path,saved,error))
                throw std::runtime_error("GM6 keyboard restore: настройки не прочитаны: "+error);
            KeyboardBacklightChange change;
            if(const auto level=saved[DeviceSetting::KeyboardLevel]){
                const int current=(actual.keyboard>>5)&7;
                // A retained selected level can be temporarily blanked by
                // native idle. Do not wake it merely because bit1 is set.
                if(*level==0||*level!=current){
                    change.level=*level;
                    if(*level>0)change.powerDisabled=(actual.keyboard&2)!=0;
                }
            }
            const auto effect=saved[DeviceSetting::KeyboardEffect];
            const auto pulse=saved[DeviceSetting::PulseColor];
            const auto red=saved[DeviceSetting::Red],green=saved[DeviceSetting::Green],blue=saved[DeviceSetting::Blue];
            const unsigned rgbCount=unsigned(red.has_value())+unsigned(green.has_value())+unsigned(blue.has_value());
            if(rgbCount&&rgbCount!=3)throw std::runtime_error("GM6 keyboard restore: сохранена неполная RGB тройка");
            // Saved RGB is already raw0..50. Never run the UI converter here.
            std::optional<KeyboardColor> color;
            if(rgbCount==3)color=KeyboardColor{static_cast<std::uint8_t>(*red),static_cast<std::uint8_t>(*green),static_cast<std::uint8_t>(*blue)};
            if(effect){
                if(*effect<0||*effect>2)throw std::runtime_error("GM6 keyboard restore: сохраняются только статические native effects0..2");
                const int current=(actual.effect&0x80)?2:actual.breathing?1:0;
                if(*effect==0){
                    // Without an explicit saved raw colour, changing an effect
                    // would invent a target (notably rainbow's zero RGB).
                    if(current!=0&&!color)throw std::runtime_error("GM6 keyboard restore: нет явного RGB для режима Моно");
                    if(current!=0)change.effect=0;
                    if(color&&(current!=0||*color!=actual.color))change.color=*color;
                }else if(*effect==1){
                    if(!pulse)throw std::runtime_error("GM6 keyboard restore: нет сохранённого пресета пульсации");
                    if(current!=1)change.effect=1;
                    if(actual.breathing!=*pulse+1)change.pulse=*pulse+1;
                }else if(current!=2||actual.color!=KeyboardColor{}||actual.breathing)change.effect=2;
            }else{
                // Partial legacy preferences may restore just colour/pulse,
                // but only if the current mode already makes that field meaningful.
                if(color){if(actual.breathing||(actual.effect&0x80))throw std::runtime_error("GM6 keyboard restore: RGB без сохранённого Mono effect неоднозначен");if(*color!=actual.color)change.color=*color;}
                if(pulse){if(!actual.breathing||(actual.effect&0x80))throw std::runtime_error("GM6 keyboard restore: pulse без сохранённого эффекта неоднозначен");if(actual.breathing!=*pulse+1)change.pulse=*pulse+1;}
            }
            const bool changed=change.level||change.effect||change.pulse||change.color;
            if(changed){
                guardKeyboardRestore(true);
                if(const auto owner=uniwillControlOwner();!owner.empty())throw std::runtime_error("GM6 keyboard restore: появился OEM "+owner);
                // From this point a strobe/write might have been sent. Any
                // failure is terminal, even if the common core performed rollback.
                writeMayHaveStarted=true;actual=applyKeyboardBacklight(*ec,change);
            }
            guardKeyboardRestore(false);actual=readKeyboardBacklight(*ec);knownKeyboard(actual);
            ensureLightingWritable();
            pause->complete(actual);
            keyboardRestoreConfirmed=true;
            keyboardRestoreStatus=changed?"GM6: сохранённая статическая подсветка подтверждена чтением EC":"GM6: подсветка прочитана; записей восстановления не требовалось";
            logMessage("backlight_restore",std::format("reason={} attempt={} changed={} explicit_file={} no_cycle_autoplay=true fan_writes=0 apu_writes=0; {}",keyboardRestoreReason,keyboardRestoreAttempts,changed,exists,keyboardRestoreStatus));
        }catch(const std::exception& e){
            keyboardRestorePending=!writeMayHaveStarted&&transientKeyboardRead(e)&&keyboardRestoreAttempts<5&&Clock::now()<keyboardRestoreDeadline;
            if(keyboardRestorePending&&pause)pause->defer();
            keyboardRestoreNext=Clock::now()+std::chrono::seconds(1);
            keyboardRestoreStatus=std::string("GM6 keyboard restore: ")+e.what()+(keyboardRestorePending?"; повтор только pre-write чтения":"; автоматические повторы остановлены");
            logMessage("backlight_restore",std::format("reason={} attempt={} retry={} write_may_have_started={}; {}",keyboardRestoreReason,keyboardRestoreAttempts,keyboardRestorePending,writeMayHaveStarted,keyboardRestoreStatus));
        }
    }
    std::array<int,3> cpuPresetValues(int preset) {
        if(preset<0||preset>=static_cast<int>(gm6::powerDefaults.size()))throw std::invalid_argument("Неизвестный CPU PL-пресет GM6IR0C");
        std::array<int,3> values{};
        for(int i=0;i<3;++i)values[i]=ec->readStable(gm6::powerDefaults[preset][i]);
        if(gmPowerDoubleCodec(deviceProfile(readBoardDescriptor()).id)&&(ec->readStable(gm6::light)&128))values[2]*=2;
        if(values==std::array<int,3>{})throw std::runtime_error("Прошивка не сообщила PL-параметры этого CPU-пресета");
        validatePower(values);return values;
    }
    // Caller owns gate, exact PowerLimits admission and one EC transaction.
    // Normal CPU presets write only783/784/785. GM5 Custom also owns the B0
    // CPU bits of751; it never writes Boost, RAM or FAN gates.
    void applyCpuPowerLocked(const std::array<int,3>& values,bool saveDraft,bool gm5CustomTurbo=false) {
        validatePower(values);const bool reset=values==std::array<int,3>{};
        SYSTEM_POWER_STATUS supply{};
        if(!reset&&(!GetSystemPowerStatus(&supply)||supply.ACLineStatus!=1))
            throw std::runtime_error("Intel PL-пресеты и ручные лимиты доступны только от сети");
        const bool doubled=(ec->readStable(gm6::light)&128)!=0;
        const bool amd=gmAmdProfile(deviceProfile(readBoardDescriptor()).id);
        if(!reset&&doubled&&!gmPowerDoubleCodec(deviceProfile(readBoardDescriptor()).id))
            throw std::runtime_error("PL4 Double: масштаб этой ревизии ещё не подтверждён; используйте штатные лимиты");
        auto encoded=values;
        if(gmPowerDoubleCodec(deviceProfile(readBoardDescriptor()).id)&&doubled){
            if(values[2]%2)throw std::runtime_error("PL4/FPPT Double: укажите чётное значение мощности");
            encoded[2]/=2;
        }
        if(hydrocProfile(deviceProfile(readBoardDescriptor()).id)&&!reset){const auto ceiling=cpuPresetValues(2);for(int i=0;i<3;++i)if(values[i]>ceiling[i])throw std::runtime_error("HYDROC: request exceeds own live Turbo PL ceiling");}
        const auto base=gm5CustomTurbo?selectedCustomBase(deviceProfile(readBoardDescriptor()).id):0;
        if(gm5CustomTurbo){
            if(base)requireGm5TurboBase();
            if(online)checkOnline();
            if(!ownedFanFields.empty()&&!matchesFan(ownedFanFields))throw std::runtime_error("GM5: владение вентиляторами изменилось; Custom не применён");
        }
        Journal journal{*ec};
        try{
            if(gm5CustomTurbo)journal.put(gm6::mode,base?0x10:0,0xB0); // Preserve Fan Boost and all unrelated bits.
            for(int i=0;i<3;++i)journal.put(gm6::power[i],encoded[i]);
            // Confirm the complete triple, including unchanged fields, before save.
            for(int i=0;i<3;++i)if(ec->readStable(gm6::power[i])!=encoded[i])
                throw std::runtime_error("EC не подтвердил всю тройку CPU PL");
            if(gm5CustomTurbo&&(ec->readStable(gm6::mode)&0xB0)!=(base?0x10:0))throw std::runtime_error("GM5: база Custom не подтверждена");
            if(saveDraft){std::string error;
                const auto text=std::format("{} {} {}\n",values[0],values[1],values[2]);
                if(!savePortableText(portableSettingsPath(std::string(gmPowerDraftFile(deviceProfile(readBoardDescriptor()).id))),text,error))
                    throw std::runtime_error("Черновик CPU PL не сохранён: "+error);
            }
        }catch(const std::exception& failure){
            const std::string reason=failure.what();
            // A new OEM owner or changed identity must also block rollback.
            const auto rollback=journal.rollback([this]{guard(DeviceFeature::PowerLimits,true);});
            throw std::runtime_error(reason+(rollback.empty()?"; собственные изменения PL возвращены/подтверждены":"; возврат PL НЕ ПОДТВЕРЖДЁН: "+rollback));
        }
        journal.done=true;readbackConfirmed=true;
        if(gm5CustomTurbo){
            // CPU profile bits share 751 with FAN ownership. Retarget both the
            // expected and original CPU bits so an online-fan exit cannot undo
            // the user's new power base or falsely report lost FAN ownership.
            const auto update=[base](FanFields& fields){if(auto it=fields.find(gm6::mode);it!=fields.end()){
                const auto mask=static_cast<std::uint8_t>(it->second.mask&0xB0);
                it->second.before=static_cast<std::uint8_t>((it->second.before&~mask)|((base?0x10:0)&mask));
                it->second.expected=static_cast<std::uint8_t>((it->second.expected&~mask)|((base?0x10:0)&mask));}};
            update(onlineFields);update(ownedFanFields);
        }
        logMessage("gm6ir0c",std::format("CPU-only EC override readback PL1={} PL2={} PL4={}; saved_draft={}; fan_table_writes=0 turbo_base={}; CPU enforcement not measured",values[0],values[1],values[2],saveDraft,gm5CustomTurbo));
    }
    std::optional<int> rpm(int channel) {
        // Exact active OEM FanInfo: GPU low=0x46B. X5 low=0x46D is incompatible.
        const auto hi=static_cast<std::uint16_t>(channel?0x46C:0x464);
        const auto lo=static_cast<std::uint16_t>(channel?0x46B:0x465);
        std::vector<int> positive;int zeros{};
        for(int n=0;n<3;++n){const auto h=ec->read(hi);const auto l=ec->read(lo);
            if(h==ec->read(hi)){const int value=(h<<8)|l;if(value==0)++zeros;else if(value<=20000)positive.push_back(value);}}
        if(positive.size()>=2){std::sort(positive.begin(),positive.end());return positive[positive.size()/2];}
        if(zeros>=2&&positive.empty())return 0;
        return {};
    }
};
EcBatteryReading readGm6EcBatteryOnly(){
    if(wcsstr(GetCommandLineW(),L"--no-ec"))return {};
    const auto board=readBoardDescriptor();
    if(!gmRamFanProfile(deviceProfile(board).id))return {};
    const auto profileId=deviceProfile(board).id;
    const std::string batteryModel(gmModelName(profileId));
    const auto label=[&](EcBatteryReading value){
        if(gmAmdProfile(profileId)){
            value.profileId=profileId;value.battery.deviceId="EC/"+batteryModel+"/BAT0";
            value.battery.source="EC · "+batteryModel+" · shared XIF/XST";value.restartRaw.reset();
            const auto replace=[&](const std::string& from,const std::string& to){
                if(const auto at=value.battery.powerStatus.find(from);at!=std::string::npos)value.battery.powerStatus.replace(at,from.size(),to);
            };
            replace("lower7D0=","DBD1_7D0=");replace("lower_role=unverified","lower_role=not_charge_restart");
            for(auto& reg:value.registers)if(reg.address==0x7D0)reg.name="DBD1_raw_not_charge_restart";
        }
        return value;
    };
    auto result=gm6::emptyBatteryReading(batteryModel);
    if(deviceOperations().passiveEcReadsPaused()){result.status="Опрос EC временно приостановлен";return label(result);}
    // Monitoring owns one read-only handle/cache, never a controller, restore
    // worker or setter. FANs and SETs reuse the same timestamped sample.
    static std::mutex gate;static std::unique_ptr<IEcBackend> reader;
    static EcBatteryReading last;static std::uint64_t lastAt{};
    static gm6::CurrentDiagnosticSchedule currentDiagnostics;
    std::lock_guard lock(gate);const auto now=GetTickCount64();
    if(last.coherent&&now>=lastAt&&now-lastAt<800)return label(last);
    try{
        {
            if(deviceOperations().passiveEcReadsPaused())throw EcObservationBusy("GM6 battery telemetry: passive EC reads paused");
            if(!reader){
                if(gm5Profile(profileId)){std::string evidence;reviewed_uniwill::verifyGm5Driver(evidence);}
                if(l16Profile(profileId)){std::string evidence;reviewed_uniwill::verifyL16Driver(evidence);}
                reader=createWindowsAcpiDiagnosticBackend();
            }
            struct Transaction{IEcBackend& io;explicit Transaction(IEcBackend& x):io(x){io.lockTransaction();}~Transaction(){io.unlockTransaction();}} transaction{*reader};
            const auto byte=[&](std::uint16_t address){
                if(deviceOperations().passiveEcReadsPaused())throw EcObservationBusy("GM6 battery telemetry: passive EC reads paused");
                return reader->read(address);
            };
            const auto stableByte=[&](std::uint16_t address){
                auto previous=byte(address);for(int n=0;n<3;++n){const auto value=byte(address);if(previous==value)return value;previous=value;}
                throw UnstableRead(address,"GM6 battery telemetry: unstable identity");
            };
            const auto identity=[&]{
                if(l16Profile(profileId)){
                    reviewed_uniwill::verifyL16Identity();reviewed_uniwill::verifyL16EcIdentity(stableByte);
                }
                if(!gmRamFanProfile(deviceProfile(readBoardDescriptor()).id)||!gmRamProject(profileId,stableByte(0x740))||!(stableByte(0x78E)&0x40))
                    throw std::runtime_error("GM6 battery telemetry: exact identity/RAM1.5 not confirmed");
            };
            identity();
            const auto sample=[&]{std::array<std::uint8_t,gm6::batteryCoreAddresses.size()> raw{};
                for(std::size_t n=0;n<raw.size();++n)raw[n]=byte(gm6::batteryCoreAddresses[n]);return raw;};
            decltype(sample()) raw{};bool stable{};
            for(int attempt=0;attempt<3&&!stable;++attempt){const auto first=sample();raw=sample();stable=first==raw;}
            if(!stable){result.status="Согласованный снимок батареи "+batteryModel+" не получен";return label(result);}
            result=label(gm6::decodeBatteryCore(raw,batteryModel));
            // Optional evidence after the ordinary stable core. Never include fast
            // internal samples in the whole-core equality test or infer charge flow.
            // Exact GM6 only: no corresponding firmware trace for GM5/L16/HYDROC.
            if(profileId=="mechrevo-gm6ir0c"&&batteryLoggingEnabled()){
                const auto coreAt=GetTickCount64();
                if(const auto plan=currentDiagnostics.take(result,coreAt)){
                    EcBatteryDiagnosticSample diagnostic;
                    diagnostic.kind="gm6_current_calculation";diagnostic.sequence=plan->first;
                    diagnostic.reason=plan->second;diagnostic.coreAtMs=coreAt;
                    diagnostic.startedAtMs=GetTickCount64();
                    for(const auto& definition:gm6::currentDiagnosticFields){
                        const auto& def=definition.field;EcBatteryRegisterReading field;
                        field.address=def.address;field.width=def.width;field.bigEndian=def.bigEndian;
                        field.name=def.name;field.confirmed=def.confirmed;field.sampledTelemetry=true;
                        field.signedValue=definition.signedValue;
                        try{
                            const auto value=[&]{
                                const auto first=byte(def.address);
                                if(def.width==1)return std::uint16_t(first);
                                const auto second=byte(def.address+1);
                                return std::uint16_t(def.bigEndian?(unsigned(first)<<8)|second:first|(unsigned(second)<<8));
                            };
                            for(int attempt=0;attempt<3&&!field.value;++attempt){
                                const auto first=value(),second=value();if(first==second)field.value=second;
                            }
                            field.status=field.value?"ok":"unstable_per_field";
                        }catch(const std::exception& error){field.status=error.what();}
                        diagnostic.fields.push_back(std::move(field));
                    }
                    diagnostic.finishedAtMs=GetTickCount64();result.diagnostics=std::move(diagnostic);
                }
            }
            if(gmAmdProfile(profileId)){
                // Supplied GM5 MBB5.52.50.0 Define.ECSpec.ecBt1Temperature
                // names1186=4A2. The word is SBS tenths kelvin (dump0C1C).
                // Read only after exact driver/board admission. A failed
                // temperature read must not hide XIF/XST voltage/capacities.
                auto& battery=result.battery;
                battery.temperatureSource="EC · "+batteryModel+" · 0x4A2 · OEM ecBt1Temperature";
                battery.temperatureStatus="Батарея не готова";
                if(result.readyRaw&&*result.readyRaw!=255&&(*result.readyRaw&6)==6){
                    EcBatteryRegisterReading field;field.address=0x4A2;field.width=2;
                    field.name="ecBt1Temperature_tenths_kelvin";field.confirmed=true;field.sampledTelemetry=true;
                    try{
                        const auto word=[&]{const auto lo=byte(0x4A2);return std::uint16_t(lo|(unsigned(byte(0x4A3))<<8));};
                        std::optional<std::uint16_t> temperature;
                        for(int attempt=0;attempt<3&&!temperature;++attempt){const auto first=word();const auto second=word();if(first==second)temperature=second;}
                        if(!temperature)throw UnstableRead(0x4A2,"GM5 battery temperature: unstable word");
                        field.value=*temperature;field.status="ok";battery.temperatureRaw=*temperature;
                        battery.celsius=batteryTemperatureCelsius(*temperature);
                        battery.temperatureStatus=battery.celsius?"ok":"Недопустимая температура EC";
                    }catch(const std::exception& error){field.status=error.what();battery.temperatureStatus=error.what();}
                    result.registers.push_back(std::move(field));
                }
            }
            identity(); // Publish only after the same exact board is confirmed.
        }
        stampBatteryReading(result.battery);
        if(result.coherent){last=result;lastAt=now;}
    }catch(const std::exception& error){reader.reset();result=gm6::emptyBatteryReading();result.status=error.what();}
    return label(result);
}
void readGm6BatteryTelemetry(BatteryReading& result,bool singlePhysicalBattery){
    if(!singlePhysicalBattery||result.present==false||result.deviceId.empty()||result.deviceId=="Windows/PowerStatus/aggregate")return;
    const auto ec=readGm6EcBatteryOnly();if(!ec.supported)return;
    const std::string model(gmModelName(ec.profileId));
    const auto source=model+" EC";
    if(!ec.coherent){
        // All admitted GM-family decoders own health provenance. A missed/paused
        // sample must not replace it with the OS/BIF design-substituted100%.
        const auto original=result.healthPercent;result.healthPercent.reset();result.healthReadUnavailable=true;
        result.powerStatus+="; "+source+" health unavailable; Windows_original_health_percent="+(original?std::format("{:.6f}",*original):"unavailable")+"; "+ec.status;
        return;
    }
    const auto oldHealth=result.healthPercent;const auto oldCycles=result.cycles;
    if(!result.cycles&&ec.battery.cycles)result.cycles=ec.battery.cycles;
    if(!result.celsius&&ec.battery.celsius){
        result.celsius=ec.battery.celsius;result.temperatureRaw=ec.battery.temperatureRaw;
        result.temperatureSource=ec.battery.temperatureSource;result.temperatureStatus=ec.battery.temperatureStatus;
        result.dynamicAtMs[2]=ec.battery.dynamicAtMs[2];
    }
    else if(!result.celsius&&gmAmdProfile(ec.profileId)){
        result.temperatureRaw=ec.battery.temperatureRaw;
        result.temperatureStatus+="; "+ec.battery.temperatureSource+": "+ec.battery.temperatureStatus;
    }
    result.healthReadUnavailable=false;
    if(ec.battery.healthPercent){result.healthPercent=ec.battery.healthPercent;result.healthUnavailable=false;result.healthSource=source+" · full/design";result.healthAtMs=ec.battery.healthAtMs;}
    else {result.healthPercent.reset();result.healthUnavailable=true;} // Coherent invalid capacity invalidates old health.
    const auto originalHealth=oldHealth?std::format("{:.6f}",*oldHealth):"unavailable";
    const auto originalCycles=oldCycles?std::to_string(*oldCycles):"unavailable";
    result.powerStatus+="; "+source+" battery supplement: Windows_original_health_percent="+originalHealth+
        " Windows_original_cycles="+originalCycles+" EC_status="+ec.status+" "+ec.battery.powerStatus+" read_only=true";
    if(ec.battery.healthPercent&&result.source.find(source+" · full/design")==std::string::npos)result.source+=" · "+source+" · full/design";
    else if(!oldCycles&&ec.battery.cycles&&result.source.find(source+" · cycles")==std::string::npos)result.source+=" · "+source+" · cycles";
}
Gm6Controller::Gm6Controller(bool startupQuickRestore):impl_(std::make_unique<Impl>(this,startupQuickRestore)){}
Gm6Controller::~Gm6Controller()=default; // Native EC tables persist; no exit writes.
Gm6State Gm6Controller::read(bool tables) {
    std::lock_guard lock(impl_->gate);Gm6State s;
    s.keyboardRestorePending=impl_->keyboardRestorePending;s.keyboardSuspended=impl_->keyboardSuspended;
    s.keyboardRestoreConfirmed=impl_->keyboardRestoreConfirmed;
    s.keyboardRestoreStatus=impl_->keyboardRestoreStatus;
    try {
        impl_->guard(DeviceFeature::Fans,false);auto& ec=*impl_->ec;EcAccess::Transaction transaction(ec);
        if(tables&&impl_->online)impl_->checkOnline(); // Fresh command-result observation, never a write.
        const auto mode=ec.readStable(gm6::mode);s.mode=(mode&0x90)==0x80?0:(mode&0x90)==0?1:(mode&0x90)==0x10?2:-1;
        s.boost=mode&64;s.custom=ec.readStable(gm6::custom)&128;
        s.ramActive=ec.readStable(gm6::commit)&4;s.respective=ec.readStable(gm6::respective)&128;
        const auto light=ec.readStable(gm6::light);s.pl4Double=light&128;s.overrideActive=light&64;
        s.nativeProfile=gm6::nativeProfile({mode,static_cast<std::uint8_t>(s.custom?128:0),light,static_cast<std::uint8_t>(s.ramActive?4:0)});
        s.nativeProfileStatus=impl_->nativeProfileStatus;
        for(int i=0;i<3;++i)s.limits[i]=ec.readStable(gm6::power[i]);
        s.customPresetRevision=impl_->customPresetRevision;
        if(s.nativeProfile==gm6::NativeProfile::Custom&&!impl_->transientEconomy&&!impl_->automaticPower&&impl_->stockCpuIntent<0)s.selectedCustomCurves=impl_->nativeCustomCurves;
        s.autoPowerActive=impl_->persistentModel()&&impl_->automaticPower&&s.limits==std::array<int,3>{};
        s.economyActive=deviceProfile(readBoardDescriptor()).id=="mechrevo-gm6ir0c"&&impl_->transientEconomy&&
            s.nativeProfile==gm6::NativeProfile::Custom&&s.limits==economyPower;
        const bool amd=gmAmdProfile(deviceProfile(readBoardDescriptor()).id);
        if(gmPowerDoubleCodec(deviceProfile(readBoardDescriptor()).id)&&s.pl4Double)s.limits[2]*=2;
        if(s.pl4Double&&!gmPowerDoubleCodec(deviceProfile(readBoardDescriptor()).id))s.cpuPresetStatus="CPU PL-пресеты недоступны: масштаб PL4 Double не подтверждён";
        else {
            int matches{};
            for(int preset=0;preset<3;++preset)try{
                s.cpuPresetLimits[preset]=impl_->cpuPresetValues(preset);
                if(*s.cpuPresetLimits[preset]==s.limits){s.cpuPreset=preset;++matches;}
            }catch(const std::exception& e){
                if(!s.cpuPresetStatus.empty())s.cpuPresetStatus+="; ";
                s.cpuPresetStatus+=std::format("CPU PL preset{}: {}",preset,e.what());
            }
            if(matches!=1)s.cpuPreset=-1;
            s.stockCpuActive=impl_->stockCpuIntent>=0&&s.cpuPresetLimits[impl_->stockCpuIntent]&&
                *s.cpuPresetLimits[impl_->stockCpuIntent]==s.limits;
            if(s.stockCpuActive)s.cpuPreset=impl_->stockCpuIntent; // Explicit choice disambiguates equal stock triples.
            if(s.cpuPresetStatus.empty())s.cpuPresetStatus="CPU PL-пресеты используют свежие параметры прошивки; FANs не меняются";
        }
        for(int ch=0;ch<2;++ch){s.rpm[ch]=impl_->rpm(ch);const auto raw=ec.readStable(ch?0x75C:0x75B);if(raw<=200)s.pwm[ch]=raw/2;}
        if(tables) {
            s.tablesRead=true;
            s.curves=readRamCurves(ec);
        }
        auto blocker=activeOem();
        if(hydrocProfile(deviceProfile(readBoardDescriptor()).id)&&(ec.readStable(0x7C8)&0x20)&&(ec.readStable(0x7C7)&2))blocker="HYDROC: connected liquid cooling owns FAN/PL policy";
        s.connected=true;s.writable=blocker.empty();
        SYSTEM_POWER_STATUS supply{};
        const bool knownPreset=std::any_of(s.cpuPresetLimits.begin(),s.cpuPresetLimits.end(),[](const auto& p){return p.has_value();});
        const bool acConfirmed=GetSystemPowerStatus(&supply)&&supply.ACLineStatus==1;
        s.cpuPresetWritable=s.writable&&(!s.pl4Double||gmPowerDoubleCodec(deviceProfile(readBoardDescriptor()).id))&&knownPreset&&acConfirmed;
        if(impl_->persistentModel()&&(!(light&64)||!(ec.readStable(0x741)&1))){
            s.cpuPresetWritable=false;
            s.cpuPresetStatus+="; GM6 CPU override gate is inactive; FAN policy will not be changed to enable it";
        }
        if(!acConfirmed&&s.writable&&!s.pl4Double)s.cpuPresetStatus+="; применение доступно только при подтверждённом питании от сети";
        const auto triple=[](const std::optional<std::array<int,3>>& values){return values?std::format("{}/{}/{}",(*values)[0],(*values)[1],(*values)[2]):"unavailable";};
        const auto cpuLine=std::format("CPU-only PL observation actual={}/{}/{} matched_preset={} Office734={} Gaming730={} Turbo7A7={} writable={} status={}",
            s.limits[0],s.limits[1],s.limits[2],s.cpuPreset,triple(s.cpuPresetLimits[0]),triple(s.cpuPresetLimits[1]),triple(s.cpuPresetLimits[2]),s.cpuPresetWritable,s.cpuPresetStatus);
        if(cpuLine!=impl_->lastCpuLog){logMessage("gm6ir0c",cpuLine);impl_->lastCpuLog=cpuLine;}
        try{if(hydrocProfile(deviceProfile(readBoardDescriptor()).id))s.keyboardStatus="HYDROC: separate ITE HID lighting, no SingleZone EC mapping";
            else {s.keyboard=readKeyboardBacklight(ec);s.keyboardStatus=s.keyboard->supported?(std::string(gmModelName(deviceProfile(readBoardDescriptor()).id))+" RGB EC"):"RGB capability отсутствует";}}
        catch(const std::exception& e){s.keyboardStatus=e.what();}
        s.online=impl_->online;s.onlineLinked=impl_->onlineLinked;s.onlineControlled=impl_->onlineControlled;s.needsRestore=impl_->needsRestore;
        if(s.online)s.onlineCurves=impl_->onlineCurves;
        for(int ch=0;ch<2;++ch)if(s.online&&impl_->onlineControlled[ch]&&impl_->onlineLast[ch]>=0)s.onlineTarget[ch]=impl_->onlineLast[ch];
        s.commandReadbackConfirmed=impl_->readbackConfirmed;s.fanResponseConfirmed=impl_->fanResponseConfirmed;
        s.status=std::string(gmModelName(deviceProfile(readBoardDescriptor()).id))+(s.writable?" · EC: чтение подтверждено":" · только чтение: "+blocker);
        s.status+=s.commandReadbackConfirmed?"; readback команд подтверждён":"; readback команд в этой сессии ещё не проверен";
        if(s.fanResponseConfirmed)s.status+="; реакция вентиляторов подтверждена пробой";
        s.status+="; исполнение CPU PL не измерено";
        s.status+="; OEM profile="+(s.nativeProfile?std::string(gm6::nativeProfileName(*s.nativeProfile)):"unknown/mixed flags");
        if(!s.nativeProfileStatus.empty())s.status+="; последняя команда: "+s.nativeProfileStatus;
        if(!impl_->onlineStatus.empty())s.status+="; "+impl_->onlineStatus;
        const auto line=std::format("mode={} boost={} custom={} ram={} respective={} PL1/PL2/PL4(raw)={}/{}/{} double={} CPU RPM={} PWM={} GPU RPM={} PWM={} writable={} reads={} writes={}",
            s.mode,s.boost,s.custom,s.ramActive,s.respective,s.limits[0],s.limits[1],s.limits[2],s.pl4Double,
            s.rpm[0].value_or(-1),s.pwm[0].value_or(-1),s.rpm[1].value_or(-1),s.pwm[1].value_or(-1),s.writable,ec.readCount(),ec.writeCount());
        // I/O counters and RPM are telemetry, not events. Reuse the existing
        // ten-sample power/action burst; never add a polling loop or EC access.
        if(loggingEnabled()){
            const auto signature=std::format("{}|{}|{}|{}|{}|{}/{}/{}|{}|{}|{}|{}|{}|{}|{}|{}",
                s.mode,s.boost,s.custom,s.ramActive,s.respective,s.limits[0],s.limits[1],s.limits[2],s.pl4Double,
                s.writable,s.online,s.onlineLinked,s.onlineControlled[0],s.onlineControlled[1],s.needsRestore,blocker);
            const bool changed=signature!=impl_->lastLog||!impl_->lastReadError.empty();
            const auto now=GetTickCount64();
            if(changed)recordPowerAction("typed_ec_state","observed",s.mode,signature);
            const auto generation=powerDiagnosticBurstGeneration();
            if(generation!=impl_->lastBurstGeneration){impl_->lastBurstGeneration=generation;impl_->burstDetailsRemaining=10;}
            if(changed||(powerDiagnosticBurstActive()&&impl_->burstDetailsRemaining&&now-impl_->lastTelemetryLogMs>=1000)){
                logMessage("gm6ir0c",line+" blocker="+(blocker.empty()?"none":blocker));impl_->lastTelemetryLogMs=now;
                if(impl_->burstDetailsRemaining)--impl_->burstDetailsRemaining;
            }
            impl_->lastLog=signature;impl_->lastReadError.clear();
        }else{impl_->lastLog.clear();impl_->lastReadError.clear();}
    }catch(const std::exception& e){
        s.connected=false;s.writable=false;s.status=e.what();
        if(loggingEnabled()&&s.status!=impl_->lastReadError){
            logMessage("gm6ir0c",s.status);recordPowerAction("typed_ec_state","read_failed",-1,s.status);impl_->lastReadError=s.status;
        }
    }
    return s;
}
std::vector<std::uint8_t> Gm6Controller::readDiagnosticBlock(std::uint16_t address,std::size_t count){
    if(count==0||count>64||address>=0x1000||count>0x1000u-address)
        throw std::invalid_argument("GM6IR0C: диагностическое чтение вне0x000–0xFFF/64байт");
    std::lock_guard lock(impl_->gate);impl_->guard(DeviceFeature::Fans,false);
    if(impl_->ec->observationsPaused())throw EcObservationBusy("EC: временная пауза наблюдений другой транзакцией");
    return impl_->ec->readBlock(address,count);
}
uniwill::RamFanFactoryResult Gm6Controller::exportFactoryTables(bool explicitWithFanProbes,std::stop_token token){
    const std::uint8_t project=hydrocProfile(deviceProfile(readBoardDescriptor()).id)?26:25;
    if(gmAmdProfile(deviceProfile(readBoardDescriptor()).id))return uniwill::refusedRamFanFactory("AMD model: factory mailbox not admitted; Manual RAM commands available",true);
    if(!explicitWithFanProbes)return uniwill::exportRamFanFactory({},project,false,token);
    if(token.stop_requested())return uniwill::refusedRamFanFactory("GM6 factory cancelled before access",true);
    try{
        std::lock_guard lock(impl_->gate);impl_->guard(DeviceFeature::Fans,true);
        if(impl_->online||impl_->needsRestore||impl_->probeActive)
            throw std::runtime_error("GM6 factory: active/uncertain controller refused; explicit Default is required");
        EcAccess::Transaction transaction(*impl_->ec);
        uniwill::RamFanFactoryIo io{
            [&](auto address){return impl_->ec->read(address);},
            [&](auto address,auto value){
                // Command bytes can self-clear: raw command admission with a
                // later mailbox ACK, never retry a strobe as a persistent byte.
                if(address==0x741)impl_->ec->updateBitsCommand(address,1,value);
                else if(address>=0xF5D&&address<=0xF5F)impl_->ec->updateBitsCommand(address,255,value);
                else throw std::runtime_error("GM6 factory: non-mailbox write refused");
            },
            [&]{impl_->guard(DeviceFeature::Fans,true);const auto owner=uniwillControlOwner();
                if(!owner.empty())throw std::runtime_error("GM6 factory: OEM owner "+owner);
            }};
        auto r=uniwill::exportRamFanFactory(io,project,true,token);
        logMessage("factory_tables",std::format("GM6 attempted={} complete={} restored={} status={}",r.attempted,r.complete,r.restored,r.status));return r;
    }catch(const std::exception& e){return uniwill::refusedRamFanFactory(e.what(),token.stop_requested());}
}
FanMaximumResult Gm6Controller::measureMaximum(std::stop_token cancellation){
    auto operation=deviceOperations().tryBegin();
    if(!operation){FanMaximumResult r;r.source="GM6IR0C · EC Fan Boost";r.status="Управление занято другой операцией";return r;}
    // Pause only our Max probe. Ordinary EC dump reads/GUI work must not
    // suspend temperature protection; all native operations share gate anyway.
    {std::lock_guard lock(impl_->gate);impl_->probeActive=true;++impl_->onlineGeneration;}
    struct ResumeOnline {Impl* p;~ResumeOnline(){std::lock_guard lock(p->gate);p->probeActive=false;++p->onlineGeneration;p->wake.notify_all();}} resume{impl_.get()};
    std::optional<std::uint8_t> before;
    std::array<std::uint8_t,4> flags{};
    const std::array fields{std::pair{gm6::commit,4},std::pair{gm6::custom,128},std::pair{gm6::light,64},std::pair{gm6::respective,128}};
    auto result=measureMaximumFans("GM6IR0C · EC Fan Boost",[&]{
        std::lock_guard lock(impl_->gate);impl_->guard(DeviceFeature::Fans,true);EcAccess::Transaction transaction(*impl_->ec);
        for(std::size_t i=0;i<fields.size();++i)flags[i]=impl_->ec->readStable(fields[i].first)&fields[i].second;
        before=impl_->ec->readStable(gm6::mode);
        if(!(*before&64))(void)impl_->ec->updateBitsVerified(gm6::mode,64,64);
    },[&]{
        std::lock_guard lock(impl_->gate);impl_->guard(DeviceFeature::Fans,false);
        if(before&&!(impl_->ec->readStable(gm6::mode)&64))throw std::runtime_error("Fan Boost изменён вне пробы");
        FanMaximumSample sample;
        for(int ch=0;ch<2;++ch){sample.rpm[ch]=impl_->rpm(ch);const auto raw=impl_->ec->readStable(ch?0x75C:0x75B);if(raw<=200)sample.percent[ch]=raw/2.;}
        return sample;
    },[&]{
        if(!before)return;
        std::lock_guard lock(impl_->gate);impl_->guard(DeviceFeature::Fans,true);EcAccess::Transaction transaction(*impl_->ec);
        for(std::size_t i=0;i<fields.size();++i)if((impl_->ec->readStable(fields[i].first)&fields[i].second)!=flags[i])
            throw std::runtime_error("Флаги GM6IR0C изменены извне; прежнее состояние не перезаписано");
        const auto current=impl_->ec->readStable(gm6::mode);
        if((current&~64)!=(*before&~64))throw std::runtime_error("Профиль GM6IR0C изменён извне");
        if((current&64)!=(*before&64))(void)impl_->ec->updateBitsVerified(gm6::mode,64,*before&64);
    },cancellation);
    {std::lock_guard lock(impl_->gate);if(result.commandApplied&&result.restored)impl_->readbackConfirmed=true;
        // A stable plateau alone never proves a response. Baseline may already
        // be Max; common sampling explicitly distinguishes that unknown case.
        if(result.commandApplied&&result.responseObserved&&result.restored)impl_->fanResponseConfirmed=true;}
    return result;
}
FanDiagnosticSession Gm6Controller::prepareDiagnostic(){
    const auto epoch=diagnosticFanEpoch();
    const auto keepAlive=shared_from_this();
    std::lock_guard lock(impl_->gate);impl_->guard(DeviceFeature::Fans,true);
    EcAccess::Transaction transaction(*impl_->ec);
    const auto flags=impl_->fanFields(false);
    const bool wasDefault=!(flags.at(gm6::commit).expected&4)&&!(flags.at(gm6::custom).expected&128)&&
        !(flags.at(gm6::light).expected&64)&&!(flags.at(gm6::mode).expected&64);
    bool wasOnline=impl_->online&&!impl_->needsRestore;
    Impl::FanFields saved;
    std::string mode="unknown/foreign FAN state";
    if(wasOnline){
        // Only our active expected fields establish ownership, not a loaded CSV.
        try{impl_->checkOnline();saved=impl_->fanFields(true);mode="Manual online";}
        catch(const OwnershipChanged&){wasOnline=false;} // Explicit Default still follows; foreign replay does not.
    }else if(!impl_->ownedFanFields.empty()&&impl_->matchesFan(impl_->ownedFanFields)){
        saved=impl_->ownedFanFields;mode=impl_->ownedFanMode;
    }else if(wasDefault)mode="Default";
    const auto curves=impl_->onlineCurves,table=impl_->onlineTemplate;
    const auto points=impl_->onlinePoints;const auto controlled=impl_->onlineControlled;
    const auto levels=impl_->onlineLevels,last=impl_->onlineLast;
    const bool linked=impl_->onlineLinked,protection=impl_->onlineProtection;
    const auto generation=++impl_->fanCommandGeneration;
    impl_->automatic();
    const auto baseline=impl_->fanFields(false);
    FanDiagnosticSession session;session.previousMode=mode;
    if(saved.empty()&&!wasDefault)return session;
    const auto consumed=std::make_shared<bool>(false);
    session.restore=[keepAlive,generation,saved,baseline,wasOnline,curves,table,points,controlled,levels,last,linked,protection,consumed,epoch]{
        // End-of-dump restoration is an independent admitted command. A new
        // explicit user request invalidates it even when that request failed.
        auto operation=deviceOperations().tryBegin();
        std::lock_guard lock(keepAlive->impl_->gate);auto& state=*keepAlive->impl_;
        FanDiagnosticRestore result;
        if(*consumed){result.status="GM6: diagnostic restore callback already consumed";return result;}
        if(state.fanCommandGeneration!=generation||diagnosticFanEpoch()!=epoch){result.superseded=true;result.status="GM6: newer FAN command/power epoch superseded diagnostic restore";return result;}
        if(!operation){result.status="GM6: diagnostic restore deferred/refused; another operation owns hardware";return result;}
        *consumed=true;++state.fanCommandGeneration;result.attempted=true;
        try{
            // GPU/provider telemetry can be slow. Never hold the shared EC
            // transaction while waiting for DLL reads; re-admit afterwards.
            MonitoringSnapshot sample;
            if(wasOnline){sample=Impl::temperatures();Impl::requireTemperatures(sample);}
            state.guard(DeviceFeature::Fans,true);EcAccess::Transaction transaction(*state.ec);
            if(state.online||state.needsRestore||state.probeActive||!state.matchesFan(baseline))
                throw std::runtime_error("GM6: Default baseline changed; old FAN state not replayed");
            if(saved.empty()){
                if(diagnosticFanEpoch()!=epoch){result.attempted=false;result.superseded=true;result.status="GM6: power epoch superseded diagnostic Default observation";return result;}
                result.confirmed=true;result.status="GM6: previous Default still confirmed; no writes";return result;}
            if(wasOnline){try{state.captureOnline();}catch(...){state.onlineFields.clear();throw;}}
            Journal journal{*state.ec};
            // Recheck after fresh provider reads/capture, immediately before
            // target writes; a GUI sleep/resume never replays the old request.
            if(diagnosticFanEpoch()!=epoch){if(wasOnline)state.onlineFields.clear();result.attempted=false;result.superseded=true;result.status="GM6: power epoch superseded diagnostic restore before write";return result;}
            try{
                // Restore only the previously owned mask/table. Boost owns only
                // mode.bit6 and never adopts an underlying foreign RAM table.
                if(saved.contains(gm6::commit))journal.put(gm6::commit,0,4);
                for(const auto& [address,field]:saved)if(address!=gm6::commit)journal.put(address,field.expected,field.mask);
                if(saved.contains(gm6::commit)){const auto& field=saved.at(gm6::commit);journal.put(gm6::commit,field.expected,field.mask);}
                journal.done=true;
            }catch(const std::exception& error){
                const auto rollback=journal.rollback([&]{state.guard(DeviceFeature::Fans,true);});
                if(wasOnline)state.onlineFields.clear();
                state.needsRestore=!rollback.empty();
                throw std::runtime_error(std::string(error.what())+(rollback.empty()?"; Default baseline rollback confirmed":"; rollback unknown: "+rollback));
            }
            if(wasOnline){
                state.confirmOnline(journal);state.onlineCurves=curves;state.onlineTemplate=table;state.onlinePoints=points;
                state.onlineControlled=controlled;state.onlineLevels=levels;state.onlineLast=last;
                state.onlineLinked=linked;state.onlineProtection=protection;state.online=true;
                try{state.driveOnline(sample);state.wake.notify_all();}
                catch(...){state.stopOnline();throw;}
            }else{state.ownedFanFields=saved;state.ownedFanMode=saved.size()==1?"Max":"Manual";}
            state.readbackConfirmed=true;result.confirmed=true;
            result.status="GM6: previous owned FAN mode restored with readback; physical RPM response not inferred";
        }catch(const std::exception& error){result.status=std::string("GM6 diagnostic restore: ")+error.what();}
        return result;
    };
    return session;
}
std::array<int,3> Gm6Controller::setCpuPreset(int preset) {
    if(preset<0||preset>=static_cast<int>(gm6::powerDefaults.size()))throw std::invalid_argument("Неизвестный CPU PL-пресет GM6IR0C");
    std::lock_guard lock(impl_->gate);impl_->cancelConfigurationRestore();impl_->guard(DeviceFeature::PowerLimits,true);
    auto& ec=*impl_->ec;EcAccess::Transaction transaction(ec);
    // Exact GM6 EC1.12 consumes CPU overrides only under APP and RAM gates.
    // Never activate a FAN gate behind a CPU-only selection or report ignored
    // bytes as a working profile. Physical whole-profile switching is separate.
    if(impl_->persistentModel()&&(!(ec.readStable(gm6::light)&64)||!(ec.readStable(0x741)&1)))
        throw std::runtime_error("GM6: CPU override gate is inactive; selected FAN mode retained, CPU profile not applied");
    // Use this machine's current firmware defaults. Archived45/75/140 watt
    // profiles establish provenance only and are never fallback values.
    const auto values=impl_->cpuPresetValues(preset);
    impl_->applyCpuPowerLocked(values,false);
    impl_->automaticPower=false;impl_->transientEconomy=false;impl_->stockCpuIntent=preset;
    impl_->remember([&](Configuration& c){
        // v1 full profiles implicitly selected FANs. Preserve that existing FAN
        // intent on migration, then separate subsequent CPU-only restoration.
        if(c.fan<0&&c.profile>=0){c.fan=c.profile<3?0:1;c.fanCurves=c.profileCurves;}
        c.profile=-1;c.stockCpu=preset;c.resetPower=false;c.power=values;
    });
    setCustomPresetActive(deviceProfile(readBoardDescriptor()).id,false);
    logMessage("gm6ir0c",std::format("CPU-only application preset={} uses firmware default PL triple; native whole-system751 mode/FANs/online unchanged",preset));
    return values;
}
void Gm6Controller::resetCpuPower() {
    std::lock_guard lock(impl_->gate);impl_->cancelConfigurationRestore();impl_->guard(DeviceFeature::PowerLimits,true);
    EcAccess::Transaction transaction(*impl_->ec);
    impl_->applyCpuPowerLocked({0,0,0},false);
    impl_->automaticPower=true;impl_->transientEconomy=false;impl_->stockCpuIntent=-1;
    setCustomPresetActive(deviceProfile(readBoardDescriptor()).id,false);
    impl_->remember([](Configuration& c){c.resetPower=true;c.stockCpu=-1;});
    logMessage("gm6ir0c","CPU-only Auto: zero overrides read back; FANs, native mode and saved manual draft unchanged");
}
void Gm6Controller::setEconomyPower(){
    std::lock_guard lock(impl_->gate);impl_->cancelConfigurationRestore();impl_->guard(DeviceFeature::PowerLimits,true);
    if(deviceProfile(readBoardDescriptor()).id=="mechrevo-gm6ir0c"){
        Gm6NativeProfileResult result;result.receivedAt=GetTickCount64();
        impl_->applyNativeProfile(result,impl_->nativeEpoch.load(),gm6::NativeProfile::Custom,economyPower,false,true);
        return;
    }
    EcAccess::Transaction transaction(*impl_->ec);impl_->applyCpuPowerLocked({10,15,20},false);
    setCustomPresetActive(deviceProfile(readBoardDescriptor()).id,false);
    logMessage("gm6ir0c","CPU-only Economy: owner-selected10/15/20 read back; FANs, native mode and saved manual draft unchanged");
}
void Gm6Controller::setBoost(bool on) {
    std::lock_guard lock(impl_->gate);impl_->cancelConfigurationRestore();++impl_->fanCommandGeneration;impl_->ownedFanFields.clear();impl_->ownedFanMode.clear();impl_->stopOnline();impl_->guard(DeviceFeature::Fans,true);
    EcAccess::Transaction transaction(*impl_->ec);Journal j{*impl_->ec};j.put(gm6::mode,on?64:0,64);j.done=true;impl_->readbackConfirmed=true;
    // Boost owns only its bit; pre-existing foreign RAM is never claimed.
    if(on){const auto value=impl_->ec->readStable(gm6::mode);impl_->ownedFanFields.emplace(gm6::mode,Impl::OwnedField{64,value,value});impl_->ownedFanMode="Max";}
    logMessage("gm6ir0c",on?"Fan Boost=1 readback confirmed":"Fan Boost=0 readback confirmed");
    if(on)impl_->remember([](Configuration& c){c.fan=2;});
}
void Gm6Controller::setAutomatic() {
    std::lock_guard lock(impl_->gate);impl_->cancelConfigurationRestore();++impl_->fanCommandGeneration;impl_->automatic();
    impl_->remember([](Configuration& c){c.fan=0;});
}
void Gm6Controller::setCurves(const gm6::Curves& curves,std::optional<std::array<int,3>> power,bool respective) {
    validate(curves);if(power)validatePower(*power);std::lock_guard lock(impl_->gate);impl_->cancelConfigurationRestore();
    const bool gm5TurboCustom=power.has_value()&&gm5Profile(deviceProfile(readBoardDescriptor()).id)&&selectedCustomBase(deviceProfile(readBoardDescriptor()).id)==1;
    if(power){impl_->guard(DeviceFeature::PowerLimits,true);SYSTEM_POWER_STATUS supply{};
        if(!GetSystemPowerStatus(&supply)||supply.ACLineStatus!=1)throw std::runtime_error("Custom preset: AC required");
        EcAccess::Transaction preflight(*impl_->ec);
        if(gm5TurboCustom)impl_->requireGm5TurboBase();
        if(gmAmdProfile(deviceProfile(readBoardDescriptor()).id)&&(impl_->ec->readStable(gm6::light)&128)&&(*power)[2]%2)throw std::runtime_error("Custom preset: FPPT Double requires even watts");}
    ++impl_->fanCommandGeneration;impl_->ownedFanFields.clear();impl_->ownedFanMode.clear();impl_->stopOnline();impl_->guard(DeviceFeature::Fans,true);
    auto& ec=*impl_->ec;EcAccess::Transaction transaction(ec);Journal j{ec};
    auto encoded=power.value_or(std::array<int,3>{});
    if(power){
        validatePower(*power);impl_->guard(DeviceFeature::PowerLimits,true);
        SYSTEM_POWER_STATUS supply{};if(!GetSystemPowerStatus(&supply)||supply.ACLineStatus!=1)throw std::runtime_error("Custom preset: AC required");
        if(gmAmdProfile(deviceProfile(readBoardDescriptor()).id)&&(ec.readStable(gm6::light)&128)){
            if(encoded[2]%2)throw std::runtime_error("Custom preset: FPPT Double requires even watts");encoded[2]/=2;
        }
    }
    if(gm5TurboCustom)impl_->requireGm5TurboBase();
    putTables(j,curves,respective);j.put(gm6::light,64,64);j.put(gm6::custom,128,128);
    j.put(gm6::mode,gm5TurboCustom?0x10:0,0xF0); // Custom preset uses GM5 Turbo base; prior FAN semantics remain.
    if(power)for(int i=0;i<3;++i)j.put(gm6::power[i],encoded[i]);
    j.put(gm6::commit,4,4);
    if(power){std::string error;if(!savePortableText(portableSettingsPath(std::string(gmPowerDraftFile(deviceProfile(readBoardDescriptor()).id))),std::format("{} {} {}\n",(*power)[0],(*power)[1],(*power)[2]),error))throw std::runtime_error(error);}
    j.done=true;impl_->readbackConfirmed=true;impl_->ownedFanFields=impl_->fanFields(true);impl_->ownedFanMode="Manual";impl_->nativeCustomCurves=curves;impl_->nativeCustomRespective=respective;logMessage("gm6ir0c","Manual CPU/GPU16-point RAM curves verified; physical response unconfirmed");
    impl_->remember([&](Configuration& c){c.fan=1;c.fanCurves=curves;c.customCurvesSaved=true;c.custom.curves=curves;c.custom.respective=respective;});
}
void Gm6Controller::setOnlineCurves(const gm6::Curves& curves,std::array<bool,2> controlled,bool linked){
    validate(curves);if(linked)controlled={true,true};if(!controlled[0]&&!controlled[1])throw std::invalid_argument("Online requires a selected fan channel");
    const auto sample=Impl::temperatures();Impl::requireTemperatures(sample);
    std::lock_guard lock(impl_->gate);impl_->cancelConfigurationRestore();++impl_->fanCommandGeneration;impl_->ownedFanFields.clear();impl_->ownedFanMode.clear();impl_->stopOnline();impl_->guard(DeviceFeature::Fans,true);
    auto& ec=*impl_->ec;EcAccess::Transaction transaction(ec);
    try{impl_->captureOnline();}catch(...){impl_->onlineFields.clear();throw;}
    impl_->onlineCurves=curves;impl_->onlineLinked=linked;impl_->onlineControlled=controlled;impl_->onlineLevels={};impl_->onlineLast={-1,-1};impl_->onlineProtection=false;
    const auto raw=ec.readStable(gm6::mode)&0x90;const int mode=raw==0x80?0:raw==0x10?2:1;
    impl_->onlineTemplate=curves;
    std::optional<gm6::Curves> native;
    if(l16Profile(deviceProfile(readBoardDescriptor()).id)&&(!controlled[0]||!controlled[1])){
        native=readRamCurves(ec);
        if(!native){impl_->onlineFields.clear();throw std::runtime_error("L16: live curve for the untouched channel is unavailable");}
    }
    for(int ch=0;ch<2;++ch){
        if(!controlled[ch])impl_->onlineTemplate[ch]=native?(*native)[ch]:gm6::factoryCurves[mode][ch];
        impl_->onlinePoints[ch].clear();for(const auto& point:curves[ch])if(point.up!=255)impl_->onlinePoints[ch].push_back({point.up,point.duty});
        if(controlled[ch]&&impl_->onlinePoints[linked?0:ch].empty()){impl_->onlineFields.clear();throw std::invalid_argument("Online curve is empty");}
    }
    impl_->online=true;
    try{impl_->driveOnline(sample);impl_->nativeCustomCurves=curves;impl_->nativeCustomRespective=true;impl_->wake.notify_all();}
    catch(...){impl_->online=false;try{impl_->stopOnline();}catch(const std::exception& e){impl_->needsRestore=!impl_->onlineFields.empty();logMessage("gm6ir0c",e.what());}throw;}
    impl_->remember([&](Configuration& c){c.fan=3;c.fanCurves=curves;c.controlled=controlled;c.linked=linked;
        c.customCurvesSaved=true;c.custom.curves=curves;c.custom.respective=true;});
}
void Gm6Controller::setCustomBase(int base) {
    if(base!=0&&base!=1)throw std::invalid_argument("Custom Base: invalid selection");
    std::lock_guard lock(impl_->gate);
    impl_->cancelConfigurationRestore();
    const auto id=deviceProfile(readBoardDescriptor()).id;
    if(id!="mechrevo-gm6ir0c"&&!gm5Profile(id))throw std::runtime_error("Custom Base: неподтверждённая модель");
    impl_->guard(DeviceFeature::PowerLimits,true);
    EcAccess::Transaction transaction(*impl_->ec);
    if(base&&gm5Profile(id))impl_->requireGm5TurboBase();
    if(impl_->online)impl_->checkOnline();
    if(!impl_->ownedFanFields.empty()&&!impl_->matchesFan(impl_->ownedFanFields))throw std::runtime_error("Custom Base: владение вентиляторами изменилось");
    const auto before=impl_->ec->readStable(gm6::mode);constexpr std::uint8_t mask=0xB0;
    if((before&mask)!=0&&(before&mask)!=0x10)throw std::runtime_error("Custom Base: не подтверждён активный Custom");
    Journal journal{*impl_->ec};
    try{journal.put(gm6::mode,base?0x10:0,mask);
        const auto after=impl_->ec->readStable(gm6::mode);
        if(after!=static_cast<std::uint8_t>((before&~mask)|(base?0x10:0)))throw std::runtime_error("Custom Base: EC readback mismatch");
        journal.done=true;
    }catch(const std::exception& error){const auto rollback=journal.rollback([this]{impl_->guard(DeviceFeature::PowerLimits,true);});
        throw std::runtime_error(std::string(error.what())+(rollback.empty()?"; изменения возвращены":"; rollback: "+rollback));}
    const auto update=[base](Impl::FanFields& fields){if(auto it=fields.find(gm6::mode);it!=fields.end()){
        const auto owned=static_cast<std::uint8_t>(it->second.mask&mask);
        it->second.before=static_cast<std::uint8_t>((it->second.before&~owned)|((base?0x10:0)&owned));
        it->second.expected=static_cast<std::uint8_t>((it->second.expected&~owned)|((base?0x10:0)&owned));}};
    update(impl_->onlineFields);update(impl_->ownedFanFields);
    impl_->readbackConfirmed=true;
}
void Gm6Controller::setPower(const std::array<int,3>& values) {
    validatePower(values);
    std::lock_guard lock(impl_->gate);impl_->cancelConfigurationRestore();impl_->guard(DeviceFeature::PowerLimits,true);
    if(deviceProfile(readBoardDescriptor()).id=="mechrevo-gm6ir0c"){
        Gm6NativeProfileResult result;result.receivedAt=GetTickCount64();
        impl_->applyNativeProfile(result,impl_->nativeEpoch.load(),gm6::NativeProfile::Custom,values,true);
        return;
    }
    auto& ec=*impl_->ec;EcAccess::Transaction transaction(ec);
    impl_->applyCpuPowerLocked(values,true,gm5Profile(deviceProfile(readBoardDescriptor()).id));
}
std::optional<std::array<int,3>> Gm6Controller::loadPowerDraft(std::string& error) {
    std::lock_guard lock(impl_->gate);return powerDraft(error);
}
void Gm6Controller::saveCustomPresetDraft(int slot,const std::array<int,3>& values){
    std::lock_guard lock(impl_->gate);requireCustomPreset(slot);validatePower(values);
    const auto id=deviceProfile(readBoardDescriptor()).id;auto lib=loadCustomPresetLibrary(id);
    CustomPreset p;std::string source;p.power=values;p.curves=impl_->customCurves(source);p.respective=impl_->nativeCustomCurves?impl_->nativeCustomRespective:false;
    validate(p.curves);lib.slots[slot]=p;lib.selected=slot;lib.active=true;saveCustomPresetLibrary(id,lib);++impl_->customPresetRevision;
}
bool Gm6Controller::customPresetFilled(int slot){
    // Pure bounded file lookup: never block the UI on the hardware worker gate.
    requireCustomPreset(slot);return loadCustomPresets()[slot].has_value();
}
std::optional<std::array<int,3>> Gm6Controller::useCustomPreset(int slot){
    std::unique_lock lock(impl_->gate);impl_->cancelConfigurationRestore();requireCustomPreset(slot);
    const auto id=std::string(deviceProfile(readBoardDescriptor()).id);auto slots=loadCustomPresets();
    auto p=*slots[slot];
    if(p.powerRecipe){impl_->guard(DeviceFeature::PowerLimits,false);EcAccess::Transaction transaction(*impl_->ec);p.power=impl_->cpuPresetValues(p.powerRecipe-1);}
    validatePower(p.power);validate(p.curves);
    if(id=="mechrevo-gm6ir0c"){
        Gm6NativeProfileResult result;result.receivedAt=GetTickCount64();
        impl_->applyNativeProfile(result,impl_->nativeEpoch.load(),gm6::NativeProfile::Custom,p.power,true,false,p.curves,p.respective);
    }else{lock.unlock();setCurves(p.curves,p.power,p.respective);lock.lock();++impl_->customPresetRevision;}
    selectCustomPresetSlot(id,slot);
    logMessage("gm6ir0c",std::format("custom_preset slot={} action=applied native_readback=true",slot+1));
    return p.power;
}

KeyboardBacklightState Gm6Controller::readKeyboard(){
    if(hydrocProfile(deviceProfile(readBoardDescriptor()).id))throw std::runtime_error("HYDROC lighting uses the typed ITE HID controller");
    std::lock_guard lock(impl_->gate);impl_->guard(DeviceFeature::Backlight,false);
    return readKeyboardBacklight(*impl_->ec);
}
KeyboardBacklightState Gm6Controller::setKeyboard(const KeyboardBacklightChange& c){
    if(hydrocProfile(deviceProfile(readBoardDescriptor()).id))throw std::runtime_error("HYDROC lighting uses the typed ITE HID controller");
    std::lock_guard lock(impl_->gate);
    if(impl_->keyboardSuspended)throw std::runtime_error("GM6 подсветка приостановлена до подтверждённого возобновления");
    const bool idlePowerOnly=c.powerDisabled&&!c.level&&!c.effect&&!c.pulse&&!c.color&&!c.factoryColor;
    if(!c.expected&&!idlePowerOnly){
        // A newer explicit user command supersedes an old lifecycle request,
        // even if the new command fails admission. Periodic cycle steps do not.
        impl_->keyboardRestorePending=false;
        impl_->keyboardRestoreStatus.clear();
    }
    impl_->guard(DeviceFeature::Backlight,true);
    return applyKeyboardBacklight(*impl_->ec,c);
}
QuickSwitchStates Gm6Controller::readQuickSwitches(){
    std::lock_guard lock(impl_->gate);QuickSwitchStates states;
    try{impl_->guard(DeviceFeature::Backlight,false,false);}
    catch(const std::exception& e){for(auto& state:states)state.detail=e.what();return states;}
    for(std::size_t i=0;i<states.size();++i)try{
        states[i].enabled=mc::readQuickSwitch(*impl_->ec,quickSwitches[i]);states[i].writable=true;
    }catch(const std::exception& e){states[i].detail=e.what();}
    return states;
}
void Gm6Controller::setQuickSwitch(QuickSwitch key,bool enabled){
    const auto index=static_cast<std::size_t>(key);
    if(index>=quickSettings.size())throw std::invalid_argument("Unknown quick switch");
    std::lock_guard lock(impl_->gate);
    // This explicit intent supersedes an older queued lifecycle replay.
    impl_->guard(DeviceFeature::Backlight,true,false);mc::setQuickSwitch(*impl_->ec,key,enabled);
    // Hardware setter only. Common KEYs records the confirmed explicit choice
    // in keys-settings.ini; lifecycle replay must not rewrite model preferences.
}
std::optional<bool> Gm6Controller::readCopilotKey(){
    if(!uniwillCopilotEcProfile(deviceProfile(readBoardDescriptor()).id))return {};
    std::lock_guard lock(impl_->gate);impl_->guard(DeviceFeature::Backlight,false,false);
    return mc::readCopilotKey(*impl_->ec);
}
void Gm6Controller::setCopilotKey(bool locked){
    std::lock_guard lock(impl_->gate);
    if(impl_->keyboardSuspended)throw std::runtime_error("Copilot: keyboard firmware suspended");
    impl_->guard(DeviceFeature::Backlight,true,false);
    mc::setCopilotKey(*impl_->ec,locked);
}
void Gm6Controller::restoreKeyboard(const std::string& reason,std::function<void()> radioAccess){
    if(hydrocProfile(deviceProfile(readBoardDescriptor()).id))return; // HID selection never replays an EC keyboard snapshot.
    if(reason!="startup"&&reason!="power_resume"&&reason!="system_sleep_clock")
        throw std::invalid_argument("GM6 keyboard restore: неизвестное lifecycle событие");
    std::lock_guard lock(impl_->gate);impl_->keyboardSuspended=false;
    (void)radioAccess; // Radio consent belongs to common KEYs, not keyboard restore.
    impl_->keyboardRestoreConfirmed=false;
    if(impl_->persistentModel()&&!impl_->configurationPending){
        impl_->configurationPending=true;impl_->configurationAttempts=0;impl_->configurationNext={};
        impl_->configurationDeadline=Impl::Clock::now()+std::chrono::seconds(30);impl_->configurationReason=reason;
    }
    if(impl_->nativeSubscription.load()){++impl_->nativeArmGeneration;impl_->nativeArmPending.store(true);}
    // Coalesce duplicate resume/clock notifications without restarting the
    // retry budget. A genuine suspend cancels the previous pass first.
    if(!impl_->keyboardRestorePending){
        impl_->keyboardRestoreAttempts=0;impl_->keyboardRestoreNext={};
        impl_->keyboardRestorePending=true;
    }
    impl_->keyboardRestoreReason=reason;
    impl_->keyboardRestoreStatus="GM6: запланирована проверка сохранённой статической подсветки";
    impl_->wake.notify_all();
}
std::array<std::uint8_t,3> Gm6Controller::readLiquidCoolingPolicy(){
    if(!hydrocProfile(deviceProfile(readBoardDescriptor()).id))throw std::runtime_error("LPP: native policy belongs only to exact HYDROC");
    std::lock_guard lock(impl_->gate);impl_->guard(DeviceFeature::LiquidCooling,false);EcAccess::Transaction transaction(*impl_->ec);
    if(!(impl_->ec->readStable(hydroc16g2::lpp::capability)&hydroc16g2::lpp::capabilityMask))throw std::runtime_error("LPP: native LC capability absent");
    return {impl_->ec->readStable(hydroc16g2::lpp::presence),impl_->ec->readStable(hydroc16g2::lpp::pumpTarget),impl_->ec->readStable(hydroc16g2::lpp::fanTarget)};
}
void Gm6Controller::verifyLiquidCoolingPolicyAdmission(){
    if(!hydrocProfile(deviceProfile(readBoardDescriptor()).id))throw std::runtime_error("LPP: native policy belongs only to exact HYDROC");
    std::lock_guard lock(impl_->gate);impl_->guard(DeviceFeature::LiquidCooling,true);EcAccess::Transaction transaction(*impl_->ec);
    impl_->requireLiquidCoolingPolicyAdmission();
}
void Gm6Controller::setLiquidCoolingPresence(bool enabled){
    if(!hydrocProfile(deviceProfile(readBoardDescriptor()).id))throw std::runtime_error("LPP: native policy belongs only to exact HYDROC");
    std::lock_guard lock(impl_->gate);impl_->guard(DeviceFeature::LiquidCooling,true);EcAccess::Transaction transaction(*impl_->ec);
    namespace lpp=hydroc16g2::lpp;
    if(!(impl_->ec->readStable(lpp::capability)&lpp::capabilityMask))throw std::runtime_error("LPP: native LC capability absent");
    const auto before=impl_->ec->readStable(lpp::presence);
    if(enabled&&(before&lpp::presenceMask))throw std::runtime_error("LPP: native LC presence already set outside this session; ownership refused");
    if(enabled)impl_->requireLiquidCoolingPolicyAdmission();
    if(!enabled&&!impl_->lppPresenceOwned.load())throw std::runtime_error("LPP: refusing to release an unowned native presence flag");
    if(!enabled&&!(before&lpp::presenceMask)){impl_->lppPresenceOwned.store(false);return;}
    // Exact OEM LiquidCooling.SetECSupport. Fresh masked RMW/readback changes
    // only7C7.1; all HWOC and unrelated bits, P13 and FAN/PL state stay intact.
    // Mark a guarded, initially-clear field before IO: a failed readback must
    // retain cleanup responsibility, never masquerade as "no write happened".
    if(enabled)impl_->lppPresenceOwned.store(true);
    const auto after=impl_->ec->updateBitsVerified(lpp::presence,lpp::presenceMask,enabled?lpp::presenceMask:0);
    if(bool(after&lpp::presenceMask)!=enabled)throw std::runtime_error("LPP: presence-bit readback failed");
    if(!enabled)impl_->lppPresenceOwned.store(false);
    logMessage("liquid_cooling",std::format("native LC presence7C7.1={} readback; no FAN-table/PL/battery writes",enabled));
}
bool Gm6Controller::liquidCoolingPresenceOwned()const noexcept{return impl_->lppPresenceOwned.load();}
void Gm6Controller::suspendKeyboard(){
    cancelNativeProfileCommands(); // A physical press never replays after sleep.
    std::lock_guard lock(impl_->gate);impl_->keyboardSuspended=true;impl_->keyboardRestorePending=false;
    impl_->configurationPending=false;
    impl_->nativeArmed=false;
    impl_->keyboardRestoreStatus="GM6: подсветка приостановлена на время сна; аппаратных записей нет";
    impl_->wake.notify_all();
}
void Gm6Controller::enqueueNativeProfileCycle(std::uint64_t receivedAt,std::function<void(Gm6NativeProfileResult)> complete){
    bool full{};
    {std::lock_guard queueLock(impl_->nativeQueueGate);
        full=impl_->nativeRequests.size()>=32;
        if(!full)impl_->nativeRequests.push_back({receivedAt,impl_->nativeEpoch.load(),Impl::Clock::now(),std::move(complete)});}
    if(full){Gm6NativeProfileResult result;result.receivedAt=receivedAt;result.status="GM6 native profile: очередь нажатий заполнена";if(complete)complete(std::move(result));}
    else impl_->wake.notify_all();
}
void Gm6Controller::cancelNativeProfileCommands(){
    ++impl_->nativeEpoch;
    impl_->nativeArmPending.store(false);
    std::deque<Impl::NativeRequest> cancelled;
    {std::lock_guard queueLock(impl_->nativeQueueGate);cancelled.swap(impl_->nativeRequests);}
    for(auto& request:cancelled)try{if(request.complete){Gm6NativeProfileResult result;result.receivedAt=request.receivedAt;
        result.status="GM6 native profile: нажатие отменено при сне/закрытии";request.complete(std::move(result));}}catch(...){}
    impl_->wake.notify_all();
}
void Gm6Controller::nativeHotkeySubscribed(){
    impl_->nativeSubscription.store(true);++impl_->nativeArmGeneration;impl_->nativeArmPending.store(true);impl_->wake.notify_all();
}
std::shared_ptr<Gm6Controller> gm6Controller(bool startupQuickRestore) {
    if(wcsstr(GetCommandLineW(),L"--no-ec")||!ramFanControlProfile(deviceProfile(readBoardDescriptor()).id))return {};
    static std::mutex gate;static std::weak_ptr<Gm6Controller> weak;
    std::lock_guard lock(gate);auto value=weak.lock();if(!value){value=std::make_shared<Gm6Controller>(startupQuickRestore);weak=value;}return value;
}
}
