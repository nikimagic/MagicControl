#include "MagicControl/ResourceControl.h"
#include "FanControlWindows.h"
#include "I2EcWindows.h"
#include "DefaultFansWindows.h"
#include "../../../protocols/huawei/Hwmi.h"
#include "MagicControl/Logging.h"
#include "MagicControl/DeviceOperation.h"
#include "MagicControl/BoardIdentity.h"
#include "MagicControl/DeviceProfile.h"
#include <windows.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <stdexcept>
#include <format>
#include <cstdint>
namespace mc::windows {
namespace {
struct DiagnosticSuperseded:std::runtime_error {using std::runtime_error::runtime_error;};
double cpuTemperature(){
    auto session=huawei::openSession();const auto data=huawei::checkedCall(*session,huawei::command(huawei::getTemperature,0),4);
    const double value=data[2]+data[3]/10.0;
    if(data[1]!=0||data[3]>9||value<10||value>125)
        throw std::runtime_error(std::format("HWMI GTMP opcode0x0202/index0: invalid sign={} magnitude={} fraction={} decoded={}C",data[1],data[2],data[3],value));
    return value;
}
void guard(HlylI2EcSession& io){
    if(!io.writable||(io.read(0x042c)&1)||io.read(0x1801)!=200||(io.read(0x180f)&0xc0)||(io.read(0x1823)&2)==0||(io.read(0x031a)&1)==0||io.read(0x0400)!=0x10)
        throw std::runtime_error("Honor: питание/PWM не допускают ручное управление");
}
struct Runtime {int flags,target0,target1,duty0,duty1;};
Runtime snapshot(HlylI2EcSession& io){return {io.read(0x042c),io.read(0x0532),io.read(0x053c),io.read(0x1808),io.read(0x1809)};}
const char* autoResultText(HlylAutoResult result){
    switch(result){
    case HlylAutoResult::Applied:return "автоматика восстановлена и подтверждена";
    case HlylAutoResult::AlreadyAuto:return "автоматика уже активна; ручной режим не записывался";
    case HlylAutoResult::OwnershipLost:return "владение потеряно; чужой ручной режим не изменён, возврат Auto не подтверждён";
    }
    return "неизвестный результат возврата Auto";
}
}
struct HlylFanController::Impl {
    std::mutex mutex;std::condition_variable wake;bool stopping{},owned{},releasePending{};
    std::uint64_t commandGeneration{};
    std::array<int,2> requested{},originalTargets{};std::string message;ResourceActor watchdog;
    Impl():watchdog([this]{watch();}){}
    HlylAutoResult automatic(bool explicitRequest=false){
        HlylI2EcSession io;
        const auto current=snapshot(io);
        // The watchdog/destructor release only the exact state we installed.
        // Default is a separate explicit user request and may select Auto.
        if(!(current.flags&2)){
            io.finish(); // A no-op still must leave configuration mode safely.
            owned=false;releasePending=false;
            logMessage("huawei_fans","Firmware automatic already active; no cleanup writes");return HlylAutoResult::AlreadyAuto;
        }
        if(!explicitRequest){
            if(!owned||current.target0!=requested[0]*2||current.target1!=requested[1]*2){
                io.finish();
                owned=false;releasePending=false;
                logMessage("huawei_fans","Cleanup skipped: manual ownership changed externally; Auto not confirmed");return HlylAutoResult::OwnershipLost;
            }
        }
        io.writeFanRuntime(0x042c,static_cast<std::uint8_t>(io.read(0x042c)&0xfd));
        if(io.read(0x042c)&2)throw std::runtime_error("Honor: автоматика не подтверждена");
        if(owned&&!releasePending&&io.read(0x0532)==requested[0]*2&&io.read(0x053c)==requested[1]*2){
            io.writeFanRuntime(0x0532,static_cast<std::uint8_t>(originalTargets[0]));
            io.writeFanRuntime(0x053c,static_cast<std::uint8_t>(originalTargets[1]));
        }
        // Retain cleanup intent if port exit fails; no more writes on this session.
        try{io.finish();}catch(const HlylI2EcFinishError&){if(owned)releasePending=true;throw;}
        owned=false;releasePending=false;logMessage("huawei_fans","Firmware automatic confirmed; other flags preserved");return HlylAutoResult::Applied;
    }
    ~Impl(){
        {std::lock_guard lock(mutex);stopping=true;wake.notify_all();}
        if(watchdog.joinable())watchdog.join();
        if(owned)try{logMessage("huawei_fans",std::string("Exit: ")+autoResultText(automatic()));}catch(const std::exception& error){logMessage("huawei_fans",std::string("Exit automatic failed: ")+error.what());}
    }
    void watch(){
        std::unique_lock lock(mutex);
        while(!stopping){
            wake.wait_for(lock,std::chrono::seconds(1),[this]{return stopping;});if(stopping)break;if(!owned)continue;
            try{
                if(releasePending){message=std::string("Honor: завершение отката: ")+autoResultText(automatic());logMessage("huawei_fans",message);continue;}
                bool stillOurs;
                {HlylI2EcSession io;const auto state=snapshot(io);stillOurs=(state.flags&2)&&state.target0==requested[0]*2&&state.target1==requested[1]*2;io.finish();}
                if(!stillOurs){owned=false;message="Honor: управление изменено извне";continue;}
                const auto temperature=cpuTemperature();
                if(temperature>=80){message=std::string("Honor: температура>=80°C; ")+autoResultText(automatic());logMessage("huawei_fans",message);}
            }catch(const std::exception& error){
                message=std::string("Honor: контроль температуры/EC потерян: ")+error.what();
                try{message+="; ";message+=autoResultText(automatic());}catch(const std::exception& restore){message+="; возврат не подтверждён: ";message+=restore.what();}
                logMessage("huawei_fans",message);
            }
        }
    }
    void fixed(const std::array<int,2>& percent,bool requireAutomatic=false,const Runtime* baseline=nullptr,const std::uint64_t* diagnosticEpoch=nullptr){
        for(const auto value:percent)if(value<0||value>100)throw std::invalid_argument("Honor: PWM должен быть0–100%");
        const auto temperature=cpuTemperature();if(temperature>=80){
            const auto cleanup=automatic();
            throw std::runtime_error(std::string("Honor: ручной PWM запрещён при80°C; ")+autoResultText(cleanup));
        }
        HlylI2EcSession io;guard(io);const auto before=snapshot(io);
        if(baseline&&((before.flags&2)||(before.flags&2)!=(baseline->flags&2)||before.target0!=baseline->target0||before.target1!=baseline->target1))
            throw std::runtime_error("Honor: diagnostic Default baseline changed; old PWM not replayed");
        if(before.target0>200||before.target1>200)throw std::runtime_error("Honor: прежние fan targets неизвестны");
        if(owned&&(!(before.flags&2)||before.target0!=requested[0]*2||before.target1!=requested[1]*2)){
            // Sleep/firmware may have released the previous request before the
            // watchdog's next tick. Capture the NEW baseline for this action.
            owned=false;releasePending=false;logMessage("huawei_fans","Previous fixed ownership ended before new request");
        }
        if(requireAutomatic&&(before.flags&2))throw std::runtime_error("Honor Max: до пробы требуется подтверждённый Default");
        const bool previouslyOwned=owned;const auto previousRequest=requested;
        // GUI power lifecycle invalidates a pre-dump request independently of
        // lighting; check after fresh admission, immediately before any write.
        if(diagnosticEpoch&&diagnosticFanEpoch()!=*diagnosticEpoch)
            throw DiagnosticSuperseded("Honor: power epoch superseded diagnostic restore before write");
        bool attempted=false,sessionFinished=false;
        try{
            attempted=true;io.writeFanRuntime(0x0532,static_cast<std::uint8_t>(percent[0]*2));guard(io);
            io.writeFanRuntime(0x053c,static_cast<std::uint8_t>(percent[1]*2));guard(io);
            io.writeFanRuntime(0x042c,static_cast<std::uint8_t>(io.read(0x042c)|2));Sleep(500);guard(io);
            const auto after=snapshot(io);
            if(!(after.flags&2)||after.target0!=percent[0]*2||after.target1!=percent[1]*2||after.duty0!=percent[0]*2||after.duty1!=percent[1]*2)
                throw std::runtime_error("Honor: ручной PWM не подтверждён");
            if(!owned)originalTargets={before.target0,before.target1};
            requested=percent;owned=true;releasePending=false;
            io.finish(); // Commit ownership before exit, publish success only after it.
            sessionFinished=true;
            message="Honor: ручной PWM подтверждён; контроль температуры активен";
            logMessage("huawei_fans",std::format("Fixed PWM confirmed {}/{}%; temperature={}C; firmware manual bit1",percent[0],percent[1],temperature));
        }catch(const HlylI2EcFinishError& error){
            // Readback succeeded but transport completion did not. Preserve the
            // attempted owner for a fresh-session cleanup, never write after finish.
            releasePending=true;message=std::string(error.what())+"; ручной режим/возврат Auto не подтверждены";
            logMessage("huawei_fans",message);throw std::runtime_error(message);
        }catch(const std::exception& error){
            const std::string reason=error.what();
            message=reason;
            if(sessionFinished)throw; // Never reopen target access after checked exit.
            if(attempted)try{
                // A failed write/readback may be partial. Never replace targets
                // which are neither the captured baseline nor our attempted pair.
                const auto current=snapshot(io);
                if((current.target0!=before.target0&&current.target0!=percent[0]*2)||
                    (current.target1!=before.target1&&current.target1!=percent[1]*2))
                    throw std::runtime_error("manual rollback ownership lost; external targets preserved");
                // Release our manual mode before restoring owned targets; only
                // bit1 is restored. Firmware/other042C flags are read afresh.
                io.writeFanRuntime(0x042c,static_cast<std::uint8_t>(io.read(0x042c)&0xfd));
                io.writeFanRuntime(0x0532,static_cast<std::uint8_t>(before.target0));
                io.writeFanRuntime(0x053c,static_cast<std::uint8_t>(before.target1));
                io.writeFanRuntime(0x042c,static_cast<std::uint8_t>((io.read(0x042c)&0xfd)|(before.flags&2)));
                const auto restored=snapshot(io);
                if((restored.flags&2)!=(before.flags&2)||restored.target0!=before.target0||restored.target1!=before.target1)throw std::runtime_error("readback rollback mismatch");
                io.finish();
                logMessage("huawei_fans","Fan mode/targets rollback confirmed");
            }catch(const HlylI2EcFinishError& restore){
                // The baseline readback passed; keep its former ownership only
                // as pending cleanup until a NEW session confirms the interface.
                requested=previouslyOwned?previousRequest:percent;owned=true;releasePending=true;
                message=reason+"; завершение отката не подтверждено: "+restore.what();
                logMessage("huawei_fans",message);throw std::runtime_error(message);
            }catch(const std::exception& restore){
                // A failed rollback is not proof that our targets are present.
                // Future cleanup rechecks the attempted targets atomically.
                requested=percent;owned=true;releasePending=true;
                // An uncertain final rollback read may nevertheless have put
                // back our old fixed request. Retain its watchdog only if an
                // additional snapshot proves that exact previously-owned pair.
                try{const auto now=snapshot(io);io.finish();
                    if(!(now.flags&2)){owned=false;releasePending=false;}
                    else if(previouslyOwned&&now.target0==previousRequest[0]*2&&now.target1==previousRequest[1]*2){requested=previousRequest;releasePending=false;}
                }catch(const std::exception& observe){logMessage("huawei_fans",std::string("Rollback state observation failed: ")+observe.what());}
                message=reason+"; откат не подтверждён: "+restore.what();logMessage("huawei_fans",message);throw std::runtime_error(message);}
            throw std::runtime_error(reason+"; прежний режим восстановлен");
        }
    }
};
HlylFanController::HlylFanController():impl_(std::make_unique<Impl>()){}
HlylFanController::~HlylFanController()=default;
HlylFanState HlylFanController::read(){
    std::lock_guard lock(impl_->mutex);HlylI2EcSession io;const auto value=snapshot(io);
    const int period=io.read(0x1801);const bool valid=period==200&&value.duty0<=200&&value.duty1<=200&&value.target0<=200&&value.target1<=200;
    if(!valid)throw std::runtime_error(std::format("Honor: invalid PWM period={} targets={}/{} duty={}/{}",period,value.target0,value.target1,value.duty0,value.duty1));
    HlylFanState state{(value.flags&2)!=0,io.writable&&valid,{value.duty0/2,value.duty1/2},{value.target0/2,value.target1/2},impl_->message};
    if(state.manual)state.defaultFans.observation=NativeDefaultFanObservation::Inactive;
    else try{state.defaultFans=readHlylDefaultFans(io);}
    catch(const std::exception& error){state.defaultFans={};logMessage("huawei_default_fans",error.what());}
    io.finish();
    return state;
}
void HlylFanController::setFixed(const std::array<int,2>& percent){std::lock_guard lock(impl_->mutex);++impl_->commandGeneration;impl_->fixed(percent);}
HlylAutoResult HlylFanController::setAutomatic(){
    std::lock_guard lock(impl_->mutex);++impl_->commandGeneration;const auto result=impl_->automatic(true);
    impl_->message=std::string("Honor: ")+autoResultText(result);return result;
}
FanMaximumResult HlylFanController::measureMaximum(std::stop_token token){
    const std::string source="Honor HLYL-WXX9 · fixed PWM100/100% · HWMI fan0/fan1";
    FanMaximumResult skipped;skipped.source=source;
    if(wcsstr(GetCommandLineW(),L"--no-ec")){skipped.status="Honor Max: --no-ec";return skipped;}
    auto operation=deviceOperations().tryBegin();
    if(!operation){skipped.status="Honor Max: управление занято другой операцией";return skipped;}
    bool probeStarted=false;
    const auto read=[&]{
        // Common helper owns the30-second duration. The adapter only supplies
        // fresh, guarded observations and never equates PWM with physical RPM.
        const auto temperature=cpuTemperature();
        if(temperature>=80)throw std::runtime_error("Honor Max: температура>=80°C, требуется Auto");
        FanMaximumSample sample;
        {auto session=huawei::openSession();for(int channel=0;channel<2;++channel){
            const auto data=huawei::checkedCall(*session,huawei::command(huawei::getFan,static_cast<std::uint8_t>(channel)),3);
            const int rpm=data[1]+(data[2]<<8);
            if(rpm>15000)throw std::runtime_error("Honor Max: invalid fan"+std::to_string(channel)+" RPM="+std::to_string(rpm));
            sample.rpm[channel]=rpm;
        }}
        std::lock_guard lock(impl_->mutex);HlylI2EcSession io;guard(io);const auto current=snapshot(io);
        if(!probeStarted){if(current.flags&2)throw std::runtime_error("Honor Max: сначала требуется Default");}
        else if(!impl_->owned||impl_->releasePending||!(current.flags&2)||current.target0!=200||current.target1!=200||current.duty0!=200||current.duty1!=200)
            throw std::runtime_error("Honor Max: PWM100/100 или владение не подтверждено; проба остановлена");
        sample.percent={current.duty0/2.0,current.duty1/2.0};io.finish();return sample;
    };
    return measureMaximumFans(source,[&]{
        std::lock_guard lock(impl_->mutex);
        // fixed() rechecks Auto under ISA mutex immediately before its writes,
        // rather than trusting the earlier dumper baseline or this GUI state.
        impl_->fixed({100,100},true);probeStarted=true;
    },read,[&]{
        std::lock_guard lock(impl_->mutex);
        const auto result=impl_->automatic();impl_->message=std::string("Honor Max cleanup: ")+autoResultText(result);
        if(result==HlylAutoResult::OwnershipLost)throw std::runtime_error(impl_->message);
        // Applied/AlreadyAuto already include a fresh manual-bit readback.
        // Cancellation never bypasses this ownership-aware cleanup callback.
        logMessage("huawei_fans",impl_->message);
    },token);
}
FanDiagnosticSession HlylFanController::prepareDiagnostic(){
    const auto epoch=diagnosticFanEpoch();
    const auto keepAlive=shared_from_this();
    std::lock_guard lock(impl_->mutex);
    Runtime before;
    {HlylI2EcSession io;before=snapshot(io);io.finish();}
    const bool ours=impl_->owned&&!impl_->releasePending&&(before.flags&2)&&
        before.target0==impl_->requested[0]*2&&before.target1==impl_->requested[1]*2;
    const auto targets=impl_->requested;
    FanDiagnosticSession session;
    session.previousMode=(before.flags&2)?(ours?(targets==std::array<int,2>{100,100}?"Max":"Manual fixed PWM"):"unknown/foreign Manual"):"Default";
    const auto generation=++impl_->commandGeneration;
    impl_->automatic(true);
    Runtime baseline;
    {HlylI2EcSession io;baseline=snapshot(io);io.finish();}
    if(baseline.flags&2)throw std::runtime_error("Honor: diagnostic Default not confirmed");
    if(!ours&&(before.flags&2))return session; // Foreign replay is never admitted.
    const auto consumed=std::make_shared<bool>(false);
    session.restore=[keepAlive,generation,targets,baseline,ours,consumed,epoch]{
        auto operation=deviceOperations().tryBegin();
        std::lock_guard lock(keepAlive->impl_->mutex);auto& state=*keepAlive->impl_;
        FanDiagnosticRestore result;
        if(*consumed){result.status="Honor: diagnostic restore callback already consumed";return result;}
        if(state.commandGeneration!=generation||diagnosticFanEpoch()!=epoch){result.superseded=true;result.status="Honor: newer FAN command/power epoch superseded diagnostic restore";return result;}
        if(!operation){result.status="Honor: diagnostic restore refused; another operation owns hardware";return result;}
        *consumed=true;++state.commandGeneration; // Consume once even when a guard refuses it.
        result.attempted=true;
        try{
            if(ours){state.fixed(targets,true,&baseline,&epoch);result.status="Honor: previous owned PWM restored with readback; RPM response not inferred";}
            else{HlylI2EcSession io;const auto current=snapshot(io);io.finish();
                if(diagnosticFanEpoch()!=epoch)throw DiagnosticSuperseded("Honor: power epoch superseded diagnostic Default observation");
                if((current.flags&2)||current.target0!=baseline.target0||current.target1!=baseline.target1)throw std::runtime_error("Default baseline changed");
                result.status="Honor: previous Default still confirmed; no writes";}
            result.confirmed=true;
        }
        catch(const DiagnosticSuperseded& error){result.attempted=false;result.superseded=true;result.status=error.what();}
        catch(const std::exception& error){result.status=std::string("Honor diagnostic restore: ")+error.what();}
        return result;
    };
    return session;
}
std::shared_ptr<HlylFanController> hlylFanController(){
    if(wcsstr(GetCommandLineW(),L"--no-ec")||deviceProfile(readBoardDescriptor()).id!="huawei-hlyl-wxx9")return {};
    static std::mutex gate;static std::weak_ptr<HlylFanController> weak;
    std::lock_guard lock(gate);auto controller=weak.lock();
    if(!controller){controller=std::make_shared<HlylFanController>();weak=controller;}
    return controller;
}
}
