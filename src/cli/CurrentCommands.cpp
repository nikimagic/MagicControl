// Text adapters only. Hardware operations belong to the SAME guarded API as
// GUI; there are no raw writes, alternate register maps or startup restore.
#include "CurrentCommands.h"
#include "MagicControl/Version.h"
#include "MagicControl/BoardIdentity.h"
#include "MagicControl/DeviceProfile.h"
#include "MagicControl/HardwareInventory.h"
#include "MagicControl/Monitoring.h"
#include "MagicControl/ResourceCatalog.h"
#include "MagicControl/ResourceProvider.h"
#include "MagicControl/ControlAudit.h"
#include "MagicControl/DiagnosticDump.h"
#include "MagicControl/InfoDumpResource.h"
#include "MagicControl/InfoInventoryResource.h"
#include "MagicControl/BiosRomDump.h"
#include "MagicControl/VbiosRomDump.h"
#include "MagicControl/LaptopControl.h"
#include "MagicControl/LaptopLightingResource.h"
#include "MagicControl/LaptopChargeResource.h"
#include "MagicControl/KeyboardBacklight.h"
#include "MagicControl/QuickSwitch.h"
#include "MagicControl/Performance.h"
#include "MagicControl/FanControlLimits.h"
#include "MagicControl/PortableSettings.h"
#include "MagicControl/MicrophoneControl.h"
#include "MagicControl/GpuDevice.h"
#include "../platform/common/DiscreteGpuOperation.h"
#include "MagicControl/DeviceOperation.h"
#include "MagicControl/Logging.h"
#include "MagicControl/RomIdHistory.h"
#include "MagicControl/LiquidCooling.h"
#include "../platform/common/DiagnosticDumpInternal.h"
#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <csignal>
#include <iostream>
#include <stdexcept>
#include <stop_token>
#include <thread>
#if defined(__linux__)
#include "../platform/linux/HwmonResourceProvider.h"
#include "../platform/linux/KeysControlLinux.h"
#include "../platform/linux/SettingsServicesLinux.h"
#endif
#if defined(_WIN32)
#include "../platform/windows/ResourceDriversWindows.h"
#include "../devices/minisforum/hpbsd/FanControlWindows.h"
#include <windows.h>
#include <winrt/base.h>
#include "../platform/windows/InstanceNamesWindows.h"
#include "../devices/mechrevo/gm6ir0c/ControlWindows.h"
#include "../devices/huawei/hlyl-wxx9/FanControlWindows.h"
#include "../platform/windows/FanControlWindows.h"
#include "../platform/windows/FanDeviceActionsWindows.h"
#include "../platform/windows/ApuPowerWindows.h"
#include "../platform/windows/NativeBatteryWindows.h"
#include "../platform/windows/IteLightingWindows.h"
#include "../platform/windows/WindowsDevices.h"
#include "../platform/windows/KeysHardwareWindows.h"
#include "../platform/windows/SettingsActionWindows.h"
#include "../platform/windows/SettingsPersistenceWindows.h"
#include "../platform/windows/SettingsSnapshotWindows.h"
#include "../platform/windows/CustomModeWindows.h"
#include "../platform/windows/BatterySettingsWindows.h"
#endif

namespace mc::cli {
namespace {
using namespace std::chrono_literals;
using audit::quote;using audit::optional;
volatile std::sig_atomic_t stopped{};
void signal(int){stopped=1;}
struct Unsupported:std::runtime_error{using std::runtime_error::runtime_error;};
using detail::Options;using detail::number;using detail::boolean;using detail::arity;using detail::parse;using detail::writer;

std::string arguments(const std::vector<std::string>& values){std::string out="[";for(const auto& v:values){if(out.size()>1)out+=',';out+=quote(v);}return out+"]";}
void emit(std::string_view command,const std::string& fields){
    const auto body="\"app_version\":"+quote(MAGICCONTROL_VERSION_STRING)+",\"command\":"+quote(command)+","+fields;
    std::cout<<'{'<<body<<"}\n"<<std::flush;recordLog("cli",body);
}
void unsupported(){throw Unsupported("Function unavailable for this actual model/OS; no substitute backend was selected");}
struct Runtime {
    BoardDescriptor board{readBoardDescriptor()};
    const DeviceProfile& profile{deviceProfile(board)};
    std::stop_source stop;
    std::jthread cancellation;
    std::unique_ptr<EcAccess> ec;
#if defined(_WIN32)
    HANDLE lease{};bool owned{};
#else
    std::shared_ptr<void> applicationLease;
#endif
    explicit Runtime(bool writes){
        std::signal(SIGINT,signal);std::signal(SIGTERM,signal);
#if defined(_WIN32)
        if(writes){lease=CreateMutexW(nullptr,FALSE,windows::applicationInstanceMutex);
            if(!lease)throw std::runtime_error("Cannot acquire application ownership");
            const auto state=WaitForSingleObject(lease,0);owned=state==WAIT_OBJECT_0||state==WAIT_ABANDONED;
            if(!owned){CloseHandle(lease);lease=nullptr;throw std::runtime_error("GUI or another CLI control command is active; close it before applying SSH control");}}
#else
        if(writes)applicationLease=linux_platform::acquireApplicationControlLease();
#endif
        cancellation=std::jthread([this](std::stop_token t){while(!t.stop_requested()){if(stopped){stop.request_stop();return;}std::this_thread::sleep_for(50ms);}});
    }
    ~Runtime(){cancellation.request_stop();if(cancellation.joinable())cancellation.join();
#if defined(_WIN32)
        if(owned)ReleaseMutex(lease);if(lease)CloseHandle(lease);
#endif
    }
    EcAccess& wujie(const Options& o){
        if(o.noEc||profile.id!=kReferenceModelId)unsupported();
        if(!ec){ec=std::make_unique<EcAccess>(createDefaultBackend());if(!ec->verifyBoard().verified)throw std::runtime_error("Actual board verification failed");}
        return *ec;
    }
    template<class F>void repeat(const Options& o,F&& f,bool foreground=false){
        unsigned i{};do{f();++i;if((!o.watch&&!foreground)||(o.count&&i>=static_cast<unsigned>(o.count)))break;
            for(int elapsed=0;elapsed<o.interval&&!stopped;elapsed+=50)std::this_thread::sleep_for(50ms);
        }while(!stopped);
    }
};
void usage(){std::cout<<R"(MagicControl mctl - SSH command line, same guarded core as GUI
usage: mctl COMMAND [arguments] [--yes] [--watch] [--count N] [--interval-ms N]

  --version | --help                     no hardware access
  models | board                         registry/SMBIOS/catalogue only
  status | diag | fans [status]           current sensors; --watch for JSONL
  resources                              read-only resource catalog; --watch for JSONL
  resources all                          separate catalogs for available read-only providers
  resources hwmon                        Linux hwmon inputs, read-only
  resources drivers                      Windows installed driver metadata, read-only
  inventory                              same INFO hardware inventory JSON
  dump-ec [--output DIR]                  ReadOnly dump (minimum60s)
  dump-ec --with-fan-probes --yes         explicit GUI Default/Max diagnostic
  dump-bios | --dump-bios                 same automatic AFU/FPT16/FPT19 collector
  dump-vbios  [0000:01:00.0] [--output DIR] NVIDIA VBIOS, two reads
  dump-eeprom [0000:01:00.0] [--output DIR] Entire primary NVIDIA EEPROM, requires known capacity
  power [status|economy|office|gaming|balanced|performance|turbo|auto]
  power custom A B C [key=value ...] --yes model-native order/ranges as in GUI
    Lenovo options: gpu.offset gpu.ctgp gpu.boost cpu.tau cpu.temperature gpu.temperature
  power slot 1|2|3 --yes                  existing Custom library (RAM models)
  fans default|max --yes                 same model-specific controller
  fans manual|online FILE --yes           shared GUI fan-curves.csv; host control stays foreground
  fans fixed CPU_PERCENT GPU_PERCENT --yes  Honor foreground watchdog
  battery [status|limit N|restart N]      setters require --yes
  battery thresholds START END --yes     typed Honor/Huawei API
  battery native-turbo on|off --yes      admitted Honor Windows backend
  battery policy 4/FF|7/04|7/FF --yes    admitted models only
  battery view native|capacity --yes    native mode releases percentage control
  battery priority battery|performance --yes    Wujie X5SP4NAG only
  battery priority status --read-only          Native status and write support
  battery protection performance|balanced|healthy --yes
  battery mode normal|conservation|rapid --yes  (Lenovo)
  keyboard [status|level N|effect mono|pulse|multi|default]
  keyboard color R G B --yes              model-native RGB conversion
  keyboard pulse 1|2|3|4 --yes             native Pulse preset
  keyboard timeout SECONDS --yes          typed Honor timeout
  keyboard cycle --yes                   existing six-colour foreground cycle
  lighting zones                         actual HID zones only
  lighting set ZONE LEVEL EFFECT SPEED DIRECTION R G B --yes
  keys NAME [on|off --yes]                windows/fn/usb/ac/copilot/microphone/wifi/bluetooth/camera/touchpad
  gpu [status|power VENDOR ID VALUE|reset VENDOR ID]  vendor=nvidia/radeon
  lpp [status|discover]                   existing inert/read-only LPP interface

Writes without --yes are dry runs. No raw EC or firmware-flash commands.
Windows: run SSH under an account with the required rights; no UAC/dialogs.
Linux: only existing Linux backends; unsupported controls stay unavailable.
No startup restore. Ctrl+C cancels readers after their current operation.
Read-only requests can coexist with GUI; Windows writes require its owner lease.
)";}
void board(Runtime& r){emit("board","\"manufacturer\":"+quote(r.board.manufacturer)+",\"baseboard\":"+quote(r.board.baseBoard)+",\"product\":"+quote(r.board.productName)+",\"profile\":"+quote(r.profile.id));}
std::string battery(const BatteryReading& b){return "{\"present\":"+optional(b.present)+",\"percent\":"+optional(b.percent)+",\"watts\":"+optional(b.watts)+",\"volts\":"+optional(b.volts)+",\"milliamps\":"+optional(b.milliamps)+",\"celsius\":"+optional(b.celsius)+",\"cycles\":"+optional(b.cycles)+",\"health_percent\":"+optional(b.healthPercent)+",\"online\":"+(b.onlineKnown?std::string(b.online?"true":"false"):"null")+",\"source\":"+quote(b.source)+",\"status\":"+quote(b.powerStatus)+"}";}
void status(Runtime& r,const Options& o){
    auto hardware=o.noEc?std::unique_ptr<diagnostics::ReadOnlyDiagnosticHardware>{}:diagnostics::createReadOnlyDiagnosticHardware();
    MonitoringReader reader(hardware?hardware->access():nullptr,false,true);
    r.repeat(o,[&]{auto s=reader.read();
        if(!o.noEc&&r.profile.id=="wu-jie-15-xpro"&&hardware&&hardware->access())try{
            auto native=readNativeDefaultFans(*hardware->access());
            s.nativeDefaultFanObservation=native.observation;s.nativeDefaultFans=std::move(native.fans);
            if(s.nativeDefaultFans)for(int ch=0;ch<2;++ch)s.processors[ch].fanPwm=s.nativeDefaultFans->actualPercent[ch];
        }catch(...){}
        std::string processors="[";
        for(unsigned i=0;i<2;++i){if(i)processors+=',';const auto& p=s.processors[i];processors+="{\"model\":"+quote(s.processorModels[i])+",\"celsius\":"+optional(p.celsius)+",\"usage_percent\":"+optional(p.usagePercent)+",\"clock_mhz\":"+optional(p.clockMhz)+",\"power_watts\":"+optional(p.powerWatts)+",\"rpm\":"+optional(p.fanRpm)+",\"pwm\":"+optional(p.fanPwm)+"}";}
        std::string native;
        if(s.nativeDefaultFans){auto catalog=observeMachineResources(r.board,s);
            catalog.projectionCheck=verifyMonitoringProjection(s,catalog);
            native=",\"native_default_resources\":"+resourceCatalogJson(catalog);}
        emit("status","\"profile\":"+quote(r.profile.id)+",\"processors\":"+processors+"],\"battery\":"+battery(s.battery)+",\"presentation\":"+audit::presentation(s)+native);});
}
void resources(Runtime& r,const Options& o){
    const auto output=[&](ResourceProvider& provider){r.repeat(o,[&]{const auto catalog=provider.observe();
        // Explicit CLI output may contain device identifiers; unlike audit
        // summaries, never copy this complete payload into application logs.
        std::cout<<"{\"app_version\":"<<quote(MAGICCONTROL_VERSION_STRING)<<",\"command\":\"resources\",\"catalog\":"
            <<resourceCatalogJson(catalog)<<"}\n"<<std::flush;});};
    if(o.words.size()==2){
        if(o.words[1]=="all"){
            std::unique_ptr<diagnostics::ReadOnlyDiagnosticHardware> hardware;
            std::unique_ptr<MonitoringReader> reader;
            DeferredResourceProvider monitoring([&]() -> std::unique_ptr<ResourceProvider> {
                if(!o.noEc&&!hardware)hardware=diagnostics::createReadOnlyDiagnosticHardware();
                if(!reader)reader=std::make_unique<MonitoringReader>(hardware?hardware->access():nullptr,false,true);
                return std::make_unique<MonitoringResourceProvider>(r.board,[&]{return reader->read();});
            });
#if defined(_WIN32)
            windows::WindowsDriverResourceProvider drivers(r.board,[]{return windows::observeResourceDrivers();});
            const std::array providers{ResourceProviderEntry{"monitoring",&monitoring,{}},ResourceProviderEntry{"drivers",&drivers,{}}};
#elif defined(__linux__)
            linux_platform::HwmonResourceProvider hwmon(r.board,[token=r.stop.get_token()]{return linux_platform::scanHwmonResources("/sys/class/hwmon",token);});
            const std::array providers{ResourceProviderEntry{"monitoring",&monitoring,{}},
                ResourceProviderEntry{"hwmon",o.noEc?nullptr:&hwmon,o.noEc?"--no-ec: hwmon may access EC":""}};
#else
            const std::array providers{ResourceProviderEntry{"monitoring",&monitoring,{}}};
#endif
            r.repeat(o,[&]{const auto observed=observeResourceProviders(providers);
                std::cout<<"{\"app_version\":"<<quote(MAGICCONTROL_VERSION_STRING)<<",\"command\":\"resources all\",\"providers\":"
                    <<resourceProviderResultsJson(observed)<<"}\n"<<std::flush;});return;
        }
        if(o.words[1]=="drivers"){
#if defined(_WIN32)
            windows::WindowsDriverResourceProvider provider(r.board,[]{return windows::observeResourceDrivers();});
            output(provider);return;
#else
            unsupported();
#endif
        }
        if(o.words[1]!="hwmon")throw std::invalid_argument("Unknown resource provider");
        if(o.noEc)throw std::invalid_argument("hwmon drivers may access EC; --no-ec cannot be guaranteed");
#if defined(__linux__)
        linux_platform::HwmonResourceProvider provider(r.board,[token=r.stop.get_token()]{
            return linux_platform::scanHwmonResources("/sys/class/hwmon",token);});
        output(provider);return;
#else
        unsupported();
#endif
    }
    auto hardware=o.noEc?std::unique_ptr<diagnostics::ReadOnlyDiagnosticHardware>{}:diagnostics::createReadOnlyDiagnosticHardware();
    MonitoringReader reader(hardware?hardware->access():nullptr,false,true);
    MonitoringResourceProvider provider(r.board,[&]{return reader.read();});
    output(provider);
}
#if defined(_WIN32)
std::shared_ptr<windows::Gm6Controller> ram(Runtime& r,const Options& o){if(o.noEc||!ramFanControlProfile(r.profile.id))unsupported();auto c=windows::gm6Controller(false);if(!c)unsupported();return c;}
std::string ramState(const windows::Gm6State& s){return "{\"connected\":"+std::string(s.connected?"true":"false")+",\"writable\":"+(s.writable?"true":"false")+",\"native_profile\":"+(s.nativeProfile?quote(gm6::nativeProfileName(*s.nativeProfile)):"null")+",\"cpu_preset\":"+std::to_string(s.cpuPreset)+",\"stock_cpu_active\":"+(s.stockCpuActive?"true":"false")+",\"cpu_preset_writable\":"+(s.cpuPresetWritable?"true":"false")+",\"cpu_preset_status\":"+quote(s.cpuPresetStatus)+",\"limits\":["+std::to_string(s.limits[0])+","+std::to_string(s.limits[1])+","+std::to_string(s.limits[2])+"],\"boost\":"+(s.boost?"true":"false")+",\"custom\":"+(s.custom?"true":"false")+",\"online\":"+(s.online?"true":"false")+",\"status\":"+quote(s.status)+"}";}
void fanCommand(Runtime& r,const Options& o){
    const auto& action=o.words[1];const bool hpbsd=r.profile.id=="minisforum-hpbsd"||r.profile.id=="lenovo-legion-kwcn";arity(o,2,action=="fixed"||(hpbsd&&action=="manual")?4:action=="manual"||action=="online"?3:2);
    if(o.noEc)unsupported();
    FanCurveProfiles p;
    if(!hpbsd&&(action=="manual"||action=="online")){std::string error;if(o.words.size()!=3||!loadFanCurveProfiles(std::filesystem::u8path(o.words[2]),p,error))throw std::runtime_error("Cannot load GUI fan profiles: "+error);p.select(0,FanCurvePreset::Manual);p.select(1,FanCurvePreset::Manual);}
    auto operation=deviceOperations().tryBegin();if(!operation)throw std::runtime_error("Device operation busy");
    if(hpbsd){
        auto c=windows::nativeFixedFanController();if(!c)unsupported();
        FanResourceOperation mode;std::array<int,2> target{};
        if(action=="default")mode=FanResourceOperation::Default;
        else if(action=="max"){mode=FanResourceOperation::Maximum;target={100,100};}
        else if(action=="min"){mode=FanResourceOperation::Minimum;}
        else if(action=="manual"&&(o.words.size()==2||o.words.size()==3))mode=FanResourceOperation::Manual;
        else if((action=="fixed"||action=="manual")&&o.words.size()==4){mode=FanResourceOperation::Manual;target={number(o.words[2],fanInput::percentMinimum,fanInput::percentMaximum),number(o.words[3],fanInput::percentMinimum,fanInput::percentMaximum)};}else unsupported();
        FanWriteRequest request{mode};if(mode==FanResourceOperation::Manual){
            if(o.words.size()==2){FanCurveProfiles saved;for(int ch=0;ch<2;++ch)saved.setActivePointCount(ch,r.profile.fanPointCounts[ch]);const auto actual=c->read();if(!actual.curves)unsupported();saved.curves[0]=actual.curves->draft;request.profiles=saved;}
            else if(o.words.size()==3){std::string error;FanCurveProfiles saved;
                if(!loadFanCurveProfiles(std::filesystem::u8path(o.words[2]),saved,error))throw std::runtime_error(error);
                for(int ch=0;ch<2;++ch)saved.setActivePointCount(ch,r.profile.fanPointCounts[ch]);request.profiles=std::move(saved);}
            else request.target=target;
        }request.wait=true;windows::executeFanResource({nullptr,c.get(),nullptr,operation,hpbsd,r.profile.id},request).requireAccepted();
        operation.reset();r.repeat(o,[&]{const auto s=c->read();emit("fans","\"mode\":"+std::to_string(s.mode)+",\"target_units\":\"percent_of_nominal_max_rpm\",\"target\":["+std::to_string(s.targetPercent[0])+","+std::to_string(s.targetPercent[1])+"],\"status\":"+quote(s.status));},action!="default");return;
    }
    if(r.profile.id=="huawei-hlyl-wxx9"){
        auto c=windows::nativeFixedFanController();if(!c)unsupported();
        FanResourceOperation mode;std::array<int,2> target{};
        if(action=="default")mode=FanResourceOperation::Default;
        else if(action=="max"){mode=FanResourceOperation::Maximum;target={100,100};}
        else if(action=="fixed"&&o.words.size()==4){mode=FanResourceOperation::Manual;target={number(o.words[2],fanInput::percentMinimum,fanInput::percentMaximum),number(o.words[3],fanInput::percentMinimum,fanInput::percentMaximum)};}else unsupported();
        FanWriteRequest request{mode};if(mode==FanResourceOperation::Manual)request.target=target;request.wait=true;windows::executeFanResource({nullptr,c.get(),nullptr,operation,hpbsd,r.profile.id},request).requireAccepted();
        operation.reset();r.repeat(o,[&]{const auto s=c->read();emit("fans","\"manual\":"+std::string(s.manual?"true":"false")+",\"target\":["+std::to_string(s.targetPercent[0])+","+std::to_string(s.targetPercent[1])+"],\"status\":"+quote(s.status));},action!="default");return;
    }
    if(ramFanControlProfile(r.profile.id)){
        auto c=ram(r,o);FanResourceOperation mode;gm6::Curves curves{};
        if(action=="default")mode=FanResourceOperation::Default;else if(action=="max")mode=FanResourceOperation::Maximum;
        else if(action=="manual"||action=="online"){mode=action=="manual"?FanResourceOperation::Manual:FanResourceOperation::Online;curves=customCurvesFromFanProfiles(p);}else unsupported();
        FanWriteRequest request{mode};request.curves=curves;request.controlled={true,true};request.linked=p.linked;request.wait=true;windows::executeFanResource({c.get(),nullptr,nullptr,operation},request).requireAccepted();
        operation.reset();r.repeat(o,[&]{emit("fans","\"state\":"+ramState(c->read(true)));},action=="online");
        return; // Existing controller owns its online-exit restoration, never force Default here.
    }
    auto& ec=r.wujie(o);windows::FanController c(ec);
    FanWriteRequest request;request.wait=true;
    if(action=="default")request.operation=FanResourceOperation::Default;
    else if(action=="max")request.operation=FanResourceOperation::Maximum;
    else if(action=="manual"){request.operation=FanResourceOperation::Manual;request.ecCurve=p.ecManualCurve;}
    else if(action=="online"){request.operation=FanResourceOperation::Online;request.profiles=p;}
    else unsupported();
    windows::executeFanResource({nullptr,nullptr,&c,operation},request).requireAccepted();
    operation.reset();
    r.repeat(o,[&]{const auto s=c.status();emit("fans","\"revision\":"+std::to_string(s.revision)+",\"status\":"+quote(s.message));},action=="manual"||action=="online");
    // Preserve the existing Wujie shutdown contract: no CLI-only exit reset.
}
void completeWujieCommand(EcAccess& ec,gui::SettingsRememberChoice choice,int index=0){
    const auto actual=gui::collectSettingsSnapshot({&ec,nullptr,nullptr,nullptr,nullptr,LightingZone::Keyboard});
    if(!actual.valid)throw std::runtime_error("Command applied, but fresh state is unavailable; settings not saved");
    const auto path=portableSettingsPath("device-settings.ini");
    gui::LightingControlContext lighting;lighting.ec=&ec;lighting.model=SingleZoneModel::Wujie;
    lighting.profile=std::string(kReferenceModelId);lighting.preferences=path;
    gui::rememberSettingsChoice({actual,choice,index,0,false,false,false,false,
        [lighting]{return lighting;},[]{return false;},[path]{return path;},[]{return true;},
        [](std::string message){logMessage("cli",message);}});
}
void powerCommand(Runtime& r,const Options& o){
    const auto action=o.words.size()>1?o.words[1]:"status";
    if(r.profile.id=="lenovo-legion-kwcn"){
        if(o.noEc)unsupported();auto control=createLaptopControl();if(!control)unsupported();
        static_cast<void>(control->read());windows::CustomModeContext context;context.profileId=std::string(r.profile.id);context.laptop=control.get();
        if(action=="custom"){
            arity(o,5,11);windows::PowerWriteRequest request{windows::PowerWrite::Custom};
            const auto lo=control->customCpuPowerMinimum(),hi=control->customCpuPowerMaximum();
            for(int i=0;i<3;++i)request.values[i]=number(o.words[i+2],lo[i],hi[i]);
            for(std::size_t i=5;i<o.words.size();++i){const auto at=o.words[i].find('=');
                if(at==std::string::npos||!request.options.emplace(o.words[i].substr(0,at),number(o.words[i].substr(at+1),0,100000)).second)throw std::invalid_argument("Expected unique power option key=value");}
            windows::executePowerResource(context,request);
        }else if(action=="slot"){arity(o,3,3);windows::executePowerResource(context,{windows::PowerWrite::Preset,{},number(o.words[2],1,3)-1});}
        else if(action!="status"){arity(o,2,2);const int mode=action=="quiet"||action=="office"?0:action=="balanced"||action=="gaming"?1:action=="performance"||action=="turbo"?2:-1;
            if(mode<0)unsupported();windows::executePowerResource(context,{windows::PowerWrite::Stock,{},mode});}
        else arity(o,1,2);
        const auto actual=control->read();std::string fields="[";bool first=true;
        const auto append=[&](const PowerSetting& field){if(!first)fields+=',';first=false;fields+="{\"key\":"+quote(field.key)+",\"unit\":"+quote(field.unit)+",\"actual\":"+optional(field.actual)+",\"min\":"+std::to_string(field.minimum)+",\"max\":"+std::to_string(field.maximum)+"}";};
        for(const auto& field:actual.customCpuPowerSettings)if(field)append(*field);for(const auto& field:actual.customPowerOptions)append(field);
        emit("power","\"source\":"+quote(actual.source)+",\"stock_profile\":"+optional(actual.stockCpuProfile)+",\"custom\":"+optional(actual.customCpuPowerActive)+",\"fields\":"+fields+"]");return;
    }
    if(r.profile.id=="minisforum-hpbsd"){
        if(o.noEc)unsupported();
        if(action!="status"){
            auto control=createLaptopControl();if(!control)unsupported();
            windows::CustomModeContext context;context.profileId=std::string(r.profile.id);context.laptop=control.get();
            if(action=="custom"){arity(o,5,5);const auto maximum=control->customCpuPowerMaximum();std::array<int,3> values{};for(int i=0;i<3;++i)values[i]=number(o.words[i+2],5,maximum[i]);windows::executePowerResource(context,{windows::PowerWrite::Custom,values});}
            else if(action=="slot"){arity(o,3,3);windows::executePowerResource(context,{windows::PowerWrite::Preset,{},number(o.words[2],1,3)-1});}
            else{arity(o,2,2);if(action!="balance"&&action!="balanced"&&action!="performance")unsupported();windows::executePowerResource(context,{windows::PowerWrite::Stock,{},action=="performance"?1:0});}
        }else arity(o,1,2);
        windows::ApuPowerReader reader;const auto s=reader.read(true);
        emit("power","\"stapm_watts\":"+optional(s.stapmLimitWatts)+",\"slow_watts\":"+optional(s.slowLimitWatts)+",\"fast_watts\":"+optional(s.fastLimitWatts)+",\"stale\":"+(s.stale?"true":"false")+std::string(",\"status\":")+quote(s.status));return;
    }
    if(ramFanControlProfile(r.profile.id)){
        auto c=ram(r,o);windows::CustomModeContext context{};context.gm6=c;context.profileId=std::string(r.profile.id);
        if(action=="auto"){arity(o,2,2);windows::executePowerResource(context,{windows::PowerWrite::Automatic});}
        else if(action=="economy"||action=="minimal"){arity(o,2,2);windows::executePowerResource(context,{windows::PowerWrite::Economy});}
        else if(action=="custom"){arity(o,5,5);std::array<int,3> values{};for(int i=0;i<3;++i)values[i]=number(o.words[i+2],windows::customFieldMinimum(context),windows::customFieldMaximum(context,i));windows::executePowerResource(context,{windows::PowerWrite::Custom,values});}
        else if(action=="slot"){arity(o,3,3);windows::executePowerResource(context,{windows::PowerWrite::Preset,{},number(o.words[2],1,3)-1});}
        else if(action!="status"){arity(o,2,2);const int preset=action=="office"?0:action=="gaming"||action=="balanced"?1:action=="turbo"?2:-1;if(preset<0)throw std::invalid_argument("Unknown power preset");windows::executePowerResource(context,{windows::PowerWrite::Stock,{},preset});}
        emit("power","\"state\":"+ramState(c->read()));return;
    }
    if(r.profile.id=="huawei-hlyl-wxx9"){
        if(o.noEc)unsupported();windows::ApuPowerReader c;windows::ApuPowerDiagnostic s;
        if(action=="status"){arity(o,1,2);s=c.read(true);}else if(action=="custom"){arity(o,5,5);windows::CustomModeContext context{};context.profileId=std::string(r.profile.id);context.honor=true;context.apu=&c;s=windows::executePowerResource(context,{windows::PowerWrite::Custom,{number(o.words[2],5,100),number(o.words[3],5,100),number(o.words[4],5,100)}}).diagnostic.value();}
        else if(action=="balanced"||action=="performance"){arity(o,2,2);auto control=createLaptopControl();if(!control)unsupported();windows::CustomModeContext context{};context.profileId=std::string(r.profile.id);context.laptop=control.get();windows::executePowerResource(context,{windows::PowerWrite::Stock,{},action=="performance"?1:0});s=c.read(true);}
        else unsupported();
        emit("power","\"stapm_watts\":"+optional(s.stapmLimitWatts)+",\"slow_watts\":"+optional(s.slowLimitWatts)+",\"fast_watts\":"+optional(s.fastLimitWatts)+",\"stale\":"+(s.stale?"true":"false")+std::string(",\"status\":")+quote(s.status));return;
    }
    auto& ec=r.wujie(o);
    const auto emitObserved=[&](const EcPerformanceState& s,std::string extra={}){
        windows::ApuPowerReader reader;const auto smu=reader.read(ec.supports(DeviceFeature::SmuTelemetry));
        emit("power",extra+"\"sustained\":"+optional(s.overrides.sustained)+",\"slow\":"+optional(s.overrides.shortBoost)+",\"fast\":"+optional(s.overrides.peak)+
            ",\"ec_power\":"+audit::ecPower(s)+",\"smu_comparison\":{\"stapm\":"+optional(smu.stapmLimitWatts)+",\"slow\":"+optional(smu.slowLimitWatts)+",\"fast\":"+optional(smu.fastLimitWatts)+",\"stale\":"+(smu.stale?"true":"false")+",\"status\":"+quote(smu.status)+"}");
    };
    if(action=="auto"){arity(o,2,2);windows::CustomModeContext context{};context.ec=&ec;context.profileId=std::string(r.profile.id);windows::executePowerResource(context,{windows::PowerWrite::Automatic});
        completeWujieCommand(ec,gui::SettingsRememberChoice::Performance,4);
        emitObserved(readEcPerformance(ec),"\"automatic\":true,");return;}
    if(action!="status"){
        std::array<int,3> limits{};
        if(action=="custom"){arity(o,5,5);limits={number(o.words[2],5,kApuManualMaximum),number(o.words[3],5,kApuManualMaximum),number(o.words[4],5,kApuManualMaximum)};}
        else{arity(o,2,2);const int preset=action=="economy"||action=="minimal"?0:action=="office"?1:action=="gaming"||action=="balanced"?2:action=="turbo"?3:-1;if(preset<0)unsupported();
            if(preset>0){windows::CustomModeContext context{};context.ec=&ec;context.profileId=std::string(r.profile.id);windows::executePowerResource(context,{windows::PowerWrite::Stock,{},preset-1});completeWujieCommand(ec,gui::SettingsRememberChoice::Performance,preset);const auto s=readEcPerformance(ec);
                emitObserved(s,"\"native_profile\":"+std::to_string(preset-1)+",\"mode_751\":"+std::to_string(s.rawMode)+",");return;}
        }
        windows::CustomModeContext context{};context.ec=&ec;
        context.profileId=std::string(r.profile.id);
        windows::executePowerResource(context,{action=="custom"?windows::PowerWrite::Custom:windows::PowerWrite::Economy,limits});
        completeWujieCommand(ec,action=="custom"?gui::SettingsRememberChoice::ManualPower:gui::SettingsRememberChoice::Performance,action=="custom"?4:0);
    }
    emitObserved(readEcPerformance(ec));
}
#endif
void batteryCommand(Runtime& r,const Options& o){
    const auto action=o.words.size()>1?o.words[1]:"status";
    if(action=="status"){arity(o,1,2);MonitoringReader reader;emit("battery","\"battery\":"+battery(reader.readBatteryOnly()));return;}
    if(o.noEc)unsupported();
#if defined(_WIN32)
    if(action=="aging"){
        arity(o,3,3);const auto& value=o.words[2];
        if(value!="on"&&value!="off")throw std::invalid_argument("Battery aging: on or off required");
        windows::BatterySettingsContext context{};std::shared_ptr<windows::NativeBatteryController> native;
        if(r.profile.id==kReferenceModelId)context.ec=&r.wujie(o);
        else{native=windows::nativeBatteryController();context.native=native.get();}
        context.profile=std::string(r.profile.id);
        BatteryWriteRequest request{BatteryWrite::Aging};request.enabled=value=="on";
        const auto result=windows::executeBatteryResource(context,request);
        emit("battery","\"configuration_result\":"+quote(result)+",\"physical_enforcement_measured\":false");return;
    }
    if(r.profile.id=="lenovo-legion-kwcn"&&action=="mode"){
        arity(o,3,3);const auto& name=o.words[2];const int value=name=="normal"?0:name=="conservation"?1:name=="rapid"?2:throw std::invalid_argument("Lenovo battery mode: normal, conservation or rapid");
        auto c=auditedLaptopControl(createLaptopControl());if(!c)unsupported();windows::BatterySettingsContext context{};context.laptop=c.get();context.profile=std::string(r.profile.id);
        windows::executeBatteryResource(context,{BatteryWrite::Protection,value});emit("battery","\"typed\":"+audit::laptop(c->read()));return;
    }
#endif
    if(action=="thresholds"||r.profile.id=="huawei-hlyl-wxx9"){
        auto c=auditedLaptopControl(createLaptopControl());if(!c)unsupported();BatteryWriteRequest request;
        if(action=="thresholds"){arity(o,4,4);request=BatteryWriteRequest::pair(number(o.words[2],0,100),number(o.words[3],0,100));}
        else if(action=="limit"){arity(o,3,3);request={BatteryWrite::Upper,number(o.words[2],1,100)};}
        else if(action=="restart"){arity(o,3,3);request={BatteryWrite::Restart,number(o.words[2],1,99)};}
        else if(action=="native-turbo"){arity(o,3,3);request.kind=BatteryWrite::NativeTurbo;request.enabled=boolean(o.words[2]);}
        else unsupported();
#if defined(_WIN32)
        windows::BatterySettingsContext context{};context.laptop=c.get();context.profile=std::string(r.profile.id);
        windows::executeBatteryResource(context,request);
#else
        mc::executeBatteryResource(*c,request);
#endif
        emit("battery","\"typed\":"+audit::laptop(c->read()));return;
    }
#if defined(_WIN32)
    if(r.profile.id==kReferenceModelId){std::string result;
        if(action=="limit"){arity(o,3,3);windows::BatterySettingsContext context{};context.ec=&r.wujie(o);result=windows::executeBatteryResource(context,{BatteryWrite::Upper,number(o.words[2],1,100)});}
        else if(action=="restart"){arity(o,3,3);windows::BatterySettingsContext context{};context.ec=&r.wujie(o);result=windows::executeBatteryResource(context,{BatteryWrite::Restart,number(o.words[2],1,99)});}
        else if(action=="view"){arity(o,3,3);const auto view=o.words[2]=="native"?0:o.words[2]=="capacity"?1:throw std::invalid_argument("Battery view: native or capacity required");windows::BatterySettingsContext context{};context.ec=&r.wujie(o);context.profile=std::string(r.profile.id);result=windows::executeBatteryResource(context,{BatteryWrite::View,view});}
        else if(action=="policy"){arity(o,3,3);const auto& v=o.words[2];const auto p=v=="4/FF"?BatteryEcPolicy::Platform4Original:v=="7/04"?BatteryEcPolicy::Platform7Percent:v=="7/FF"?BatteryEcPolicy::Platform7Original:throw std::invalid_argument("Unknown EC policy");windows::BatterySettingsContext context{};context.ec=&r.wujie(o);BatteryWriteRequest request{BatteryWrite::Policy};request.policy=p;result=windows::executeBatteryResource(context,request);}
        else if(action=="priority"){
            arity(o,3,3);auto& ec=r.wujie(o);
            const auto show=[&]{const EcAccess::Transaction transaction(ec);const auto raw=ec.readUsbPriorityStatus();
                emit("battery","\"usb_priority_raw\":"+(raw?std::to_string(raw->raw):"null")+
                    ",\"control_supported\":"+(raw&&raw->writable?"true":"false")+
                    ",\"status\":"+quote(raw?raw->reason:"USB-C priority unavailable")+",\"host_status_raw\":"+std::to_string(ec.readStable(0x7cc))+
                    ",\"power_flags\":["+std::to_string(ec.readStable(0x490))+","+std::to_string(ec.readStable(0x491))+
                    "],\"performance\":"+(raw&&raw->writable?((raw->raw&0x80)?std::string("true"):std::string("false")):std::string("null"))+
                    ",\"fan_context\":["+std::to_string(ec.readStable(0x741))+","+std::to_string(ec.readStable(0x726))+","+std::to_string(ec.readStable(0x727))+","+std::to_string(ec.readStable(0x751))+","+std::to_string(ec.readStable(0x7c6))+"]");};
            if(o.words[2]=="status"){r.repeat(o,show);return;}
            const auto value=o.words[2]=="battery"?0:o.words[2]=="performance"?1:throw std::invalid_argument("USB-C priority must be battery, performance or status");
            windows::BatterySettingsContext context{};context.ec=&ec;context.profile=std::string(r.profile.id);
            windows::executeBatteryResource(context,{BatteryWrite::UsbPriority,value});result="USB-C priority confirmed through I2EC; physical watt response unverified";
            if(o.watch){r.repeat(o,show);return;}
        }
        else if(action=="protection"){
            arity(o,3,3);auto voltage=windows::nativeBatteryVoltageController();if(!voltage)unsupported();
            const auto& value=o.words[2];const int mode=value=="performance"?0:value=="balanced"?1:value=="healthy"?2:throw std::invalid_argument("Unknown protection mode");
            windows::BatterySettingsContext context{};context.voltage=voltage.get();context.ec=&r.wujie(o);context.profile=std::string(r.profile.id);
            result=windows::executeBatteryResource(context,{BatteryWrite::Protection,mode});
        }
        else unsupported();
        emit("battery","\"configuration_result\":"+quote(result)+",\"physical_enforcement_measured\":false");return;
    }
    auto c=windows::nativeBatteryController();if(!c)unsupported();arity(o,3,3);
    windows::BatterySettingsContext context{};context.native=c.get();context.profile=std::string(r.profile.id);
    BatteryWriteRequest request{BatteryWrite::Upper};
    if(action=="limit")request.value=number(o.words[2],1,100);
    else if(action=="restart"){request.kind=BatteryWrite::Restart;request.value=number(o.words[2],1,100);}
    else if(action=="policy"){request.kind=BatteryWrite::Policy;const auto& v=o.words[2];request.policy=v=="4/FF"?BatteryEcPolicy::Platform4Original:v=="7/04"?BatteryEcPolicy::Platform7Percent:v=="7/FF"?BatteryEcPolicy::Platform7Original:throw std::invalid_argument("Unknown EC policy");}
    else if(action=="protection"){request.kind=BatteryWrite::Protection;const auto& v=o.words[2];request.value=static_cast<int>(v=="performance"?BatteryProtectionMode::Performance:v=="balanced"?BatteryProtectionMode::Balanced:v=="healthy"?BatteryProtectionMode::Healthy:throw std::invalid_argument("Unknown protection mode"));}
    else if(action=="view"){request.kind=BatteryWrite::View;request.value=o.words[2]=="native"?0:o.words[2]=="capacity"?1:throw std::invalid_argument("Battery view: native or capacity required");}else unsupported();
    windows::executeBatteryResource(context,request);
    const auto s=c->read();emit("battery","\"configured_upper\":"+optional(s.configuredUpper)+",\"configured_restart\":"+optional(s.configuredRestart)+",\"platform\":"+optional(s.platformRaw)+",\"rom_id\":"+optional(s.romIdRaw)+",\"status\":"+quote(s.status)+",\"physical_enforcement_measured\":false");return;
#else
    (void)r;unsupported();
#endif
}
void keyboardCommand(Runtime& r,const Options& o){
    const auto action=o.words.size()>1?o.words[1]:"status";
    if(r.profile.id=="huawei-hlyl-wxx9"){
        if(o.noEc)unsupported();auto c=auditedLaptopControl(createLaptopControl());if(!c)unsupported();
        if(action=="level"||action=="timeout"){
            arity(o,3,3);const bool level=action=="level";const int value=number(o.words[2],0,level?2:3600);
#if defined(_WIN32)
            gui::LightingControlContext context{};context.laptop=c.get();
            gui::executeLightingResource(context,{level?LightingWrite::Level:LightingWrite::Timeout,value});
#else
            const auto result=mc::executeLightingResource(*c,{level?LightingWrite::Level:LightingWrite::Timeout,value});if(!result.success)throw std::runtime_error(result.message);
#endif
        }else if(action!="status")unsupported();
        emit("keyboard","\"state\":"+audit::laptop(c->read()));return;
    }
#if defined(_WIN32)
    if(hydrocProfile(r.profile.id)){
        if(o.noEc)unsupported();auto c=windows::iteLightingController();if(!c)unsupported();const auto s=c->read();const auto* zone=s.find(LightingZone::Keyboard);if(!zone)unsupported();
        if(action!="status"){
            if(!zone->configured)throw std::runtime_error("Independent keyboard configuration unknown; use lighting set with all fields");
            gui::LightingControlContext context{};context.ite=c.get();context.zone=LightingZone::Keyboard;context.configuration=*zone->configured;
            LightingWriteRequest request{LightingWrite::Native};
            if(action=="level"){arity(o,3,3);request={LightingWrite::Level,number(o.words[2],0,4)};}else if(action=="default"){arity(o,2,2);}
            else if(action=="effect"){arity(o,3,3);request={LightingWrite::Effect,o.words[2]=="mono"?0:o.words[2]=="pulse"?1:o.words[2]=="multi"?2:throw std::invalid_argument("Unknown effect")};}
            else if(action=="color"){arity(o,5,5);request.kind=LightingWrite::CustomColor;for(int i=0;i<3;++i)request.color[i]=static_cast<std::uint8_t>(number(o.words[i+2],0,255));}else unsupported();
            gui::executeLightingResource(context,request);
        }
        const auto after=c->read();std::string data="[";for(const auto& z:after.zones){if(data.size()>1)data+=',';data+="{\"zone\":"+quote(lightingZoneKey(z.zone))+",\"configured\":"+(z.configured?audit::lighting(*z.configured):"null")+",\"observed_level\":"+optional(z.observedLevel)+",\"status\":"+quote(z.status)+"}";}emit("keyboard","\"zones\":"+data+"]");return;
    }
    std::shared_ptr<windows::Gm6Controller> controller;if(ramFanControlProfile(r.profile.id))controller=ram(r,o);
    KeyboardColorCycle* activeCycle{};KeyboardCycleSettings settings;
    const auto lightingContext=[&]{
        gui::LightingControlContext context{};context.gm=controller.get();if(!controller)context.ec=&r.wujie(o);
        context.model=controller?SingleZoneModel::Gm6:SingleZoneModel::Wujie;
        context.profile=std::string(r.profile.id);context.preferences=portableSettingsPath(controller?std::string(gmSettings(r.profile.id)):"device-settings.ini");
        context.cycle=activeCycle;context.cycleSettings=settings;context.nativeCycleSettings=r.profile.id=="mechrevo-gm6ir0c";return context;
    };
    const auto read=[&]{return gui::readLighting(lightingContext());};
    const auto tick=[&](const KeyboardBacklightChange& change){return gui::executeLightingResource(lightingContext(),LightingWriteRequest::singleZone(change,LightingWriteOrigin::Cycle)).keyboard.value();};
    KeyboardColorCycle cycle(read,tick);activeCycle=&cycle;
    LightingWriteRequest request{LightingWrite::Native};gui::LightingChoice choice=gui::LightingChoice::Native;
    if(action=="level"){arity(o,3,3);request={LightingWrite::Level,number(o.words[2],0,4)};choice=gui::LightingChoice::Level;}
    else if(action=="effect"){arity(o,3,3);request={LightingWrite::Effect,o.words[2]=="mono"?0:o.words[2]=="pulse"?1:o.words[2]=="multi"?2:throw std::invalid_argument("Unknown effect")};choice=gui::LightingChoice::Effect;}
    else if(action=="pulse"){arity(o,3,3);request={LightingWrite::Pulse,number(o.words[2],1,4)};choice=gui::LightingChoice::Color;}
    else if(action=="color"){arity(o,5,5);request.kind=LightingWrite::CustomColor;for(int i=0;i<3;++i)request.color[i]=static_cast<std::uint8_t>(number(o.words[i+2],0,255));choice=gui::LightingChoice::CustomColor;}
    else if(action=="default"){arity(o,2,2);}
    else if(action=="cycle"){
        arity(o,2,2);std::string error;if(!loadKeyboardCycleSettings(settings,error)&&!error.empty())throw std::runtime_error(error);
        request={LightingWrite::Effect,3};choice=gui::LightingChoice::Effect;
    }else if(action!="status")unsupported();
    const auto remember=[&](const KeyboardBacklightState& actual){
        gui::rememberLightingChoice(lightingContext(),choice,request.value,
            {actual.supported,cycle.status().active,cycle.status().active,actual.keyboard,actual.effect,actual.breathing,actual.color});
    };
    if(action=="cycle"){
        {auto operation=deviceOperations().tryBegin();if(!operation)throw std::runtime_error("Device operation busy");gui::executeLightingResource(lightingContext(),request);remember(read());}
        r.repeat(o,[&]{const auto state=cycle.status();emit("keyboard_cycle","\"active\":"+std::string(state.active?"true":"false")+",\"index\":"+std::to_string(state.index)+",\"status\":"+quote(state.message));},true);cycle.stop();return;
    }
    if(action!="status")gui::executeLightingResource(lightingContext(),request);
    const auto state=read();if(action!="status")remember(state);
    std::optional<KeyboardIdleState> idle;
    if(action=="status"&&!controller)try{idle=readKeyboardIdle(r.wujie(o));}catch(...){}
    emit("keyboard","\"state\":"+audit::keyboard(state)+",\"idle\":"+audit::keyboardIdle(idle));return;
#else
    (void)o;unsupported();
#endif
}
void keysCommand(Runtime& r,const Options& o){
    arity(o,2,3);const auto& name=o.words[1];const bool setting=o.words.size()==3;const bool on=setting?boolean(o.words[2]):false;
#if defined(_WIN32)
    const auto applyKey=[&](const windows::KeysHardwareContext& context,KeyAction action){
        if(!setting)return;
        const auto result=windows::executeKeyHardwareCommand(context,action,on);
        if(!result.success)throw std::runtime_error(result.error.empty()?"KEYs readback failed":result.error);
    };
#endif
    if(name=="microphone"){
#if defined(_WIN32)
        EcAccess* indicatorEc=nullptr;
        if(!o.noEc&&uniwillMicrophoneLedProfile(r.profile.id))try{indicatorEc=&r.wujie(o);}catch(const std::exception&){}
        const windows::KeysHardwareContext context{r.profile.id,false,indicatorEc,nullptr,nullptr};
#endif
        if(setting){
#if defined(_WIN32)
            applyKey(context,KeyAction::Microphone);
#else
            std::mutex gate;LaptopControlSnapshot snapshot;MicrophoneState microphone;std::string error;
            linux_platform::executeKeyOperation(r.profile.id,false,nullptr,gate,std::pair{KeyAction::Microphone,on},snapshot,microphone,error);
            if(!error.empty())throw std::runtime_error(error);
#endif
        }
#if defined(_WIN32)
        const auto s=windows::readKeySystemDevice(context,KeyAction::Microphone);
#else
        const auto s=readDefaultMicrophone();
#endif
        emit("keys","\"name\":"+quote(name)+",\"enabled\":"+optional(s.enabled)+",\"available\":"+(s.available?"true":"false")+std::string(",\"status\":")+quote(s.detail));return;}
    if(name=="fn"&&r.profile.id=="huawei-hlyl-wxx9"){
        if(o.noEc)unsupported();auto c=auditedLaptopControl(createLaptopControl());if(!c)unsupported();
        if(setting){
#if defined(_WIN32)
            applyKey({r.profile.id,false,nullptr,c.get(),nullptr},KeyAction::Fn);
#else
            std::mutex gate;LaptopControlSnapshot snapshot;MicrophoneState microphone;std::string error;
            linux_platform::executeKeyOperation(r.profile.id,false,c.get(),gate,std::pair{KeyAction::Fn,on},snapshot,microphone,error);
            if(!error.empty())throw std::runtime_error(error);
#endif
        }
        emit("keys","\"state\":"+audit::laptop(c->read()));return;}
#if defined(_WIN32)
    const auto i=name=="windows"?0:name=="fn"?1:name=="usb"?2:name=="ac"?3:-1;
    if(name=="copilot"){
        if(o.noEc)unsupported();
        std::shared_ptr<windows::Gm6Controller> native;
        EcAccess* ec=nullptr;
        if(uniwillCopilotEcProfile(r.profile.id)){
            if(ramFanControlProfile(r.profile.id))native=ram(r,o);else ec=&r.wujie(o);
        }
        const windows::KeysHardwareContext context{r.profile.id,false,ec,nullptr,native.get()};
        applyKey(context,KeyAction::Copilot);
        const auto observed=windows::observeKeyHardware(context,KeyAction::Copilot);
        emit("keys","\"name\":\"copilot\",\"enabled\":"+optional(observed.enabled)+",\"available\":"+(observed.available?"true":"false")+",\"status\":"+quote(observed.detail));return;
    }
    if(i>=0){const auto key=quickSwitches[static_cast<std::size_t>(i)];constexpr std::array actions{KeyAction::Windows,KeyAction::Fn,KeyAction::Usb,KeyAction::Ac};const auto keyAction=actions[static_cast<std::size_t>(i)];
        if(ramFanControlProfile(r.profile.id)){auto c=ram(r,o);applyKey({r.profile.id,false,nullptr,nullptr,c.get()},keyAction);const auto s=c->readQuickSwitches()[i];emit("keys","\"enabled\":"+optional(s.enabled)+",\"writable\":"+(s.writable?"true":"false")+std::string(",\"status\":")+quote(s.detail));}
        else{auto& ec=r.wujie(o);applyKey({r.profile.id,false,&ec,nullptr,nullptr},keyAction);emit("keys","\"enabled\":"+std::string(readQuickSwitch(ec,key)?"true":"false"));}return;}
    const auto kind=name=="wifi"?windows::DeviceKind::Wifi:name=="bluetooth"?windows::DeviceKind::Bluetooth:name=="camera"?windows::DeviceKind::Camera:name=="touchpad"?windows::DeviceKind::Touchpad:throw Unsupported("Unknown KEYs switch");
    // No requestRadioAccessForUser here: SSH must never open a permission dialog.
    const auto keyAction=name=="wifi"?KeyAction::Wifi:name=="bluetooth"?KeyAction::Bluetooth:name=="camera"?KeyAction::Camera:KeyAction::Touchpad;
    // Match GUI routing: only the verified Wujie camera uses its known EC map.
    const windows::KeysHardwareContext context{r.profile.id,false,
        kind==windows::DeviceKind::Camera&&!o.noEc&&r.profile.id=="wu-jie-15-xpro"?&r.wujie(o):nullptr,nullptr,nullptr};
    applyKey(context,keyAction);const auto s=windows::readKeySystemDevice(context,keyAction);emit("keys","\"enabled\":"+optional(s.enabled)+",\"available\":"+(s.available?"true":"false")+std::string(",\"status\":")+quote(s.detail));return;
#else
    unsupported();
#endif
}

}
int run(const std::vector<std::string>& args){
    try{
        if(args.empty()||args[0]=="--help"||args[0]=="help"||args[0]=="-h"){usage();return args.empty()?1:0;}
        if(args[0]=="--version"||args[0]=="version"){std::cout<<"MagicControl "<<MAGICCONTROL_VERSION_STRING<<" (mctl, "<<currentPlatformName()<<")\n";return 0;}
        auto o=parse(args);auto& cmd=o.words[0];if(cmd=="--dump-bios")cmd="dump-bios";
        if(cmd=="models"){
            arity(o,1,1);std::string data="[";for(const auto* p:deviceProfiles()){if(data.size()>1)data+=',';data+="{\"id\":"+quote(p->id)+",\"manufacturer\":"+quote(p->manufacturer)+",\"baseboard\":"+quote(p->baseBoard)+",\"name\":"+quote(p->displayName)+",\"protocol\":"+quote(p->protocolId)+"}";}emit("models","\"profiles\":"+data+"]");return 0;
        }
        const std::array commands{"board","status","diag","resources","fans","inventory","dump-ec","dump-bios","dump-vbios","dump-eeprom","power","battery","keyboard","keys","lighting","gpu","lpp"};
        const bool gpuFirmwareDump=cmd=="dump-vbios"||cmd=="dump-eeprom";
        if(std::none_of(commands.begin(),commands.end(),[&](const char* name){return cmd==name;}))throw std::invalid_argument("Unknown command; use --help");
        if(o.probes&&cmd!="dump-ec")throw std::invalid_argument("Fan probes belong to dump-ec only");
        if(!o.output.empty()&&cmd!="dump-ec"&&cmd!="dump-bios"&&!gpuFirmwareDump)throw std::invalid_argument("Output directory belongs to dump commands only");
        const bool writes=writer(o);
        if(writes&&!o.yes){emit(cmd,"\"dry_run\":true,\"requested\":"+arguments(o.words));return 0;}
#if defined(_WIN32)
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
#endif
        Runtime r(writes);ApplicationPreferences prefs;std::string error;loadApplicationPreferences(portableSettingsPath("app-settings.ini"),prefs,error);
        if(!initializeLogging(prefs.allLogs,(cmd=="dump-bios"||gpuFirmwareDump)?o.output:std::filesystem::path{},prefs.batteryLog))
            throw std::runtime_error("Cannot initialize logs: "+loggingInitializationError());
        audit::Scope action("cli."+cmd,"\"arguments\":"+arguments(o.words));
        IdentityHistoryScope historyOrigin("user","cli."+cmd);
        const bool ownedInside=cmd=="fans"||cmd=="dump-ec"||(cmd=="keyboard"&&o.words.size()>1&&o.words[1]=="cycle");
        auto operation=writes&&!ownedInside?deviceOperations().tryBegin():std::shared_ptr<DeviceOperation>{};
        if(writes&&!ownedInside&&!operation)throw std::runtime_error("Device operation busy");
        // BIOS dump is deliberately before any EC startup read/restoration.
        if(cmd!="dump-bios"&&!gpuFirmwareDump&&cmd!="board"&&cmd!="resources")recordStartupRomId(!o.noEc);
        if(cmd=="board"){arity(o,1,1);board(r);}
        else if(cmd=="resources"){arity(o,1,2);resources(r,o);}
        else if(cmd=="status"||cmd=="diag"||(cmd=="fans"&&(o.words.size()==1||o.words[1]=="status"))){arity(o,1,2);status(r,o);}
        else if(cmd=="inventory"){
            arity(o,1,1);const auto result=executeInfoInventoryResource(r.stop.get_token());
            std::cout<<toJson(result.inventory)<<'\n';
            if(result.cancelled){std::cerr<<"Inventory cancelled\n";return 4;}
            if(!result.saved){std::cerr<<"Inventory collected but not saved: "<<result.error<<'\n';return 4;}
        }
        else if(cmd=="dump-bios"){arity(o,1,1);InfoDumpResourceRequest request;request.kind=InfoDumpKind::Bios;request.cancellation=r.stop.get_token();
            const auto s=executeInfoDumpResource(request).report;emit(cmd,"\"saved\":"+std::string(s.saved?"true":"false")+",\"message\":"+quote(s.message));return s.saved?0:4;}
        else if(gpuFirmwareDump){arity(o,1,2);InfoDumpResourceRequest request;request.kind=InfoDumpKind::Vbios;request.cancellation=r.stop.get_token();request.pci=o.words.size()==2?o.words[1]:"";request.outputRoot=o.output;
            request.vbiosMode=cmd=="dump-eeprom"?VbiosDumpMode::EntireEeprom:VbiosDumpMode::Image;
            const auto s=executeInfoDumpResource(request).vbios;const auto u=s.directory.u8string();emit(cmd,"\"outcome\":"+quote(s.outcome())+",\"cancelled\":"+(s.cancelled?"true":"false")+",\"saved\":"+std::string(s.saved?"true":"false")+",\"vbios_saved\":"+(s.vbiosSaved?"true":"false")+",\"eeprom_saved\":"+(s.eepromSaved?"true":"false")+",\"capacity_verified\":"+(s.capacityVerified?"true":"false")+",\"eeprom_bytes\":"+std::to_string(s.eepromBytes)+",\"directory\":"+quote(std::string(reinterpret_cast<const char*>(u.data()),u.size()))+",\"message\":"+quote(s.message));return s.outcome()=="completed"?0:4;}
        else if(cmd=="dump-ec"){arity(o,1,1);InfoDumpResourceRequest request;request.kind=InfoDumpKind::Ec;request.cancellation=r.stop.get_token();request.mode=o.probes?DiagnosticDumpMode::WithFanProbes:DiagnosticDumpMode::ReadOnly;request.outputRoot=o.output;
            const auto s=executeInfoDumpResource(request).report;emit(cmd,"\"saved\":"+std::string(s.saved?"true":"false")+",\"hardware_warning\":"+(s.hardwareWarning?"true":"false")+",\"hardware_warning_message\":"+quote(s.hardwareWarningMessage)+",\"message\":"+quote(s.message));return s.saved?0:4;}
        else if(cmd=="battery")batteryCommand(r,o);else if(cmd=="keyboard")keyboardCommand(r,o);else if(cmd=="keys")keysCommand(r,o);
#if defined(_WIN32)
        else if(cmd=="fans")fanCommand(r,o);else if(cmd=="power")powerCommand(r,o);
        else if(cmd=="lighting"){
            if(o.noEc||!hydrocProfile(r.profile.id))unsupported();auto c=windows::iteLightingController();if(!c)unsupported();
            if(o.words.size()>1&&o.words[1]=="set"){
                arity(o,10,10);const auto& z=o.words[2];const auto zone=z=="keyboard"?LightingZone::Keyboard:z=="lightbar"?LightingZone::Lightbar:z=="logo"?LightingZone::Logo:z=="hinge"?LightingZone::Hinge:throw std::invalid_argument("Unknown zone");
                LightingConfiguration cfg;cfg.level=number(o.words[3],0,4);cfg.effect=number(o.words[4],0,255);cfg.speed=number(o.words[5],0,255);cfg.direction=number(o.words[6],0,255);for(int i=0;i<3;++i)cfg.rgb[i]=static_cast<std::uint8_t>(number(o.words[i+7],0,255));
                gui::LightingControlContext context{};context.ite=c.get();context.zone=zone;
                gui::executeLightingResource(context,LightingWriteRequest::forZone(cfg));
            }else arity(o,1,2);
            const auto s=c->read();std::string data="[";for(const auto& z:s.zones){if(data.size()>1)data+=',';data+="{\"zone\":"+quote(lightingZoneKey(z.zone))+",\"writable\":"+(z.writable?"true":"false")+",\"configured\":"+(z.configured?audit::lighting(*z.configured):"null")+",\"observed_level\":"+optional(z.observedLevel)+",\"status\":"+quote(z.status)+"}";}emit(cmd,"\"zones\":"+data+"],\"status\":"+quote(s.status));
        }
#endif
        else if(cmd=="gpu"){
            const auto sub=o.words.size()>1?o.words[1]:"status";
            if(sub=="status"){arity(o,1,2);for(const auto vendor:{GpuVendor::Nvidia,GpuVendor::Radeon}){DiscreteGpu c(vendor);auto result=executeGpuOperation(c,{},{});if(result.readFailed)throw std::runtime_error(result.message);emit(cmd,"\"vendor\":"+quote(gpuVendorName(vendor))+",\"devices\":"+audit::gpuDisplay(result.readings));}}
            else{arity(o,sub=="power"?5:4,sub=="power"?5:4);const auto vendor=o.words[2]=="nvidia"?GpuVendor::Nvidia:o.words[2]=="radeon"?GpuVendor::Radeon:throw std::invalid_argument("Unknown GPU vendor");DiscreteGpu c(vendor);GpuOperationRequest request;
                request.providerId=o.words[3];if(sub=="power"){request.kind=GpuOperation::SetPower;request.value=number(o.words[4],-100,1000);}else if(sub=="reset")request.kind=GpuOperation::ResetPower;else unsupported();
                auto result=executeGpuOperation(c,request,{audit::capture(),false,{},operation});if(!result.success)throw std::runtime_error(result.message);emit(cmd,"\"message\":"+quote(result.message)+",\"refresh_failed\":"+(result.readFailed?"true":"false")+",\"devices\":"+audit::gpuDisplay(result.readings));}
        }
        else if(cmd=="lpp"){
            arity(o,1,2);auto c=createLiquidCoolingController();if(!c)unsupported();
            if(o.words.size()>1&&o.words[1]=="discover"){std::string devices="[";for(const auto& d:c->discover(r.stop.get_token())){if(devices.size()>1)devices+=',';devices+="{\"id\":"+quote(d.id)+",\"name\":"+quote(d.name)+"}";}emit(cmd,"\"devices\":"+devices+"]");}else emit(cmd,"\"state\":"+liquidCoolingDiagnosticsJson());
        }else unsupported();
        action.finish("completed");return 0;
    }catch(const std::invalid_argument& e){std::cerr<<"mctl: "<<e.what()<<'\n';return 1;}
    catch(const Unsupported& e){std::cerr<<"mctl: "<<e.what()<<'\n';return 6;}
    catch(const BackendUnavailable& e){std::cerr<<"mctl: "<<e.what()<<"; "<<e.hint()<<'\n';return 3;}
    catch(const std::exception& e){logMessage("cli_error",e.what());std::cerr<<"mctl: "<<e.what()<<'\n';return 4;}
}
}
