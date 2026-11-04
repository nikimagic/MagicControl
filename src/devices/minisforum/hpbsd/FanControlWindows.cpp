#include "MagicControl/ResourceControl.h"
#include "FanControlWindows.h"
#include "I2EcWindows.h"
#include "TelemetryWindows.h"
#include "PowerControlWindows.h"
#include "../../../platform/windows/ApuPowerWindows.h"
#include "../../../protocols/minisforum/HpbsdFans.h"
#include "../../../protocols/minisforum/HpbsdPower.h"
#include "MagicControl/PortableSettings.h"
#include "MagicControl/Logging.h"
#include "MagicControl/ActionLog.h"
#include <windows.h>
#include <algorithm>
#include <condition_variable>
#include <cmath>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>

namespace mc::windows {
namespace {
using namespace hpbsd;
Snapshot snapshot(HpbsdI2EcSession& io){
    Snapshot s{};s[0]=io.read(0x32f);s[1]=io.read(0x88a);s[2]=io.read(0x88b);
    for(int i=0;i<24;++i)s[3+i]=io.read(static_cast<std::uint16_t>(0x310+i));
    for(int i=0;i<8;++i)s[27+i]=io.read(static_cast<std::uint16_t>(0x8b0+i));
    for(int i=0;i<9;++i)s[35+i]=io.read(static_cast<std::uint16_t>(0x330+i));
    for(int i=0;i<3;++i)s[44+i]=io.read(static_cast<std::uint16_t>(0x8c0+i));return s;
}
Snapshot stable(HpbsdI2EcSession& io){for(int attempt=0;attempt<3;++attempt){const auto a=snapshot(io),b=snapshot(io);if(a==b){validateImmutable(a);return a;}}throw std::runtime_error("HPBSD: fan table changed during snapshot");}
struct Lease {Byte selector{};int mode{};std::array<int,2> target{};Mutable from{},to{};bool recovering{};Mutable recoveryFrom{};bool curved{};bool factorySwitch{};Byte factorySelector{};};
bool matches(const Lease& l,const Mutable& observed){return l.recovering?reachable(l.recoveryFrom,l.factorySwitch?l.from:values(factory(l.selector)),observed):reachable(l.from,l.to,observed);}
bool leaseMatches(const Lease& l,const Snapshot& s){
    if(!l.factorySwitch)return l.selector==s[0]&&matches(l,values(s));
    if(s[0]!=l.selector&&s[0]!=l.factorySelector)return false;
    return l.recovering?matches(l,values(s)):s[0]==l.selector?matches(l,values(s)):values(s)==l.to;
}
const auto receiptName="hpbsd-fan-ownership.txt";
std::optional<Lease> loadReceipt(){
    const auto path=portableSettingsPath(receiptName);if(!std::filesystem::exists(path))return {};
    if(std::filesystem::file_size(path)>4096)throw std::runtime_error("HPBSD: oversized fan receipt");
    std::ifstream file(path);std::string header,kind;int version{},selector{};Lease l;
    if(!(file>>header>>version>>kind)||header!="HPBSD_FAN_LEASE"||(version<2||version>5))throw std::runtime_error("HPBSD: invalid fan receipt");
    if(kind=="released")return {};
    int recovery{};
    if(kind!="active"||!(file>>selector>>l.mode>>l.target[0]>>l.target[1]>>recovery)||selector<0||selector>255||l.mode<0||l.mode>3||recovery<0||recovery>1)throw std::runtime_error("HPBSD: invalid fan lease metadata");
    l.recovering=recovery!=0;
    l.selector=static_cast<Byte>(selector);
    if(version>=3){int curved{};if(!(file>>curved)||curved<0||curved>1)throw std::runtime_error("HPBSD: invalid curve lease");l.curved=curved!=0;}
    if(version>=5){int changing{},target{};if(!(file>>changing>>target)||changing<0||changing>1||target<0||target>255)throw std::runtime_error("HPBSD: invalid factory lease");l.factorySwitch=changing!=0;l.factorySelector=static_cast<Byte>(target);}
    if(l.curved&&(!fanInput::validPercent(l.target[0])||!fanInput::validPercent(l.target[1])))throw std::runtime_error("HPBSD: invalid curve target metadata");
    for(auto* row:{&l.from,&l.to,&l.recoveryFrom}){*row=values(factory(l.selector));for(int i=0;i<(version==2?16:version==3?30:34);++i){int v{};if(!(file>>v)||v<0||v>255)throw std::runtime_error("HPBSD: invalid fan lease values");(*row)[i]=static_cast<Byte>(v);}}
    std::string extra;if(file>>extra)throw std::runtime_error("HPBSD: trailing fan receipt data");
    // A receipt grants no arbitrary write: both complete endpoints must be a
    // compiled canonical table or a supported nonzero fixed normal-row state.
    if(l.factorySwitch){
        if(l.mode!=0||l.curved||l.from!=values(factory(l.selector))||l.to!=values(factory(l.factorySelector)))throw std::runtime_error("HPBSD: noncanonical factory transition");
    }else if(!allowed(l.from,l.selector)||!allowed(l.to,l.selector))throw std::runtime_error("HPBSD: unsupported fan lease endpoints");
    if(l.mode==0&&!l.factorySwitch){if(l.to!=values(factory(l.selector)))throw std::runtime_error("HPBSD: invalid restore endpoint");}
    else if(!l.factorySwitch&&(l.curved?(l.mode!=1||!curveAllowed(l.to,l.selector)):(l.to!=manual(l.target,l.selector))))throw std::runtime_error("HPBSD: target/receipt mismatch");
    if(l.recovering&&!reachable(l.from,l.to,l.recoveryFrom))throw std::runtime_error("HPBSD: invalid recovery starting state");
    return l;
}
void saveReceipt(const std::optional<Lease>& lease){
    std::ostringstream out;out<<"HPBSD_FAN_LEASE 5 ";
    if(!lease)out<<"released\n";
    else{const auto& l=*lease;out<<"active "<<int(l.selector)<<' '<<l.mode<<' '<<l.target[0]<<' '<<l.target[1]<<' '<<l.recovering<<' '<<l.curved<<' '<<l.factorySwitch<<' '<<int(l.factorySelector)<<'\n';for(const auto* row:{&l.from,&l.to,&l.recoveryFrom}){for(auto v:*row)out<<int(v)<<' ';out<<'\n';}}
    std::string error;if(!savePortableText(portableSettingsPath(receiptName),out.str(),error))throw std::runtime_error("HPBSD: cannot save fan ownership: "+error);
}
}
struct HpbsdFanController::Impl {
    std::mutex mutex;std::condition_variable wake;bool stopping{},owned{},restorePending{};
    HANDLE owner{};Lease lease;std::string message;ResourceActor watchdog;
    std::optional<NativeFanCurves> curveDraft;Byte curveSelector{},preferredFactory{};
    Impl():watchdog([this]{watch();}){refreshFactoryPreference();}
    void refreshFactoryPreference(){
        // GUI and CLI share the saved selection. Refresh at an explicit FAN
        // command so a long-lived observer cannot restore its older profile.
        try{const auto p=savedHpbsdPowerProfile();preferredFactory=p?stockFanSelector(*p):0;}
        catch(const std::exception& e){logMessage("hpbsd_power",e.what());}
    }
    void acquire(){
        if(owner)return;auto gate=CreateSemaphoreW(nullptr,1,1,L"Global\\MagicControl.HpbsdFansOwner.v1");
        if(!gate)throw std::runtime_error("HPBSD: owner semaphore unavailable");
        if(WaitForSingleObject(gate,0)!=WAIT_OBJECT_0){CloseHandle(gate);throw std::runtime_error("HPBSD: fans controlled by another MagicControl process");}owner=gate;
    }
    void release(){if(owner){ReleaseSemaphore(owner,1,nullptr);CloseHandle(owner);owner=nullptr;}}
    NativeFanCurves draft(Byte selector){
        if(curveDraft){static_cast<void>(curve(*curveDraft,selector));return *curveDraft;}
        auto recipe=defaultDraft(selector);
        // Shared profile storage is authoritative after conversion. Legacy
        // native text remains a read-only import source; no observer writes it.
        FanCurveProfiles savedProfiles;for(int ch=0;ch<2;++ch)savedProfiles.setActivePointCount(ch,bandCount[ch]);
        const auto shared=portableSettingsPath("hpbsd-fan-profiles.csv");std::string loadError;
        if(std::filesystem::exists(shared)){
            if(!loadFanCurveProfiles(shared,savedProfiles,loadError))throw std::runtime_error("Shared FAN profile unreadable; legacy fallback refused: "+loadError);
            std::ifstream format(shared);std::string first;std::getline(format,first);
            const auto legacyDefaults=recipe;recipe=savedProfiles.curves[0];
            if(first.starts_with("Version,4")){static_cast<void>(curve(recipe,selector));curveSelector=selector;curveDraft=recipe;return recipe;}
            for(int ch=0;ch<2;++ch)for(int i=0;i<bandCount[ch];++i)recipe[ch][i]=legacyDefaults[ch][i];
        }
        const auto path=portableSettingsPath("hpbsd-fan-curves.txt");
        if(std::filesystem::exists(path))try{
            if(std::filesystem::file_size(path)>4096)throw std::runtime_error("oversized draft");
            std::ifstream file(path);std::string tag,extra;int version{},selected{};
            if(!(file>>tag>>version>>selected)||tag!="HPBSD_FAN_CURVES"||(version!=1&&version!=2))throw std::runtime_error("invalid draft");
            auto saved=recipe;for(int ch=0;ch<2;++ch)for(int i=0;i<bandCount[ch];++i)if(!(file>>saved[ch][i].temperature>>saved[ch][i].duty))throw std::runtime_error("truncated draft");
            if(file>>extra)throw std::runtime_error("trailing draft data");
            if(selected==0xb1||selected==0xb2){if(version==1)saved=normalizeDraft(saved);static_cast<void>(curve(saved,selector));recipe=saved;}
        }catch(const std::exception& e){logMessage("hpbsd_fans",std::string("Curve draft ignored, original file retained: ")+e.what());}
        curveSelector=selector;curveDraft=recipe;return recipe;
    }
    void saveDraft(const NativeFanCurves& recipe,Byte selector){
        static_cast<void>(curve(recipe,selector));
        // File persistence belongs to the common FAN resource completion.
        curveSelector=selector;curveDraft=recipe;
    }
    void transact(HpbsdI2EcSession& io,const Snapshot& before,const Mutable& desired,int mode,const std::array<int,2>& target,bool curved=false){
        lease={before[0],mode,target,values(before),desired};
        lease.curved=curved;
        // A crashed/partial transaction is recoverable only along this exact
        // recorded prefix. Save before the first EC payload write.
        saveReceipt(lease);owned=true;restorePending=true;
        for(const auto [i,value]:plan(lease.from,desired))io.writeFanByte(mutableAddresses[i],value);
        const auto after=stable(io);
        if(after[0]!=before[0]||values(after)!=desired)throw std::runtime_error("HPBSD: complete fan policy readback failed");
        io.finish();restorePending=false;
    }
    void factoryProfile(Byte selector){
        const auto target=factory(selector);acquire();
        try{
            HpbsdI2EcSession io;const auto before=stable(io);
            if(before!=factory(before[0]))throw std::runtime_error("HPBSD: factory change requires verified Default tables");
            if(before==target){io.finish();release();return;}
            lease={before[0],0,{},values(before),values(target)};lease.factorySwitch=true;lease.factorySelector=selector;
            saveReceipt(lease);owned=true;restorePending=true;
            for(const auto [i,v]:plan(lease.from,lease.to))io.writeFanByte(mutableAddresses[i],v);
            const auto planned=stable(io);
            if(planned[0]!=before[0]||values(planned)!=lease.to)throw std::runtime_error("HPBSD: planned native table not confirmed");
            io.selectFanFactory(before[0],selector);
            if(stable(io)!=target)throw std::runtime_error("HPBSD: native factory table/selector mismatch");
            io.finish();saveReceipt({});owned=false;restorePending=false;message.clear();release();
        }catch(const std::exception& error){
            const std::string cause=error.what();
            if(owned)try{const auto preferred=preferredFactory;preferredFactory=0;try{automatic();}catch(...){preferredFactory=preferred;throw;}preferredFactory=preferred;}
                catch(const std::exception& rollback){throw std::runtime_error(cause+"; native FAN rollback unconfirmed: "+rollback.what());}
            else release();
            throw;
        }
    }
    NativeFanAutoResult automatic(){
        acquire();
        try{
            HpbsdI2EcSession io;const auto s=stable(io),baseline=factory(s[0]);
            const auto old=owned?std::optional<Lease>(lease):loadReceipt();
            // A committed selector may already look like canonical Default,
            // yet still belong to an interrupted factory transaction. Recover
            // its original endpoint before releasing that journal.
            const bool factoryPending=old&&old->factorySwitch&&leaseMatches(*old,s);
            if(s==baseline&&!factoryPending){io.finish();saveReceipt({});owned=false;restorePending=false;message.clear();release();if(preferredFactory&&preferredFactory!=s[0])factoryProfile(preferredFactory);return NativeFanAutoResult::AlreadyAuto;}
            if(!old||!leaseMatches(*old,s))throw std::runtime_error("HPBSD: foreign/unknown fan table; Default refused");
            lease=*old;if(!lease.recovering){lease.recovering=true;lease.recoveryFrom=values(s);saveReceipt(lease);}owned=true;restorePending=true;
            // Journal the restoration prefix as well, so interruption during
            // Default/rollback remains recoverable after a process restart.
            const auto restore=lease.factorySwitch?factory(lease.selector):baseline;
            for(const auto [i,value]:plan(values(s),values(restore)))io.writeFanByte(mutableAddresses[i],value);
            io.selectFanFactory(s[0],restore[0]);
            if(stable(io)!=restore)throw std::runtime_error("HPBSD: firmware fan table restore not confirmed");
            io.finish();saveReceipt({});owned=false;restorePending=false;message.clear();release();
            if(preferredFactory&&preferredFactory!=restore[0])factoryProfile(preferredFactory);
            logMessage("hpbsd_fans","Default: selected native CPU/system tables restored; critical rows retained");return NativeFanAutoResult::Applied;
        }catch(...){restorePending=owned;if(!owned)release();throw;}
    }
    NativeFixedFanState state(){
        HpbsdI2EcSession io;const auto s=stable(io),baseline=factory(s[0]);
        const std::array<int,2> temperatures{io.read(0x888),io.read(0x889)},duty{io.read(0x1803),io.read(0x1804)};const int period=io.read(0x1841);io.finish();
        NativeFixedFanState out;out.targetRpm=true;out.actualPercentKnown=false;out.minimumSupported=true;out.minimumPercent=hpbsd::minimumPercent;out.source="HPBSD ITE5571 · requested RPM targets";
        NativeFanCurveState curves;curves.selector=s[0];curves.actual=points(s);curves.draft=draft(s[0]);
        for(int ch=0;ch<2;++ch){if(temperatures[ch]>0&&temperatures[ch]<=120)curves.temperatures[ch]=temperatures[ch];for(int i=0;i<bandCount[ch];++i)curves.slopes[ch][i]=s[(ch?44:27)+i];}
        out.curves=curves;
        out.actualPercentKnown=period==127&&duty[0]<=period&&duty[1]<=period;
        if(out.actualPercentKnown)for(int ch=0;ch<2;++ch)out.actualPercent[ch]=(duty[ch]*100+period/2)/period;
        const auto saved=owned?std::optional<Lease>(lease):loadReceipt();
        if(s==baseline&&!(saved&&saved->factorySwitch&&leaseMatches(*saved,s))){owned=false;restorePending=false;release();out.writable=true;out.mode=0;out.status=message.empty()?"HPBSD: Default EC; native CPU/system thermal control":message;return out;}
        if(saved&&leaseMatches(*saved,s)){
            out.manual=true;out.writable=true;out.targetPercent=saved->target;
            out.needsRestore=!owned||restorePending||saved->recovering||values(s)!=saved->to;out.mode=out.needsRestore?-1:saved->mode;
            out.curves->appliedCurve=saved->curved&&!out.needsRestore&&curve(out.curves->draft,s[0])==values(s);
            out.status=out.needsRestore?"HPBSD: interrupted control; select Default for verified recovery":saved->curved?"HPBSD: native CPU/System curves confirmed; firmware hot-temperature rows retained":"HPBSD: requested RPM targets confirmed; firmware hot-temperature rows retained";
            if(!message.empty())out.status+="; "+message;
        }else{owned=false;release();out.status="HPBSD: external fan table; control unavailable";}
        return out;
    }
    ApuPowerDiagnostic power(std::optional<int> profile,const std::array<int,3>& target,ApuPowerReader& reader){
        const auto fields=[](const ApuPowerDiagnostic& p){
            if(p.stale||p.tableVersion!=0x4c0009||!p.stapmLimitWatts||!p.slowLimitWatts||!p.fastLimitWatts)throw std::runtime_error("HPBSD: fresh power baseline unavailable");
            return std::array<int,3>{static_cast<int>(std::lround(*p.stapmLimitWatts*1000)),static_cast<int>(std::lround(*p.slowLimitWatts*1000)),static_cast<int>(std::lround(*p.fastLimitWatts*1000))};
        };
        // Refuse corrupt preferences before the first payload write.
        static_cast<void>(savedHpbsdPowerProfile());
        const auto beforePower=fields(reader.read(true));
        if(!recoverablePower(beforePower))throw std::runtime_error("HPBSD: power baseline is not recoverable");
        const auto observed=state();
        if(!observed.writable||observed.needsRestore)throw std::runtime_error("HPBSD: resolve FAN ownership/recovery before changing CPU profile");
        Snapshot before;
        {HpbsdI2EcSession io;before=stable(io);if(io.read(0x361)!=0x0c)throw std::runtime_error("HPBSD: native power branch EC361=0C not confirmed");io.finish();}
        const auto selector=profile?stockFanSelector(*profile):before[0];
        const bool follow=profile&&observed.mode==0;bool fanChanged=false;
        const auto previousPreference=preferredFactory;
        try{
            // Increase cooling before raising power; lower power before reducing cooling.
            if(follow&&*profile==1&&before[0]!=selector){factoryProfile(selector);fanChanged=true;}
            auto result=profile?reader.applyHpbsdStockProfile(*profile):reader.applyHpbsdLimitsMilliwatts(target);
            if(follow&&*profile==0&&before[0]!=selector){factoryProfile(selector);fanChanged=true;}
            if(!follow){HpbsdI2EcSession io;if(stable(io)!=before)throw std::runtime_error("HPBSD: user FAN policy changed during CPU-only profile switch");io.finish();}
            // The final FAN selector is part of this transaction; do not save
            // an earlier SMU sample if firmware reacted to that selector.
            result=reader.read(true);
            if(fields(result)!=target||fields(reader.read(true))!=target)throw std::runtime_error("HPBSD: final power/FAN profile not confirmed");
            if(profile)saveHpbsdPowerProfile(*profile);
            else saveHpbsdCustomPower({target[0]/1000,target[1]/1000,target[2]/1000},selector-0xb1);
            preferredFactory=selector;
            logMessage("hpbsd_power",std::string(profile?"Stock":"Custom")+" profile confirmed; Default FAN follows="+(follow?"true":"false")+"; current FAN policy preserved for CPU-only Custom; CMOS/Setup writes=0");
            return result;
        }catch(const std::exception& failure){
            const std::string reason=failure.what();preferredFactory=previousPreference;
            try{
                if(fields(reader.read(true))!=beforePower){
                    if(beforePower==hpbsd::stockPower(1))reader.applyHpbsdStockProfile(1);
                    else reader.applyHpbsdLimitsMilliwatts(beforePower);
                }
                if(fields(reader.read(true))!=beforePower||fields(reader.read(true))!=beforePower)throw std::runtime_error("power baseline not confirmed");
                if(fanChanged)factoryProfile(before[0]);
                {HpbsdI2EcSession io;if(stable(io)!=before)throw std::runtime_error("FAN baseline changed externally");io.finish();}
            }catch(const std::exception& recovery){throw std::runtime_error(reason+"; HPBSD profile rollback unconfirmed: "+recovery.what());}
            throw std::runtime_error(reason+"; previous power/FAN state confirmed");
        }
    }
    void fixed(const std::array<int,2>& target){
        acquire();
        try{
            HpbsdI2EcSession io;const auto s=stable(io);
            const auto desired=manual(target,s[0]);
            const auto current=values(s),baseline=values(factory(s[0]));
            if(current!=baseline&&(!owned||restorePending||lease.selector!=s[0]||current!=lease.to))throw std::runtime_error("HPBSD: select Default before changing an unowned fan table");
            const auto cpu=io.read(0x309),system=io.read(0x305);
            if(cpu==0||cpu>120||system==0||system>120||(target!=std::array<int,2>{100,100}&&(cpu>=80||system>=70)))throw std::runtime_error("HPBSD: manual request requires fresh CPU<80C/system<70C; Max remains available");
            const int mode=target==std::array<int,2>{100,100}?2:target==hpbsd::minimumPercent?3:1;
            transact(io,s,desired,mode,target);message.clear();wake.notify_all();
            if(mode==1){std::string error;const auto text=std::to_string(target[0])+" "+std::to_string(target[1])+"\n";if(!savePortableText(portableSettingsPath("hpbsd-fan-targets.txt"),text,error))throw std::runtime_error("HPBSD: Manual targets not saved: "+error);}
            logMessage("hpbsd_fans","Fan target readback confirmed; CPU codes3..51/system2..40; hot MAX rows retained");
        }catch(const std::exception& error){
            const std::string reason=error.what();
            if(owned)try{automatic();}catch(const std::exception& restore){message="Default recovery pending: "+std::string(restore.what());throw std::runtime_error(reason+"; "+message);}
            else release();throw std::runtime_error(reason);
        }
    }
    void curves(const NativeFanCurves& recipe){
        bool attempted{};
        acquire();
        try{
            HpbsdI2EcSession io;const auto before=stable(io);const auto desired=curve(recipe,before[0]);
            const auto current=values(before);
            if(current!=values(factory(before[0]))&&(!owned||restorePending||lease.selector!=before[0]||current!=lease.to))throw std::runtime_error("HPBSD: Default required before changing an unowned policy");
            const int cpu=io.read(0x309),system=io.read(0x305);
            if(cpu<=0||cpu>=94||system<=0||system>=83)throw std::runtime_error("HPBSD: curve apply requires fresh CPU<94C/System<83C");
            attempted=true;
            transact(io,before,desired,1,{recipe[0][0].duty,recipe[1][0].duty},true);
            saveDraft(recipe,before[0]);message.clear();wake.notify_all();logMessage("hpbsd_fans","Native curve readback confirmed; editable20..100C/5..100%; separate hot MAX stages retained or earlier");
        }catch(const std::exception& error){const std::string reason=error.what();if(attempted&&owned)try{automatic();}catch(const std::exception& restore){message=std::string("Default recovery pending: ")+restore.what();throw std::runtime_error(reason+"; "+message);}else if(!owned)release();throw std::runtime_error(reason);}
    }
    void watch(){
        std::unique_lock lock(mutex);
        while(!stopping){wake.wait_for(lock,std::chrono::seconds(1),[&]{return stopping;});if(stopping)break;if(!owned)continue;
            try{const auto status=state();if(!owned)continue;
                HpbsdI2EcSession io;const auto cpu=io.read(0x309),system=io.read(0x305);io.finish();
                if(status.needsRestore||!cpu||cpu>120||!system||system>120||(!lease.curved&&lease.mode!=2&&(cpu>=80||system>=70))){automatic();message="HPBSD: thermal/ownership guard returned Default";}
            }catch(const std::exception& error){message=error.what();try{automatic();message="HPBSD: telemetry failed; Default restored";}catch(const std::exception& restore){message+="; Default not confirmed: "+std::string(restore.what());}logMessage("hpbsd_fans",message);}
        }
    }
    ~Impl(){
        {std::lock_guard lock(mutex);stopping=true;wake.notify_all();}if(watchdog.joinable())watchdog.join();
        if(owned)try{automatic();}catch(const std::exception& e){logMessage("hpbsd_fans",std::string("Exit restore not confirmed: ")+e.what());}release();
    }
};
HpbsdFanController::HpbsdFanController():impl_(std::make_unique<Impl>()){}
HpbsdFanController::~HpbsdFanController()=default;
NativeFixedFanState HpbsdFanController::read(){std::lock_guard lock(impl_->mutex);return impl_->state();}
void HpbsdFanController::setFixed(const std::array<int,2>& target){audit::Scope scope("hpbsd.fans.fixed");std::lock_guard lock(impl_->mutex);impl_->refreshFactoryPreference();impl_->fixed(target);scope.finish("readback_confirmed");}
void HpbsdFanController::setCurves(const NativeFanCurves& curves){audit::Scope scope("hpbsd.fans.curves");std::lock_guard lock(impl_->mutex);impl_->refreshFactoryPreference();impl_->curves(curves);scope.finish("readback_confirmed");}
void HpbsdFanController::saveCurveDraft(const NativeFanCurves& curves){std::lock_guard lock(impl_->mutex);if(!impl_->curveDraft)throw std::runtime_error("HPBSD: no confirmed native curve selector");impl_->saveDraft(curves,impl_->curveSelector);}
NativeFanAutoResult HpbsdFanController::setAutomatic(){audit::Scope scope("hpbsd.fans.default");std::lock_guard lock(impl_->mutex);impl_->refreshFactoryPreference();const auto r=impl_->automatic();scope.finish("readback_confirmed");return r;}
ApuPowerDiagnostic HpbsdFanController::applyStockPowerProfile(int profile,ApuPowerReader& reader){audit::Scope scope("hpbsd.power.stock");const auto target=hpbsd::stockPower(profile);std::lock_guard lock(impl_->mutex);const auto result=impl_->power(profile,target,reader);scope.finish("readback_confirmed");return result;}
ApuPowerDiagnostic HpbsdFanController::applyCustomPower(const std::array<int,3>& values,ApuPowerReader& reader){audit::Scope scope("hpbsd.power.custom");const auto target=hpbsd::customPowerMilliwatts(values);std::lock_guard lock(impl_->mutex);const auto result=impl_->power({},target,reader);scope.finish("readback_confirmed");return result;}
std::shared_ptr<HpbsdFanController> hpbsdFanController(){
    if(wcsstr(GetCommandLineW(),L"--no-ec")||!HpbsdTelemetryReader::supportedHost())return {};static std::mutex gate;static std::weak_ptr<HpbsdFanController> cached;
    std::lock_guard lock(gate);auto c=cached.lock();if(!c){c=std::make_shared<HpbsdFanController>();cached=c;}return c;
}
}
