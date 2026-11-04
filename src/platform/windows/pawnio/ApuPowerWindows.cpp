#include "ApuPowerWindows.h"
#include "../../../protocols/minisforum/HpbsdPower.h"
#include "HardwareMutexWindows.h"
#include "../../../devices/minisforum/hpbsd/TelemetryWindows.h"
#include "../EmbeddedRuntimeWindows.h"
#include "MagicControl/BoardIdentity.h"
#include "MagicControl/DeviceProfile.h"
#include "MagicControl/Logging.h"

#include <windows.h>
#include <intrin.h>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <vector>
#include <format>

namespace mc::windows {
// Power/profile contract and EC->SMU probe evidence: src/core/src/POWER_MODES.md.
// This reader only observes SMU. Keep manual EC overrides out of telemetry polling.
struct ApuPowerReader::Impl {
    using Open = HRESULT(WINAPI*)(void**);
    using Load = HRESULT(WINAPI*)(void*, const void*, std::size_t);
    using Execute = HRESULT(WINAPI*)(void*, const char*, const std::uint64_t*, std::size_t,
                                    std::uint64_t*, std::size_t, std::size_t*);
    using Close = HRESULT(WINAPI*)(void*);
    HMODULE library{};
    HANDLE pci{};
    void* module{};
    Execute execute{};
    Close close{};
    ULONGLONG retryAt{};
    ApuPowerDiagnostic cached;

    ~Impl() { reset(); if (pci) CloseHandle(pci); }
    void reset() {
        if (module && close) close(module);
        module = nullptr; execute = nullptr; close = nullptr;
        if (library) FreeLibrary(library);
        library = nullptr;
    }
    void prepare() {
        if (module) return;
        int cpu[4]{}; __cpuid(cpu, 0);
        if (cpu[1] != 0x68747541 || cpu[3] != 0x69746E65 || cpu[2] != 0x444D4163)
            throw std::runtime_error("SMU: этот источник доступен только для AMD");
        const auto runtime = embeddedRuntimeDirectory("PawnIO");
        library = LoadLibraryExW((runtime / "PawnIOLib.dll").c_str(), nullptr,
                                 LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!library) throw std::runtime_error("SMU: библиотека PawnIO недоступна");
        const auto open = reinterpret_cast<Open>(GetProcAddress(library, "pawnio_open"));
        const auto load = reinterpret_cast<Load>(GetProcAddress(library, "pawnio_load"));
        execute = reinterpret_cast<Execute>(GetProcAddress(library, "pawnio_execute"));
        close = reinterpret_cast<Close>(GetProcAddress(library, "pawnio_close"));
        if (!open || !load || !execute || !close || FAILED(open(&module)))
            throw std::runtime_error("SMU: драйвер PawnIO недоступен");
        std::ifstream input(runtime / "RyzenSMU.bin", std::ios::binary);
        const std::vector<char> bytes{std::istreambuf_iterator<char>(input), {}};
        if (bytes.empty() || FAILED(load(module, bytes.data(), bytes.size())))
            throw std::runtime_error("SMU: модуль RyzenSMU недоступен");
    }
    void command(const char* name, std::uint64_t* result, std::size_t count) {
        std::size_t returned{};
        if (FAILED(execute(module, name, nullptr, 0, result, count, &returned)) || returned < count)
            throw std::runtime_error(std::string("SMU: ошибка чтения ") + name);
    }
    void updateTable(std::uint32_t tableVersion) {
        // Called only after the exact Wujie5D0009 / HLYL37xxxx admission below.
        // RyzenAdj 5775fc3 request_transfer_table retries a temporary prerequisite
        // rejection for supported families, including Strix, not only Renoir.
        // Retry ONLY this telemetry transfer after10/100ms. No limit setter,
        // model admission, EC state or sleep/restore action is added here.
        HRESULT status{};std::size_t returned{};
        for(int attempt=0;attempt<3;++attempt){
            if(attempt){const int delay=attempt==1?10:100;
                logMessage("smu_telemetry",std::format("PM transfer retry: table=0x{:X} attempt={}/3 previous_HRESULT=0x{:08X} delay_ms={}",tableVersion,attempt+1,static_cast<unsigned>(status),delay));
                Sleep(delay);}
            status=execute(module,"ioctl_update_pm_table",nullptr,0,nullptr,0,&returned);
            if(SUCCEEDED(status)){
                if(attempt)logMessage("smu_telemetry",std::format("PM transfer recovered: table=0x{:X} attempts={}",tableVersion,attempt+1));
                return;
            }
            // Signed RyzenSMU translates FD to STATUS_INTERNAL_ERROR, FC to
            // STATUS_DEVICE_BUSY; PawnIOLib maps NTSTATUS through Win32.
            if(status!=HRESULT_FROM_WIN32(ERROR_INTERNAL_ERROR)&&status!=HRESULT_FROM_WIN32(ERROR_BUSY))break;
        }
        throw std::runtime_error(std::format("SMU: ошибка обновления PM table, HRESULT=0x{:08X}",static_cast<unsigned>(status)));
    }
    std::uint32_t smuRegister(std::uint32_t address) {
        const std::uint64_t input[]{address};std::uint64_t output{};std::size_t returned{};
        if(FAILED(execute(module,"ioctl_read_smu_register",input,1,&output,1,&returned))||returned!=1)
            throw std::runtime_error("SMU: чтение MP1 mailbox недоступно");
        return static_cast<std::uint32_t>(output);
    }
    void smuRegister(std::uint32_t address,std::uint32_t value) {
        const std::uint64_t input[]{address,value};std::size_t returned{};
        if(FAILED(execute(module,"ioctl_write_smu_register",input,2,nullptr,0,&returned)))
            throw std::runtime_error("SMU: запись MP1 mailbox отклонена");
    }
    void mp1Limit(std::uint32_t commandId,std::uint32_t milliwatts) {
        // Renoir MP1, RyzenAdj nb_smu_ops.c TYPE_MP1 default, api.c 14/15/16.
        // PawnIO ioctl_send_smu_command selects Renoir PSMU 3B10A20 instead;
        // sending these MP1 opcodes there would address different functions.
        constexpr std::uint32_t message=0x3B10528,response=0x3B10564,args=0x3B10998;
        if(commandId<0x14||commandId>0x16||milliwatts<5000||milliwatts>60000)
            throw std::invalid_argument("SMU: недопустимая команда лимита Renoir");
        mp1LimitAt(commandId,milliwatts,message,response,args);
    }
    void mp1LimitAt(std::uint32_t commandId,std::uint32_t milliwatts,std::uint32_t message,std::uint32_t response,std::uint32_t args) {
        auto waitReady=[&] {
            const auto until=GetTickCount64()+500;
            do{const auto value=smuRegister(response);if(value)return value;Sleep(1);}while(GetTickCount64()<until);
            throw std::runtime_error("SMU: MP1 не ответил; состояние лимита неизвестно");
        };
        static_cast<void>(waitReady());
        smuRegister(response,0);
        for(unsigned i=0;i<6;++i)smuRegister(args+4*i,i==0?milliwatts:0);
        smuRegister(message,commandId);
        const auto result=waitReady();
        if(result!=1)throw std::runtime_error(std::format("SMU: MP1 отклонил команду 0x{:X}, ответ 0x{:X}",commandId,result));
    }
    ApuPowerDiagnostic applyHpbsdLimitsMilliwatts(const std::array<int,3>& target,bool stock=false) {
        if(wcsstr(GetCommandLineW(),L"--no-ec")||!HpbsdTelemetryReader::supportedHost())
            throw std::runtime_error("HPBSD SMU: exact BIOS1.06/AMD19h:75h required");
        if(stock&&target!=hpbsd::stockPower(0)&&target!=hpbsd::stockPower(1))throw std::invalid_argument("HPBSD: uncompiled stock endpoint");
        const int ceiling=stock?70800:hpbsd::customPowerMaximum*1000;
        for(const int mw:target)if(mw<5000||mw>ceiling)throw std::invalid_argument("HPBSD: power request outside its exact policy bounds");
        if(target[0]>target[1]||target[1]>target[2])throw std::invalid_argument("HPBSD: STAPM<=Slow<=Fast required");
        const auto fields=[](const ApuPowerDiagnostic& d){
            if(d.stale||d.tableVersion!=0x4c0009||!d.stapmLimitWatts||!d.slowLimitWatts||!d.fastLimitWatts)
                throw std::runtime_error("HPBSD: fresh PM4C0009 limits required");
            return std::array<double,3>{*d.stapmLimitWatts,*d.slowLimitWatts,*d.fastLimitWatts};
        };
        retryAt=0;fields(read(true)); // prepares shared PCI mutex/runtime before transaction
        const auto waited=WaitForSingleObject(pci,1000);
        if(waited!=WAIT_OBJECT_0) {if(waited==WAIT_ABANDONED)ReleaseMutex(pci);throw std::runtime_error("HPBSD: PCI busy/abandoned; no limit writes");}
        struct Unlock {HANDLE p;~Unlock(){ReleaseMutex(p);}} unlock{pci};
        retryAt=0;const auto before=fields(read(true));
        std::array<int,3> baseline{};for(int i=0;i<3;++i)baseline[i]=static_cast<int>(std::lround(before[i]*1000));
        if(!hpbsd::recoverablePower(baseline))throw std::runtime_error("HPBSD: baseline outside recoverable Custom/native endpoints");
        constexpr std::array<std::uint32_t,3> commands{0x14,0x16,0x15};
        const auto send=[&](int i,int raw){
            if(raw<5000||raw>hpbsd::customPowerMaximum*1000)throw std::runtime_error("HPBSD: raw MP1 argument outside common Custom request bounds");
            // Hawk Point response578 differs from Renoir564. PSMU opcodes are
            // deliberately not used; no EC/FAN/thermal/voltage writes here.
            mp1LimitAt(commands[i],raw,0x3b10528,0x3b10578,0x3b10998);
        };
        const auto snapshot=[&](){Sleep(160);retryAt=0;return read(true);};
        bool wrote{};
        logMessage("hpbsd_smu",std::format("Begin baseline_mW={}/{}/{} target_mW={}/{}/{}; FAN/EC writes=0",baseline[0],baseline[1],baseline[2],target[0],target[1],target[2]));
        try{
            // Raise Fast/Slow before sustained; lower sustained before Fast.
            // Every prefix preserves STAPM<=Slow<=Fast, including rollback.
            for(int i:{2,1,0})if(target[i]>baseline[i]){wrote=true;send(i,target[i]);}
            for(int i:{0,1,2})if(target[i]<baseline[i]){wrote=true;send(i,target[i]);}
            const auto first=fields(snapshot()),second=fields(snapshot());
            for(int i=0;i<3;++i){
                if(std::abs(first[i]-second[i])>.03)throw std::runtime_error("HPBSD: unstable PM limit response");
                if(std::abs(second[i]-target[i]/1000.)>.05)throw std::runtime_error("HPBSD: MP1/PM limits differ; no guessed scaling");
            }
            // Exact1:1 mW was verified on BIOS1.06 with both20/25/30 and8/10/12W.
            // Do not import the Honor compensation or add speculative coefficients.
            ApuPowerDiagnostic result;
            for(int pass=0;pass<2;++pass){result=snapshot();const auto actual=fields(result);for(int i=0;i<3;++i)if(std::abs(actual[i]-target[i]/1000.)>.05)throw std::runtime_error("HPBSD: desired limits not confirmed");}
            logMessage("hpbsd_smu","Confirmed by repeated PM reads; exact1:1 mW, no compensation");
            return result;
        }catch(const std::exception& error){
            const std::string reason=error.what();std::string rollback="no writes";
            if(wrote)try{
                // Same exact1:1 HPBSD mailbox path restores the saved factory mW.
                retryAt=0;const auto current=fields(read(true));
                for(int i:{2,1,0})if(before[i]>current[i]+.05){prepare();send(i,baseline[i]);}
                for(int i:{0,1,2})if(before[i]<current[i]-.05){prepare();send(i,baseline[i]);}
                for(int pass=0;pass<2;++pass){const auto actual=fields(snapshot());for(int i=0;i<3;++i)if(std::abs(actual[i]-before[i])>.05)throw std::runtime_error("baseline readback mismatch");}
                rollback="baseline confirmed twice";
            }catch(const std::exception& failure){rollback=std::string("UNCONFIRMED: ")+failure.what();}
            logMessage("hpbsd_smu",reason+"; rollback="+rollback);
            throw std::runtime_error(reason+"; rollback="+rollback);
        }
    }
    ApuPowerDiagnostic applyHlylLimitsMilliwatts(const std::array<int,3>& milliwatts) {
        const std::array<double,3> limits{milliwatts[0]/1000.0,milliwatts[1]/1000.0,milliwatts[2]/1000.0};
        int cpuid[4]{};__cpuid(cpuid,1);
        const int family=((cpuid[0]>>8)&15)+((cpuid[0]>>20)&255);
        const int model=((cpuid[0]>>4)&15)|((cpuid[0]>>12)&240);
        if(deviceProfile(readBoardDescriptor()).id!="huawei-hlyl-wxx9"||family!=0x17||model!=0x60)
            throw std::runtime_error("SMU: запись допускается только на HLYL-WXX9/Renoir");
        SYSTEM_POWER_STATUS power{};
        if(!GetSystemPowerStatus(&power)||power.ACLineStatus>1)throw std::runtime_error("SMU: источник питания неизвестен");
        const std::array<int,3> maximum=power.ACLineStatus?std::array<int,3>{45,45,54}:std::array<int,3>{30,30,35};
        for(int i=0;i<3;++i)if(limits[i]<5||limits[i]>maximum[i])
            throw std::invalid_argument("Honor: лимиты STAPM/Slow/Fast 5–45/45/54 Вт от сети, 5–30/30/35 Вт от батареи");
        if(limits[0]>limits[1]||limits[1]>limits[2])throw std::invalid_argument("Honor: Длительно ≤ Средняя ≤ Пик");
        // Preflight validates the signed runtime and supported PM table before
        // any setter. Keep Access_PCI over the complete transaction/readback.
        auto before=read(true);
        if(before.stale||before.tableVersion<0x370000||before.tableVersion>0x370005||
           !before.stapmLimitWatts||!before.slowLimitWatts||!before.fastLimitWatts)
            throw std::runtime_error("SMU: перед записью нужны актуальные лимиты PM37xxxx");
        const auto wait=WaitForSingleObject(pci,1000);
        if(wait!=WAIT_OBJECT_0&&wait!=WAIT_ABANDONED)throw std::runtime_error("SMU: PCI занят");
        struct Unlock {HANDLE handle;~Unlock(){ReleaseMutex(handle);}} unlock{pci};
        // Another monitor/profile controller could change SMU after preflight.
        // Re-read the rollback baseline while holding the shared PCI mutex.
        retryAt=0;before=read(true);
        if(before.stale||!before.stapmLimitWatts||!before.slowLimitWatts||!before.fastLimitWatts)
            throw std::runtime_error("SMU: не удалось получить актуальные лимиты перед записью");
        const std::array<double,3> previous{*before.stapmLimitWatts,*before.slowLimitWatts,*before.fastLimitWatts};
        // Do not start a transaction that our bounded rollback cannot restore.
        // PM floats report e.g.54 W as54.0000038. Validate the exact integer
        // milliwatts we can restore, rather than rejecting float noise at54.
        for(const auto value:previous){const auto restoredMilliwatts=std::lround(value*1000);
            if(restoredMilliwatts<5000||restoredMilliwatts>54000)
                throw std::runtime_error("SMU: прежние лимиты вне диапазона восстановления 5–54 Вт; запись отменена");}
        constexpr std::array<std::uint32_t,3> commands{0x14,0x16,0x15};
        logMessage("huawei_smu",std::format("Limits requested STAPM/Slow/Fast={}/{}/{} W; MP1; fan writes=0",limits[0],limits[1],limits[2]));
        bool wrote=false;double observedScale=0;
        try{
            // HLYL can report exactly90% of the MP1 argument. Do not assume
            // that coefficient at startup: require two matching live PM reads
            // for ALL three limits before ONE bounded correction. No timer
            // enforcement, no thermal/STT command and no signed-module patch.
            for(int phase=0;phase<2;++phase){
                SYSTEM_POWER_STATUS currentPower{};
                if(!GetSystemPowerStatus(&currentPower)||currentPower.ACLineStatus!=power.ACLineStatus)
                    throw std::runtime_error("SMU: питание изменилось во время применения");
                const double scale=phase?observedScale:1;
                for(int i=0;i<3;++i){wrote=true;mp1Limit(commands[i],static_cast<std::uint32_t>(std::lround(milliwatts[i]/scale)));}
                int exactReads=0,scaledReads=0;
                for(int attempt=0;attempt<6;++attempt){
                    Sleep(150);retryAt=0;const auto actual=read(true);
                    if(!actual.stale&&actual.stapmLimitWatts&&actual.slowLimitWatts&&actual.fastLimitWatts){
                        const std::array<double,3> measured{*actual.stapmLimitWatts,*actual.slowLimitWatts,*actual.fastLimitWatts};
                        bool exact=true,scaled=true;
                        for(int i=0;i<3;++i){exact&=std::abs(measured[i]-limits[i])<0.05;scaled&=std::abs(measured[i]-limits[i]*0.9)<0.05;}
                        logMessage("huawei_smu",std::format("PM readback phase={} STAPM/Slow/Fast={}/{}/{} W",phase,measured[0],measured[1],measured[2]));
                        exactReads=exact?exactReads+1:0;scaledReads=scaled?scaledReads+1:0;
                        if(exactReads>=2){if(!phase)observedScale=1;logMessage("huawei_smu","Limits confirmed by two PM table reads");return actual;}
                        if(!phase&&scaledReads>=2){observedScale=0.9;break;}
                    }
                    // Transport failure resets the module. Never call through
                    // a stale execute pointer, or repeat writes in a loop.
                    if(!module)break;
                }
                if(phase||observedScale!=0.9||!module)break;
            }
            throw std::runtime_error("SMU: прошивка не подтвердила заданные лимиты; возвращаю прежние");
        }catch(const std::exception& failure){
            const std::string cause=failure.what();
            std::string rollbackStatus="предыдущие лимиты не изменены";
            if(wrote)try{
                // Reusing a PM float as an MP1 argument caused a second90%
                // reduction. Restore through the coefficient proven in THIS
                // transaction; unknown mappings are reported, never guessed.
                if(observedScale==0)throw std::runtime_error("коэффициент MP1/PM не подтверждён; повторная запись отменена");
                SYSTEM_POWER_STATUS currentPower{};
                if(!GetSystemPowerStatus(&currentPower)||currentPower.ACLineStatus!=power.ACLineStatus)
                    throw std::runtime_error("питание изменилось; откат через SMU отменён");
                prepare();
                for(int i=0;i<3;++i)mp1Limit(commands[i],static_cast<std::uint32_t>(std::lround(previous[i]*1000/observedScale)));
                retryAt=0;const auto restored=read(true);
                if(restored.stale||!restored.stapmLimitWatts||!restored.slowLimitWatts||!restored.fastLimitWatts||
                   std::abs(*restored.stapmLimitWatts-previous[0])>=0.6||std::abs(*restored.slowLimitWatts-previous[1])>=0.6||std::abs(*restored.fastLimitWatts-previous[2])>=0.6)
                    throw std::runtime_error("прежние значения не подтверждены");
                logMessage("huawei_smu","Limits rollback confirmed");
                rollbackStatus="прежние лимиты восстановлены и подтверждены";
            }catch(const std::exception& error){
                rollbackStatus=std::string("откат не подтверждён, фактическое состояние неизвестно: ")+error.what();
                logMessage("huawei_smu",std::string("Limits rollback failed; state unknown: ")+error.what());
            }
            throw std::runtime_error(cause+"; "+rollbackStatus);
        }
    }
    ApuPowerDiagnostic read(bool verifiedBoard) {
        if (!verifiedBoard) return {{}, {}, {}, {}, 0, false, "SMU: плата не подтверждена"};
        if (GetTickCount64() < retryAt) return cached;
        bool acquired{};
        try {
            if (!pci) pci = openSharedHardwareMutex(kPciMutexName);
            if (!pci) throw std::runtime_error("SMU: mutex PCI недоступен, Win32="+std::to_string(GetLastError()));
            const auto wait = WaitForSingleObject(pci, 250);
            acquired = wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED;
            if (!acquired) {
                if(wait!=WAIT_TIMEOUT)throw std::runtime_error("SMU: ошибка ожидания mutex PCI, Win32="+std::to_string(GetLastError()));
                auto result = cached; result.stale = true; result.status = "SMU: доступ PCI занят";
                return result;
            }
            prepare();
            std::uint64_t info[2]{};
            command("ioctl_resolve_pm_table", info, 2);
            // These are READ-ONLY SMU telemetry values, not EC mode flags/setters.
            // Strix Point and the listed Renoir layouts share these float indices:
            // 0 STAPM limit, 2 Fast limit, 3 Fast-window APU power, 4 Slow limit.
            // APU includes the integrated GPU: never add GPU watts to APU watts.
            // Keep the version guard; another table version may reuse these offsets.
            const auto id=deviceProfile(readBoardDescriptor()).id;
            int cpuid[4]{};__cpuid(cpuid,1);
            const int family=((cpuid[0]>>8)&15)+((cpuid[0]>>20)&255);
            const int model=((cpuid[0]>>4)&15)|((cpuid[0]>>12)&240);
            const bool strix=id=="wu-jie-15-xpro"&&info[0]==0x5D0009;
            // RyzenAdj pinned 5775fc3, api.c: PM sizes + get_{stapm,fast,slow}.
            // Do not admit other versions just because their first floats look plausible.
            const bool renoir=id=="huawei-hlyl-wxx9"&&family==0x17&&model==0x60&&info[0]>=0x370000&&info[0]<=0x370005;
            // RyzenAdj api.c explicitly lists Phoenix/Hawk Point PM4C0006..9;
            // the first STAPM/Fast/Slow fields retain offsets0/8/10h.
            const bool hpbsd=HpbsdTelemetryReader::supportedHost()&&family==0x19&&model==0x75&&info[0]>=0x4C0006&&info[0]<=0x4C0009;
            if (!strix&&!renoir&&!hpbsd)
                throw std::runtime_error(std::format("SMU: unsupported PM table 0x{:X}",info[0]));
            updateTable(static_cast<std::uint32_t>(info[0])); // Telemetry transfer, not a limit setter.
            std::array<std::uint64_t, 4> raw{};
            command("ioctl_read_pm_table", raw.data(), raw.size());
            std::array<float, 8> values{};
            std::memcpy(values.data(), raw.data(), sizeof(raw));
            for (int index : {0, 2, 3, 4})
                if (!std::isfinite(values[index]) || values[index] < 0 || values[index] > 500)
                    throw std::runtime_error("SMU: недействительное значение таблицы");
            if (!values[0] || !values[2] || !values[4])
                throw std::runtime_error("SMU: лимиты ещё не доступны");
            cached = {values[3], values[0], values[2], values[4],
                      static_cast<std::uint32_t>(info[0]), false, "PawnIO / SMU"};
            ReleaseMutex(pci);
            return cached;
        } catch (const std::exception& error) {
            if (acquired) ReleaseMutex(pci);
            reset(); retryAt = GetTickCount64() + 30000;
            cached.stale = true; cached.status = error.what();
            return cached;
        }
    }
};

ApuPowerReader::ApuPowerReader() : impl_(std::make_unique<Impl>()) {}
ApuPowerReader::~ApuPowerReader() = default;
ApuPowerDiagnostic ApuPowerReader::read(bool verifiedBoard) { return impl_->read(verifiedBoard); }
ApuPowerDiagnostic ApuPowerReader::applyHlylLimits(const std::array<int,3>& values) {
    std::array<int,3> milliwatts{};
    for(int i=0;i<3;++i){if(values[i]<5||values[i]>54)throw std::invalid_argument("Honor: лимиты вне диапазона5–54 Вт");milliwatts[i]=values[i]*1000;}
    return impl_->applyHlylLimitsMilliwatts(milliwatts);
}
ApuPowerDiagnostic ApuPowerReader::applyHlylLimitsMilliwatts(const std::array<int,3>& values) { return impl_->applyHlylLimitsMilliwatts(values); }
ApuPowerDiagnostic ApuPowerReader::applyHpbsdLimitsMilliwatts(const std::array<int,3>& values) { return impl_->applyHpbsdLimitsMilliwatts(values); }
ApuPowerDiagnostic ApuPowerReader::applyHpbsdStockProfile(int profile) {
    const auto target=hpbsd::stockPower(profile);
    SYSTEM_POWER_STATUS supply{};if(!GetSystemPowerStatus(&supply)||supply.ACLineStatus!=1)throw std::runtime_error("HPBSD: stock power requires confirmed external supply");
    return impl_->applyHpbsdLimitsMilliwatts(target,true);
}
}
