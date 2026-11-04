#include "../common/DiagnosticDumpInternal.h"
#include "MagicControl/BoardIdentity.h"
#include "MagicControl/DeviceProfile.h"
#include "MagicControl/EcAccess.h"
#include "MagicControl/DeviceOperation.h"
#include "MagicControl/LaptopControl.h"
#include "MagicControl/EcRegister.h"
#include "MagicControl/FanCurve.h"
#include <format>
#include <algorithm>
#include "../../devices/mechrevo/gm6ir0c/ControlWindows.h"
#include "../../devices/mechrevo/gm6ir0c/FactoryCurves.h"
#include "../../devices/huawei/hlyl-wxx9/I2EcWindows.h"
#include "../../devices/huawei/hlyl-wxx9/BacklightWindows.h"
#include "../../devices/huawei/hlyl-wxx9/FanControlWindows.h"
#include "../../devices/eluktronics/hydroc-16-g2/DiagnosticsWindows.h"
#include "../../protocols/uniwill/hydroc16g2/Diagnostics.h"
#include "RamFanFactoryWindows.h"
#include "uniwill-acpi/ReviewedDriverWindows.h"
#include "BackendWindows.h"
#include "../../devices/minisforum/hpbsd/I2EcWindows.h"
#include "../../protocols/minisforum/HpbsdFans.h"
#include "../../devices/lenovo/legion-kwcn/ControlWindows.h"
#include "../../devices/lenovo/legion-kwcn/I2EcWindows.h"
#include "../../devices/lenovo/legion-kwcn/WmiWindows.h"
#include "../../protocols/lenovo/LegionFirmware.h"
#include "WindowsDisplay.h"
#include <windows.h>
#include "MagicControl/PawnIoRuntimeFiles.h"
#include "EmbeddedRuntimeWindows.h"
#include "BinaryHashWindows.h"
namespace mc::diagnostics {
std::string capturePlatformDisplays(std::stop_token token){return windows::displayDiagnosticJson(token);}
DependencySnapshot platformDependencies(){
    DependencySnapshot snapshot;snapshot.pawnIoApplicable=true;
    snapshot.pawnIoRuntimeStorage="embedded executable RCDATA; protected ProgramData cache only when loaded";
    try{snapshot.pawnIoRuntimeDirectory=windows::embeddedRuntimeCachePath("PawnIO");}catch(const std::exception& e){snapshot.error=e.what();}
    for(const auto name:dependencies::requiredRuntimeFiles){
        DependencyRuntimeFile file;file.name=name;
        try{const auto bytes=windows::embeddedRuntimeBytes("PawnIO",name);file.present=true;file.bytes=bytes.size();file.sha256=windows::detail::binarySha256(bytes);}
        catch(const std::exception& e){file.error=e.what();}
        snapshot.pawnIoRuntimeFiles.push_back(std::move(file));
    }
    try{snapshot.pawnIo=dependencies::pawnIoDriverStatus(dependencies::PawnIoVersionQuery::LoadedDriver);snapshot.status="Windows SCM + installed SYS VERSIONINFO + loaded driver version IOCTL; no module or hardware IO";}
    catch(const std::exception& e){snapshot.status="Windows SCM query failed; installation/running state unknown";snapshot.error=e.what();}
    return snapshot;
}
std::string capturePlatformKeyboardContext(std::stop_token token){
    if(token.stop_requested()||wcsstr(GetCommandLineW(),L"--no-ec"))return {};
    if(deviceProfile(readBoardDescriptor()).id!="huawei-hlyl-wxx9")return {};
    auto operation=deviceOperations().tryBegin(DeviceOperationKind::Background);
    if(!operation)throw std::runtime_error("Keyboard context: управление занято; snapshot unavailable");
    return windows::readHlylKeyboardDiagnosticJson();
}
FirmwareDefaultResult preparePlatformFanDefault(EcAccess* access,const FirmwareFanDefault& restore,std::stop_token token){
    FirmwareDefaultResult result;
    try{
        if(token.stop_requested()){result.status="Default отменён до запуска";return result;}
        if(wcsstr(GetCommandLineW(),L"--no-ec")){result.status="Default пропущен: --no-ec";return result;}
        const auto id=deviceProfile(readBoardDescriptor()).id;
        if(id=="wu-jie-15-xpro"){
            if(!access||!access->boardVerified()||!restore){result.status="Default: нет активного контроллера X5";return result;}
            // The shared controller stops its online worker before releasing
            // RAM/custom/override/boost. Never race it with a second raw writer.
            result.attempted=true;result.session=restore();
            if(!readNativeFanFlags(*access).defaultMode)throw std::runtime_error("Default X5: флаги EC не сняты");
        }else if(id==lenovo::profileId){
            result.attempted=true;result.session=windows::prepareLegionDiagnostic();
        }else if(ramFanControlProfile(id)||id=="huawei-hlyl-wxx9"){
            auto operation=deviceOperations().tryBegin();if(!operation){result.status="Default: управление занято другой операцией";return result;}
            result.attempted=true;
            if(ramFanControlProfile(id)){
                auto controller=windows::gm6Controller();result.session=controller->prepareDiagnostic();
                const auto state=controller->read();
                if(!state.connected||state.online||state.ramActive||state.custom||state.boost||state.needsRestore)
                    throw std::runtime_error("Default GM6IR0C не подтверждён");
            }else{
                auto controller=windows::hlylFanController();result.session=controller->prepareDiagnostic();
                if(controller->read().manual)throw std::runtime_error("Default Honor не подтверждён");
            }
        }else{result.status="Default: для этой платы нет подтверждённого контроллера; только пассивная диагностика";return result;}
        result.confirmed=true;
        result.status=result.session.restore?"Default вентиляторов подтверждён; сессия возврата FANs подготовлена":
            "Default вентиляторов подтверждён; прежний режим FANs не подтверждён для безопасного возврата";
    }catch(const std::exception& e){result.status=e.what();}
    return result;
}
std::optional<bool> platformSnapshotFanDefault(const EcDump& dump){
    const auto id=deviceProfile(readBoardDescriptor()).id;
    const auto byte=[&](unsigned a)->std::optional<unsigned>{
        if(a<dump.base||a-dump.base>=dump.bytes.size()||a-dump.base>=dump.valid.size()||!dump.valid[a-dump.base])return {};
        return dump.bytes[a-dump.base];};
    if(id==lenovo::profileId){
        const auto flags=byte(0xdfe0),overrides=byte(0xdfdd),mode=byte(0xc41d),count=byte(0xdfef),selected=byte(0xdfeb),sensors=byte(0xdff1),fans=byte(0xdff0);
        if(!flags||!overrides||!mode||!count||!selected||!sensors||!fans)return {};
        lenovo::FanPacket table{};for(std::size_t i=0;i<table.size();++i){const auto v=byte(lenovo::tableAddress+static_cast<unsigned>(i));if(!v)return {};table[i]=static_cast<std::uint8_t>(*v);}
        return !(*flags&3)&&!*overrides&&!(*mode&4)&&lenovo::factoryPacketKnown(*selected,*count,table,*sensors&63,*fans&3);
    }
    if(id=="minisforum-hpbsd"){
        hpbsd::Snapshot s{};const auto set=[&](int index,unsigned a){const auto v=byte(a);if(!v)throw std::runtime_error("Incomplete HPBSD table");s[index]=static_cast<std::uint8_t>(*v);};
        try{set(0,0x32f);set(1,0x88a);set(2,0x88b);for(int i=0;i<24;++i)set(3+i,0x310+i);for(int i=0;i<8;++i)set(27+i,0x8b0+i);for(int i=0;i<9;++i)set(35+i,0x330+i);for(int i=0;i<3;++i)set(44+i,0x8c0+i);hpbsd::validateImmutable(s);return s==hpbsd::factory(s[0]);}catch(...){return {};}
    }
    if(id=="huawei-hlyl-wxx9"){auto flags=byte(0x042C);if(flags)return !(*flags&2);return {};}
    if(id!="wu-jie-15-xpro"&&!gmRamFanProfile(id)&&id!=hydroc16g2::profileId)return {};
    const auto commit=byte(0x7C6),custom=byte(0x726),override=byte(0x727),mode=byte(0x751);
    if(!commit||!custom||!override||!mode)return {};
    return !((*commit&4)||(*custom&128)||(*override&64)||(*mode&64));
}
std::string platformFanTables(const EcDump& dump,bool defaultConfirmed){
    const auto id=deviceProfile(readBoardDescriptor()).id;
    std::string out=std::format("{{\"schema\":1,\"default_command_verified\":{},\"source\":\"initial ec.bin before telemetry and Max\",\"firmware_internal_table_exported\":false",defaultConfirmed);
    if(id==lenovo::profileId){
        const auto value=[&](unsigned a){return a>=dump.base&&a-dump.base<dump.bytes.size()&&a-dump.base<dump.valid.size()&&dump.valid[a-dump.base]?std::to_string(dump.bytes[a-dump.base]):std::string("null");};
        const auto observed=platformSnapshotFanDefault(dump);
        out+=",\"protocol\":\"Lenovo IT5507 KWCN54WW\",\"snapshot_is_canonical_default\":"+std::string(observed?(*observed?"true":"false"):"null")+
            ",\"descriptor\":"+value(0xdfeb)+",\"next_descriptor\":"+value(0xdfec)+",\"native_row_count\":"+value(0xdfef)+",\"sensor_mask\":"+value(0xdff1)+",\"loader_governor_holds\":"+value(0xdfe0)+",\"rows_raw_16_bytes\":[";
        for(int row=0;row<lenovo::nativeCapacity;++row){if(row)out+=',';out+='[';for(int b=0;b<16;++b){if(b)out+=',';out+=value(lenovo::tableAddress+row*16+b);}out+=']';}
        out+="],\"filtered_sensor_temperatures\":[";for(int i=0;i<6;++i){if(i)out+=',';out+=value(0xdffa+i);}
        out+="],\"current_stages\":[";for(int i=0;i<7;++i){if(i)out+=',';out+=value(0xdfe4+i);}
        return out+"],\"note\":\"Initial live SRAM table, not a saved Manual curve or a flash image. Row[0..1] targets are100 RPM units; row[4..15] are six up/down temperature pairs.\"}\n";
    }
    if(id=="minisforum-hpbsd"){
        const auto value=[&](unsigned a){return a>=dump.base&&a-dump.base<dump.bytes.size()&&a-dump.base<dump.valid.size()&&dump.valid[a-dump.base]?std::to_string(dump.bytes[a-dump.base]):std::string("null");};
        const auto observed=platformSnapshotFanDefault(dump);
        out+=",\"protocol\":\"HPBSD ITE5571\",\"snapshot_is_canonical_default\":"+std::string(observed?(*observed?"true":"false"):"null")+",\"cpu_selector\":"+value(0x32f);
        const auto block=[&](const char* name,unsigned start,int count){out+=",\""+std::string(name)+"\":[";for(int i=0;i<count;++i){if(i)out+=',';out+=value(start+i);}out+=']';};
        block("cpu_rows_base_upper_lower",0x310,24);block("cpu_slopes",0x8b0,8);block("system_rows_base_upper_lower",0x330,9);block("system_slopes",0x8c0,3);
        return out+",\"note\":\"Observed RAM policy, not an EC-flash dump. Critical CPU/system rows and overrides are never changed by this collector.\"}\n";
    }
    if(id!="wu-jie-15-xpro"&&!gmRamFanProfile(id)&&id!=hydroc16g2::profileId)return out+",\"status\":\"No confirmed RAM fan-table map for this board\"}\n";
    const auto byte=[&](unsigned a)->std::optional<unsigned>{
        if(a<dump.base||a-dump.base>=dump.bytes.size()||a-dump.base>=dump.valid.size()||!dump.valid[a-dump.base])return {};
        return dump.bytes[a-dump.base];};
    const auto value=[&](unsigned a){auto v=byte(a);return v?std::to_string(*v):"null";};
    if(id==hydroc16g2::profileId){
        out+=",\"read_only\":true,\"layout_confirmed\":false,\"status\":\"Raw OEM capability evidence; no FANs layout selected or commands sent\",\"registers\":{";
        bool first=true;
        for(const auto address:{0x740u,0x74Cu,0x78Eu,0x7C8u,0x73Cu,0x726u,0x727u,0x751u,0x7C5u,0x7C6u,0x4A2u,0x4A3u}){
            if(!first)out+=',';first=false;out+=std::format("\"0x{:03X}\":{}",address,value(address));}
        const auto bit=[&](unsigned a,unsigned mask){const auto v=byte(a);return v?(*v&mask?"true":"false"):"null";};
        out+="},\"ramfan1p5_capability_bit\":"+std::string(bit(0x78E,0x40))+",\"lc_fan_table_capability_bit\":"+bit(0x7C8,0x20);
        out+=",\"raw_fan_ram_0xF00_0xFFF\":[";
        for(unsigned i=0;i<256;++i){if(i)out+=',';out+=value(0xF00+i);}out+="]}\n";return out;
    }
    out+=",\"ram_commit_raw\":"+value(0x7C6)+",\"custom_raw\":"+value(0x726)+",\"override_raw\":"+value(0x727)+",\"mode_raw\":"+value(0x751);
    out+=",\"snapshot_default_flags\":";
    const auto observed=platformSnapshotFanDefault(dump);out+=observed?(*observed?"true":"false"):"null";
    out+=",\"ram_note\":\"Inactive RAM is not the firmware internal default table; zeros are retained exactly as read\",\"ram_tables\":[";
    for(unsigned ch=0;ch<2;++ch){if(ch)out+=',';const unsigned base=ch?0xF30:0xF00;
        out+=std::format("{{\"channel\":\"{}\",\"base\":{},\"raw_bytes\":[",ch?"GPU":"CPU",base);
        for(unsigned i=0;i<48;++i){if(i)out+=',';out+=value(base+i);}out+="]}";}
    out+=']';
    if(gmRamFanProfile(id)){
        const auto gpuPolicy=byte(0x78B);
        out+=",\"gpu_oem_policy\":{\"register\":\"0x78B\",\"raw\":"+value(0x78B)+",\"low3bits\":"+(gpuPolicy?std::to_string(*gpuPolicy&7):"null")+
            ",\"meaning\":\"OEM SetGPUdstate/SetGPUdstateByGpuMode request bits; not measured PCI D-state, not proven MUX mode; no writes performed\"}";
        out+=",\"oem_reference\":{\"source\":\"GM6IR0C OEM DefaultFanTable Office/Gaming/Turbo JSON from ControlCenterX-Logs-20261017-215646.zip; not EC flash readback\",\"modes\":[";
        for(int m=0;m<3;++m){if(m)out+=',';out+=std::format("{{\"mode\":{},\"channels\":[",m);
            for(int ch=0;ch<2;++ch){if(ch)out+=',';out+='[';for(int p=0;p<16;++p){if(p)out+=',';const auto& v=gm6::factoryCurves[m][ch][p];out+=std::format("{{\"up_c\":{},\"down_c\":{},\"pwm_percent\":{}}}",v.up,v.down,v.duty);}out+=']';}out+="]}";}
        out+="]}";
    }
    return out+"}\n";
}
EcDump capturePlatformEc(EcAccess* access,std::stop_token token){
    EcDump dump;
    try{
        // Presentation/model preview never changes this actual-SMBIOS choice.
        const auto id=deviceProfile(readBoardDescriptor()).id;
        if(wcsstr(GetCommandLineW(),L"--no-ec")){dump.status="EC отключён параметром --no-ec";return dump;}
        if(id!="wu-jie-15-xpro"&&!gmRamFanProfile(id)&&id!="huawei-hlyl-wxx9"&&id!="minisforum-hpbsd"&&id!=lenovo::profileId&&id!=hydroc16g2::profileId&&!l16Profile(id)){
            dump.status="Полный EC RAM дамп для этой модели пока не подключён; сохранены системные сведения и доступная GPU-диагностика";
            if(auto typed=createLaptopControl()){const auto s=typed->read();dump.interfaceNotes=std::format("OEM источник: {}\nOEM состояние: {}\nПодсветка: level={}, getter/setter-reported={}\nБатарея: start={} end={} setter-reported={} (остановка тока отдельно не проверяется)\n",
                s.source,s.status,s.keyboardLevel.value_or(-1),s.keyboardWritable,s.chargeStart.value_or(-1),s.chargeEnd.value_or(-1),s.chargeWritable);}
            return dump;
        }
        auto operation=deviceOperations().tryBegin();
        if(!operation){dump.status="EC занят другой операцией. Повторите снятие после её завершения";return dump;}
        if(id==lenovo::profileId){
            {windows::legion::FanRam check;static_cast<void>(check.read(0xc200));check.finish();}
            dump.base=0xc000;dump.source="PawnIO · signed LpcIO · Lenovo IT5507 RAM C000–DFFF";
            dump.interfaceNotes="KWCN54WW / physical ITE5507. Host C000-DFFF maps to EC SRAM80000-81FFF (8192 bytes). Address selectors only; no RAM payload/SFR/flash writes. This live RAM snapshot is not EC firmware flash.\n";
            try{dump.metadataJson["lenovo-wmi.json"]=windows::legion::readWmiEvidence();}catch(const std::exception& e){dump.interfaceNotes+=std::string("WMI metadata unavailable: ")+e.what()+"\n";}
            captureEcBlocks(dump,[](auto address,auto count,auto*){windows::legion::FanRam io;std::vector<std::uint8_t> bytes;bytes.reserve(count);
                for(std::size_t i=0;i<count;++i)bytes.push_back(io.read(static_cast<std::uint16_t>(address+i)));io.finish();return bytes;},token,8192);
        }else if(id=="minisforum-hpbsd"){
            dump.source="PawnIO · LpcIO · HPBSD ITE5571 · full RAM0000–0FFF";
            dump.interfaceNotes="HPBSD BIOS1.06/AMD19h:75h; PNP/live5571+clock signature checked per block. Full4096-byte RAM window; address-latch writes only. No fan payload, override, flash or SFR sweep. This is not live EC firmware flash.\n";
            captureEcBlocks(dump,[](auto address,auto count,auto*){
                windows::HpbsdI2EcSession io;auto bytes=io.readRamBlock(address,count);io.finish();return bytes;
            },token);
        }else if(id=="mechrevo-gm5hg0a"||l16Profile(id)){
            // Exact board DSDT N.1.09MRO19 defines INOU.ECRR as byte read
            // at FED50000 + address. No inferred FAN/APU/battery/RGB writers.
            const bool l16=l16Profile(id);
            const auto identity=[l16] {
                if(l16){windows::reviewed_uniwill::verifyL16Identity();return;}
                const auto board=readBoardDescriptor();
                if(board.manufacturer!="MECHREVO"||board.baseBoard!="GM5HG0A"||
                   board.productName!="Yilong15Pro Series GM5HG0A"||
                   deviceProfile(board).id!="mechrevo-gm5hg0a"||
                   windows::reviewed_uniwill::registry(HKEY_LOCAL_MACHINE,L"HARDWARE\\DESCRIPTION\\System\\BIOS",L"BIOSVersion")!=L"N.1.09MRO19")
                    throw std::runtime_error("GM5HG0A: exact board/BIOS required for reviewed read-only EC mapping");
            };
            identity();dump.source=l16?"UWACPIDriver · MACHENIKE L16 · read-only EC RAM":"UWACPIDriver · GM5HG0A · read-only EC RAM";
            dump.interfaceNotes=l16?"MACHENIKE L16 N.1.13MAC10; INOU.ECRR byte reader; RAM 0000-0FFF; no EC payload writes.\n":"GM5HG0A N.1.09MRO19; INOU.ECRR byte reader; RAM 0000-0FFF; no EC payload writes.\n";
            windows::reviewed_uniwill::verifyReviewedUniwillDriver(dump.interfaceNotes,
                l16?"6e360ee4a0c42b2a4eaf46208c1592c87f8cd71aca2a6c52831a2ad356e61235":"e9840521975e9073881a488f9e978a18c93aed44c57fe2382b1d513fd27126e2");
            auto reader=createWindowsAcpiDiagnosticBackend();
            captureEcBlocks(dump,[&](auto address,auto size,auto*){
                identity();
                if(address>0xFFF||size>0x1000-static_cast<std::size_t>(address))
                    throw std::runtime_error("Uniwill diagnostic: RAM read outside reviewed window");
                reader->lockTransaction();
                struct Unlock{IEcBackend& reader;~Unlock(){reader.unlockTransaction();}} unlock{*reader};
                return reader->readBlock(address,size);
            },token);
            const bool allZero=std::all_of(dump.bytes.begin(),dump.bytes.end(),[](auto b){return b==0;});
            const bool allFF=std::all_of(dump.bytes.begin(),dump.bytes.end(),[](auto b){return b==255;});
            if(dump.complete&&(allZero||allFF)){
                dump.complete=false;std::fill(dump.valid.begin(),dump.valid.end(),false);
                dump.status="Uniwill diagnostic: uniform fill; firmware response not established";
            }
            dump.interfaceNotes+="Raw observations only. 7D0=DBD1 / 7D1=DBD2 in exact DSDT; not admitted as charge restart thresholds.\n";
        }else if(id==hydroc16g2::profileId){
            dump.source="UWACPIDriver · HYDROC-16 G2 · read-only EC RAM";
            windows::HydrocDiagnosticSession io(dump.interfaceNotes);
            captureEcBlocks(dump,[&io](auto address,auto size,auto*){return io.readBlock(address,size);},token);
            // This read-only session has no payload writer. It does not own a
            // global counter for other controllers; leave those counts unknown.
            dump.interfaceNotes+="Reader payload writes=0 by interface contract; global application EC writes are not measured by this session.\n";
            if(dump.valid.size()>0x7A6&&dump.valid[0x7A6]){
                const auto raw=dump.bytes[0x7A6];const int mode=(raw&0x30)>>4;
                dump.interfaceNotes+=std::format("BatteryProtection2: raw7A6=0x{:02X}; native_mode={} (0 Performance,1 Balanced,2 Healthy,3 unknown); observation only, no battery writes/percentage mapping.\n",raw,mode);
            }
        }else if(id=="huawei-hlyl-wxx9"){
            dump.source="PawnIO · LpcIO · IT8987 I2EC · HLYL BIOS1.09";
            windows::HlylI2EcSession io;
            dump.interfaceNotes=std::format("HLYL-WXX9-PCB BIOS1.09; IT8987 ID/canary8987; I2EC mode={} (не изменяется).\n"
                "Диапазон RAM0x0000–0x0FFF; SFR/peripheral/flash за пределами окна не сканируются.\n"
                "PWM period=0x{:02X}; fan PWM0=0x{:02X}, PWM1=0x{:02X} (фиксированные ранее проверенные getter-адреса).\n"
                "Дамп не записывает RAM, не переключает вентиляторы/подсветку и не является образом прошивки.\n",
                io.writable?"read-write":"read-only",io.read(0x1801),io.read(0x1808),io.read(0x1809));
            captureEcBlocks(dump,[&io](auto address,auto size,auto*){return io.readDiagnosticBlock(address,size);},token);
        }else if(gmRamFanProfile(id)){
            dump.source="UWACPIDriver · GM6IR0C · RamFan1.5";
            const auto controller=windows::gm6Controller();
            if(!controller)throw std::runtime_error("Контроллер GM6IR0C недоступен");
            captureEcBlocks(dump,[controller](auto address,auto size,auto*){return controller->readDiagnosticBlock(address,size);},token);
            dump.interfaceNotes="GM6IR0C: RGB SingleZone и именованные BatteryProtection2 подключены. Процентные пороги не приписываются этим профилям. Сам сбор EC подсветку/батарею не изменяет.\n";
        }else{
            dump.source="UWACPIDriver · X5SP4NAG · EC82/72/26";
            if(!access||!access->boardVerified())throw std::runtime_error("Общий EC-доступ не подтверждён");
            access->ensureCapability(DeviceFeature::Fans,"uniwill-x5sp4nag");
            dump.writesBefore=access->writeCount();
            captureEcBlocks(dump,[access](auto address,auto size,auto* timing){
                if(access->observationsPaused())throw EcObservationBusy("EC: временная пауза наблюдений другой транзакцией");
                return access->readBlock(address,size,timing);
            },token);
            dump.writesAfter=access->writeCount();
            dump.interfaceNotes="EC transport: "+access->info().displayName+"; board verified=true.\n";
            for(auto [name,address]:{std::pair{"Keyboard capability",ec::kKeyboardCapability},std::pair{"Keyboard level",ec::kKeyboardLevel},std::pair{"Keyboard effect",ec::kKeyboardEffect},std::pair{"Charge limit",ec::kBatteryChargeControl},std::pair{"Charge platform",std::uint16_t{0x7C3}},std::pair{"Charge gate",std::uint16_t{0x742}}})
                if(address<dump.valid.size()&&dump.valid[address])dump.interfaceNotes+=std::format("{}: EC0x{:03X}=0x{:02X}\n",name,address,dump.bytes[address]);
        }
        // The EC block session has finished and exited PNPCFG above. This
        // separate fixed-context read stays in the parent process and under
        // DeviceOperation; never put a live config-port session in a helper
        // that may be killed at its deadline.
        if(id=="huawei-hlyl-wxx9"&&!token.stop_requested()){
            try{dump.keyboardJson=windows::readHlylKeyboardDiagnosticJson();}
            catch(const std::exception& e){dump.keyboardStatus=e.what();}
        }
    }catch(const std::exception& e){dump.status=e.what();dump.complete=false;}
    return dump;
}
FirmwareFactoryResult capturePlatformFactoryTables(std::stop_token token){
    FirmwareFactoryResult result;
    try{
        const auto id=deviceProfile(readBoardDescriptor()).id;
        if(!gmRamFanProfile(id)&&id!=hydroc16g2::profileId){result.status="Factory mailbox для этой платы не подключён";return result;}
        if(token.stop_requested()){result.status="Factory отменён до запуска";return result;}
        auto operation=deviceOperations().tryBegin();if(!operation){result.status="Factory: управление занято другой операцией";return result;}
        const auto native=windows::gm6Controller()->exportFactoryTables(true,token);
        result.attempted=native.attempted;result.complete=native.complete;result.restored=!native.attempted||native.restored;
        result.json=native.json;result.status=native.status;
    }catch(const std::exception& e){result.status=e.what();}
    return result;
}
FanMaximumResult capturePlatformMaximum(EcAccess*,const MaximumFanProbe& probe,std::stop_token token){
    FanMaximumResult result;result.source="EC Fan Boost";
    try{
        const auto id=deviceProfile(readBoardDescriptor()).id;
        if(wcsstr(GetCommandLineW(),L"--no-ec")){result.status="EC отключён параметром --no-ec";return result;}
        if(id=="wu-jie-15-xpro"&&probe)return probe(token);
        if(ramFanControlProfile(id))return windows::gm6Controller()->measureMaximum(token);
        if(id=="huawei-hlyl-wxx9")return windows::hlylFanController()->measureMaximum(token);
        if(id==lenovo::profileId)return windows::measureLegionMaximum(token);
        result.status="Проба Max для этой модели/контроллера не подключена";
    }catch(const std::exception& e){result.status=e.what();}
    return result;
}
FanMaximumSample readPlatformFans(EcAccess* access){
    FanMaximumSample sample;
    if(wcsstr(GetCommandLineW(),L"--no-ec"))return sample;
    const auto id=deviceProfile(readBoardDescriptor()).id;
    if(ramFanControlProfile(id)){
        const auto state=windows::gm6Controller()->read();
        for(int ch=0;ch<2;++ch){sample.rpm[ch]=state.rpm[ch];if(state.pwm[ch])sample.percent[ch]=*state.pwm[ch];}
    }else if(id==lenovo::profileId)return windows::readLegionFans();
    else if(id=="wu-jie-15-xpro"&&access&&access->boardVerified()){
        for(int ch=0;ch<2;++ch){const auto value=access->readFanFeedback(ch?FanChannel::Gpu:FanChannel::Cpu,true);sample.rpm[ch]=static_cast<int>(value.rpm);sample.percent[ch]=value.dutyPercent;}
    }
    return sample;
}
}
