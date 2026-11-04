#include "LightingControlWindows.h"
#include "NativeFixedFans.h"
#include "MagicControl/LaptopControl.h"
#include "MagicControl/LaptopLightingResource.h"
#include "MagicControl/DeviceProfile.h"
#include "MagicControl/BoardIdentity.h"
#include "MagicControl/BatteryUiPolicy.h"
#include "MagicControl/ResourceControl.h"
#include "ConfigurationRestoreWindows.h"
#include "FanControlWindows.h"
#include "FanDeviceActionsWindows.h"
#include "WindowsDevices.h"
#include "PowerDiagnosticsWindows.h"
#include "BatterySettingsWindows.h"
#include "SettingsPersistenceWindows.h"
#include "CustomModeWindows.h"
#include "ApuPowerPolicyWindows.h"
#include "LightingOwnershipWindows.h"
#include "MagicControl/EcAccess.h"
#include "MagicControl/EcRegister.h"
#include "MagicControl/Performance.h"
#include "MagicControl/Logging.h"
#include "MagicControl/RomIdHistory.h"
#include "MagicControl/PortableSettings.h"
#include "MagicControl/KeyboardBacklight.h"
#include "MagicControl/QuickSwitch.h"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <format>
#include <mutex>
#include <thread>
#include <utility>
#include <stdexcept>
#include <windows.h>

namespace mc::windows {
using namespace std::chrono_literals;
namespace {
using S = DeviceSetting;
class AwakeDeadline {
    std::chrono::steady_clock::time_point fallback_;
    ULONGLONG start_{};
    ULONGLONG duration_{};
    bool available_{};
public:
    explicit AwakeDeadline(std::chrono::milliseconds duration):fallback_(std::chrono::steady_clock::now()+duration),
        duration_(static_cast<ULONGLONG>(duration.count())*10000),available_(QueryUnbiasedInterruptTime(&start_)!=FALSE){}
    bool expired() const noexcept {
        ULONGLONG now{};
        return available_&&QueryUnbiasedInterruptTime(&now)!=FALSE&&now>=start_ ?
            now-start_>=duration_:std::chrono::steady_clock::now()>=fallback_;
    }
};
void log(const std::string& text) { logMessage("configuration", text); }
DevicePreferences capture(EcAccess& ec) {
    DevicePreferences result;
    const auto read = [&](const char* name, auto action) {
        try { const EcAccess::Transaction transaction(ec); action(); }
        catch (const std::exception& error) { log(std::string("Setting read unavailable: ") + name + ": " + error.what()); }
    };
    read("charge limit", [&] {
        const int charge = ec.readStable(ec::kBatteryChargeControl) & ec::kBatteryChargeLimitMask;
        if (const auto captured=capturedBatteryChargeLimit(charge)) {
            result[S::ChargeLimit] = *captured;
            const auto platform=ec.readStable(ec::kBatteryPlatformWmi),rom=ec.readStable(0x770);
            if(const auto policy=observedBatteryEcPolicy(platform,rom))result[S::ChargeAuto]=static_cast<int>(*policy);
        }
    });
    read("performance", [&] {
        const int performance = ec.readStable(ec::kPerformanceMode) & ec::kPerformancePresetMask;
        if (performance == 0 || performance == 0x10 || performance == 0x80)
            result[S::Performance] = performance == 0x80 ? 0 : performance == 0 ? 1 : 2;
    });
    read("keyboard", [&] {
        if ((ec.readStable(ec::kKeyboardCapability) & ec::kKeyboardRgbBit) == 0) return;
        const auto level = ec.readStable(ec::kKeyboardLevel);
        // Native idle blank sets78C.1 without changing the selected level. A
        // migration capture must not turn this effective blank into desired Off.
        const int selectedLevel = (level >> 5) & 7;
        if (selectedLevel <= 4) result[S::KeyboardLevel] = selectedLevel;
        const int breathing = ec.readStable(ec::kFanControlRespective) & ec::kPulsationMask;
        if (breathing >= 1 && breathing <= 4) result[S::PulseColor] = breathing - 1;
        if (ec.readStable(ec::kKeyboardEffect) & ec::kKeyboardRainbowBit) result[S::KeyboardEffect] = 2;
        else if (breathing <= 4) result[S::KeyboardEffect] = breathing ? 1 : 0;
        for (int index = 0; index < 3; ++index) {
            const auto value = ec.readStable(static_cast<std::uint16_t>(ec::kKeyboardRed + index));
            if (value <= 50) result[static_cast<S>(static_cast<int>(S::Red) + index)] = value;
        }
    });
    // KEYs intent is migrated/read only by the computer-wide KeysLifecycle.
    return result;
}
void applyKeyboard(EcAccess& ec, const DevicePreferences& saved) {
    ec.ensureCapability(DeviceFeature::Backlight,"uniwill-x5sp4nag");
    ensureLightingWritable();
    const EcAccess::Transaction transaction(ec);
    const auto actual=readKeyboardBacklight(ec);
    KeyboardBacklightChange change;change.effect=*saved[S::KeyboardEffect];
    change.expected=actual;
    if(saved[S::Red]&&saved[S::Green]&&saved[S::Blue])change.color=KeyboardColor{
        static_cast<std::uint8_t>(*saved[S::Red]),static_cast<std::uint8_t>(*saved[S::Green]),static_cast<std::uint8_t>(*saved[S::Blue])};
    if(change.effect==1){if(!saved[S::PulseColor])throw std::runtime_error("Не сохранён цвет пульсации");change.pulse=*saved[S::PulseColor]+1;}
    if(saved[S::KeyboardLevel]){change.level=*saved[S::KeyboardLevel];
        if(*saved[S::KeyboardLevel]>0)change.powerDisabled=(actual.keyboard&2)!=0;}
    mc::gui::LightingControlContext context;context.ec=&ec;static_cast<void>(mc::gui::executeLightingResource(context,LightingWriteRequest::singleZone(change,LightingWriteOrigin::Restore)));
}

}
struct ConfigurationRestore::Impl {
    EcAccess* ec{};
    std::string nativeProfile;bool paused{};std::uint64_t sleepEpoch{};
    std::shared_ptr<FanController> fans;
    std::function<std::unique_ptr<LaptopControl>()> laptopFactory;
    std::function<std::shared_ptr<NativeFixedFans>()> nativeFactory;
    std::mutex mutex;
    std::condition_variable condition;
    std::string pendingReason;
    std::optional<bool> pendingIndicator;
    std::function<void()> pendingRadioAccess;
    std::chrono::steady_clock::time_point requestedAt;
    mc::ResourceActor worker;
    Impl(EcAccess& access, std::shared_ptr<FanController> control)
        : ec(&access), fans(std::move(control)), worker([this](std::stop_token stop) { run(stop); }) {}
    Impl(std::string profile,std::function<std::unique_ptr<LaptopControl>()> laptop,std::function<std::shared_ptr<NativeFixedFans>()> native)
        :nativeProfile(std::move(profile)),laptopFactory(std::move(laptop)),nativeFactory(std::move(native)),worker([this](std::stop_token stop){run(stop);}){}
    EcAccess& access(){if(!ec)throw std::logic_error("No raw EC transport");return *ec;}
    ~Impl() { worker.request_stop(); condition.notify_all(); worker.join(); }
    void request(std::string reason, std::function<void()> access) {
        if (reason != "startup" && reason != "power_resume" && reason != "system_sleep_clock") return;
        { std::lock_guard lock(mutex);
            paused=false;pendingIndicator.reset();
            pendingReason = std::move(reason);
            if (access) pendingRadioAccess = std::move(access);
            requestedAt = std::chrono::steady_clock::now();
        }
        condition.notify_all();
    }
    void indicate(bool ac) {
        { std::lock_guard lock(mutex); pendingIndicator = ac;
          if (pendingReason.empty()) pendingReason = "adapter_change";
          requestedAt = std::chrono::steady_clock::now() - 1s; }
        condition.notify_all();
    }
    void restoreNative(std::stop_token stop,const std::string& reason,std::uint64_t requestEpoch){
        const auto valid=[&]{std::lock_guard lock(mutex);return !stop.stop_requested()&&!paused&&requestEpoch==sleepEpoch;};
        const auto& profile=deviceProfile(readBoardDescriptor());
        if(profile.id!=nativeProfile)throw std::runtime_error("Configuration identity changed");
        auto laptop=laptopFactory();auto native=nativeFactory();
        if(!laptop||!native)return;
        const auto path=portableSettingsPath(std::string(gmFanCurveFile(profile.id)));
        const auto load=[&]{FanCurveProfiles value;for(int ch=0;ch<2;++ch)value.setActivePointCount(ch,profile.fanPointCounts[ch]);std::string error;
            if(std::filesystem::exists(path)&&!loadFanCurveProfiles(path,value,error))throw std::runtime_error(error);
            for(int ch=0;ch<2;++ch)value.setActivePointCount(ch,profile.fanPointCounts[ch]);return value;};
        DevicePreferences energy;std::string energyError;
        if(profile.id=="lenovo-legion-kwcn"){
            const auto path=settingsReadPath(portableSettingsPath("lenovo-energy-settings.ini"));
            if(std::filesystem::exists(path)&&!loadDevicePreferences(path,energy,energyError))throw std::runtime_error(energyError);
        }
        if(!load().restoreSelected&&!laptop->savedCpuPowerChoice()&&!energy[S::BatteryProtection]&&!energy[S::KeyboardLevel])return;
        std::shared_ptr<DeviceOperation> operation;const AwakeDeadline deadline(10s);
        while(valid()&&!(operation=deviceOperations().tryBegin())){
            if(deadline.expired())throw std::runtime_error("Configuration operation remained busy");
            std::unique_lock lock(mutex);condition.wait_for(lock,50ms);
        }
        if(!valid())return;
        // Re-read after operation admission; explicit newer commands win. The
        // same core resources and Lenovo owner are used as for button clicks.
        if(profile.id=="lenovo-legion-kwcn"){
            DevicePreferences latest;std::string error;const auto path=settingsReadPath(portableSettingsPath("lenovo-energy-settings.ini"));
            if(std::filesystem::exists(path)&&!loadDevicePreferences(path,latest,error))log("Lenovo Energy restore settings unavailable: "+error);
            else {
                if(latest[S::BatteryProtection]&&valid())try{const auto state=laptop->readTelemetry();if(state.chargeMode!=latest[S::BatteryProtection]){
                    BatteryWriteRequest request{BatteryWrite::Protection,*latest[S::BatteryProtection]};request.origin=BatteryWriteOrigin::Restore;
                    BatterySettingsContext context;context.laptop=laptop.get();context.profile=std::string(profile.id);executeBatteryResource(context,request);}}
                    catch(const std::exception& e){log(std::string("Lenovo Energy battery restore not confirmed: ")+e.what());}
                if(latest[S::KeyboardLevel]&&valid())try{const auto state=laptop->read();if(state.keyboardLevel!=latest[S::KeyboardLevel]){
                    LightingWriteRequest request{LightingWrite::Level,*latest[S::KeyboardLevel]};request.origin=LightingWriteOrigin::Restore;
                    const auto result=mc::executeLightingResource(*laptop,request);if(!result.success)throw std::runtime_error(result.message);}}
                    catch(const std::exception& e){log(std::string("Lenovo Energy keyboard restore not confirmed: ")+e.what());}
            }
        }
        // Read the latest intent AFTER admission: a newer explicit command is
        // authoritative, not a stale copy captured before waiting for the gate.
        const auto saved=load();const auto cpu=laptop->savedCpuPowerChoice();
        NativeFixedFanState actual;
        for(;;){try{actual=native->read();break;}catch(...){if(!valid()||deadline.expired())throw;std::unique_lock lock(mutex);condition.wait_for(lock,200ms);}}
        const auto apply=[&](FanWriteRequest request){if(!valid())throw std::runtime_error("Configuration interrupted by sleep/exit");request.origin=FanWriteOrigin::Restore;
            const auto result=executeFanResource({nullptr,native.get(),nullptr,operation,actual.targetRpm,profile.id},request);result.requireAccepted();if(result.native)actual=*result.native;};
        if(saved.restoreSelected){
            if(saved.ecManual||(saved.linked&&!actual.sharedCurve)||saved.selected[0]!=saved.selected[1])throw std::runtime_error("Native FAN selection cannot represent this saved control mode");
            if(actual.needsRestore){if(!actual.writable)throw std::runtime_error("Foreign FAN state; restore refused");apply({FanResourceOperation::Default});}
            const auto wanted=saved.selected[0];
            if(wanted!=FanCurvePreset::Default){
                if(wanted==FanCurvePreset::Manual){
                    if(!actual.curves||!actual.curves->appliedCurve||actual.mode!=1||actual.curves->draft!=saved.curves[0]){FanWriteRequest request{FanResourceOperation::Manual};request.profiles=saved;apply(std::move(request));}
                }else if(wanted==FanCurvePreset::Max&&actual.mode!=2)apply({FanResourceOperation::Maximum});
                else if(wanted==FanCurvePreset::Min&&actual.mode!=3)apply({FanResourceOperation::Minimum});
            }
        }
        if(cpu&&valid()){
            const auto observed=laptop->read();
            if(cpu->custom?observed.customCpuPowerActive!=true:observed.stockCpuProfile!=cpu->baseProfile){
                CustomModeContext context;context.profileId=std::string(profile.id);context.laptop=laptop.get();context.nativeFans=native;context.operation=operation;
                PowerWriteRequest power{cpu->custom?PowerWrite::Custom:PowerWrite::Stock,cpu->values,cpu->baseProfile,PowerWriteOrigin::Restore};power.options=cpu->options;
                executePowerResource(context,power);
            }
        }
        // Existing device command owns coupled factory power/FAN ordering.
        // Manual is established first so a Stock CPU replay preserves it.
        if(saved.restoreSelected&&saved.selected[0]==FanCurvePreset::Default)apply({FanResourceOperation::Default});
        log("Selected CPU/FAN configuration confirmed: "+reason+" profile="+nativeProfile);
    }
    void indicateKeyboard(bool ac, DevicePreferences saved, std::stop_token stop) {
        if (!saved[S::KeyboardEffect] || fans->status().suspended) return;
        ensureLightingWritable();
        const auto before = readKeyboardBacklight(access());
        const int selectedBefore = (before.keyboard >> 5) & 7;
        // An adapter notification is not a user request to wake an idle-blanked
        // keyboard or override explicit Off. No preferences are written here.
        if (!before.supported || saved[S::KeyboardLevel] == 0 ||
            (before.keyboard & 2) || selectedBefore == 0) return;
        KeyboardBacklightChange flash;
        flash.effect = 0; flash.level = 4; flash.expected = before;
        flash.color = KeyboardColor{static_cast<std::uint8_t>(ac ? 0 : 50),
            static_cast<std::uint8_t>(ac ? 50 : 0), 0};
        mc::gui::LightingControlContext resource;resource.ec=&access();
        const auto applied = mc::gui::executeLightingResource(resource,LightingWriteRequest::singleZone(flash,LightingWriteOrigin::Temporary)).keyboard.value();
        { std::unique_lock lock(mutex);
          condition.wait_for(lock, 1s, [&] { return stop.stop_requested() || pendingIndicator.has_value(); }); }
        if (fans->status().suspended)
            throw std::runtime_error("Индикация подсветки прервана сном; старый снимок не воспроизведён");
        ensureLightingWritable();
        const EcAccess::Transaction transaction(access());
        const auto actual = readKeyboardBacklight(access());
        // Return only our temporary colour/effect, never a saved or foreign RGB
        // snapshot. A changed owner is a terminal decline, not a forced replay.
        if (actual.color != applied.color || actual.breathing != applied.breathing ||
            ((actual.effect ^ applied.effect) & ec::kKeyboardRainbowBit))
            throw std::runtime_error("Подсветка изменена извне во время индикации; возврат старого эффекта отменён");
        KeyboardBacklightChange restore;
        restore.preserveEffectsWhenBlanked=true;
        restore.expected = actual;
        restore.effect = (before.effect & ec::kKeyboardRainbowBit) ? 2 : before.breathing ? 1 : 0;
        restore.color = before.color;
        if (before.breathing) restore.pulse = before.breathing;
        if (((actual.keyboard ^ applied.keyboard) & 0xE0) == 0) {
            // Preserve a fresh idle power-disable while returning only our level
            // bits. A physical Fn level change remains untouched and unsaved.
            restore.level = selectedBefore;
            restore.powerDisabled = (actual.keyboard & 2) != 0;
        }
        static_cast<void>(mc::gui::executeLightingResource(resource,LightingWriteRequest::singleZone(restore,LightingWriteOrigin::Temporary)));
    }
    void run(std::stop_token stop) {
        while (!stop.stop_requested()) {
            std::string reason; std::function<void()> radioAccess; std::optional<bool> indicator;std::uint64_t requestEpoch{};
            {
                std::unique_lock lock(mutex);
                condition.wait(lock, [&] { return stop.stop_requested() || !pendingReason.empty(); });
                if (stop.stop_requested()) return;
                // Consolidate the resume and display notifications for one transition.
                while (!stop.stop_requested() && std::chrono::steady_clock::now() - requestedAt < 750ms)
                    condition.wait_for(lock, 100ms);
                if (stop.stop_requested()) return;
                reason = std::exchange(pendingReason, {});
                indicator = std::exchange(pendingIndicator, std::nullopt);
                radioAccess = std::move(pendingRadioAccess);
                requestEpoch = sleepEpoch;
            }
            try {
                if(!ec){restoreNative(stop,reason,requestEpoch);continue;}
                std::shared_ptr<DeviceOperation> operation;
                const AwakeDeadline deadline(60s);
                bool cancelled = false;
                while (!stop.stop_requested()) {
                    { std::lock_guard lock(mutex);
                      if (paused || requestEpoch != sleepEpoch) { cancelled = true; break; } }
                    const auto state = fans->status();
                    if (!state.suspended && !state.applying) operation = deviceOperations().tryBegin();
                    if (operation) break;
                    if (deadline.expired())
                        throw std::runtime_error(std::format("Конфигурация не применена: ожидание EC; FAN suspended={} applying={} operation_busy={}",
                            state.suspended,state.applying,!state.suspended&&!state.applying));
                    std::unique_lock lock(mutex); condition.wait_for(lock, 100ms);
                }
                if (stop.stop_requested()) return;
                if (cancelled) continue;
                { std::lock_guard lock(mutex); if (paused || requestEpoch != sleepEpoch) continue; }
                access().ensureWritable();
                IdentityHistoryScope historyOrigin("automatic",reason);
                // The caller already owns DeviceOperation. Hold one common
                // lighting pause over our temporary flash and restore steps so
                // our own colour changes are not mistaken for a foreign cycle.
                // Failure leaves the cycle unconfirmed; only a fresh successful
                // final lighting observation is allowed to resume/rebase it.
                KeyboardLightingPause lightingPause("Wujie configuration: " + reason);
                const auto completeLighting = [&] {
                    if (fans->status().suspended)
                        throw std::runtime_error("Подсветка не подтверждена: начался переход сна");
                    ensureLightingWritable();lightingPause.complete(readKeyboardBacklight(access()));
                };
                DevicePreferences saved;
                std::string error;
                if (!loadDevicePreferences(portableSettingsPath("device-settings.ini"), saved, error) && !error.empty())
                    throw std::runtime_error(error);
                if (indicator) {
                    log(std::string("ADAPTER keyboard indication: ") + (*indicator ? "connected" : "disconnected"));
                    indicateKeyboard(*indicator, saved, stop);
                }
                if (reason == "adapter_change") {
                    // USB-C priority is firmware-owned on this BIOS; no replay.
                    completeLighting();continue;
                }
                log("RESTORE begin: reason=" + reason);
                recordPowerAction("configuration_restore", "requested", -1, reason);
                // Migration only: fill fields that have never had a saved choice.
                // Existing choices are never replaced with values reset by firmware.
                const auto current = capture(access());
                if (fans->status().suspended) throw std::runtime_error("Чтение конфигурации прервано переходом питания.");
                DevicePreferences missing;
                // Legacy 100% becomes the explicitly requested platform-4 preset.
                // Auto is a new saved choice, never infer it from old platform 7.
                if (saved[S::ChargeLimit] && !saved[S::ChargeAuto])
                    saved[S::ChargeAuto] = missing[S::ChargeAuto] = 0;
                for (std::size_t key = 0; key < saved.values.size(); ++key)
                    if (!saved.values[key] && current.values[key]) {
                        saved.values[key] = missing.values[key] = current.values[key];
                    }
                if (!rememberDevicePreferences(missing, error)) throw std::runtime_error(error);
                log(std::format("RESTORE selected: charge={} performance={} effect={} brightness={} pulse_color={} RGB={},{},{}; KEYs=common service",
                    saved[S::ChargeLimit].value_or(-1), saved[S::Performance].value_or(-1),
                    saved[S::KeyboardEffect].value_or(-1), saved[S::KeyboardLevel].value_or(-1), saved[S::PulseColor].value_or(-1),
                    saved[S::Red].value_or(-1), saved[S::Green].value_or(-1), saved[S::Blue].value_or(-1)));
                // A missing read is unknown, never evidence that a setting reset.
                const auto differs = [&](S key) {
                    return saved[key] && current[key] && saved[key] != current[key];
                };
                bool failed = false;
                const auto step = [&](const char* name, auto action) {
                    if (stop.stop_requested()) throw std::runtime_error("Восстановление конфигурации отменено при выходе.");
                    if (fans->status().suspended) throw std::runtime_error("Восстановление конфигурации прервано переходом питания.");
                    try { action(); log(std::string("RESTORE confirmed: ") + name); return true; }
                    catch (const std::exception& failure) { failed = true; log(std::string("RESTORE failed: ") + name + ": " + failure.what()); return false; }
                };
                const auto customPower=[&](const std::array<int,3>& values){
                    CustomModeContext context;context.ec=&access();context.fans=fans;context.operation=operation;context.wujie=true;
                    static_cast<void>(executePowerResource(context,{PowerWrite::Custom,values,0,PowerWriteOrigin::Restore}));
                };
                if(saved[S::ApuAutoPower]==1){
                    step("APU Auto",[&]{CustomModeContext context;context.ec=&access();static_cast<void>(executePowerResource(context,{PowerWrite::Automatic,{},0,PowerWriteOrigin::Restore}));});
                } else if (saved[S::ApuMode]) {
                    step("APU limits", [&] {
                        const int mode = *saved[S::ApuMode];
                        if(mode>0&&mode<4&&saved[S::NativeCpuPresets]==1){
                            CustomModeContext context;context.ec=&access();context.fans=fans;context.operation=operation;
                            static_cast<void>(executePowerResource(context,{PowerWrite::Stock,{},mode-1,PowerWriteOrigin::Restore}));return;
                        }
                        const std::array<int, 3> values = mode == 4 ?
                            std::array<int, 3>{*saved[S::ManualStapm], *saved[S::ManualSlow], *saved[S::ManualFast]} :
                            apuPresetLimits(mode, apuOnBattery());
                        if(mode==4)customPower(values);
                        else {CustomModeContext context;context.ec=&access();static_cast<void>(executePowerResource(context,{PowerWrite::Allowed,values,0,PowerWriteOrigin::Restore}));} // Only 783/784/785; FANs untouched.
                    });
                } else {
                    // Legacy files retain their OEM selection until a new preset is clicked.
                    // Include prerequisite reads in this bounded restore step:
                    // an APU read/setter failure must not skip battery or FANs.
                    step("legacy APU settings", [&] {
                        const bool clearManualOverrides = saved[S::Performance] && saved[S::ManualPowerEnabled] == 0 &&
                            (access().readStable(ec::kPowerLimitSustained) || access().readStable(ec::kPowerLimitShort) || access().readStable(ec::kPowerLimitPeak));
                        if (saved[S::Performance] && (differs(S::Performance) || clearManualOverrides)) {
                            CustomModeContext context;context.ec=&access();context.fans=fans;context.operation=operation;
                            static_cast<void>(executePowerResource(context,{PowerWrite::Stock,{},*saved[S::Performance],PowerWriteOrigin::Restore}));
                        }
                        if (saved[S::ManualPowerEnabled] == 1)
                            customPower({*saved[S::ManualStapm], *saved[S::ManualSlow], *saved[S::ManualFast]});
                    });
                }
                // Battery restore is independent of FANs. An equal percentage in
                // 7B9 does not establish WMI storage or Enabled (742.2) survived sleep.
                if(const auto request=savedBatteryWriteRequest(saved))step(request->kind==BatteryWrite::View?"native battery context":"charge limit",[&]{
                    BatterySettingsContext context;context.ec=&access();context.profile="wu-jie-15-xpro";
                    static_cast<void>(executeBatteryResource(context,*request));

                });
                bool lightingReady = true;
                step("battery aging override",[&]{
                    BatterySettingsContext context;context.ec=&access();
                    BatteryWriteRequest request{BatteryWrite::Aging};request.enabled=true;request.lifecycle=true;request.origin=BatteryWriteOrigin::Restore;
                    static_cast<void>(executeBatteryResource(context,request));
                });
                if (differs(S::KeyboardEffect)) {
                    lightingReady = step("keyboard effect", [&] { applyKeyboard(access(), saved); });
                } else {
                    // A brightness-only reset must not reinitialize color/effect.
                    if (saved[S::KeyboardEffect] == 0 && current[S::KeyboardEffect] == 0 &&
                        (differs(S::Red) || differs(S::Green) || differs(S::Blue)))
                        lightingReady = step("keyboard RGB", [&] {
                            ensureLightingWritable();
                            const EcAccess::Transaction transaction(access());
                            auto color=readKeyboardBacklight(access()).color;
                            for(int index=0;index<3;++index){const auto key=static_cast<S>(static_cast<int>(S::Red)+index);if(differs(key))color[index]=static_cast<std::uint8_t>(*saved[key]);}
                            KeyboardBacklightChange change;change.color=color;mc::gui::LightingControlContext context;context.ec=&access();static_cast<void>(mc::gui::executeLightingResource(context,LightingWriteRequest::singleZone(change,LightingWriteOrigin::Restore)));
                        });
                    if (saved[S::KeyboardEffect] == 1 && current[S::KeyboardEffect] == 1 && differs(S::PulseColor))
                        lightingReady = step("keyboard pulse color", [&] {
                            ensureLightingWritable();
                            KeyboardBacklightChange change;change.pulse=*saved[S::PulseColor]+1;mc::gui::LightingControlContext context;context.ec=&access();static_cast<void>(mc::gui::executeLightingResource(context,LightingWriteRequest::singleZone(change,LightingWriteOrigin::Restore)));
                        }) && lightingReady;
                    if (differs(S::KeyboardLevel)) lightingReady = step("keyboard brightness", [&] {
                        ensureLightingWritable();
                        const auto actual=readKeyboardBacklight(access());
                        KeyboardBacklightChange change;change.level=*saved[S::KeyboardLevel];change.expected=actual;
                        if(*saved[S::KeyboardLevel]>0)change.powerDisabled=(actual.keyboard&2)!=0;
                        mc::gui::LightingControlContext context;context.ec=&access();static_cast<void>(mc::gui::executeLightingResource(context,LightingWriteRequest::singleZone(change,LightingWriteOrigin::Restore)));
                    }) && lightingReady;
                }
                // APU failures above remain independent. They must not prevent
                // a successfully confirmed lighting session from completing.
                if (lightingReady) step("keyboard session readback", completeLighting);
                // All KEYs use the independent common KeysLifecycle worker.
                FanCurveProfiles profiles;
                // FANs has its own identity/mode/ownership checks in the worker.
                // Even Max/Default must not depend on a successful APU override.
                // This remains a lifecycle restore, never an ordinary UI poll.
                if (loadFanCurveProfiles(portableSettingsPath("fan-curves.csv"), profiles, error)) {
                    step("fan mode flags", [&] {
                        const bool desiredMax = profiles.currentPreset(0) == FanCurvePreset::Max || profiles.currentPreset(1) == FanCurvePreset::Max;
                        const bool desiredRam = !desiredMax && (profiles.currentPreset(0) != FanCurvePreset::Default || profiles.currentPreset(1) != FanCurvePreset::Default);
                        const bool ram = (access().readStable(ec::kRamTableCommit) & ec::kRamTableCommitBit) != 0;
                        const bool boost = (access().readStable(ec::kPerformanceMode) & ec::kPerformanceBoostBit) != 0;
                        // Split/common is meaningful only for our active RAM curves.
                        // Default accepts either firmware channel arrangement.
                        const bool splitMismatch = desiredRam &&
                            (((access().readStable(ec::kFanControlRespective) & ec::kSplitModeBit) != 0) != !profiles.ecManual);
                        if ((desiredMax ? !boost : (ram != desiredRam || boost || splitMismatch)) ||
                            ((desiredRam || desiredMax) && !fans->status().owned)) {
                            FanWriteRequest request{FanResourceOperation::Restore};request.profiles=profiles;request.adoptExisting=reason=="startup";request.origin=FanWriteOrigin::Restore;
                            executeFanResource({nullptr,nullptr,fans.get(),operation},request).requireAccepted();
                        }
                        // Совпадение — no-op. Фактический Default публикует
                        // idle-наблюдатель FanController; не отправлять Native
                        // или APU-команду только ради подсветки SETs/трея.
                        else log("FAN FLAGS match saved mode; no fan restore queued");
                    });
                } else if (!error.empty()) { failed = true; log("RESTORE failed: fan profiles: " + error); }
                operation.reset(); // The fan worker owns its own lease while EC is applied.
                log("RESTORE settings " + std::string(failed ? "incomplete" : "confirmed") + ": reason=" + reason);
                recordPowerAction("configuration_restore", failed ? "failed" : "settings_confirmed", -1, reason);
            } catch (const std::exception& error) {
                log("RESTORE failed: reason=" + reason + ": " + error.what());
                recordPowerAction("configuration_restore", "failed", -1, error.what());
            }
        }
    }
};
ConfigurationRestore::ConfigurationRestore(EcAccess& ec, std::shared_ptr<FanController> fans)
    : impl_(std::make_unique<Impl>(ec, std::move(fans))) {}
ConfigurationRestore::ConfigurationRestore(std::string profile,std::function<std::unique_ptr<LaptopControl>()> laptop,std::function<std::shared_ptr<NativeFixedFans>()> native)
    :impl_(std::make_unique<Impl>(std::move(profile),std::move(laptop),std::move(native))){}
ConfigurationRestore::~ConfigurationRestore() = default;
void ConfigurationRestore::suspend(){std::lock_guard lock(impl_->mutex);impl_->paused=true;++impl_->sleepEpoch;impl_->pendingReason.clear();impl_->condition.notify_all();}
void ConfigurationRestore::request(std::string reason, std::function<void()> access) { impl_->request(std::move(reason), std::move(access)); }
}

void mc::windows::ConfigurationRestore::powerSourceChanged(bool ac) { impl_->indicate(ac); }
