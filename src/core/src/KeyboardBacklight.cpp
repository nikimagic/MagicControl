#include "MagicControl/ResourceControl.h"
#include "MagicControl/KeyboardBacklight.h"
#include "MagicControl/EcAccess.h"
#include "MagicControl/Logging.h"
#include "MagicControl/DeviceOperation.h"
#include "MagicControl/ControlAudit.h"
#include "../../protocols/uniwill/SingleZoneBacklight.h"
#include "../../protocols/uniwill/gm6ir0c/KeyboardColorCodec.h"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>
namespace mc {
namespace {
struct LightingLifecycle {
    std::mutex mutex;unsigned pauses{};bool sleeping{},resumePending{};
    std::uint64_t powerEpoch{},revision{},failure{};
    std::optional<KeyboardBacklightState> confirmed;
    std::string reason;
};
LightingLifecycle& lifecycle(){static LightingLifecycle state;return state;}
std::uint64_t lightingWriteEpoch(){
    auto& s=lifecycle();std::lock_guard lock(s.mutex);
    if(s.sleeping)throw std::runtime_error("Подсветка: система переходит в сон");
    return s.powerEpoch;
}
void lightingWriteGuard(EcAccess& ec,std::uint64_t epoch){
    if(lightingWriteEpoch()!=epoch)throw std::runtime_error("Подсветка: эпоха питания изменилась");
    ec.ensureKeyboardWriteAccess();
}
SingleZoneModel model(EcAccess& ec){
    const auto& p=deviceProfile(ec.verifyBoard().board);
    if(p.id!="wu-jie-15-xpro"&&!gmRamFanProfile(p.id))throw std::runtime_error("Подсветка: неподдерживаемая плата");
    ec.ensureCapability(DeviceFeature::Backlight,p.protocolId);
    for(const auto& probe:p.identity)if((ec.readStable(probe.address)&probe.mask)!=(probe.expected&probe.mask)){
        // Wake-up reads are not permission to write, but a transient identity
        // must not invalidate every unrelated EC function for this app lifetime.
        bool resuming{};{auto& s=lifecycle();std::lock_guard lock(s.mutex);resuming=s.sleeping||s.resumePending;}
        if(!resuming){std::this_thread::sleep_for(std::chrono::milliseconds(30));
            if((ec.readStable(probe.address)&probe.mask)!=(probe.expected&probe.mask)){std::this_thread::sleep_for(std::chrono::milliseconds(30));
                if((ec.readStable(probe.address)&probe.mask)!=(probe.expected&probe.mask))ec.invalidateBoardVerification();}}
        throw std::runtime_error("Подсветка: текущая идентификация EC не подтверждена");
    }
    return gmRamFanProfile(p.id)?SingleZoneModel::Gm6:SingleZoneModel::Wujie;
}
void validColor(const KeyboardColor& c){for(auto v:c)if(v>50)throw std::runtime_error("RGB EC: диапазон0–50");}
struct Change {std::uint16_t address;std::uint8_t before,after,mask,trigger;};
struct Journal {
    EcAccess& ec;KeyboardBacklightState baseline;std::uint64_t epoch;
    std::vector<Change> changes;bool done{},attempted{};
    Journal(EcAccess& access,const KeyboardBacklightState& state,std::uint64_t powerEpoch):ec(access),baseline(state),epoch(powerEpoch){}
    void put(std::uint16_t a,std::uint8_t mask,std::uint8_t bits,std::uint8_t trigger=0){
        const auto before=ec.readStable(a);const auto after=static_cast<std::uint8_t>((before&~mask)|(bits&mask));
        audit::event("backlight_field_requested","\"address\":"+std::to_string(a)+",\"before\":"+std::to_string(before)+",\"requested\":"+std::to_string(after)+",\"mask\":"+std::to_string(mask)+",\"trigger\":"+std::to_string(trigger)+",\"write_needed\":"+((before!=after||trigger)?"true":"false"));
        if((before&mask)!=(after&mask))changes.push_back({a,before,after,mask,trigger});
        if(before!=after||trigger){lightingWriteGuard(ec,epoch);attempted=true;(void)ec.updateBitsVerified(a,mask,bits,trigger);}
    }
    std::string rollback(){done=true;if(!attempted)return "not_attempted";
        bool rgb=false,owned=true,rgbOwned=true;std::string errors;
        const auto fail=[&](const std::string& detail){owned=false;if(!errors.empty())errors+="; ";errors+=detail;};
        try{lightingWriteGuard(ec,epoch);}catch(const std::exception& e){return std::string("unknown: ")+e.what();}
        // RGB is one apply group: never latch a partial old colour over a
        // channel that changed externally, even if our other channels match.
        try{for(int channel=0;channel<3;++channel){const auto a=static_cast<std::uint16_t>(singlezone::red+channel);
            const auto current=ec.readStable(a);auto target=baseline.color[channel];
            for(const auto& change:changes)if(change.address==a)target=change.after;
            if(current!=baseline.color[channel]&&current!=target)rgbOwned=false;
        }}catch(const std::exception& e){rgbOwned=false;fail(e.what());}
        for(auto i=changes.rbegin();i!=changes.rend();++i)try{
            const bool channel=i->address>=singlezone::red&&i->address<=singlezone::blue;
            if(channel&&!rgbOwned){fail("RGB group ownership lost");continue;}
            const auto current=ec.readStable(i->address)&i->mask;
            if(current==(i->before&i->mask))continue;
            if(current!=(i->after&i->mask)){fail("external field change");continue;}
            lightingWriteGuard(ec,epoch);
            (void)ec.updateBitsVerified(i->address,i->mask,i->before,i->trigger);
            rgb|=channel;
        }catch(const std::exception& e){fail(e.what());}
        if(rgb&&rgbOwned)try{lightingWriteGuard(ec,epoch);(void)ec.updateBitsVerified(singlezone::effect,0,0,singlezone::colorStrobe);}catch(const std::exception& e){fail(e.what());}
        try{for(const auto& change:changes)if((ec.readStable(change.address)&change.mask)!=(change.before&change.mask))fail("final field readback differs");}
        catch(const std::exception& e){fail(e.what());}
        const auto result=owned?std::string("registers_restored; physical_response_unverified"):"unknown: "+errors;
        logMessage("backlight","rollback="+result);return result;
    }
    ~Journal(){if(!done)logMessage("backlight","rollback=unknown: operation left without finalization");}
};
}
KeyboardLightingPause::KeyboardLightingPause(std::string reason){auto& s=lifecycle();std::lock_guard lock(s.mutex);++s.pauses;powerEpoch_=s.powerEpoch;s.reason=std::move(reason);}
KeyboardLightingPause::~KeyboardLightingPause(){auto& s=lifecycle();std::lock_guard lock(s.mutex);--s.pauses;if(!complete_){++s.failure;s.reason="Восстановление подсветки не подтверждено";}}
void KeyboardLightingPause::defer(){auto& s=lifecycle();std::lock_guard lock(s.mutex);s.resumePending=true;complete_=true;}
void KeyboardLightingPause::complete(const KeyboardBacklightState& actual){
    if(!actual.supported)throw std::runtime_error("Подсветка: итоговое состояние не подтверждено");
    auto& s=lifecycle();std::lock_guard lock(s.mutex);
    if(s.sleeping||s.powerEpoch!=powerEpoch_)throw std::runtime_error("Подсветка: восстановление прервано сменой питания");
    s.confirmed=actual;++s.revision;s.resumePending=false;complete_=true;
}
void setKeyboardLightingSuspended(bool suspended){auto& s=lifecycle();std::lock_guard lock(s.mutex);
    if(suspended&&!s.sleeping)++s.powerEpoch;s.sleeping=suspended;s.resumePending=true;
    s.reason=suspended?"Цикл приостановлен: сон":"Цикл ожидает подтверждения восстановления";
}
const std::array<KeyboardColor,8>& keyboardColorPalette(SingleZoneModel m){
    static constexpr std::array<KeyboardColor,8> w{{{50,50,50},{50,0,0},{50,10,0},{50,40,0},{0,50,0},{0,50,50},{0,0,50},{35,0,50}}};
    static constexpr auto g=[] {std::array<KeyboardColor,8> p{};
        constexpr std::array<KeyboardColor,8> ui{{{255,255,255},{255,0,0},{255,80,0},{255,204,0},{0,255,0},{0,255,255},{0,0,255},{180,0,255}}};
        for(int i=0;i<8;++i)p[i]=gm6::nativeKeyboardColorLevels(ui[i]);return p;}();
    return m==SingleZoneModel::Gm6?g:w;
}
KeyboardColor keyboardColorLevels(SingleZoneModel m,const KeyboardColor& rgb){
    KeyboardColor result{};
    if(m==SingleZoneModel::Gm6)return gm6::nativeKeyboardColorLevels(rgb);
    for(int i=0;i<3;++i)result[i]=static_cast<std::uint8_t>((int(rgb[i])*50+127)/255);
    return result;
}
std::optional<KeyboardIdleState> readKeyboardIdle(EcAccess& ec){
    // Exact Wujie proof: research/wujie-wake-backlight-20261030/REPORT.md.
    // Do not transplant this counter/clock to another board from a shared ABI.
    if(deviceProfile(ec.verifyBoard().board).id!="wu-jie-15-xpro")return {};
    EcAccess::Transaction transaction(ec);static_cast<void>(model(ec));
    if(!(ec.readStable(singlezone::support)&singlezone::rgbSupport))return {};
    KeyboardIdleState s;s.flags=ec.readStable(0x741);s.powerState=ec.readStable(0x6e6);
    s.platform=ec.readStable(0x7c3);s.limit=s.platform==4?30:600;
    bool stable{};
    for(int attempt=0;attempt<3&&!stable;++attempt){
        const auto high=ec.readStable(0x8a5),low=ec.readStable(0x8a6);
        stable=high==ec.readStable(0x8a5);s.elapsed=std::uint16_t((high<<8)|low);
    }
    if(!stable||s.elapsed>600)return {};
    s.workingLevel=ec.readStable(0x826);
    s.onAC=(ec.readStable(0x490)&1)!=0;s.acLevel=ec.readStable(0x825);s.dcLevel=ec.readStable(0x827);
    s.hostPowerDisabled=(ec.readStable(singlezone::level)&2)!=0;
    if(s.workingLevel>4)return {};
    s.sampledAtMs=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    return s;
}
KeyboardBacklightState readKeyboardBacklight(EcAccess& ec){
    EcAccess::Transaction transaction(ec);KeyboardBacklightState s;s.model=model(ec);
    s.supported=(ec.readStable(singlezone::support)&singlezone::rgbSupport)!=0;if(!s.supported){audit::state("keyboard_singlezone",audit::keyboard(s));return s;}
    s.keyboard=ec.readStable(singlezone::level);s.effect=ec.readStable(singlezone::effect);
    const auto rawPulse=ec.readStable(singlezone::pulse);s.pulseRaw=rawPulse;s.breathing=rawPulse&singlezone::pulseMask;
    s.color={ec.readStable(singlezone::red),ec.readStable(singlezone::green),ec.readStable(singlezone::blue)};
    audit::state("keyboard_singlezone",audit::keyboard(s,rawPulse));
    return s;
}
KeyboardBacklightState applyKeyboardBacklight(EcAccess& ec,const KeyboardBacklightChange& c){
    audit::Scope trace("keyboard.apply","\"requested_level\":"+audit::optional(c.level)+",\"requested_effect\":"+audit::optional(c.effect)+",\"requested_pulse\":"+audit::optional(c.pulse)+",\"requested_power_disabled\":"+audit::optional(c.powerDisabled)+",\"factory_color\":"+(c.factoryColor?"true":"false")+",\"requested_raw_rgb\":"+(c.color?audit::rgb(*c.color):"null")+",\"expected\":"+(c.expected?audit::keyboard(*c.expected):"null"));
    const auto epoch=lightingWriteEpoch();
    EcAccess::Transaction transaction(ec);const auto before=readKeyboardBacklight(ec);
    audit::event("backlight_before","\"state\":"+audit::keyboard(before));
    lightingWriteGuard(ec,epoch);
    if(!before.supported)throw std::runtime_error("EC не подтвердил RGB подсветку");
    if(((before.keyboard>>5)&7)>4||before.breathing>4)throw std::runtime_error("Подсветка: неизвестное состояние EC");
    validColor(before.color);
    if(c.expected&&(before.color!=c.expected->color||before.breathing!=c.expected->breathing||((before.effect^c.expected->effect)&singlezone::rainbow)))
        throw std::runtime_error("Цикл остановлен: цвет/эффект изменён извне");
    if(c.expected&&((c.level&&((before.keyboard^c.expected->keyboard)&0xe0))||
       (c.powerDisabled&&((before.keyboard^c.expected->keyboard)&2))))
        throw std::runtime_error("Подсветка: яркость изменена другим владельцем");
    if(c.level&&(*c.level<0||*c.level>4))throw std::invalid_argument("Яркость0–4");
    if(c.effect&&(*c.effect<0||*c.effect>2))throw std::invalid_argument("Неизвестный эффект подсветки");
    if(c.pulse&&(*c.pulse<1||*c.pulse>4))throw std::invalid_argument("Пресет пульсации1–4");
    if(c.pulse&&!c.effect&&(!before.breathing||(before.effect&singlezone::rainbow)))throw std::runtime_error("Пульсация не активна");
    auto color=c.color;if(color)validColor(*color);
    if(c.factoryColor||(c.effect==0&&!color&&(before.effect&singlezone::rainbow))){
        color=KeyboardColor{ec.readStable(singlezone::defaultRed),ec.readStable(singlezone::defaultRed+1),ec.readStable(singlezone::defaultRed+2)};
        validColor(*color);if(*color==KeyboardColor{})throw std::runtime_error("Штатный RGB не подтверждён EC");
    }
    if(color&&!c.effect&&((before.effect&singlezone::rainbow)||before.breathing))throw std::runtime_error("Выберите «Моно» для изменения RGB");
    // Explicit OFF matches OEM StopManual/StopRainbow/StopBreathing before
    // disabling power. Idle blank (powerDisabled alone) retains native effects.
    // Only low3 of7C5 and bit7 of767 belong to this operation: never touch FAN
    // Respective7C5.7, USB767.4 or lightbar767.1. Journal owns rollback masks.
    const bool explicitOff=c.level==0&&c.powerDisabled.value_or(true)&&!c.preserveEffectsWhenBlanked;
    const int targetPulse=explicitOff?0:c.effect?(*c.effect==1?c.pulse.value_or(before.breathing?before.breathing:before.model==SingleZoneModel::Gm6?1:2):0):c.pulse.value_or(before.breathing);
    Journal j{ec,before,epoch};
    try{
    if(explicitOff){j.put(singlezone::effect,singlezone::rainbow,0);j.put(singlezone::pulse,singlezone::pulseMask,0);}
    if(c.effect&&!explicitOff){
        if(*c.effect!=1||color)j.put(singlezone::pulse,singlezone::pulseMask,0);
        if(*c.effect==2){color=KeyboardColor{};}
        else j.put(singlezone::effect,singlezone::rainbow,0);
    }
    if(color){for(int i=0;i<3;++i)j.put(static_cast<std::uint16_t>(singlezone::red+i),255,(*color)[i]);
        if(c.effect!=2)j.put(singlezone::effect,0,0,singlezone::colorStrobe);}
    if(c.effect==2&&!explicitOff)j.put(singlezone::effect,singlezone::rainbow,singlezone::rainbow);
    if((c.effect==1||c.pulse)&&!explicitOff)j.put(singlezone::pulse,singlezone::pulseMask,static_cast<std::uint8_t>(targetPulse));
    if(c.level)j.put(singlezone::level,singlezone::levelMask,static_cast<std::uint8_t>((*c.level<<5)|(c.powerDisabled.value_or(*c.level==0)?2:0)),singlezone::levelStrobe);
    else if(c.powerDisabled)j.put(singlezone::level,2,*c.powerDisabled?2:0,singlezone::levelStrobe);
    auto after=readKeyboardBacklight(ec);
    if(!after.supported||(color&&after.color!=*color)||((c.effect||c.pulse)&&after.breathing!=targetPulse)||(c.effect&&!explicitOff&&bool(after.effect&singlezone::rainbow)!=(*c.effect==2))||
       (c.level&&((after.keyboard>>5)&7)!=*c.level)||(c.powerDisabled&&bool(after.keyboard&2)!=*c.powerDisabled)||
       (c.level&&!c.powerDisabled&&bool(after.keyboard&2)!=(*c.level==0))||
       (explicitOff&&(after.breathing||(after.effect&singlezone::rainbow))))throw std::runtime_error("Подсветка: итоговое чтение не подтвердило команду");
    lightingWriteGuard(ec,epoch);j.done=true;trace.finish("readback_confirmed","\"state\":"+audit::keyboard(after));logMessage("backlight","SingleZone command readback confirmed; physical response not inferred");return after;
    }catch(const std::exception& failure){const std::string original=failure.what();const auto rollback=j.rollback();trace.fail(original+"; rollback="+rollback);throw std::runtime_error(original+"; rollback="+rollback);}
    catch(...){const auto restored=j.rollback();throw std::runtime_error("Подсветка: неизвестная ошибка; rollback="+restored);}
}
struct KeyboardColorCycle::Impl {
    Reader read;Writer write;mutable std::mutex gate;std::mutex waitGate;
    std::condition_variable wake;mc::ResourceActor worker;KeyboardCycleStatus state;
    Impl(Reader r,Writer w):read(std::move(r)),write(std::move(w)){}
    void stop(){worker.request_stop();wake.notify_all();if(worker.joinable())worker.join();std::lock_guard lock(gate);state.active=false;state.paused=false;state.message.clear();}
};
KeyboardColorCycle::KeyboardColorCycle(Reader r,Writer w):impl_(std::make_unique<Impl>(std::move(r),std::move(w))){}
KeyboardColorCycle::~KeyboardColorCycle(){impl_->stop();}
void KeyboardColorCycle::stop(){impl_->stop();}
KeyboardCycleStatus KeyboardColorCycle::status()const{std::lock_guard lock(impl_->gate);return impl_->state;}
void KeyboardColorCycle::start(const KeyboardCycleSettings& settings){
    if(settings.intervalSeconds<1||settings.intervalSeconds>3600)throw std::invalid_argument("Интервал цикла1–3600сек");
    impl_->stop();KeyboardLightingPause initial("explicit_cycle_start");const auto before=impl_->read();KeyboardBacklightChange first;first.effect=0;first.expected=before;
    first.color=keyboardColorLevels(before.model,settings.colors[0]);auto expected=impl_->write(first);
    initial.complete(expected);
    std::uint64_t revision{},failure{};
    {auto& s=lifecycle();std::lock_guard lock(s.mutex);revision=s.revision;failure=s.failure;}
    {std::lock_guard lock(impl_->gate);impl_->state={true,0,"Цикл6 · программный таймер"};}
    logMessage("backlight_cycle","started:6 colours; interval_seconds="+std::to_string(settings.intervalSeconds));
    impl_->worker=mc::ResourceActor([p=impl_.get(),settings,expected,revision,failure](std::stop_token token)mutable{
        unsigned index=0;std::unique_lock wait(p->waitGate);auto due=std::chrono::steady_clock::now()+std::chrono::seconds(settings.intervalSeconds);
        while(!token.stop_requested()){
            if(p->wake.wait_for(wait,std::chrono::milliseconds(100),[&]{return token.stop_requested();}))break;
            try{
                {auto& s=lifecycle();std::lock_guard lock(s.mutex);
                    if(s.failure!=failure)throw std::runtime_error("Цикл остановлен: восстановление подсветки не подтверждено");
                    if(s.sleeping||s.resumePending||s.pauses){std::lock_guard stateLock(p->gate);p->state.message=s.reason;p->state.paused=true;
                        due=std::chrono::steady_clock::now()+std::chrono::seconds(settings.intervalSeconds);continue;}}
                if(std::chrono::steady_clock::now()<due)continue;
                auto operation=deviceOperations().tryBegin(DeviceOperationKind::Background);if(!operation)continue;
                // Recheck after admission: a lifecycle scope may have completed
                // while this worker was waiting for DeviceOperation.
                {auto& s=lifecycle();std::lock_guard lock(s.mutex);
                    if(s.failure!=failure)throw std::runtime_error("Цикл остановлен: восстановление подсветки не подтверждено");
                    if(s.sleeping||s.resumePending||s.pauses)continue;
                    if(s.revision!=revision){
                        if(!s.confirmed||s.confirmed->model!=expected.model||s.confirmed->breathing||(s.confirmed->effect&singlezone::rainbow))
                            throw std::runtime_error("Цикл остановлен: восстановлен другой эффект");
                        expected=*s.confirmed;revision=s.revision;
                    }}
                index=(index+1)%6;KeyboardBacklightChange change;change.effect=0;change.expected=expected;
                change.color=keyboardColorLevels(expected.model,settings.colors[index]);expected=p->write(change);
                due=std::chrono::steady_clock::now()+std::chrono::seconds(settings.intervalSeconds);
                std::lock_guard lock(p->gate);p->state.index=index;p->state.message="Цикл6 · программный таймер";p->state.paused=false;
            }catch(const std::exception& e){std::lock_guard lock(p->gate);p->state={false,index,e.what()};logMessage("backlight_cycle",e.what());break;}
        }
    });
}
}
