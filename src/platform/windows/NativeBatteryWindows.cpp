#include "MagicControl/ResourceControl.h"
#include "NativeBatteryWindows.h"
#include "BackendWindows.h"
#include "uniwill-acpi/ReviewedDriverWindows.h"
#include "BatteryEnforcementWindows.h"
#include "UniwillControlOwnerWindows.h"
#include "../../devices/eluktronics/hydroc-16-g2/DiagnosticsWindows.h"
#include "../../protocols/uniwill/BatteryProtection.h"
#include "../../protocols/uniwill/gm6ir0c/Registers.h"
#include "../../protocols/uniwill/hydroc16g2/Battery.h"
#include "MagicControl/BoardIdentity.h"
#include "MagicControl/DeviceProfile.h"
#include "MagicControl/DeviceOperation.h"
#include "MagicControl/IEcBackend.h"
#include "MagicControl/Logging.h"
#include "MagicControl/ActionLog.h"
#include "MagicControl/RomIdHistory.h"
#include "MagicControl/PortableSettings.h"
#include "MagicControl/EcByteEditor.h"
#include <windows.h>
#include <tlhelp32.h>
#include <condition_variable>
#include <format>
#include <mutex>
#include <thread>
#include <stdexcept>
#include <exception>
#include <array>

namespace mc::windows {
namespace {
bool transientReadFailure(const std::exception& error){
    if(dynamic_cast<const WindowsEcNotReady*>(&error)||dynamic_cast<const EcObservationBusy*>(&error))return true;
    if(const auto* io=dynamic_cast<const EcIoError*>(&error)){
        switch(io->nativeCode()){
        case ERROR_NOT_READY:case ERROR_DEVICE_NOT_CONNECTED:case ERROR_BUSY:case ERROR_SEM_TIMEOUT:return true;
        default:break;
        }
    }
    return false;
}
struct Transaction{IEcBackend& ec;explicit Transaction(IEcBackend& b):ec(b){ec.lockTransaction();}~Transaction(){ec.unlockTransaction();}};
// Independently recovered exact GM6 and HYDROC codecs share this mechanism.
// No Wujie platform4/7 activation protocol or inferred profile percentages.
static_assert(gm6::batteryUpper==hydroc16g2::batteryUpper&&gm6::batteryRestart==hydroc16g2::batteryRestart&&gm6::batteryThresholdMask==hydroc16g2::batteryThresholdMask);
constexpr auto experimentalUpper=gm6::batteryUpper,experimentalRestart=gm6::batteryRestart;
constexpr auto thresholdMask=gm6::batteryThresholdMask;
struct ThresholdPair {std::uint8_t upper,restart;};
using ThresholdContext=std::array<std::uint8_t,3>;
}
struct NativeBatteryController::Impl {
    const bool wujie=deviceProfile(readBoardDescriptor()).id=="wu-jie-15-xpro";
    const bool hydroc=deviceProfile(readBoardDescriptor()).id=="eluktronics-hydroc16g2";
    const bool gm5=gm5Profile(deviceProfile(readBoardDescriptor()).id);
    const bool l16=l16Profile(deviceProfile(readBoardDescriptor()).id);
    const bool gm6=deviceProfile(readBoardDescriptor()).id=="mechrevo-gm6ir0c";
    const bool dbdBattery=gm5||l16; //7D0 is OEM DBD1, never a restart percentage.
    const std::string id=hydroc?"eluktronics-hydroc16g2":std::string(deviceProfile(readBoardDescriptor()).id);
    const std::filesystem::path settings=portableSettingsPath(wujie?"device-settings.ini":hydroc?"hydroc16g2-device-settings.ini":std::string(gmSettings(id)));
    std::unique_ptr<IEcBackend> ec;bool thresholdTransport{};
    using Clock=std::chrono::steady_clock;
    std::mutex mutex;std::condition_variable wake;bool pending{true},restoreCanRetry{},suppressNativeRestore{};
    unsigned restoreAttempts{};
    Clock::time_point restoreDeadline{},nextRestore{};
    std::string lastRead,lastThresholdLog,lastChargerLog,lastChargerError,restoreStatus,restoreReason{"startup"};mc::ResourceActor worker;
    std::optional<std::array<int,3>> lastVoltage;std::uint64_t lastVoltageAt{};
    std::string lastVoltageLog;
    void retainVoltage(NativeBatteryState& s)const{
        if(!s.baseVoltageMv&&lastVoltage){s.baseVoltageMv=(*lastVoltage)[0];s.targetVoltageMv=(*lastVoltage)[1];
            s.cellCount=(*lastVoltage)[2];s.voltageAtMs=lastVoltageAt;s.voltageRetained=true;s.voltageOnline=false;}
    }
    Impl():worker([this](std::stop_token stop){
        std::unique_lock lock(mutex);
        while(!stop.stop_requested()){
            wake.wait_for(lock,std::chrono::seconds(1),[&]{return stop.stop_requested();});
            if(stop.stop_requested())break;
            if(!pending||Clock::now()<nextRestore)continue;
            if(restoreAttempts&&Clock::now()>=restoreDeadline){pending=false;restoreStatus="Время восстановления защиты истекло; примените режим явно";logMessage("battery",id+" "+restoreStatus);continue;}
            auto operation=deviceOperations().tryBegin(DeviceOperationKind::Background);
            if(!operation)continue;
            // Waiting behind another explicit operation is admission delay,
            // not a failed EC attempt. Start the bounded retry window here.
            if(!restoreAttempts)restoreDeadline=Clock::now()+std::chrono::seconds(30);
            pending=false;
            ++restoreAttempts;restoreCanRetry=false;
            try{
                IdentityHistoryScope historyOrigin("automatic",restoreReason);
                if(!std::filesystem::exists(settingsReadPath(settings)))continue;
                DevicePreferences saved;std::string error;
                if(!loadDevicePreferences(settings,saved,error))throw std::runtime_error(error);
                if(wujie){
                    if(saved[DeviceSetting::BatteryPercentageEnabled]==0)
                        if(const auto mode=saved[DeviceSetting::BatteryProtection])apply(*mode,false);
                    restoreStatus.clear();continue; // Never infer or restore native IDs.
                }
                if(saved[DeviceSetting::ChargeAuto]==2&&!nativeEcPolicyProfile(id))
                    throw std::runtime_error("Imported7/FF policy is not admitted for this model; no restore writes");
                if(l16&&(saved[DeviceSetting::ChargeAuto]||saved[DeviceSetting::ChargeRestart]))
                    throw std::runtime_error("L16: imported platform4/7 or DBD1 restart preference; no battery restore writes");
                // No saved explicit choice means absolutely no startup write.
                bool priorPolicy=false;
                const bool percentageRestore=saved[DeviceSetting::BatteryPercentageEnabled]!=0;
                if(!suppressNativeRestore&&saved[DeviceSetting::BatteryPercentageEnabled]!=1)
                    if(const auto mode=saved[DeviceSetting::BatteryProtection]){apply(*mode,false);priorPolicy=true;}
                bool restoredPair=false;
                if(percentageRestore)if(const auto upper=saved[DeviceSetting::ChargeLimit]){
                    // Wake may precede EC readiness. Retry only a transient read
                    // before any write; writeThreshold consumes this permission.
                    restoreCanRetry=!priorPolicy;
                    try{applyExperimental(*upper,gm6?saved[DeviceSetting::ChargeRestart].value_or(-1):-1);}
                    catch(...){if(priorPolicy)restoreCanRetry=false;throw;}
                    rememberThresholds();priorPolicy=true;restoredPair=!dbdBattery;
                }
                if(const auto restart=saved[DeviceSetting::ChargeRestart];restart&&!restoredPair&&percentageRestore){
                    restoreCanRetry=!priorPolicy;
                    try{applyRestart(*restart,false);}catch(...){if(priorPolicy)restoreCanRetry=false;throw;}
                }
                restoreStatus.clear();
            }catch(const std::exception& e){
                // Retry only not-ready/busy reads BEFORE any write was sent.
                // Ownership, identity/pin, invalid settings and uncertain write
                // outcomes are terminal; never reclaim the EC from an OEM owner.
                pending=restoreCanRetry&&transientReadFailure(e)&&restoreAttempts<5&&Clock::now()<restoreDeadline;
                nextRestore=Clock::now()+std::chrono::seconds(1u<<(restoreAttempts-1));
                restoreStatus=pending?"Восстановление защиты ожидает готовности EC":"Защита не восстановлена: "+std::string(e.what());
                logMessage("battery",std::format("{} restore failed attempt={}/5 retry={} before_any_write={} reason={}",id,restoreAttempts,pending,restoreCanRetry,e.what()));
            }
        }
    }){}
    ~Impl(){worker.request_stop();wake.notify_all();if(worker.joinable())worker.join();}
    std::uint8_t stable(std::uint16_t address){
        auto previous=ec->read(address);
        for(int i=0;i<3;++i){const auto actual=ec->read(address);if(actual==previous){
            if(address==0x770)observeRomId(actual,"native_battery_stable_read");
            else if(address==0x7C3)observePlatformId(actual,"native_battery_stable_read");
            return actual;
        }previous=actual;}
        throw std::runtime_error("Нестабильное чтение EC батареи");
    }
    void identity(){
        const auto board=readBoardDescriptor();
        if(wujie){
            if(deviceProfile(board).id!=id||board.manufacturer!="MECHREVO"||board.baseBoard!="WUJIE Series-X5SP4NAG")
                throw std::runtime_error("WUJIE: точная модель платы не совпадает");
            return;
        }
        if(deviceProfile(board).id!=id||(!l16&&
            (hydroc?(board.manufacturer!="Eluktronics Inc."||board.baseBoard!="HYDROC-16"||board.productName!="HYDROC-16 G2"):
                    (board.manufacturer!="MECHREVO"||board.baseBoard!=(gm5?"GM5HG0A":"GM6IR0C")||board.productName!=(gm5?"Yilong15Pro Series GM5HG0A":"JiguangX Series GM6IR0C")))))
            throw std::runtime_error("Battery policy: точная модель платы не совпадает");
        if(l16)reviewed_uniwill::verifyL16Identity();
        if(!deviceProfile(board).capabilities.supports(DeviceFeature::BatteryCharge))
            throw std::runtime_error("Battery policy: функция не разрешена профилем платы");
    }
    void open(bool writing,bool thresholdTrial=false){
        identity();
        // Recheck the installed HYDROC image before each write, not merely a
        // cached package/version string. No service/driver is installed here.
        if(gm5&&(!ec||writing)){std::string proof;reviewed_uniwill::verifyGm5Driver(proof);}
        if(l16&&(!ec||writing)){std::string proof;reviewed_uniwill::verifyL16Driver(proof);}
        if(hydroc&&(!ec||writing)){std::string proof;verifyHydrocDriver(proof);logMessage("battery",proof);}
        if(!ec||(hydroc&&writing&&thresholdTransport!=thresholdTrial)){
            if(hydroc){ec=thresholdTrial?createWindowsAcpiBatteryThresholdBackend():createWindowsAcpiBatteryBackend();thresholdTransport=thresholdTrial;}
            else ec=wujie?createWindowsAcpiBatteryBackend():createWindowsAcpiDriverBackend();
        }
    }
    void guard(){
        if(wujie){identity();if(stable(0x454)!=82||stable(0x456)!=72||stable(0x740)!=26)
            throw std::runtime_error("WUJIE: EC identity82/72/26 не подтверждена");return;}
        if(l16){identity();reviewed_uniwill::verifyL16EcIdentity([&](auto a){return stable(a);});
            if(stable(0x7C3)!=6)throw std::runtime_error("L16: native battery platform6 changed; no policy write");}
        if(!(hydroc?stable(0x740)==26:gmRamProject(id,stable(0x740)))||!(stable(0x78E)&0x40))
            throw std::runtime_error("Battery policy: ProjectID/RAM1.5 capability не совпадает");
    }
    ThresholdPair thresholds(){return {stable(experimentalUpper),dbdBattery?std::uint8_t{0}:stable(experimentalRestart)};}
    ThresholdContext thresholdContext(){return {stable(0x7A6),stable(0x7C3),stable(0x770)};}
    static bool sameThresholds(const ThresholdPair& a,const ThresholdPair& b){return (a.upper&thresholdMask)==(b.upper&thresholdMask)&&(a.restart&thresholdMask)==(b.restart&thresholdMask);}
    void experimentalGuard(const ThresholdContext& context){
        open(true,true);guard();
        const auto owner=uniwillControlOwner();if(!owner.empty())throw std::runtime_error("Управление занято: "+owner);
        if(thresholdContext()!=context)throw std::runtime_error("Контекст7A6/7C3/770 изменён; процентная запись/откат отменены");
    }
    void writeThreshold(std::uint16_t address,std::uint8_t low,const ThresholdPair& expected,const ThresholdContext& context,bool* writeAttempted=nullptr){
        experimentalGuard(context);
        if(dbdBattery&&address==experimentalRestart)throw std::runtime_error("AMD board:7D0 is DBD1, not a charge restart setting");
        if(!sameThresholds(thresholds(),expected))throw std::runtime_error("Пара порогов изменилась извне; запись отменена");
        // Bit7 can change independently of the configured low7. Read it again
        // immediately before the write, including on the rollback path.
        const auto current=stable(address);
        const auto expectedLow=(address==experimentalUpper?expected.upper:expected.restart)&thresholdMask;
        if((current&thresholdMask)!=expectedLow)throw std::runtime_error("Порог изменён перед RMW; запись отменена");
        const auto value=static_cast<std::uint8_t>((current&~thresholdMask)|(low&thresholdMask));
        if(current!=value){restoreCanRetry=false;if(writeAttempted)*writeAttempted=true;ec->write(address,value);}
        if((stable(address)&thresholdMask)!=(low&thresholdMask))throw std::runtime_error("EC не подтвердил low7 порога");
        experimentalGuard(context);
    }
    void applyExperimental(int upper,int restart=-1,bool lowerOnly=false){
        // Keep the existing lower byte when using 10 or the new 60 button.
        // Its actual restart semantics are unverified; changing the upper
        // must not silently overwrite the user's lower threshold.
        const bool preserveRestart=!dbdBattery&&(upper==10||upper==60)&&restart<0&&!lowerOnly;
        if(dbdBattery){if(lowerOnly)throw std::runtime_error("AMD board: charge restart register not established; DBD1 is not written");restart=0;}
        else if(restart<0&&!preserveRestart)restart=selectedChargeRestart(id,80);
        if(upper<1||upper>100||(!lowerOnly&&upper!=100&&upper!=95&&upper!=85&&upper!=60&&upper!=10))throw std::invalid_argument("Эксперимент допускает100/95/85/60/10%");
        if(!dbdBattery&&!preserveRestart&&(restart<1||restart>95||(upper!=10&&(upper!=60||lowerOnly)&&restart>=upper)))throw std::invalid_argument("Нижний порог должен быть1–95% и ниже верхнего");
        const auto owner=uniwillControlOwner();if(!owner.empty())throw std::runtime_error("Управление занято: "+owner);
        open(true,true);Transaction transaction(*ec);guard();
        const auto context=thresholdContext();experimentalGuard(context);
        const auto before=thresholds();
        if(preserveRestart)restart=before.restart&thresholdMask;
        if(lowerOnly&&upper!=((before.upper&thresholdMask)?(before.upper&thresholdMask):100))
            throw std::runtime_error("Верхний порог изменился перед применением нижнего");
        if((before.upper&thresholdMask)>100||(before.restart&thresholdMask)>95)
            throw std::runtime_error("Исходные low7 upper>100 или restart>95; безопасный откат неизвестен");
        const ThresholdPair target{static_cast<std::uint8_t>(lowerOnly?(before.upper&thresholdMask):(upper==100?0:upper)),static_cast<std::uint8_t>(restart)};
        bool upperAttempted=false,restartAttempted=false;
        std::unique_ptr<batteryEnforcement::Change> romPolicy;
        std::uint8_t confirmedRom=context[2];
        logMessage("battery",std::format("{} EXPERIMENT begin upper={} restart={} raw_before upper=0x{:02X} restart=0x{:02X} context7A6=0x{:02X} 7C3=0x{:02X} 770=0x{:02X}; platform preserved, automatic owned P7 ROM policy",id,upper,restart,before.upper,before.restart,context[0],context[1],context[2]));
        try{
            // Restart FIRST: lowering upper to85 must not temporarily retain an
            // older restart95. No native-mode policy is changed.
            if((before.restart&thresholdMask)!=restart)writeThreshold(experimentalRestart,target.restart,before,context,&restartAttempted);
            auto expected=before;expected.restart=target.restart;
            if((before.upper&thresholdMask)!=target.upper)writeThreshold(experimentalUpper,target.upper,expected,context,&upperAttempted);
            experimentalGuard(context);const auto after=thresholds();
            if(!sameThresholds(after,target))throw std::runtime_error("Пара upper/restart не подтверждена чтением");
            experimentalGuard(context);
            if(!lowerOnly&&context[1]==7&&nativeEcPolicyProfile(id)){
                const auto choice=*batteryPolicyForChargeLimit(7,upper);
                romPolicy=std::make_unique<batteryEnforcement::Change>([&](auto a){return stable(a);},
                    [&](auto a,auto mask,auto value){policyGuard();const auto current=stable(a);restoreCanRetry=false;
                        ec->write(a,static_cast<std::uint8_t>((current&~mask)|(value&mask)));},policyOwnerPath(),id,choice);
                romPolicy->apply();policyGuard();confirmedRom=stable(0x770);
                if(stable(0x7c3)!=7||confirmedRom!=batteryPolicyRom(choice)||stable(0x7a6)!=context[0]||!sameThresholds(thresholds(),target))
                    throw std::runtime_error("P7 ROM policy or preserved battery fields not confirmed");
                romPolicy->commit();
            }
            logMessage("battery",std::format("{} EXPERIMENT configured_confirmed upper={} restart={} raw_before=0x{:02X}/0x{:02X} raw_after=0x{:02X}/0x{:02X} platform_preserved={} rom_after=0x{:02X} enforcement_unverified=true",id,upper,restart,before.upper,before.restart,after.upper,after.restart,context[1],confirmedRom));
        }catch(const std::exception& error){
            const std::string reason=error.what();
            try{const auto observed=thresholds();const auto now=thresholdContext();
                logMessage("battery",std::format("{} EXPERIMENT failed raw_after=0x{:02X}/0x{:02X} context_after7A6=0x{:02X} 7C3=0x{:02X} 770=0x{:02X} upper_write_attempted={} restart_write_attempted={} enforcement_unverified=true error={}",id,observed.upper,observed.restart,now[0],now[1],now[2],upperAttempted,restartAttempted,reason));
            }catch(const std::exception& observe){logMessage("battery",id+" EXPERIMENT failed; raw_after unavailable: "+observe.what()+"; original="+reason);}
            if(romPolicy){
                try{policyGuard();romPolicy->rollback();}
                catch(const std::exception& rollback){throw std::runtime_error(reason+"; ROM-ID rollback unconfirmed: "+rollback.what());}
            }
            if(!upperAttempted&&!restartAttempted)throw;
            std::string rollback;
            try{
                experimentalGuard(context);auto current=thresholds();
                const auto oursOrOriginal=[&](const ThresholdPair& pair){return
                    ((pair.upper&thresholdMask)==(before.upper&thresholdMask)||(upperAttempted&&(pair.upper&thresholdMask)==target.upper))&&
                    ((pair.restart&thresholdMask)==(before.restart&thresholdMask)||(restartAttempted&&(pair.restart&thresholdMask)==target.restart));};
                // The shared transaction serializes our callers, not arbitrary
                // firmware/OEM actors. A foreign component blocks BOTH restores.
                if(!oursOrOriginal(current))throw std::runtime_error(std::format("Чужое значение в паре raw=0x{:02X}/0x{:02X}; оба rollback writes отменены",current.upper,current.restart));
                // Reverse order: upper FIRST, then restart; every write rechecks
                // the whole expected pair, context, owner and fresh high bit.
                if((current.upper&thresholdMask)!=(before.upper&thresholdMask)){
                    writeThreshold(experimentalUpper,before.upper&thresholdMask,current,context);current.upper=before.upper;
                }
                current=thresholds();if(!oursOrOriginal(current))throw std::runtime_error("Пара изменена во время отката; restart не записан");
                if((current.restart&thresholdMask)!=(before.restart&thresholdMask))writeThreshold(experimentalRestart,before.restart&thresholdMask,current,context);
                experimentalGuard(context);const auto restored=thresholds();
                if(!sameThresholds(restored,before))throw std::runtime_error("Парный rollback readback не совпадает");
                experimentalGuard(context);
                rollback=std::format("исходная пара восстановлена raw=0x{:02X}/0x{:02X}",restored.upper,restored.restart);
                logMessage("battery",id+" EXPERIMENT rollback_confirmed: "+rollback);
            }catch(const std::exception& restore){rollback=std::string("откат не подтверждён, фактическая пара неизвестна: ")+restore.what();logMessage("battery",id+" EXPERIMENT rollback_unknown: "+rollback);}
            throw std::runtime_error(reason+"; "+rollback);
        }
    }
    void applyRestart(int percent,bool save){
        if(dbdBattery)throw std::runtime_error("AMD board:7D0 belongs to DBD1; lower threshold is unavailable");
        open(true,true);Transaction transaction(*ec);guard();const auto before=thresholds();
        const int upper=(before.upper&thresholdMask)?before.upper&thresholdMask:100;
        applyExperimental(upper,percent,true);
        if(save){if(gm6)rememberThresholds();else rememberChargeRestart(id,percent);}
    }
    void rememberThresholds(){
        Transaction transaction(*ec);policyGuard();
        const auto pair=thresholds();const auto context=thresholdContext();
        DevicePreferences patch;
        const int upper=pair.upper&thresholdMask;
        patch[DeviceSetting::ChargeLimit]=upper?upper:100;
        if(pair.restart&thresholdMask)patch[DeviceSetting::ChargeRestart]=pair.restart&thresholdMask;
        patch[DeviceSetting::BatteryCardView]=1;
        patch[DeviceSetting::BatteryPercentageEnabled]=1;
        if(context[1]==4&&context[2]==255)patch[DeviceSetting::ChargeAuto]=0;
        else if(nativeEcPolicyProfile(id)&&context[1]==7&&context[2]==255)patch[DeviceSetting::ChargeAuto]=2;
        else if(context[1]==7&&context[2]==4){
            batteryEnforcement::requireContext([&](auto a){return stable(a);},policyOwnerPath(),id);
            patch[DeviceSetting::ChargeAuto]=1;
        }
        std::string error;if(!rememberDevicePreferences(patch,error,settings))throw std::runtime_error("Пороги применены, но не сохранены: "+error);
    }
    std::filesystem::path policyOwnerPath()const{return batteryEnforcement::ownerPath(settings.parent_path(),id);}
    void policyGuard(){
        identity();guard();
        const auto owner=uniwillControlOwner();if(!owner.empty())throw std::runtime_error("Управление занято: "+owner);
    }
    void applyEnforcement(BatteryEcPolicy choice,bool save){
        if(l16)throw std::runtime_error("L16: platform6 is preserved; platform4/7 ROM-ID switching is not admitted");
        restoreCanRetry=true;open(true,true);Transaction transaction(*ec);policyGuard();
        const auto upperBefore=stable(experimentalUpper)&thresholdMask;
        const auto lowerBefore=stable(experimentalRestart)&thresholdMask;
        const auto profileBefore=stable(0x7a6);
        batteryEnforcement::Change policy([&](auto a){return stable(a);},
            [&](auto a,auto mask,auto value){
                policyGuard();const auto before=stable(a);
                restoreCanRetry=false;
                ec->write(a,static_cast<std::uint8_t>((before&~mask)|(value&mask)));
            },policyOwnerPath(),id,choice);
        try{
            policy.apply();policyGuard();
            if(stable(0x7c3)!=batteryPolicyPlatform(choice)||stable(0x770)!=batteryPolicyRom(choice)||
               (stable(experimentalUpper)&thresholdMask)!=upperBefore||
               (stable(experimentalRestart)&thresholdMask)!=lowerBefore||stable(0x7a6)!=profileBefore)
                throw std::runtime_error("ПолитикаEC или сохранённые параметры батареи не совпали");
            policy.commit();
            logMessage("battery",std::format("{} EC policy configured platform={} rom=0x{:02X} gate=0x{:02X} upper={} restart={} saved={} fan_writes=0 nvm_writes=0 physical_enforcement=unverified",id,stable(0x7c3),stable(0x770),stable(0x742),upperBefore,lowerBefore,save));
        }catch(...){const auto failure=std::current_exception();
            try{policyGuard();policy.rollback();}
            catch(const std::exception& error){throw std::runtime_error(std::string("ПолитикаEC: откат не подтверждён: ")+error.what());}
            std::rethrow_exception(failure);
        }
        if(save){DevicePreferences patch;patch[DeviceSetting::ChargeAuto]=static_cast<int>(choice);std::string error;
            if(!rememberDevicePreferences(patch,error,settings))throw std::runtime_error("Политика применена, но не сохранена: "+error);}
    }
    void writeMode(int mode){
        guard();
        if(!uniwillControlOwner().empty())throw std::runtime_error("OEM появился перед записью штатного профиля");
        if(!nativeBatteryVoltageContext(id,stable(0x7C3),stable(0x770)))
            throw std::runtime_error("Контекст изменён перед записью; идентификаторы не записывались");
        if(mode!=0&&!(stable(0x741)&1))throw std::runtime_error("APP presence исчезло; профиль не записан");
        // Always fresh RMW, including rollback. Never replay a whole old byte.
        const auto current=stable(uniwill::batteryProtection);
        ec->write(uniwill::batteryProtection,static_cast<std::uint8_t>((current&~uniwill::batteryProtectionMask)|(mode<<4)));
        if((stable(uniwill::batteryProtection)&uniwill::batteryProtectionMask)!=(mode<<4))
            throw std::runtime_error("EC не подтвердил профиль защиты батареи");
    }
    void apply(int mode,bool save){
        restoreCanRetry=true;
        if(!batteryProtectionMode(mode))throw std::invalid_argument("Неизвестный профиль защиты батареи");
        // Check the owner even before a transient device-open failure. An OEM
        // session must terminate the queued restore, not silently keep retrying
        // until that session exits and then reclaim its policy.
        const auto preOwner=uniwillControlOwner();
        if(!preOwner.empty())throw std::runtime_error("Управление занято: "+preOwner+"; восстановление не перехватывает OEM");
        if(!wujie&&nativeEcPolicyProfile(id)){
            open(true,true);bool release{};
            {Transaction transaction(*ec);guard();release=stable(0x7c3)==7&&stable(0x770)==4;}
            if(release)applyExperimental(100); // Same owned ROM/upper off path.
        }
        open(true);Transaction transaction(*ec);guard();
        const auto nativePlatform=stable(0x7C3),nativeRom=stable(0x770);
        if(wujie&&!save&&nativePlatform==7&&nativeRom==4){
            restoreCanRetry=true;
            throw EcObservationBusy("Ожидаю возврата ROM FF общим владельцем настроек батареи");
        }
        if(!nativeBatteryVoltageContext(id,nativePlatform,nativeRom))
            throw std::runtime_error("Текущий штатный контекст не подтверждён; платформа и ROM ID не изменялись");
        if(mode!=0&&!(stable(0x741)&1))throw std::runtime_error("APP presence741.0 отсутствует; штатный профиль будет сброшен прошивкой. Запись отменена");
        const auto owner=uniwillControlOwner();if(!owner.empty())throw std::runtime_error("Управление занято: "+owner+". Завершите родной Control Center перед применением.");
        const auto before=stable(uniwill::batteryProtection);
        const int oldMode=(before&uniwill::batteryProtectionMask)>>4;
        if(!batteryProtectionMode(oldMode))throw std::runtime_error("EC сообщил неизвестную комбинацию защиты; запись отменена");
        bool attempted=false;
        try{
            if(oldMode!=mode){attempted=true;restoreCanRetry=false;writeMode(mode);}
            if(save){DevicePreferences patch;patch[DeviceSetting::BatteryProtection]=mode;std::string error;
                patch[DeviceSetting::BatteryPercentageEnabled]=0;patch[DeviceSetting::BatteryCardView]=0;
                if(!rememberDevicePreferences(patch,error,settings))throw std::runtime_error("Профиль не сохранён: "+error);}
        }catch(...){
            const auto failure=std::current_exception();
            // Preserve typed pre-write failures for bounded restore admission.
            // An uncertain write is terminal and must carry its cleanup result
            // to the caller, rather than hiding it only in a session log.
            if(!attempted)std::rethrow_exception(failure);
            std::string rollback;
            try{
                const int actual=(stable(uniwill::batteryProtection)&uniwill::batteryProtectionMask)>>4;
                if(actual==mode){
                    if(!uniwillControlOwner().empty())throw std::runtime_error("OEM появился во время операции");
                    guard();writeMode(oldMode);rollback="исходный профиль восстановлен и подтверждён чтением";
                }else if(actual==oldMode)rollback="исходный профиль уже прочитан; повторная запись не нужна";
                else throw std::runtime_error("чужая политика mode="+std::to_string(actual)+"; откат не записывался");
                logMessage("battery",id+" rollback confirmed: "+rollback);
            }catch(const std::exception& error){
                rollback=std::string("откат защиты батареи не подтверждён; фактическое состояние неизвестно: ")+error.what();
                logMessage("battery",id+" rollback NOT confirmed: "+rollback);
            }catch(...){
                rollback="откат защиты батареи не подтверждён; фактическое состояние неизвестно: неизвестная ошибка";
                logMessage("battery",id+" rollback NOT confirmed: "+rollback);
            }
            std::string cause="неизвестная ошибка профиля защиты";
            try{std::rethrow_exception(failure);}catch(const std::exception& error){cause=error.what();}catch(...){}
            throw std::runtime_error(cause+"; "+rollback);
        }
        logMessage("battery",std::format("{} OEM protection mode={} raw_before=0x{:02X} mask=0x30 readback=confirmed saved={} fan_writes=0 thresholds_writes=0; physical charge enforcement not measured",id,mode,before,save));
    }
};
NativeBatteryController::NativeBatteryController():impl_(std::make_unique<Impl>()){}
NativeBatteryController::~NativeBatteryController()=default; // No exit/reset writes.
NativeBatteryState NativeBatteryController::read(){
    std::lock_guard lock(impl_->mutex);NativeBatteryState state;
    try{impl_->open(false);Transaction transaction(*impl_->ec);impl_->guard();
        const auto raw=impl_->stable(uniwill::batteryProtection);
        state.appPresent=(impl_->stable(0x741)&1)!=0;
        state.mode=batteryProtectionMode((raw&uniwill::batteryProtectionMask)>>4);
        const auto owner=uniwillControlOwner();state.connected=true;state.writable=state.mode.has_value()&&owner.empty();
        state.projectRaw=impl_->stable(0x740);state.platformRaw=impl_->stable(0x7C3);state.romIdRaw=impl_->stable(0x770);
        state.writable=state.writable&&nativeBatteryVoltageContext(impl_->id,*state.platformRaw,*state.romIdRaw);
        if(nativeOwnedVoltageTransition(impl_->id,*state.platformRaw,*state.romIdRaw,true)&&state.mode&&owner.empty()){
            try{state.voltageTransitionWritable=nativeOwnedVoltageTransition(impl_->id,*state.platformRaw,*state.romIdRaw,
                batteryEnforcement::hasValidOwnerReadOnly(impl_->settings.parent_path(),impl_->id));}
            catch(const std::exception& error){logMessage("battery",std::string("ROM owner check unavailable: ")+error.what());}
        }
        if(impl_->wujie||impl_->gm6)try{
            const auto capture=[&]{return std::array<std::uint8_t,6>{impl_->stable(0x30E),impl_->stable(0x30F),impl_->stable(0x522),impl_->stable(0x523),impl_->stable(0x491),impl_->stable(0x490)};};
            const auto a=capture(),b=capture();
            if(a==b&&b[5]!=255&&!(b[5]&2))impl_->lastVoltage.reset(); // Confirmed removal, not a missed poll.
            if(a==b&&b[5]!=255&&(b[5]&6)==6){const int base=(int(b[0])<<8)|b[1],target=b[2]|(int(b[3])<<8);
                if(base>=5000&&base<=25000&&target>=5000&&target<=25000){
                    state.baseVoltageMv=base;state.targetVoltageMv=target;state.voltageOnline=b[5]&1;
                    state.cellCount=(b[4]&0xC0)==0xC0?4:(b[4]&0xC0)==0x80?3:2;
                    state.voltageAtMs=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();impl_->lastVoltageAt=state.voltageAtMs;
                    impl_->lastVoltage=std::array<int,3>{base,target,*state.cellCount};}
            }
        }catch(const std::exception& error){logMessage("battery",impl_->id+" voltage read unavailable: "+error.what());}
        impl_->retainVoltage(state);
        const auto voltageLine=std::format("{} voltage observation base_mV={} target_mV={} cells={} online={} retained={} measured_voltage=false atomic=false",
            impl_->id,state.baseVoltageMv.value_or(-1),state.targetVoltageMv.value_or(-1),state.cellCount.value_or(-1),state.voltageOnline,state.voltageRetained);
        if(voltageLine!=impl_->lastVoltageLog){impl_->lastVoltageLog=voltageLine;logMessage("battery",voltageLine+" sampled_at_ms="+std::to_string(state.voltageAtMs));}
        state.status=!owner.empty()?"Только чтение: "+owner:state.mode?"Штатный профиль прочитан из EC; процент остановки не установлен":"Неизвестная комбинация защиты EC";
        if(state.voltageTransitionWritable)state.status="Доступен переход 7/04 → 7/FF с проверкой владельца и чтением EC";
        else if(!nativeBatteryVoltageContext(impl_->id,*state.platformRaw,*state.romIdRaw))state.status="Штатный контекст не подтверждён; идентификаторы автоматически не изменяются";
        if(impl_->wujie){
            const auto line=std::format("{} voltage mode={} writable={} context={}/{} app_present={} status={}",impl_->id,state.mode?static_cast<int>(*state.mode):-1,state.writable,*state.platformRaw,*state.romIdRaw,state.appPresent,state.status);
            if(line!=impl_->lastRead){impl_->lastRead=line;logMessage("battery",line);}
            return state; // Existing WUJIE percentage path remains separate.
        }
        try{
            const auto pair=impl_->thresholds();state.upperRaw=pair.upper;if(!impl_->dbdBattery)state.restartRaw=pair.restart;
            const int upper=(pair.upper&thresholdMask)?pair.upper&thresholdMask:100;
            const int restart=pair.restart&thresholdMask;
            const auto context=impl_->thresholdContext();
            state.platformRaw=context[1];state.romIdRaw=context[2];
            try{
                if(!impl_->l16){
                batteryEnforcement::requireContext([&](auto a){return impl_->stable(a);},impl_->policyOwnerPath(),impl_->id);
                state.enforcementWritable=owner.empty();
                if(context[1]==7&&context[2]==4)state.enforcementOn=true;
                else if(context[1]==4&&context[2]==255)state.enforcementOn=false;
                }
            }catch(const std::exception& error){state.status+="; EC policy: "+std::string(error.what());}
            const auto thresholdOwner=uniwillControlOwner();
            state.percentageWritable=owner.empty()&&thresholdOwner.empty()&&(pair.upper&thresholdMask)<=100&&restart<=95;
            if(impl_->dbdBattery){state.configuredUpper=upper;state.status=std::string(gmModelName(impl_->id))+": верхний предел7B9; физическое удержание не проверено;7D0 не записывается";}
            if(!thresholdOwner.empty())state.status="Только чтение: "+thresholdOwner;
            if(restart>0&&restart<=95&&(upper==10||upper==60||restart<upper)&&upper<=100){state.configuredUpper=upper;state.configuredRestart=restart;
                const auto& displayOwner=owner.empty()?thresholdOwner:owner;
                state.status=(!displayOwner.empty()?"Только чтение: "+displayOwner+"; ":"")+std::string("Порог записан; остановка зарядки не проверена.");}
            if(nativeEcPolicyProfile(impl_->id)&&context[1]==7&&context[2]==255)
                state.status=(!owner.empty()?"Только чтение: "+owner+"; ":"")+std::string("7/FF · без процентного ограничения; сохранённый порог не активен");
            const auto line=std::format("{} threshold observation raw_upper=0x{:02X} raw_restart=0x{:02X} configured_upper={} configured_restart={} percentage_writable={} context7A6=0x{:02X} 7C3=0x{:02X} 770=0x{:02X} enforcement_unverified=true",impl_->id,pair.upper,pair.restart,state.configuredUpper.value_or(-1),state.configuredRestart.value_or(-1),state.percentageWritable,context[0],context[1],context[2]);
            if(line!=impl_->lastThresholdLog){logMessage("battery",line);impl_->lastThresholdLog=line;}
        }catch(const std::exception& error){state.percentageWritable=false;state.status+="; Чтение процентных кандидатов: "+std::string(error.what());}
        try{
            const auto portAddress=impl_->hydroc?hydroc16g2::chargerStatus:gm6::chargerStatus;
            const auto rawPort=impl_->stable(portAddress);
            std::optional<std::uint8_t> a,b;
            if(impl_->hydroc){a=impl_->stable(hydroc16g2::typeCPowerA);b=impl_->stable(hydroc16g2::typeCPowerB);}
            if(impl_->stable(portAddress)!=rawPort)throw std::runtime_error("Charger source changed during observation");
            state.chargerRaw=rawPort;state.typeCPowerA=a;state.typeCPowerB=b;
            const auto chargerLine=std::format("{} charger observation raw7CC=0x{:02X} raw7CD={} raw7CE={} round={} typec_status=0x{:02X}; read_only=true reported_rating_not_consumption=true port_number_and_priority_unverified=true",impl_->id,rawPort,a?int(*a):-1,b?int(*b):-1,bool(rawPort&1),rawPort&0x36);
            if(chargerLine!=impl_->lastChargerLog||!impl_->lastChargerError.empty())logMessage("battery",chargerLine);
            impl_->lastChargerLog=chargerLine;impl_->lastChargerError.clear();
        }catch(const std::exception& error){
            const auto message=impl_->id+" charger observation unavailable: "+error.what();
            if(message!=impl_->lastChargerError){logMessage("battery",message);impl_->lastChargerError=message;}
        }
        if(!impl_->restoreStatus.empty())state.status+="; "+impl_->restoreStatus;
        const auto line=std::format("{} observation raw=0x{:02X} mode={} writable={} status={}",impl_->id,raw,state.mode?static_cast<int>(*state.mode):-1,state.writable,state.status);
        if(line!=impl_->lastRead){impl_->lastRead=line;logMessage("battery",line);}
    }catch(const std::exception& e){state.connected=state.writable=state.percentageWritable=state.enforcementWritable=false;state.status=e.what();const auto line=impl_->id+" observation failed: "+state.status;
        if(line!=impl_->lastRead){impl_->lastRead=line;logMessage("battery",line);}}
    impl_->retainVoltage(state);return state;
}
void NativeBatteryController::set(BatteryProtectionMode mode){
    std::lock_guard lock(impl_->mutex);
    // A new explicit command cancels an older retry even if this new command
    // fails: a queued old preference must not overwrite the user's next choice.
    impl_->pending=false;impl_->restoreAttempts=0;impl_->restoreStatus.clear();
    impl_->suppressNativeRestore=false;
    impl_->apply(static_cast<int>(mode),true);
}
void NativeBatteryController::setExperimentalChargeLimit(int upper){
    std::lock_guard lock(impl_->mutex);
    // A rejected explicit experiment still cancels an older native retry.
    // GM6 now restores confirmed percentage choices as well as EC policy.
    impl_->pending=false;impl_->restoreCanRetry=false;impl_->restoreAttempts=0;impl_->restoreStatus.clear();impl_->suppressNativeRestore=true;
    impl_->applyExperimental(upper);
    impl_->rememberThresholds();
}
void NativeBatteryController::restore(std::string reason){
    std::lock_guard lock(impl_->mutex);
    // Suppress the old native choice for this controller lifetime, but permit
    // restoring the independently saved lower using the current upper/context.
    impl_->pending=true;impl_->restoreAttempts=0;impl_->restoreReason=std::move(reason);
    impl_->restoreDeadline={};impl_->nextRestore={};
    impl_->restoreStatus.clear();impl_->wake.notify_all();
}
void NativeBatteryController::setChargeRestart(int percent){
    std::lock_guard lock(impl_->mutex);impl_->pending=false;impl_->restoreStatus.clear();
    impl_->applyRestart(percent,true);
}
void NativeBatteryController::setEnforcement(bool enabled){
    setEcPolicy(enabled?BatteryEcPolicy::Platform7Percent:BatteryEcPolicy::Platform4Original);
}
void NativeBatteryController::setEcPolicy(BatteryEcPolicy choice){
    IdentityHistoryScope historyOrigin("user","battery.ec_policy",true);
    if(batteryEcPolicy(static_cast<int>(choice)))recordIdentityPolicyRequest(batteryPolicyPlatform(choice),batteryPolicyRom(choice),"native_battery.ec_policy");
    audit::Scope trace("battery.ec_policy","\"choice\":"+std::to_string(static_cast<int>(choice)));
    std::lock_guard lock(impl_->mutex);impl_->pending=false;impl_->restoreAttempts=0;impl_->restoreStatus.clear();
    impl_->applyEnforcement(choice,true);trace.finish("readback_confirmed");
}
std::uint8_t NativeBatteryController::readEditorByte(std::uint16_t address){
    requireEcEditorAddress(address);std::lock_guard lock(impl_->mutex);
    impl_->open(false,true);Transaction transaction(*impl_->ec);impl_->guard();return impl_->stable(address);
}
std::vector<std::uint8_t> NativeBatteryController::readEditorBytes(std::uint16_t address,unsigned count){
    if(!count||count>4||static_cast<unsigned>(address)+count-1>ecEditorMaximumAddress)throw std::invalid_argument("EC range denied");
    requireEcEditorAddress(address);std::lock_guard lock(impl_->mutex);
    impl_->open(false,true);Transaction transaction(*impl_->ec);impl_->guard();
    std::vector<std::uint8_t> result;for(unsigned i=0;i<count;++i)result.push_back(impl_->stable(static_cast<std::uint16_t>(address+i)));return result;
}
std::uint8_t NativeBatteryController::writeEditorByte(std::uint16_t address,std::uint8_t expected,std::uint8_t value){
    const auto mask=ecEditorChangeMask(impl_->id,address,expected,value);std::lock_guard lock(impl_->mutex);
    impl_->open(true,true);Transaction transaction(*impl_->ec);impl_->guard();
    const auto owner=uniwillControlOwner();if(!owner.empty())throw std::runtime_error("Управление занято: "+owner);
    const auto before=impl_->stable(address);
    if(before!=expected)throw std::runtime_error("EC изменился: сначала перечитайте адрес клавишей Enter");
    if(mask)impl_->ec->write(address,static_cast<std::uint8_t>((before&~mask)|(value&mask))); // One attempt; transport permissions remain authoritative.
    return impl_->stable(address);
}
namespace {
std::shared_ptr<NativeBatteryController> sharedBatteryController(bool allowWujie){
    if(wcsstr(GetCommandLineW(),L"--no-ec"))return {};
    const auto id=deviceProfile(readBoardDescriptor()).id;
    if(!gmRamFanProfile(id)&&id!="eluktronics-hydroc16g2"&&!(allowWujie&&id=="wu-jie-15-xpro"))return {};
    static std::mutex mutex;static std::weak_ptr<NativeBatteryController> weak;
    std::lock_guard lock(mutex);auto controller=weak.lock();
    if(!controller){controller=std::make_shared<NativeBatteryController>();weak=controller;}return controller;
}
}
std::shared_ptr<NativeBatteryController> nativeBatteryController(){return sharedBatteryController(false);}
std::shared_ptr<NativeBatteryController> nativeBatteryVoltageController(){
    const auto id=deviceProfile(readBoardDescriptor()).id;
    return id=="wu-jie-15-xpro"||nativeBatteryProfile(id)?sharedBatteryController(true):nullptr;
}
}
