#include "MagicControl/ResourceCatalog.h"
#include "MagicControl/ResourceProvider.h"
#include "MagicControl/ResourceControl.h"
#include "MagicControl/KeyResourceControl.h"
#include "MagicControl/FanResourceControl.h"
#include "MagicControl/SettingsResourceControl.h"
#include "MagicControl/InfoResourceControl.h"
#include "MagicControl/InfoDumpResource.h"
#include "MagicControl/InfoInventoryResource.h"
#include "MagicControl/GpuResourceControl.h"
#include "MagicControl/Monitoring.h"
#include "MagicControl/EcAccess.h"
#include "MagicControl/ControlAudit.h"
#include "MagicControl/DeviceProfile.h"
#include "../src/gui/common/MonitoringCards.h"
#include "MagicControl/BatteryResourceState.h"
#include "../archive/resource-shadow-0.1.384/ProjectionReference.h"
#include "../archive/resource-shadow-0.1.384/LegacyMonitoringCards.h"
#include <cstdlib>
#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#ifdef __linux__
#include "../src/platform/linux/KeysControlLinux.h"
#include "MagicControl/LaptopControl.h"
#include <mutex>
struct FakeLinuxKeyControl final:mc::LaptopControl {
    int writes{};bool fn{};
    mc::LaptopControlSnapshot read() override{mc::LaptopControlSnapshot s;s.fnLock=fn;return s;}
    void setChargeThresholds(int,int) override{throw std::runtime_error("unexpected battery write");}
    void setFnLock(bool value) override{++writes;fn=value;}
};
#endif
#if defined(_WIN32)
#include "../src/platform/windows/ResourceDriversWindows.h"
#endif
int main(int argc,char** argv){


    if(argc==2&&std::string_view(argv[1])=="--json-fixture"){
        mc::ResourceCatalog c;c.machine.product="GPU \"A\"\n";c.machine.deviceClass=mc::DeviceClass::MiniPc;
        mc::ResourceObservation row;row.kind=mc::ResourceKind::Graphics;row.presentationSlot=0;
        row.facets.push_back({"read:temperature","mock","no writer",mc::ResourceAvailability::Unknown,mc::ResourceEvidence::Observed});
        row.measurements.push_back({"temperature","°C","mock","unknown",0,0,std::nullopt});
        row.measurements.push_back({"invalid","W","mock","invalid",std::numeric_limits<double>::quiet_NaN(),42,true});
        c.resources.push_back(std::move(row));std::cout<<mc::resourceCatalogJson(c)<<'\n';return 0;
    }
    auto check=[](bool ok,const char* what){if(!ok){std::cerr<<what<<'\n';std::exit(1);}};
    {
        mc::MonitoringSnapshot idle;idle.npuUsagePercent=0.;idle.npuClockMhz=0.;idle.npuPowerSource="AMD XRT";
        const auto shown=mc::audit::presentation(idle);
        check(shown.find("\"npu\":{\"usage_percent\":0.000000,\"clock_mhz\":0.000000,\"watts\":null")!=std::string::npos,
            "NPU audit distinguishes valid idle zeros from missing power");
    }
    {
        mc::GraphicsAdapter apu;apu.id="apu";apu.vendorId=0x1002;apu.type=mc::GpuType::Integrated;
        mc::GpuReading generic;generic.osAdapterId="apu";generic.available=true;generic.powerMw=31000;
        generic.powerDomain=mc::GpuPowerDomain::Gpu;generic.powerSource="ADLX GPUPower";
        mc::GpuReading adl=generic;adl.powerDomain=mc::GpuPowerDomain::Asic;adl.powerSource="ADL ASIC";
        adl.additionalSensors.push_back({"apu","power/gfx","W","AMD ADL · PMLog/30","supported",1.,{},100});
        const auto projected=mc::gpuPresentation({apu},{{0x1002,generic,100},{0x1002,adl,100}},{},100);
        check(projected.cards[0].powerWatts==1.,"APU display uses explicit GFX, never broad ASIC/GPUPower");
        check(projected.rawReadings[0].reading.powerMw==31000,"raw broad GPU observation retained");
        check(!mc::gpuPresentation({apu},{{0x1002,generic,100}},{},100).cards[0].powerWatts,
            "missing APU GFX stays unknown");
        adl.additionalSensors[0].value=0.;
        check(mc::gpuPresentation({apu},{{0x1002,adl,100}},{},100).cards[0].powerWatts==0.,"supported zero GFX remains zero");
        apu.type=mc::GpuType::Discrete;
        check(mc::gpuPresentation({apu},{{0x1002,generic,100}},{},100).cards[0].powerWatts==31.,"discrete GPU selection unchanged");
        mc::GpuCardReading card;card.powerWatts=1.;card.powerSource="AMD ADL · PMLog/30 · GFX";
        check(mc::gui::ui::gpu::baseCard(card,{},"").summary[2].label=="GFX ","graphics-only watts labelled GFX");
    }
    {
        mc::ResourceState state;mc::MonitoringSnapshot sample;mc::BoardDescriptor board;
        sample.processors[0].celsius=40;sample.processors[0].temperatureSource="fixture";
        state.update(board,sample);const mc::ResourcePresentationView view(state);
        sample.processors[0].celsius=99;
        check(mc::resourceDisplayValue(&view,mc::ResourceKind::Processor,0,"temperature")==40,
            "accepted resource state is independent of subsequent source DTO edits");
        check(!mc::resourceDisplayValue(&view,mc::ResourceKind::Processor,0,"missing"),
            "missing resource never fills from source DTO");
        sample.gpuCards.initialized=true;mc::GpuCardReading gpu;gpu.adapterId="fixture-gpu";gpu.usedBytes=UINT64_MAX;sample.gpuCards.cards.push_back(gpu);
        sample.battery.cycles=0;sample.battery.healthPercent=95;
        state.update(board,sample);const mc::ResourcePresentationView counters(state);
        check(mc::resourceDisplayUnsigned(&counters,mc::ResourceKind::Graphics,0,"memory_used")==UINT64_MAX,
            "resource state preserves GPU byte counters without floating-point rounding");
        check(mc::resourceDisplayUnsigned(&counters,mc::ResourceKind::Battery,0,"cycles")==0&&
            mc::resourceDisplayValue(&counters,mc::ResourceKind::Battery,0,"health")==95,
            "battery counters and health have one resource source, including zero");
        state.clear();check(!view.available()&&!mc::resourceDisplayValue(&view,mc::ResourceKind::Processor,0,"temperature"),
            "invalidated state never falls back to a source DTO");
        if(argc==2&&std::string_view(argv[1])=="--resource-state")return 0;
    }

    {
        struct FakeInventory final:mc::IHardwareProvider {
            mutable int calls{};mutable bool cancelled{};
            const char* id()const noexcept override{return "fixture";}
            const char* description()const noexcept override{return "fixture";}
            mc::HardwareInventory collect(std::stop_token token)const override{
                ++calls;cancelled=token.stop_requested();mc::HardwareInventory result;result.readAt="fixture";return result;}
        } provider;
        std::stop_source cancel;cancel.request_stop();
        const auto result=mc::collectInfoInventoryResource(cancel.get_token(),&provider);
        check(provider.calls==1&&provider.cancelled&&result.readAt=="fixture","INFO inventory preserves its provider, cancellation and result");
    }
    {
        using namespace mc;std::vector<std::string> calls;std::atomic<std::uint64_t> progress{};
        InfoDumpResourceServices services{
            [&](const InfoDumpResourceRequest& r){calls.push_back("ec");check(r.mode==DiagnosticDumpMode::ReadOnly&&r.outputRoot==std::filesystem::path("output"),"EC retains mode and output path");DiagnosticDumpResult out;out.saved=true;return out;},
            [&](const InfoDumpResourceRequest& r){calls.push_back("bios");check(r.progress==&progress,"BIOS retains progress pointer");DiagnosticDumpResult out;out.message="bios";return out;},
            [&](const InfoDumpResourceRequest& r){calls.push_back("power");check(r.powerSession!=nullptr&&r.displaySnapshot=="display","power diagnostic retains session and snapshot");DiagnosticDumpResult out;out.paused=true;return out;},
            [&](const InfoDumpResourceRequest& r){calls.push_back("vbios");check(r.pci=="0000:01:00.0"&&r.outputRoot==std::filesystem::path("output"),"VBIOS retains exact target and output");VbiosRomDumpResult out;out.vbiosSaved=true;out.message="partial";return out;}};
        InfoDumpResourceRequest request;request.outputRoot="output";request.progress=&progress;request.pci="0000:01:00.0";
        check(executeInfoDumpResource(request,&services).report.saved,"EC resource invokes its collector");
        request.kind=InfoDumpKind::Bios;check(executeInfoDumpResource(request,&services).report.message=="bios","BIOS resource invokes its collector");
        request.kind=InfoDumpKind::Vbios;const auto partial=executeInfoDumpResource(request,&services);
        check(partial.report.message=="partial"&&partial.vbios.outcome()=="completed_partial","VBIOS partial result is retained");
        PowerInputDumpSession session;request.kind=InfoDumpKind::PowerInput;request.powerSession=&session;request.displaySnapshot="display";
        check(executeInfoDumpResource(request,&services).report.paused,"guided power resource invokes its collector");
        check(calls==std::vector<std::string>({"ec","bios","vbios","power"}),"each INFO request invokes only its matching collector");
        request.kind=static_cast<InfoDumpKind>(99);try{executeInfoDumpResource(request,&services);check(false,"invalid dump kind must fail");}catch(const std::invalid_argument&){}
        check(calls.size()==4,"unknown INFO request cannot call a collector");
    }
    {
        using namespace mc;
        const ResourceControlKey battery{ResourceKind::Battery,"active","charge.upper"};
        const ResourceControlKey cpuFan{ResourceKind::Fan,"cpu","mode.manual"};
        using Table=ResourceControlTable<int,int*,int>;
        const auto invoke=+[](const int& amount,int* const& target){*target+=amount;return *target;};
        const std::array<Table::Binding,2> tableBindings{{{ResourceKind::Battery,"active","charge.upper",invoke},
            {ResourceKind::Fan,"cpu","mode.manual",invoke}}};
        Table table(tableBindings);int total=0;int* target=&total;
        check(table.hasRoute(battery)&&!table.hasRoute({ResourceKind::Fan,"gpu","mode.manual"})&&total==0,
            "typed table describes routes without invoking a device");
        check(table.execute(battery,5,target)==5&&table.execute(cpuFan,7,target)==12,"typed table passes exact request to selected adapter");
        try{static_cast<void>(table.execute({ResourceKind::Fan,"gpu","mode.manual"},1,target));check(false,"unknown typed route must fail");}catch(const std::invalid_argument&){}
        const std::array<Table::Binding,2> repeated{{tableBindings[0],tableBindings[0]}};
        try{Table duplicate(repeated);check(false,"duplicate typed route must fail");}catch(const std::invalid_argument&){}
        check(total==12,"typed route rejection cannot invoke an adapter");
        const auto ownedTable=[&]{std::array<Table::Binding,1> local{{{ResourceKind::Battery,std::string("ephemeral-instance"),std::string("charge.upper"),invoke}}};return Table(local);}();
        check(ownedTable.execute({ResourceKind::Battery,"ephemeral-instance","charge.upper"},1,target)==13,
            "typed route table owns registration names after source array is destroyed");
        const std::array<Table::Binding,1> emptyInstance{{{ResourceKind::Battery,"","charge.upper",invoke}}};
        const std::array<Table::Binding,1> noHandler{{{ResourceKind::Battery,"active","charge.upper",nullptr}}};
        try{Table invalid(emptyInstance);check(false,"empty resource instance must fail");}catch(const std::invalid_argument&){}
        try{Table invalid(noHandler);check(false,"missing handler must fail");}catch(const std::invalid_argument&){}
        const std::array<Table::Binding,2> failureBindings{{
            {ResourceKind::Battery,"active","charge.upper",+[](const int&,int* const&)->int{throw std::runtime_error("backend failure");}},
            {ResourceKind::Fan,"cpu","mode.manual",invoke}}};
        Table failing(failureBindings);
        try{failing.execute(battery,100,target);check(false,"backend exception must propagate");}catch(const std::runtime_error&){}
        check(total==13,"failed route cannot fall through to another resource handler");

        std::vector<ResourceControlKey> keyRoutes;
        for(std::size_t i=0;i<kKeyCount;++i){const auto route=keyResourceControlKey(static_cast<KeyAction>(i));
            check(!route.instance.empty()&&!route.operation.empty()&&std::find(keyRoutes.begin(),keyRoutes.end(),route)==keyRoutes.end(),
                "KEYs resource routes are explicit and distinct");keyRoutes.push_back(route);}
        check(keyResourceControlKey(KeyAction::Bluetooth).kind==ResourceKind::Connectivity&&
            keyResourceControlKey(KeyAction::Microphone).kind==ResourceKind::Audio&&
            keyResourceControlKey(KeyAction::Fn).kind==ResourceKind::Input,
            "KEYs operations identify logical resources, not an inferred EC protocol");
        try{static_cast<void>(keyResourceControlKey(static_cast<KeyAction>(999)));check(false,"invalid key action must be rejected");}catch(const std::invalid_argument&){}
        std::vector<ResourceControlKey> fanRoutes;
        for(const auto operation:{FanResourceOperation::Default,FanResourceOperation::Manual,FanResourceOperation::Maximum,
                FanResourceOperation::Minimum,FanResourceOperation::Online,FanResourceOperation::Restore}){
            const auto route=fanResourceControlKey(operation);
            check(route.kind==ResourceKind::Fan&&std::find(fanRoutes.begin(),fanRoutes.end(),route)==fanRoutes.end(),
                "FAN modes have distinct logical resource operations");fanRoutes.push_back(route);}
        try{static_cast<void>(fanResourceControlKey(static_cast<FanResourceOperation>(999)));check(false,"invalid FAN operation must be rejected");}catch(const std::invalid_argument&){}
        std::vector<ResourceControlKey> settingsRoutes;
        for(const auto operation:{SettingsResourceOperation::ChargeUpper,SettingsResourceOperation::ChargeRestart,SettingsResourceOperation::ChargePair,
            SettingsResourceOperation::ChargePolicy,SettingsResourceOperation::ChargeVoltage,SettingsResourceOperation::PowerStock,
            SettingsResourceOperation::PowerCustomPreset,SettingsResourceOperation::PowerCustomEditor,SettingsResourceOperation::LightLevel,
            SettingsResourceOperation::LightEffect,SettingsResourceOperation::LightColor,SettingsResourceOperation::LightCustomColor,
            SettingsResourceOperation::LightNative,SettingsResourceOperation::LightTimeout,SettingsResourceOperation::LightCycle,
            SettingsResourceOperation::LightZone,SettingsResourceOperation::LightUpdate,SettingsResourceOperation::EcRead,SettingsResourceOperation::EcWrite,
            SettingsResourceOperation::ChargeAutomatic,SettingsResourceOperation::ChargeNativeTurbo,
            SettingsResourceOperation::PowerCustomWrite,SettingsResourceOperation::PowerCustomSelect,
            SettingsResourceOperation::PowerAutomatic,SettingsResourceOperation::PowerEconomy,SettingsResourceOperation::PowerAllowed}){
            const auto route=settingsResourceControlKey(operation);
            check(std::find(settingsRoutes.begin(),settingsRoutes.end(),route)==settingsRoutes.end(),"SETs shared resource operations are distinct");settingsRoutes.push_back(route);}
        check(settingsResourceControlKey(SettingsResourceOperation::ChargeUpper).kind==ResourceKind::Battery&&
            settingsResourceControlKey(SettingsResourceOperation::PowerStock).kind==ResourceKind::Processor&&
            settingsResourceControlKey(SettingsResourceOperation::LightLevel).kind==ResourceKind::Lighting,
            "SETs shared resource kinds are explicit");
        std::vector<ResourceControlKey> infoRoutes;
        for(const auto operation:{InfoResourceOperation::Inventory,InfoResourceOperation::EcDump,InfoResourceOperation::BiosDump,
                InfoResourceOperation::VbiosDump,InfoResourceOperation::PawnIoPrepare}){
            const auto route=infoResourceControlKey(operation);
            check(std::find(infoRoutes.begin(),infoRoutes.end(),route)==infoRoutes.end(),"INFO resource operations are distinct");infoRoutes.push_back(route);}
        check(gpuPowerResourceControlKey("gpu-1",false)!=gpuPowerResourceControlKey("gpu-1",true)&&
            gpuPowerResourceControlKey("gpu-1",false)!=gpuPowerResourceControlKey("gpu-2",false),"GPU operations retain provider identity and action");
        try{static_cast<void>(gpuPowerResourceControlKey("",false));check(false,"GPU without provider identity must be rejected");}catch(const std::invalid_argument&){}
        std::vector<std::string> gpuCalls;GpuPowerResourceActions gpu{
            [&](const std::string& id,int watts){gpuCalls.push_back(id+":"+std::to_string(watts));},
            [&](const std::string& id){gpuCalls.push_back(id+":reset");}};
        applyGpuPowerResource("gpu-1",75,false,gpu);applyGpuPowerResource("gpu-2",{},true,gpu);
        check(gpuCalls==std::vector<std::string>({"gpu-1:75","gpu-2:reset"}),"GPU route preserves selected provider and exact action");
        try{applyGpuPowerResource("gpu-1",{},false,gpu);check(false,"missing GPU power value must be rejected");}catch(const std::invalid_argument&){}
        try{applyGpuPowerResource("",75,false,gpu);check(false,"empty GPU identity must be rejected");}catch(const std::invalid_argument&){}
        check(gpuCalls.size()==2,"invalid GPU command cannot invoke a backend");
#ifdef __linux__
        FakeLinuxKeyControl linuxKey;std::mutex gate;
        linux_platform::applyKey("huawei-hlyl-wxx9",false,&linuxKey,gate,KeyAction::Fn,true);
        check(linuxKey.writes==1&&linuxKey.fn,"Linux KEYs resource route uses the original typed setter");
        try{linux_platform::applyKey("huawei-hlyl-wxx9",true,&linuxKey,gate,KeyAction::Fn,false);check(false,"preview KEYs must be rejected");}catch(const std::runtime_error&){}
        try{linux_platform::applyKey("unknown",false,&linuxKey,gate,KeyAction::Fn,false);check(false,"unknown KEYs backend must be rejected");}catch(const std::runtime_error&){}
        check(linuxKey.writes==1,"Linux preview and unknown board never write KEYs");
#endif
    }
    {
        mc::BoardDescriptor board;board.manufacturer="original manufacturer";
        mc::MonitoringResourceProvider owned(board,[]{return mc::MonitoringSnapshot{};});
        board.manufacturer="changed after construction";
        check(owned.observe().machine.manufacturer=="original manufacturer","monitor provider owns machine snapshot");
        auto temporary=mc::MonitoringResourceProvider(mc::BoardDescriptor{"temporary manufacturer",{}, {},{}},
            []{return mc::MonitoringSnapshot{};});
        check(temporary.observe().machine.manufacturer=="temporary manufacturer","monitor provider accepts temporary board safely");
    }
    mc::ResourceObservation resource;
#if defined(_WIN32)
    {
        auto temporary=mc::windows::WindowsDriverResourceProvider(mc::BoardDescriptor{"temporary driver machine",{}, {},{}},
            []{return std::vector<mc::ResourceDriverObservation>{};});
        check(temporary.observe().machine.manufacturer=="temporary driver machine","driver provider owns machine snapshot");
        mc::dependencies::PawnIoDriverStatus pawn;pawn.installed=true;pawn.running=true;pawn.fileVersion="2.2.0.0";
        pawn.versionStatus="Installed SYS VERSIONINFO; not an in-memory driver version query";
        const auto observed=mc::windows::projectPawnIoDriver(pawn);
        check(observed.installed==true&&observed.running==true&&observed.fileVersion=="2.2.0.0"&&
            !observed.packageVersion&&!observed.loadedVersion,"installed PawnIO version is not a loaded version");
        const std::array<mc::windows::UniwillBindingEvidence,2> bindings{{
            {"ACPI\\INOU0000\\A","UWACPIDriver","10.48.11.331"},
            {"ACPI\\INOU0000\\B","UWACPIDriver","11.2.18.347"}}};
        const auto versions=mc::windows::projectUniwillBindings(bindings,true,{});
        check(versions.size()==2&&versions[0].packageVersion=="10.48.11.331"&&versions[1].packageVersion=="11.2.18.347"&&
            !versions[0].fileVersion&&!versions[0].loadedVersion&&!versions[0].running&&!versions[1].running,
            "distinct PnP driver packages never imply a loaded driver version");
        const auto missing=mc::windows::projectUniwillBindings({},true,{});
        check(missing.size()==1&&!missing[0].installed&&!missing[0].running&&!missing[0].packageVersion,
            "missing PnP device does not imply driver absence");
        const auto partial=mc::windows::projectUniwillBindings(bindings,false,"enumeration error");
        check(partial.size()==3&&partial.back().status.find("incomplete")!=std::string::npos,
            "incomplete enumeration does not imply complete inventory");
        mc::BoardDescriptor fake;int calls=0;
        mc::windows::WindowsDriverResourceProvider provider(fake,[&]{++calls;return versions;});
        const auto catalog=provider.observe();const auto json=mc::resourceCatalogJson(catalog);
        check(calls==1&&catalog.resources.empty()&&catalog.drivers.size()==2&&json.find("11.2.18.347")!=std::string::npos&&
            json.find("\"loaded_version\":null")!=std::string::npos,"driver provider reports evidence without resources or admission");
    }
#endif
    check(resource.identity.resource.empty()&&resource.facets.empty()&&resource.measurements.empty(),"unknown resource has no invented identity or capability");
    mc::ResourceMeasurement measurement;
    check(!measurement.value.has_value(),"unknown measurement remains unknown");
    const auto emptyJson=mc::resourceCatalogJson({});
    check(emptyJson.find("\"class\":\"unknown\"")!=std::string::npos&&
        emptyJson.find("\"resources\":[]")!=std::string::npos&&emptyJson.find("\"projection_check\":null")!=std::string::npos,
        "empty catalog JSON retains unknown class and unattempted comparison");
    mc::MonitoringSnapshot sample;sample.processors[0].celsius=0;sample.processors[0].fanRpm=1800;
    sample.processors[0].temperatureSource="test";sample.processorSampledAtMs=90;
    sample.retainedForDisplay.push_back({"processor.0.rpm",25});
    const auto projected=mc::observeMonitoringResources(sample);
    mc::BoardDescriptor providerBoard;providerBoard.manufacturer="unknown";providerBoard.baseBoard="unknown";
    int reads=0;mc::MonitoringResourceProvider provider(providerBoard,[&]{++reads;return sample;});
    {
        struct Stub final:mc::ResourceProvider {int calls{};bool fail{},nonstandard{};
            mc::ResourceCatalog observe() override {++calls;if(nonstandard)throw 42;if(fail)throw std::runtime_error("source failed");
                mc::ResourceCatalog out;mc::ResourceObservation row;row.kind=mc::ResourceKind::Sensor;row.presentationSlot=0;
                out.resources.push_back(std::move(row));return out;}} first,second;
        second.fail=true;
        const std::array entries{mc::ResourceProviderEntry{"first",&first,{}},mc::ResourceProviderEntry{"second",&second,{}},
            mc::ResourceProviderEntry{"blocked",nullptr,"no EC"}};
        const auto batch=mc::observeResourceProviders(entries);
        check(batch.size()==3&&first.calls==1&&second.calls==1&&batch[0].status=="ok"&&batch[0].catalog&&
            batch[1].status=="failed"&&!batch[1].catalog&&batch[2].status=="skipped"&&!batch[2].catalog,
            "independent providers keep successful data when another fails or is skipped");
        const auto json=mc::resourceProviderResultsJson(batch);
        check(json.find("\"status\":\"failed\"")!=std::string::npos&&json.find("\"detail\":\"no EC\"")!=std::string::npos&&
            json.find("\"catalog\":null")!=std::string::npos,"provider batch reports missing catalogs honestly");
        second.fail=false;const auto both=mc::observeResourceProviders(entries);
        check(both[0].catalog&&both[1].catalog&&both[0].catalog->resources[0].presentationSlot==0&&
            both[1].catalog->resources[0].presentationSlot==0,"provider slots remain separate and are not merged");
        const int before=first.calls;const std::array duplicates{entries[0],entries[0]};bool rejected=false;
        try{static_cast<void>(mc::observeResourceProviders(duplicates));}catch(const std::invalid_argument&){rejected=true;}
        check(rejected&&first.calls==before,"duplicate provider names rejected before observation");
        second.nonstandard=true;const auto unknown=mc::observeResourceProviders(entries);
        check(unknown[1].status=="failed"&&!unknown[1].catalog,"non-standard provider failure is isolated");
        int constructions=0;mc::DeferredResourceProvider lazy([&]() -> std::unique_ptr<mc::ResourceProvider>{
            if(++constructions==1)throw std::runtime_error("source setup failed");return std::make_unique<Stub>();});
        const std::array setupEntries{mc::ResourceProviderEntry{"lazy",&lazy,{}},mc::ResourceProviderEntry{"independent",&first,{}}};
        const auto setupFailed=mc::observeResourceProviders(setupEntries);
        check(setupFailed[0].status=="failed"&&!setupFailed[0].catalog&&setupFailed[1].status=="ok"&&setupFailed[1].catalog,
            "provider construction failure does not suppress another source");
        const auto retry=mc::observeResourceProviders(setupEntries);
        check(retry[0].status=="ok"&&retry[0].catalog&&constructions==2,"provider construction retries on later observation");
        static_cast<void>(mc::observeResourceProviders(setupEntries));
        check(constructions==2,"successfully constructed provider is reused");
    }
    mc::ResourceProvider& abstractProvider=provider;
    const auto supplied=abstractProvider.observe();
    check(reads==1&&mc::compareMonitoringProjection(sample,supplied).empty()&&!supplied.machine.catalogRecognized,
        "provider reads exactly once and leaves unknown board unadmitted");
    check(supplied.projectionCheck&&supplied.projectionCheck->verified()&&supplied.projectionCheck->comparedValues==2&&
        supplied.projectionCheck->mismatches.empty(),"one source snapshot has explicit projection verification");
    check(mc::resourceCatalogJson(supplied).find("\"projection_check\":{\"compared_values\":2,\"mismatch_count\":0,\"verified\":true")!=std::string::npos,
        "projection result is present in JSON");
    mc::MonitoringResourceProvider emptyProvider(providerBoard,[]{return mc::MonitoringSnapshot{};});
    const auto emptyResult=emptyProvider.observe();
    check(emptyResult.projectionCheck&&!emptyResult.projectionCheck->verified()&&emptyResult.projectionCheck->comparedValues==0,
        "an empty source cannot claim verified observation");
    sample.processors[0].celsius=41;const auto changed=abstractProvider.observe();
    check(reads==2&&mc::compareMonitoringProjection(sample,changed).empty(),"provider returns the next actual sample");
    sample.processors[0].celsius=0;
    int failures=0;mc::MonitoringResourceProvider failing(providerBoard,[&]()->mc::MonitoringSnapshot{++failures;throw std::runtime_error("source failed");});
    try{static_cast<void>(failing.observe());check(false,"provider failure must propagate");}catch(const std::runtime_error&){}
    check(failures==1,"provider failure does not silently retry or fabricate a snapshot");
    check(projected.resources.size()==2,"processor and fan projection");
    check(projected.resources[0].measurements[0].value==0,"zero is a measurement");
    check(projected.resources[0].measurements[0].timestampOrigin==mc::ResourceTimestampOrigin::CollectorStarted,
        "processor timestamp describes collection start, not hardware time");
    check(projected.resources[1].measurements[0].retained==true&&projected.resources[1].measurements[0].sampledAtMs==25,"retained timestamp");
    check(projected.resources[1].measurements[0].timestampOrigin==mc::ResourceTimestampOrigin::DisplayCache,
        "retained timestamp belongs to display cache");
    mc::MonitoringSnapshot noTime;noTime.processors[0].celsius=42;noTime.memory[0].present=true;noTime.memory[0].celsius=35;
    noTime.battery.present=true;noTime.battery.percent=55;
    const auto noTimeProjection=mc::observeMonitoringResources(noTime);
    check(noTimeProjection.resources.size()==3,"untimed readings remain visible");
    for(const auto& row:noTimeProjection.resources){check(!row.measurements[0].retained.has_value()&&
        row.facets[0].availability==mc::ResourceAvailability::Unknown,"untimed reading cannot claim fresh source");}
    mc::MonitoringSnapshot invalid;invalid.processorSampledAtMs=42;
    invalid.processors[0].celsius=std::numeric_limits<double>::infinity();
    invalid.processors[0].temperatureSource="invalid CPU source";
    invalid.additionalSensors.push_back({"mock sensor","temperature","C","mock","ok",
        std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity(),43});
    const auto invalidProjection=mc::observeMonitoringResources(invalid);
    check(invalidProjection.resources.size()==2,"invalid numeric sources remain represented");
    for(const auto& row:invalidProjection.resources){
        check(!row.measurements[0].value&&row.measurements[0].status.find("non-finite measurement rejected")!=std::string::npos&&
            row.facets[0].availability==mc::ResourceAvailability::Unknown&&row.facets[0].evidence==mc::ResourceEvidence::Unknown,
            "non-finite measurements are unknown, not fresh observations");
    }
    check(!invalidProjection.resources[1].measurements[0].raw,"non-finite raw evidence is not serialized as a number");
    check(!mc::compareMonitoringProjection(invalid,invalidProjection).empty(),"invalid source cannot pass UI parity");
    reference::ResourceCatalogShadow invalidShadow;invalidShadow.update(providerBoard,invalid);
    check(!reference::ResourcePresentationView(invalid,invalidShadow).available(),"invalid projection leaves the existing UI path in control");
    check(projected.resources[0].identity.resource.empty()&&projected.resources[1].identity.resource.empty(),"presentation slots are not stable identities");
    check(mc::compareMonitoringProjection(sample,projected).empty(),"projection agrees with source");
    auto damaged=projected;damaged.resources[1].measurements[0].value=100;
    check(!mc::compareMonitoringProjection(sample,damaged).empty(),"parity detects a changed fan speed");
    const auto failedProjection=mc::verifyMonitoringProjection(sample,damaged);
    check(!failedProjection.verified()&&!failedProjection.mismatches.empty()&&failedProjection.comparedValues==2,
        "mismatched projection is explicitly unverified");
    damaged=projected;damaged.resources[1].measurements[0].retained=false;
    check(!mc::compareMonitoringProjection(sample,damaged).empty(),"retained fan cannot become fresh");
    damaged=projected;damaged.resources[1].measurements[0].sampledAtMs=90;
    check(!mc::compareMonitoringProjection(sample,damaged).empty(),"fan sample time cannot be replaced");
    damaged=projected;damaged.resources[1].measurements[0].timestampOrigin=mc::ResourceTimestampOrigin::SoftwareObservation;
    check(!mc::compareMonitoringProjection(sample,damaged).empty(),"timestamp provenance mismatch detected");
    damaged=projected;damaged.resources[0].measurements[0].unit="F";
    check(!mc::compareMonitoringProjection(sample,damaged).empty(),"unit mismatch detected");
    damaged=projected;damaged.resources[0].measurements[0].status="available";
    check(!mc::compareMonitoringProjection(sample,damaged).empty(),"status mismatch detected");
    damaged=projected;damaged.resources[1].facets[0].availability=mc::ResourceAvailability::Available;
    check(!mc::compareMonitoringProjection(sample,damaged).empty(),"retained source availability mismatch detected");
    damaged=projected;damaged.resources[0].measurements.push_back({"invented","W","test","",1,0,false});
    check(!mc::compareMonitoringProjection(sample,damaged).empty(),"unexpected metric detected");
    damaged=projected;damaged.resources[0].identity.machine="invented machine";
    check(!mc::compareMonitoringProjection(sample,damaged).empty(),"invented machine identity rejected");
    damaged=projected;damaged.resources[0].identity.scope="invented durable identity";
    check(!mc::compareMonitoringProjection(sample,damaged).empty(),"invented identity scope rejected");
    damaged=projected;damaged.resources[0].owner="invented owner";
    check(!mc::compareMonitoringProjection(sample,damaged).empty(),"invented resource owner rejected");
    damaged=projected;damaged.resources[0].ownerSource="invented owner source";
    check(!mc::compareMonitoringProjection(sample,damaged).empty(),"invented owner source rejected");
    damaged=projected;damaged.resources[0].sourceSerialHint="invented serial";
    check(!mc::compareMonitoringProjection(sample,damaged).empty(),"invented source serial hint rejected");
    damaged=projected;damaged.resources.push_back({});damaged.resources.back().kind=mc::ResourceKind::Other;
    damaged.resources.back().presentationSlot=0;
    check(!mc::compareMonitoringProjection(sample,damaged).empty(),"invented empty resource rejected");
    damaged=projected;damaged.resources.push_back({});
    check(!mc::compareMonitoringProjection(sample,damaged).empty(),"unscoped empty resource rejected");
    damaged=projected;damaged.resources.erase(damaged.resources.begin()+1);
    check(!mc::compareMonitoringProjection(sample,damaged).empty(),"parity detects missing fan");
    mc::MonitoringSnapshot components;components.memory[0].present=true;components.memory[0].celsius=32;
    components.memory[0].source="SPD";components.retainedForDisplay.push_back({"memory.0",17});
    components.disks[0].present=false;components.disks[0].celsius=40;
    components.battery.present=true;components.battery.percent=0;components.battery.dynamicAtMs[0]=55;
    const auto all=mc::observeMonitoringResources(components);
    check(all.resources.size()==3,"memory, absent disk channel and battery remain distinct");
    check(all.resources[0].kind==mc::ResourceKind::Memory&&all.resources[0].measurements[0].retained==true&&all.resources[0].measurements[0].sampledAtMs==17,"memory source freshness");
    check(all.resources[2].kind==mc::ResourceKind::Battery&&all.resources[2].measurements[0].value==0,"battery zero charge");
    check(all.resources[2].measurements[0].timestampOrigin==mc::ResourceTimestampOrigin::SoftwareObservation,
        "battery timestamp is a software observation");
    check(mc::compareMonitoringProjection(components,all).empty(),"memory and battery parity");
    auto wrongSource=all;wrongSource.resources[0].measurements[0].source="other";
    check(!mc::compareMonitoringProjection(components,wrongSource).empty(),"source mismatch detected");
    check(all.resources[0].facets[0].availability==mc::ResourceAvailability::Unknown,"retained reading does not establish current availability");
    {
        mc::MonitoringSnapshot power;power.systemPowerWatts=50;power.systemConsumptionWatts=35;
        power.systemPowerSource="EC candidate";const auto catalog=mc::observeMonitoringResources(power);
        check(catalog.resources.size()==1&&catalog.resources[0].kind==mc::ResourceKind::SystemPower&&
            catalog.resources[0].measurements.size()==2&&catalog.resources[0].measurements[0].value==50&&
            catalog.resources[0].measurements[1].value==35,"reported system power and derived consumption stay separate");
        check(!catalog.resources[0].measurements[0].retained&&catalog.resources[0].facets[0].availability==mc::ResourceAvailability::Unknown,
            "untimed system power has unknown freshness");
        check(mc::verifyMonitoringProjection(power,catalog).verified()&&
            mc::resourceCatalogJson(catalog).find("\"kind\":\"system_power\"")!=std::string::npos,
            "system power is serialized and independently compared");
        auto broken=catalog;broken.resources[0].measurements[1].value=50;
        check(!mc::compareMonitoringProjection(power,broken).empty(),"derived consumption cannot be replaced by reported power");
        broken=catalog;broken.resources[0].measurements[0].source="different source";
        check(!mc::compareMonitoringProjection(power,broken).empty(),"system power source mismatch is rejected");
        broken=catalog;broken.resources.clear();
        check(!mc::compareMonitoringProjection(power,broken).empty(),"missing system power resource is rejected");
        auto changedBattery=power;changedBattery.battery.flow=mc::BatteryFlow::Charging;changedBattery.battery.dynamicStale=true;
        const auto staleInput=mc::observeMonitoringResources(changedBattery);
        check(staleInput.resources[0].measurements[1].status.find("battery reading stale")!=std::string::npos&&
            !mc::compareMonitoringProjection(changedBattery,catalog).empty(),"stale battery input remains visible in estimate provenance");
        reference::ResourceCatalogShadow shadow;shadow.update(providerBoard,power);reference::ResourcePresentationView view(power,shadow);
        check(view.available()&&mc::gui::ui::displayedSystemConsumption(power,&view)==35&&
            mc::gui::ui::legacyui::displayedSystemConsumption(power)==35,"verified and legacy footer use the same estimate");
        power.systemConsumptionWatts=0;check(!reference::ResourcePresentationView(power,shadow).available()&&
            mc::gui::ui::legacyui::displayedSystemConsumption(power)==0,"changed snapshot falls back and preserves zero");
        power.systemPowerStale=true;shadow.update(providerBoard,power);reference::ResourcePresentationView stale(power,shadow);
        const auto* reported=stale.measurement(mc::ResourceKind::SystemPower,0,"reported_power");
        check(stale.available()&&reported&&reported->retained==true&&mc::gui::ui::displayedSystemConsumption(power,&stale)==0,
            "retained system power remains marked and available to the footer");
        const auto invalid=std::numeric_limits<double>::infinity();power.systemConsumptionWatts=invalid;
        check(!mc::verifyMonitoringProjection(power,mc::observeMonitoringResources(power)).verified(),
            "non-finite system consumption cannot pass parity");
    }
    mc::MonitoringSnapshot batteries;batteries.diagnostic=true;
    mc::BatteryReading first,second;first.present=true;first.deviceId="battery-A";first.percent=55;first.source="OS-A";
    second.present=true;second.deviceId="battery-B";second.percent=77;second.source="OS-B";
    batteries.battery=first;batteries.batteries={first,second};const auto both=mc::observeMonitoringResources(batteries);
    check(both.resources.size()==2,"diagnostic batteries are not merged or duplicated");
    check(both.resources[0].identity.resource=="battery-A"&&both.resources[1].identity.resource=="battery-B","battery identities remain distinct");
    check(both.resources[1].measurements[0].source=="OS-B"&&both.resources[1].measurements[0].value==77,"battery source remains attached");
    check(mc::compareMonitoringProjection(batteries,both).empty(),"multiple battery parity");
    auto wrongBattery=both;wrongBattery.resources[1].measurements[0].value=55;
    check(!mc::compareMonitoringProjection(batteries,wrongBattery).empty(),"battery mismatch detected");
    wrongBattery=both;wrongBattery.resources[1].identity.resource="battery-A";
    check(!mc::compareMonitoringProjection(batteries,wrongBattery).empty(),"battery identity mismatch detected");
    mc::MonitoringSnapshot multi;multi.processors[1].powerWatts=9;multi.processors[1].fanRpm=1500;
    multi.gpuCards.initialized=true;mc::GpuCardReading a,b;
    a.name="integrated";a.adapterId="os-a";a.usagePercent=0;a.usageSource="source-a";
    b.name="discrete";b.physicalId="pci-b";b.powerWatts=40;b.powerSource="source-b";
    multi.gpuCards.cards={a,b};const auto multiple=mc::observeMonitoringResources(multi);
    check(multiple.resources.size()==3,"fan and two GPU adapters without duplicate cooling-slot GPU");
    check(multiple.resources[1].identity.resource=="os-a"&&multiple.resources[2].identity.resource=="pci-b","GPU identities stay distinct");
    check(multiple.resources[1].measurements[0].value==0&&multiple.resources[1].measurements[0].source=="source-a","GPU zero and source preserved");
    check(!multiple.resources[1].measurements[0].retained.has_value()&&
        multiple.resources[1].facets[0].availability==mc::ResourceAvailability::Unknown,"untimed GPU load freshness is unknown");
    check(!multiple.resources[2].measurements[0].retained.has_value(),"GPU freshness remains unknown");
    check(mc::compareMonitoringProjection(multi,multiple).empty(),"multiple GPU parity");
    mc::MonitoringSnapshot retainedGpu;retainedGpu.gpuCards.initialized=true;
    mc::GpuCardReading gpuA,gpuB,gpuUnknown;gpuA.adapterId="a";gpuA.celsius=40;gpuA.clockMhz=500;
    gpuB.adapterId="b";gpuB.celsius=50;gpuB.powerWatts=30;gpuUnknown.celsius=60;
    retainedGpu.gpuCards.cards={gpuA,gpuB,gpuUnknown};
    retainedGpu.retainedForDisplay={{"gpu.a.temperature",11},{"gpu.a.clock",12},{"gpu.b.power",13},{"gpu..temperature",99}};
    const auto retainedRows=mc::observeMonitoringResources(retainedGpu);
    check(retainedRows.resources.size()==3,"three GPU rows preserved");
    check(retainedRows.resources[0].measurements[0].retained==true&&retainedRows.resources[0].measurements[0].sampledAtMs==11&&
        retainedRows.resources[0].measurements[1].retained==true&&retainedRows.resources[0].measurements[1].sampledAtMs==12,
        "GPU temperature and frequency retain their own cache times");
    check(!retainedRows.resources[1].measurements[0].retained.has_value()&&
        retainedRows.resources[1].measurements[1].retained==true&&retainedRows.resources[1].measurements[1].sampledAtMs==13,
        "GPU power freshness does not leak between adapters or metrics");
    check(!retainedRows.resources[2].measurements[0].retained.has_value()&&retainedRows.resources[2].measurements[0].sampledAtMs==0,
        "empty GPU identity never inherits another cache timestamp");
    check(mc::compareMonitoringProjection(retainedGpu,retainedRows).empty(),"GPU retained metadata parity");
    {
        mc::MonitoringSnapshot fans;fans.processors[0].fanRpm=1894.77;fans.processors[0].fanPwm=26;
        fans.processors[0].fanSource="cpu fan";fans.processors[1].fanRpm=1439.42;
        fans.processors[1].fanSource="system fan";
        mc::BoardDescriptor fanBoard;reference::ResourceCatalogShadow fanShadow;fanShadow.update(fanBoard,fans);
        const reference::ResourcePresentationView fanView(fans,fanShadow);
        const auto cpu=mc::gui::ui::monitoringFanReading(fans,0,&fanView);
        const auto system=mc::gui::ui::monitoringFanReading(fans,1,&fanView);
        check(fanView.available()&&cpu.rpmAvailable&&cpu.rpm==1895&&cpu.pwmAvailable&&cpu.dutyPercent==26,
            "catalog fan uses matching CPU channel and preserves rounding");
        check(system.rpmAvailable&&system.rpm==1439&&!system.pwmAvailable,
            "system fan remains separate and unknown duty stays unavailable");
        auto changedFans=fans;changedFans.processors[0].fanRpm=2000;
        const reference::ResourcePresentationView staleFan(changedFans,fanShadow);
        const auto fallback=mc::gui::ui::monitoringFanReading(changedFans,0,&staleFan);
        check(!staleFan.available()&&!fallback.rpmAvailable,
            "stale fan catalog falls back to the new snapshot");
        const reference::ResourcePresentationView disabledFan(fans,fanShadow,false);
        check(!mc::gui::ui::monitoringFanReading(fans,1,&disabledFan).rpmAvailable,
            "disabled fan projection preserves original reading");
        // Windows readout is separately sampled and contains integral RPM.
        // It must not borrow fractional or stale data just because a catalog
        // contains a fan at the same presentation slot.
        mc::FanReadout original;original.rpmAvailable=true;original.rpm=1895;
        original.pwmAvailable=true;original.dutyPercent=26;
        const auto partial=mc::gui::ui::legacyui::projectVerifiedFanReading(original,0,&fanView);
        check(!partial.speedUsed&&partial.dutyUsed&&partial.reading.rpm==1895&&partial.reading.dutyPercent==26,
            "Windows accepts only fan metrics matching its direct readout");
        auto integral=fans;integral.processors[0].fanRpm=1895;reference::ResourceCatalogShadow integralShadow;integralShadow.update(fanBoard,integral);
        const reference::ResourcePresentationView integralView(integral,integralShadow);
        const auto matching=mc::gui::ui::legacyui::projectVerifiedFanReading(original,0,&integralView);
        check(matching.speedUsed&&matching.dutyUsed&&matching.reading.rpm==1895,
            "matching fan values are exposed through catalog");
        original.rpmAvailable=false;original.pwmAvailable=false;
        const auto absent=mc::gui::ui::legacyui::projectVerifiedFanReading(original,0,&integralView);
        check(!absent.speedUsed&&!absent.dutyUsed&&!absent.reading.rpmAvailable&&!absent.reading.pwmAvailable,
            "catalog cannot fabricate a missing direct fan observation");
        original.rpmAvailable=true;original.rpm=2000;
        const auto stale=mc::gui::ui::legacyui::projectVerifiedFanReading(original,0,&integralView);
        check(!stale.speedUsed&&stale.reading.rpm==2000,"different direct RPM keeps current reading");
    }
    auto wrongGpu=multiple;wrongGpu.resources[2].measurements[0].value=41;
    check(!mc::compareMonitoringProjection(multi,wrongGpu).empty(),"GPU mismatch detected");
    auto unplugged=multi;unplugged.gpuCards.cards.pop_back();
    check(!mc::compareMonitoringProjection(unplugged,multiple).empty(),"stale GPU after removal detected");
    wrongGpu=multiple;wrongGpu.resources[2].identity.resource="os-a";
    check(!mc::compareMonitoringProjection(multi,wrongGpu).empty(),"GPU identity mismatch detected");
    mc::MonitoringSnapshot empty;reference::ResourceCatalogShadow emptyShadow;
    mc::BoardDescriptor emptyBoard;emptyShadow.update(emptyBoard,empty);
    check(emptyShadow.current()&&emptyShadow.observedValues()==0,"empty snapshot is not a measured comparison");
    check(reference::resourceShadowSummary(emptyShadow).find("\"observed_values\":0")!=std::string::npos,"audit reports comparison coverage");
    // Values below are anonymized FANs.presentation log excerpts. Those logs
    // expose GPU rows, not complete CPU/fan snapshots or write admission.
    struct GpuFixture {const char* name;double load,watts,temp,clock;};
    const GpuFixture logs[]{
        {"AMD Radeon(TM) 880M Graphics",9,20,47,1231},
        {"AMD Radeon(TM) Graphics",0,4,42,0},
        {"AMD Radeon 780M Graphics",0,8,35,800}};
    for(const auto& f:logs){
        mc::MonitoringSnapshot captured;captured.gpuCards.initialized=true;
        mc::GpuCardReading gpu;gpu.name=f.name;gpu.usagePercent=f.load;gpu.powerWatts=f.watts;
        gpu.celsius=f.temp;gpu.clockMhz=f.clock;gpu.powerSource="AMD ADLX · GPUPower (generic GPU domain; not asserted ASIC)";
        captured.gpuCards.cards.push_back(gpu);const auto result=mc::observeMonitoringResources(captured);
        check(result.resources.size()==1&&result.resources[0].kind==mc::ResourceKind::Graphics,"captured GPU row");
        const auto& readings=result.resources[0].measurements;
        check(readings.size()==4&&readings[0].value==f.temp&&readings[1].value==f.load&&readings[2].value==f.clock&&readings[3].value==f.watts,"captured GPU values preserved");
        check(readings[3].source==gpu.powerSource,"captured GPU power source retained");
    }
    // Diagnostic sensor rows below are copied from the first recorded sample
    // in the three source dumps. Null RPM stays missing, never an assumed zero.
    struct FanFixture {double cpu, gpu;std::optional<double> cpuRpm,secondRpm;const char* cpuSource;const char* gpuSource;const char* fanSource;const char* secondFanSource;};
    const FanFixture fanLogs[]{
        {35,32,std::nullopt,std::nullopt,"PawnIO · Tctl/Tdie","AMD ADL · GFX","",""},
        {105.125,75,5811,4252,"PawnIO · Tctl/Tdie","AMD ADL · Edge","Huawei HWMI · fan 0","Huawei HWMI · fan 1"},
        {37.5,0,1894.771529,1439.41922563,"PawnIO · Tctl/Tdie","AMD ADL · PMLog","HPBSD ITE5571 · CPU tachometer","HPBSD ITE5571 · system fan (not GPU fan)"}};
    for(unsigned index=0;index<3;++index){
        const auto& f=fanLogs[index];mc::MonitoringSnapshot captured;
        captured.processors[0].celsius=f.cpu;captured.processors[0].temperatureSource=f.cpuSource;
        captured.processors[0].fanRpm=f.cpuRpm;captured.processors[0].fanSource=f.fanSource;
        if(index!=2)captured.processors[1].celsius=f.gpu;
        captured.processors[1].temperatureSource=f.gpuSource;
        captured.processors[1].fanRpm=f.secondRpm;captured.processors[1].fanSource=f.secondFanSource;
        const auto result=mc::observeMonitoringResources(captured);
        check(mc::compareMonitoringProjection(captured,result).empty(),"captured CPU/fan values preserved");
        if(index==0){for(const auto& r:result.resources)check(r.kind!=mc::ResourceKind::Fan,"missing RPM must not fabricate fans");}
        if(index==2){bool found=false;for(const auto& r:result.resources)if(r.kind==mc::ResourceKind::Fan&&r.presentationSlot==1){
            found=true;check(r.measurements[0].source==f.secondFanSource,"system fan provenance preserved");}
            check(found,"HPBSD system fan is a fan resource");}
    }
    {
        struct Names {std::string_view id,model,settings,draft,curve;};
        constexpr Names expected[]{
            {"mechrevo-gm6ir0c","GM6IR0C","gm6ir0c-device-settings.ini","gm6ir0c-power.txt","fan-curves.csv"},
            {"mechrevo-gm5hg0a","GM5HG0A","gm5hg0a-device-settings.ini","gm5hg0a-power.txt","fan-curves.csv"},
            {"machenike-l16","L16","l16-device-settings.ini","l16-power.txt","l16-fan-curves.csv"},
            {"eluktronics-hydroc16g2","HYDROC-16 G2","hydroc16g2-device-settings.ini","hydroc16g2-power.txt","hydroc16g2-fan-curves.csv"}};
        for(const auto& item:expected){
            check(mc::ramFanDataForProfile(item.id)!=nullptr&&mc::gmModelName(item.id)==item.model&&
                mc::gmSettings(item.id)==item.settings&&mc::gmPowerDraftFile(item.id)==item.draft&&mc::gmFanCurveFile(item.id)==item.curve,
                "profile-backed model and filenames preserve existing storage locations");
        }
        for(const std::string_view unknown:{"unknown","mechrevo-gm5hg0a-extra","MECHREVO-GM5HG0A",""})
            check(!mc::ramFanDataForProfile(unknown)&&mc::gmModelName(unknown)=="GM6IR0C"&&
                mc::gmSettings(unknown)=="gm6ir0c-device-settings.ini"&&mc::gmPowerDraftFile(unknown)=="gm6ir0c-power.txt"&&
                mc::gmFanCurveFile(unknown)=="fan-curves.csv","unknown ID keeps old fallback without selecting another profile");
        for(const auto& item:expected)for(int channel=-1;channel<=3;++channel){
            const bool l16=item.id=="machenike-l16";
            const int previous=channel==2?(l16?174:200):(l16?145:100);
            check(mc::gmAmdPowerMaximum(item.id,channel)==previous,"profile AMD bounds preserve prior model and channel limits");
        }
        for(const std::string_view unknown:{"unknown","machenike-l16-extra",""})
            check(mc::gmAmdPowerMaximum(unknown,0)==100&&mc::gmAmdPowerMaximum(unknown,2)==200,
                "unknown ID keeps old AMD-bound fallback without selecting a profile");
    }
    mc::BoardDescriptor board;board.manufacturer="MECHREVO";board.baseBoard="WUJIE Series-X5SP4NAG";
    const auto wujie=mc::observeMachineResources(board,{});
    check(wujie.machine.catalogRecognized&&wujie.machine.profile=="wu-jie-15-xpro"&&wujie.machine.deviceClass==mc::DeviceClass::Laptop,"exact Wujie profile");
    check(!wujie.machine.declaredFeatures.empty()&&wujie.machine.declaredFeatures[0].availability==mc::ResourceAvailability::Unknown,"profile is not runtime admission");
    board.baseBoard="unknown";const auto unknown=mc::observeMachineResources(board,{});
    check(!unknown.machine.catalogRecognized&&unknown.machine.declaredFeatures.empty()&&unknown.machine.deviceClass==mc::DeviceClass::Unknown,"unknown never inherits writes");
    board.manufacturer="HUAWEI";board.baseBoard="HLYL-WXX9-PCB";board.productName="HLYL-WXX9";
    const auto honor=mc::observeMachineResources(board,{});
    check(honor.machine.profile=="huawei-hlyl-wxx9"&&honor.machine.ecBackend.empty()&&honor.machine.deviceClass==mc::DeviceClass::Laptop,"Honor remains typed, without raw EC backend");
    board.manufacturer="Micro Computer (HK) Tech Limited";board.baseBoard="HPBSD";board.productName="EliteMini Series";
    const auto mini=mc::observeMachineResources(board,{});
    check(mini.machine.profile=="minisforum-hpbsd"&&mini.machine.protocol=="minisforum-hpbsd"&&mini.machine.deviceClass==mc::DeviceClass::MiniPc,"HPBSD exact profile");
    mc::MonitoringSnapshot domains;domains.processors[0].powerWatts=25;domains.processors[0].powerSource="package";
    domains.cpuCorePowerWatts=9;domains.cpuCorePowerSource="cores";domains.apuPackagePowerWatts=12;domains.apuPackagePowerSource="apu";
    domains.socCelsius=43;domains.socSource="soc";domains.pchCelsius=51;domains.pchSource="pch";
    domains.npuUsagePercent=0;domains.npuClockMhz=800;domains.npuPowerWatts=1.25;
    domains.npuUsageSource="npu load";domains.npuClockSource="npu clock";domains.npuPowerSource="npu power";
    domains.retainedForDisplay={{"cpu.cores_power",17},{"npu.power",18},{"pch.temperature",19}};
    reference::ResourceCatalogShadow domainsShadow;domainsShadow.update(board,domains);
    const reference::ResourcePresentationView domainView(domains,domainsShadow);
    const auto* package=domainView.measurement(mc::ResourceKind::Processor,0,"power");
    const auto* cores=domainView.measurement(mc::ResourceKind::Processor,0,"core_power");
    const auto* apu=domainView.measurement(mc::ResourceKind::Processor,0,"apu_package_power");
    const auto* npu=domainView.measurement(mc::ResourceKind::Accelerator,0,"power");
    const auto* pch=domainView.measurement(mc::ResourceKind::Chipset,0,"temperature");
    check(package&&cores&&apu&&package->value==25&&cores->value==9&&apu->value==12&&
        package->source=="package"&&cores->source=="cores"&&apu->source=="apu","CPU cores and package domains remain distinct");
    check(npu&&npu->value==1.25&&npu->retained==true&&npu->sampledAtMs==18&&
        pch&&pch->value==51&&pch->retained==true&&pch->sampledAtMs==19,"NPU and PCH keep separate retained timestamps");
    check(domainsShadow.current()&&mc::resourceCatalogJson(*domainsShadow.current()).find("\"kind\":\"accelerator\"")!=std::string::npos,
        "accelerator is represented in resource JSON");
    mc::MonitoringSnapshot systemSensors;systemSensors.processorVendors[0]=mc::ProcessorVendor::Amd;
    systemSensors.additionalSensors.push_back({"HPBSD system","temperature","C","ITE5571","ok",32,32,101,mc::SensorTimestampOrigin::SoftwareObservation});
    systemSensors.additionalSensors.push_back({"HPBSD system","fan_pwm","percent","ITE5571","invalid",{},255,102});
    reference::ResourceCatalogShadow sensorShadow;sensorShadow.update(board,systemSensors);
    const reference::ResourcePresentationView sensorView(systemSensors,sensorShadow);
    const auto* systemTemp=sensorView.measurement(mc::ResourceKind::Sensor,0,"temperature");
    const auto* unknownTime=sensorView.measurement(mc::ResourceKind::Sensor,1,"fan_pwm");
    const auto* missingDuty=sensorView.measurement(mc::ResourceKind::Sensor,1,"fan_pwm");
    check(systemTemp&&systemTemp->value==32&&systemTemp->raw==32&&systemTemp->sampledAtMs==101&&
        !systemTemp->retained.has_value()&&systemTemp->timestampOrigin==mc::ResourceTimestampOrigin::SoftwareObservation,
        "system sensor keeps its source value, observation time and unknown freshness");
    check(unknownTime&&unknownTime->timestampOrigin==mc::ResourceTimestampOrigin::Unknown,
        "an unlabelled sensor timestamp is not assigned invented provenance");
    check(mc::resourceCatalogJson(*sensorShadow.current()).find("\"timestamp_origin\":\"software_observation\"")!=std::string::npos,
        "timestamp provenance is exposed in JSON");
    check(missingDuty&&!missingDuty->value&&missingDuty->raw==255&&missingDuty->status=="invalid",
        "invalid sensor value remains unknown with raw evidence");
    check(sensorShadow.current()&&sensorShadow.current()->resources[1].facets[0].availability==mc::ResourceAvailability::Unknown&&
        sensorShadow.current()->resources[1].facets[0].evidence==mc::ResourceEvidence::Unknown,"missing sensor value is not advertised as observed");
    check(sensorShadow.current()&&mc::compareMonitoringProjection(systemSensors,*sensorShadow.current()).empty(),"additional sensor parity");
    auto wrongSensor=*sensorShadow.current();
    for(auto& row:wrongSensor.resources)if(row.kind==mc::ResourceKind::Sensor&&row.presentationSlot==1)row.measurements[0].raw=200;
    check(!mc::compareMonitoringProjection(systemSensors,wrongSensor).empty(),"additional raw evidence mismatch detected");
    wrongSensor=*sensorShadow.current();
    for(auto& row:wrongSensor.resources)if(row.kind==mc::ResourceKind::Sensor&&row.presentationSlot==1)row.measurements.clear();
    const auto missingUnknown=mc::verifyMonitoringProjection(systemSensors,wrongSensor);
    check(!missingUnknown.verified()&&!missingUnknown.mismatches.empty(),
        "dropping a null measurement and its evidence fails projection verification");
    mc::FanReadout systemFan;const auto oldSystem=mc::gui::ui::legacyui::processorCard(systemSensors,systemFan,0,"fan");
    const auto directSystem=mc::gui::ui::processorCard(systemSensors,systemFan,0,"fan",{},&sensorView);
    check(directSystem.secondaryTitle==oldSystem.secondaryTitle&&directSystem.secondaryValue==oldSystem.secondaryValue&&
        directSystem.secondarySource==oldSystem.secondarySource,"HPBSD system temperature display unchanged");
    struct ExpectedProfile {const char* id;unsigned mask;};
    // Fixed before/after admission baseline: fans, power, battery, lighting,
    // SMU telemetry and liquid cooling, respectively. Inventory-only = 0.
    const ExpectedProfile known[]{ {"wu-jie-15-xpro",31},{"huawei-hlyl-wxx9",31},
        {"mechrevo-gm6ir0c",15},{"mechrevo-gm5hg0a",15},{"machenike-l16",15},
        {"eluktronics-hydroc16g2",47},{"minisforum-hpbsd",17} };
    unsigned reviewedProfiles=0;
    for(const auto* profile:mc::deviceProfiles()){
        ++reviewedProfiles;
        check(profile!=nullptr,"registered profile is present");
        const auto* ram=profile->ramFanData;
        check(bool(ram)==mc::ramFanControlProfile(profile->id),"RAM profile data exists only for the four existing family mappings");
        if(ram){
            check(ram->gmFamily==mc::gmRamFanProfile(profile->id)&&ram->amd==mc::gmAmdProfile(profile->id)&&
                ram->doubleCodec==mc::gmPowerDoubleCodec(profile->id),"RAM profile flags match existing behavior");
            check(ram->modelName==mc::gmModelName(profile->id)&&ram->settingsFile==mc::gmSettings(profile->id)&&
                ram->powerDraftFile==mc::gmPowerDraftFile(profile->id)&&ram->fanCurveFile==mc::gmFanCurveFile(profile->id),
                "RAM profile display and file names match existing behavior");
            for(int channel=0;channel<3;++channel)
                check((channel==2?ram->amdPeakMaximum:ram->amdSustainedMaximum)==mc::gmAmdPowerMaximum(profile->id,channel),
                    "RAM profile max values match existing behavior");
            for(unsigned project=0;project<=256;++project){
                const bool expectedProject=ram->projectRule==mc::RamFanProjectRule::Exact25?project==25:
                    ram->projectRule==mc::RamFanProjectRule::Exact26?project==26:project>0&&project<255;
                check(expectedProject==mc::gmRamProject(profile->id,project),"RAM profile project rule matches existing behavior");
            }
        }
        const auto declared=mc::declaredProfileFeatures(profile->capabilities);
        const mc::DeviceFeature kinds[]{mc::DeviceFeature::Fans,mc::DeviceFeature::PowerLimits,mc::DeviceFeature::BatteryCharge,
            mc::DeviceFeature::Backlight,mc::DeviceFeature::SmuTelemetry,mc::DeviceFeature::LiquidCooling};
        unsigned mask=0,expected=0;
        for(const auto& item:known)if(profile->id==item.id)expected=item.mask;
        for(unsigned i=0;i<6;++i)if(profile->capabilities.supports(kinds[i]))mask|=1u<<i;
        check(mask==expected,"legacy device capability admission baseline");
        check(profile->capabilities.anyEcControl()==bool(expected&47u),"legacy EC identity gate baseline");
        for(const auto kind:kinds){unsigned count=0;for(const auto& feature:declared)if(feature.feature==kind)++count;
            check(count==(profile->capabilities.supports(kind)?1u:0u),"legacy profile declaration preserved exactly");}
        mc::BoardDescriptor exact;exact.manufacturer=profile->manufacturer;exact.baseBoard=profile->baseBoard;exact.productName=profile->productName;
        const auto observed=mc::observeMachineResources(exact,{});
        check(observed.machine.profile==profile->id,"registered profile resolves its exact SMBIOS tuple");
        auto different=exact;different.manufacturer+=" (other)";different.modelId=profile->id;
        check(mc::deviceProfile(different).id.empty(),"spoofed model ID and different manufacturer do not select a profile");
        different=exact;different.baseBoard+="-other";different.modelId=profile->id;
        check(mc::deviceProfile(different).id.empty(),"similar board name does not select a profile");
        if(!profile->productName.empty()){
            different=exact;different.productName+="-other";
            check(mc::deviceProfile(different).id.empty(),"different required product does not select a profile");
        }
        if(observed.machine.profile==profile->id){
            check(observed.machine.declaredFeatures.size()==declared.size(),"catalog declares all and only legacy profile features");
            for(const auto& f:observed.machine.declaredFeatures)
                check(f.availability==mc::ResourceAvailability::Unknown&&f.evidence==mc::ResourceEvidence::Documented,"declaration never grants admission");
        }
    }
    std::cout<<"Exact SMBIOS profile checks: "<<reviewedProfiles<<'\n';
    constexpr std::string_view future[]{"server.sensor.read"};
    const mc::DeviceCapabilities futureCaps{future};const auto futureDeclarations=mc::declaredProfileFeatures(futureCaps);
    check(futureDeclarations.size()==1&&futureDeclarations[0].name=="server.sensor.read"&&
        !futureDeclarations[0].feature&& !futureCaps.anyEcControl(),"new declaration does not grant legacy EC admission");
    reference::ResourceCatalogShadow fullShadow;
    fullShadow.update(board,components);
    const reference::ResourcePresentationView direct(components,fullShadow);
    check(direct.measurement(mc::ResourceKind::Memory,0,"temperature")!=nullptr,"verified memory reading exposed to UI");
    check(!reference::resourceUiPolicy("0")&&reference::resourceUiPolicy("")&&reference::resourceUiPolicy("1")&&reference::resourceUiPolicy("invalid"),
        "resource UI override is opt-out and exact");
    const reference::ResourcePresentationView disabled(components,fullShadow,false);
    check(!disabled.available()&&!disabled.measurement(mc::ResourceKind::Memory,0,"temperature")&&
        mc::gui::ui::monitoringCard(components,1,&disabled).value=="—",
        "disabled resource presentation falls back to the original observation");
    const auto directMemory=mc::gui::ui::monitoringCard(components,1,&direct);
    const auto oldMemory=mc::gui::ui::legacyui::monitoringCard(components,1);
    check(directMemory.title==oldMemory.title&&directMemory.value==oldMemory.value&&directMemory.unit==oldMemory.unit&&
        directMemory.detail==oldMemory.detail&&directMemory.source==oldMemory.source,"memory card matches old presentation");
    const auto directBattery=mc::gui::ui::batteryCard(components.battery,&direct);
    const auto oldBattery=mc::gui::ui::legacyui::batteryCard(components.battery);
    check(directBattery.left[0].value==oldBattery.left[0].value&&directBattery.left[1].value==oldBattery.left[1].value&&
        directBattery.right==oldBattery.right&&directBattery.flow==oldBattery.flow,"battery card matches old presentation");
    {
        // Compare complete user-facing card data, including labels and sources,
        // across all four languages, both slots and fresh/zero/absent/retained
        // observations. Identical UI text does not prove sensor accuracy.
        const auto savedLanguage=mc::gui::ui::languageIndex();
        for(unsigned language=0;language<4;++language){
            mc::gui::ui::setLanguage(language);
            for(int scenario=0;scenario<4;++scenario){
                mc::MonitoringSnapshot s;s.processorModels[0]="fixture";s.processors[0].celsius=35;
                for(int i=0;i<2;++i){s.memory[i].present=true;s.memory[i].model="memory";s.memory[i].source="SPD";
                    s.disks[i].present=true;s.disks[i].model="disk";s.disks[i].source="NVMe";
                    s.memory[i].celsius=scenario==1?0:30+i;s.disks[i].celsius=scenario==1?0:40+i;}
                s.battery.present=true;s.battery.percent=scenario==1?0:55;s.battery.watts=scenario==1?0:-12;
                s.battery.volts=15.5;s.battery.celsius=29;s.battery.milliamps=800;s.battery.cycles=22;
                s.battery.flow=mc::BatteryFlow::Discharging;s.battery.source="battery fixture";
                if(scenario==2){s.memory[1].present=false;s.disks[0].present=false;s.battery.present=false;}
                if(scenario==3){s.retainedForDisplay={{"memory.0",17},{"disk.1",19}};s.battery.dynamicStale=true;}
                reference::ResourceCatalogShadow shadow;shadow.update(board,s);const reference::ResourcePresentationView view(s,shadow);
                check(view.available(),"card parity fixture has verified catalog");
                for(int i=1;i<=4;++i){const auto old=mc::gui::ui::legacyui::monitoringCard(s,i),now=mc::gui::ui::monitoringCard(s,i,&view);
                    check(old.title==now.title&&old.value==now.value&&old.unit==now.unit&&old.detail==now.detail&&
                        old.source==now.source&&old.flow==now.flow,"all memory and disk card fields match");
                    check(mc::gui::ui::translate(old.detail)==mc::gui::ui::translate(now.detail)&&
                        mc::gui::ui::translate(old.source)==mc::gui::ui::translate(now.source),"translated memory and disk details match");}
                const auto old=mc::gui::ui::legacyui::batteryCard(s.battery),now=mc::gui::ui::batteryCard(s.battery,&view);
                check(old.right==now.right&&old.flow==now.flow,"battery right fields and state match");
                check(mc::gui::ui::legacyui::batteryPowerStatus(s.battery)==mc::gui::ui::batteryPowerStatus(s.battery,&view),
                    "SETs battery status matches direct source");
                for(unsigned i=0;i<3;++i)check(old.left[i].label==now.left[i].label&&old.left[i].value==now.left[i].value&&
                    mc::gui::ui::translate(old.left[i].label)==mc::gui::ui::translate(now.left[i].label),"battery labels and values match");
            }
        }
        mc::gui::ui::setLanguage(savedLanguage);
    }
    {
        mc::BatteryReading b;b.present=true;b.percent=42;b.celsius=30;b.dynamicAtMs[0]=100;
        mc::BatteryResourceState projection;projection.update(board,b);
        const auto view=projection.view();check(view.available(),"accepted battery resource state available");
        auto changed=b;changed.percent=43;projection.update(board,changed);
        check(!view.available()&&projection.view().measurement(mc::ResourceKind::Battery,0,"charge")->value==43,
            "battery replacement invalidates old view and publishes its own sample");
        projection.clear();check(!projection.view().available(),"sleep invalidates battery state without source fallback");
    }
    auto newComponents=components;newComponents.memory[0].celsius=44;
    const reference::ResourcePresentationView staleView(newComponents,fullShadow);
    check(!staleView.measurement(mc::ResourceKind::Memory,0,"temperature")&&
        mc::gui::ui::monitoringCard(newComponents,1,&staleView).value=="—",
        "stale view falls back to latest observation");
    // Identical models and temperatures cannot establish that the same module
    // remains in a slot. A changed reported serial invalidates the old shadow,
    // without treating that serial as a durable or globally unique identity.
    auto twins=components;twins.memory[0].model="same";twins.memory[0].serial="module-a";
    twins.memory[0].celsius=40;twins.disks[0].present=true;twins.disks[0].model="same disk";twins.disks[0].serial="disk-a";twins.disks[0].celsius=40;
    reference::ResourceCatalogShadow twinsShadow;twinsShadow.update(board,twins);
    check(reference::ResourcePresentationView(twins,twinsShadow).available(),"matching serial hints accepted");
    check(mc::resourceCatalogJson(*twinsShadow.current()).find("\"source_serial_hint\":\"module-a\"")!=std::string::npos,
        "serial hint is identified as unverified source metadata in JSON");
    auto changedModule=twins;changedModule.memory[0].serial="module-b";
    check(!reference::ResourcePresentationView(changedModule,twinsShadow).available(),"changed same-model memory serial rejects stale projection");
    auto changedDisk=twins;changedDisk.disks[0].serial="disk-b";
    check(!reference::ResourcePresentationView(changedDisk,twinsShadow).available(),"changed same-model storage serial rejects stale projection");
    auto noSerial=twins;noSerial.memory[0].serial.clear();noSerial.disks[0].serial.clear();
    reference::ResourceCatalogShadow noSerialShadow;noSerialShadow.update(board,noSerial);
    check(reference::ResourcePresentationView(noSerial,noSerialShadow).available(),"missing serial remains acceptable for matching snapshot");
    const auto priorLanguage=mc::gui::ui::languageIndex();
    for(unsigned language=0;language<4;++language){mc::gui::ui::setLanguage(language);
    for(const auto vendor:{mc::ProcessorVendor::Amd,mc::ProcessorVendor::Intel,mc::ProcessorVendor::Unknown}){
        mc::MonitoringSnapshot processor;processor.processorVendors[0]=vendor;
        auto& p=processor.processors[0];p.celsius=42.5;p.usagePercent=12;p.clockMhz=2100;p.powerWatts=25;
        p.temperatureSource="temp";p.usageSource="load";p.clockSource="clock";p.powerSource="package";
        processor.cpuCorePowerWatts=9;processor.cpuCorePowerSource="core";
        processor.pchCelsius=51;processor.pchSource="pch";processor.socCelsius=43;processor.socSource="soc";
        processor.apuPackagePowerWatts=12;processor.apuPackagePowerSource="apu";
        reference::ResourceCatalogShadow cpuShadow;cpuShadow.update(board,processor);
        const reference::ResourcePresentationView cpuView(processor,cpuShadow);mc::FanReadout fan;
        const auto oldCpu=mc::gui::ui::legacyui::processorCard(processor,fan,0,"fan");
        const auto directCpu=mc::gui::ui::processorCard(processor,fan,0,"fan",{},&cpuView);
        check(cpuView.measurement(mc::ResourceKind::Processor,0,"temperature")&&
            directCpu.title==oldCpu.title&&directCpu.values==oldCpu.values&&directCpu.valueUnits==oldCpu.valueUnits&&
            directCpu.sources==oldCpu.sources&&directCpu.information==oldCpu.information&&
            directCpu.secondaryTitle==oldCpu.secondaryTitle&&directCpu.secondaryValue==oldCpu.secondaryValue&&
            directCpu.secondaryUnit==oldCpu.secondaryUnit&&directCpu.secondarySource==oldCpu.secondarySource&&
            directCpu.secondaryExtraTitle==oldCpu.secondaryExtraTitle&&directCpu.secondaryExtraValue==oldCpu.secondaryExtraValue&&
            directCpu.secondaryExtraUnit==oldCpu.secondaryExtraUnit&&directCpu.selectionReason==oldCpu.selectionReason,
            "CPU card catalog presentation preserves all fields for every vendor");
        for(unsigned i=0;i<3;++i)check(directCpu.summary[i].label==oldCpu.summary[i].label&&
            directCpu.summary[i].value==oldCpu.summary[i].value,"CPU summary preserves CPU versus package semantics");
    }
    }
    mc::gui::ui::setLanguage(priorLanguage);
    const auto sameGpuCard=[&](const mc::gui::ui::ProcessorCard& a,const mc::gui::ui::ProcessorCard& b){
        check(a.title==b.title&&a.values==b.values&&a.valueUnits==b.valueUnits&&a.sources==b.sources&&a.information==b.information&&
            a.secondaryTitle==b.secondaryTitle&&a.secondaryValue==b.secondaryValue&&a.secondaryUnit==b.secondaryUnit&&
            a.secondaryExtraTitle==b.secondaryExtraTitle&&a.secondaryExtraValue==b.secondaryExtraValue&&
            a.secondaryExtraUnit==b.secondaryExtraUnit&&a.secondarySource==b.secondarySource&&
            a.selectionReason==b.selectionReason,"GPU selection and card sources preserved");
        for(unsigned i=0;i<3;++i)check(a.summary[i].label==b.summary[i].label&&a.summary[i].value==b.summary[i].value,
            "GPU summary unchanged");
    };
    for(unsigned language=0;language<4;++language){mc::gui::ui::setLanguage(language);
    for(const unsigned vendor:{0x1002u,0x8086u,0x10deu,0u}){
        mc::MonitoringSnapshot graphics;graphics.gpuCards.initialized=true;graphics.gpuCards.hybridAvailable=true;
        mc::GpuCardReading first,second;first.name="first";first.adapterId="first-id";first.vendorId=0x1002;first.usagePercent=2;
        second.name="selected";second.adapterId="selected-id";second.vendorId=vendor;second.usagePercent=30;
        second.celsius=47;second.clockMhz=900;second.powerWatts=20;second.usageSource="selected source";
        second.temperatureSource="selected temperature";second.powerSource="selected power";
        graphics.gpuCards.cards={first,second};reference::ResourceCatalogShadow graphicsShadow;graphicsShadow.update(board,graphics);
        const reference::ResourcePresentationView graphicsView(graphics,graphicsShadow);mc::FanReadout fan;
        const auto oldGpu=mc::gui::ui::legacyui::processorCard(graphics,fan,1,"fan");
        const auto directGpu=mc::gui::ui::processorCard(graphics,fan,1,"fan",{},&graphicsView);
        check(graphicsView.measurement(mc::ResourceKind::Graphics,1,"temperature")!=nullptr,"selected GPU metric belongs to its own slot");
        sameGpuCard(oldGpu,directGpu);
        auto changed=graphics;changed.gpuCards.cards.erase(changed.gpuCards.cards.begin());
        const reference::ResourcePresentationView staleGpu(changed,graphicsShadow);
        check(!staleGpu.measurement(mc::ResourceKind::Graphics,0,"usage"),"stale GPU list is rejected");
        check(mc::gui::ui::processorCard(changed,fan,1,"fan",{},&staleGpu).values[2]=="—","invalidated resource view does not select legacy GPU numerics");
    }}
    mc::gui::ui::setLanguage(priorLanguage);
    mc::MonitoringSnapshot legacyGpu;legacyGpu.processorVendors[1]=mc::ProcessorVendor::Amd;
    legacyGpu.processorModels[1]="legacy";legacyGpu.processors[1].celsius=33;legacyGpu.processors[1].temperatureSource="legacy source";
    legacyGpu.npuUsagePercent=0;legacyGpu.npuClockMhz=800;legacyGpu.npuPowerWatts=1.25;
    legacyGpu.npuClockSource="npu clock";legacyGpu.npuPowerSource="npu power";
    reference::ResourceCatalogShadow legacyShadow;legacyShadow.update(board,legacyGpu);
    const reference::ResourcePresentationView legacyView(legacyGpu,legacyShadow);mc::FanReadout legacyFan;
    check(legacyView.measurement(mc::ResourceKind::Graphics,1,"temperature")!=nullptr,"legacy GPU slot retained");
    check(legacyView.measurement(mc::ResourceKind::Accelerator,0,"usage")!=nullptr,"NPU zero reading retained");
    sameGpuCard(mc::gui::ui::legacyui::processorCard(legacyGpu,legacyFan,1,"fan"),mc::gui::ui::processorCard(legacyGpu,legacyFan,1,"fan",{},&legacyView));
    fullShadow.update(board,multi);reference::ResourcePresentationView gpuView(multi,fullShadow);
    check(gpuView.available()&&gpuView.measurement(mc::ResourceKind::Graphics,0,"usage")&&
        gpuView.measurement(mc::ResourceKind::Graphics,1,"power"),"multiple GPU rows stay distinct");
    auto gpuDisplay=multi;gpuDisplay.gpuCards.cards.pop_back();
    check(!reference::ResourcePresentationView(gpuDisplay,fullShadow).available(),"removed GPU invalidates old catalog");
    fullShadow.update(board,batteries);reference::ResourcePresentationView batteryView(batteries,fullShadow);
    const auto* battery0=batteryView.measurement(mc::ResourceKind::Battery,0,"charge");
    const auto* battery1=batteryView.measurement(mc::ResourceKind::Battery,1,"charge");
    check(batteryView.available()&&battery0&&battery1&&battery0->value==55&&battery1->value==77,"multiple batteries stay distinct");
    for(int kind=0;kind<4;++kind){
        mc::MonitoringSnapshot single;
        if(kind==0){single.gpuCards.initialized=true;mc::GpuCardReading g;g.name="only gpu";g.celsius=45;single.gpuCards.cards.push_back(g);}
        if(kind==1){single.memory[0].present=true;single.memory[0].celsius=31;}
        if(kind==2){single.disks[0].present=true;single.disks[0].celsius=32;}
        if(kind==3){single.battery.present=true;single.battery.percent=64;}
        fullShadow.update(board,single);const reference::ResourcePresentationView only(single,fullShadow);
        const auto resource=kind==0?mc::ResourceKind::Graphics:kind==1?mc::ResourceKind::Memory:
            kind==2?mc::ResourceKind::Storage:mc::ResourceKind::Battery;
        check(only.available()&&only.measurement(resource,0,kind==3?"charge":"temperature"),
            "each standalone resource feeds only its matching display");
    }
    reference::ResourceCatalogShadow shadow;check(!shadow.current(),"shadow starts empty");
    check(reference::resourceShadowSummary(shadow)=="{\"available\":false,\"mismatches\":null}","unavailable is not a zero mismatch result");
    const auto original=sample;shadow.update(board,sample);
    check(shadow.current()&&shadow.current()->machine.profile=="minisforum-hpbsd","shadow observes machine");
    check(shadow.mismatches().empty(),"shadow processor and fan parity");
    check(reference::ResourcePresentationView(sample,shadow).available(),"verified catalog feeds display");
    auto display=sample;display.processors[0].fanRpm=1000;
    check(!reference::ResourcePresentationView(display,shadow).available(),"stale catalog cannot overwrite newer display data");
    auto newer=sample;newer.retainedForDisplay.clear();
    check(!reference::ResourcePresentationView(newer,shadow).available(),"stale metadata cannot overwrite a fresh reading");
    check(reference::resourceShadowSummary(shadow).find("\"mismatches\":0")!=std::string::npos,"available summary contains parity result");
    check(reference::resourceShadowSummary(shadow).find("minisforum")==std::string::npos,"summary does not expose hardware identity");
    check(sample==original,"shadow never mutates source snapshot");
    const reference::ResourcePresentationView previousView(sample,shadow);
    check(previousView.available()&&previousView.measurement(mc::ResourceKind::Processor,0,"temperature"),"view before replacement");
    shadow.update(board,sample);
    check(!previousView.available()&&!previousView.measurement(mc::ResourceKind::Processor,0,"temperature"),
        "updating the shadow invalidates an existing view even with identical values");
    const reference::ResourcePresentationView beforeClear(sample,shadow);
    shadow.clear();check(!shadow.current(),"preview clears shadow");
    check(!beforeClear.available()&&!beforeClear.measurement(mc::ResourceKind::Processor,0,"temperature"),
        "clearing the shadow invalidates its existing view");
    std::optional<reference::ResourcePresentationView> escaped;
    {reference::ResourceCatalogShadow temporary;temporary.update(board,sample);escaped.emplace(sample,temporary);
        check(escaped->available(),"temporary shadow initially exposes verified values");}
    check(!escaped->available()&&!escaped->measurement(mc::ResourceKind::Processor,0,"temperature"),
        "destroying the shadow invalidates a surviving view safely");
    check(!reference::ResourcePresentationView(display,shadow).available(),"empty shadow keeps original display");
    std::cout<<"resource catalog checks passed\n";
}
