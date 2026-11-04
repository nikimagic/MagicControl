#include "../../protocols/uniwill/x5sp4nag/BatteryDecode.h"
#include "MagicControl/Monitoring.h"
#include "MagicControl/BatteryIdentityHistory.h"
#include "MagicControl/BatteryJournal.h"
#include "../../protocols/uniwill/x5sp4nag/BatteryCandidates.h"
#include "MagicControl/DeviceOperation.h"
#include "MagicControl/SensorDecoders.h"
#include "MagicControl/EcAccess.h"
#include "MagicControl/EcRegister.h"
#include "AmdTelemetryWindows.h"
#include "XrtNpuReport.h"
#include "IntelTelemetryWindows.h"
#include "IntelMemoryWindows.h"
#include "MagicControl/NvidiaGpu.h"
#include "MagicControl/IntelGpu.h"
#include "GpuDisplayRoutesWindows.h"
#include "IntelPchWindows.h"
#include "PawnTelemetryWindows.h"
#include "ProcessorFrequencyWindows.h"
#include "MagicControl/PortableSettings.h"
#include "MagicControl/LaptopControl.h"
#include "MagicControl/BoardIdentity.h"
#include "MagicControl/DeviceProfile.h"
#include "BatteryInterfacesWindows.h"
#include "PowerDiagnosticsWindows.h"
#include "../../devices/mechrevo/gm6ir0c/ControlWindows.h"
#include "../../devices/eluktronics/hydroc-16-g2/DiagnosticsWindows.h"
#include "../../devices/minisforum/hpbsd/TelemetryWindows.h"
#include "../../devices/lenovo/legion-kwcn/ControlWindows.h"

#include <windows.h>
#include <setupapi.h>
#include <winioctl.h>
#include <poclass.h>
#include <algorithm>
#include <chrono>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <format>
#include <stdexcept>
#include <string_view>
#include <sstream>
#include <utility>
#include <vector>
#include <thread>

namespace mc {
namespace {
using Clock = std::chrono::steady_clock;
// One process-wide startup observation shared by FANs and SETs. The identity,
// presence and readiness gates precede any use of the Wujie-specific map.
// No platform, power, charging or fan setting is written here.
std::string wujieBatteryDate(EcAccess* access,bool batteryAbsent=false){
    static std::mutex mutex;
    static EcAccess* owner{};
    static std::string value;
    static Clock::time_point retry{};
    std::lock_guard lock(mutex);
    if(owner!=access||batteryAbsent){owner=access;value.clear();retry={};}
    if(batteryAbsent||!access||!access->boardVerified()||
       deviceProfile(access->verifyBoard().board).id!="wu-jie-15-xpro")return {};
    if(!value.empty())return value;
    if(deviceOperations().passiveEcReadsPaused()||access->observationsPaused())return {};
    if(Clock::now()<retry)return {};
    retry=Clock::now()+std::chrono::seconds(30);
    try{
        {
        const EcAccess::Transaction transaction(*access);
        if(access->read(0x454)!=82||access->read(0x456)!=72||access->read(0x740)!=26)return {};
        const auto ready=access->readStable(0x490);
        if(ready==0xff||(ready&6)!=6)return {};
        const auto first=access->readBlock(0x314,2);
        const auto second=access->readBlock(0x314,2);
        if(first.size()!=2||first!=second)return {};
        value=uniwill::x5sp4nag::decodeBatteryManufactureDate(first[0],first[1]);
        }
        if(!value.empty()){
            logMessage("battery","EC X5SP4NAG manufacture date (EC cache 0x314/315): "+value);
        }
    }catch(const std::exception&){return {};}
    return value;
}
bool hlylCpu(){return !wcsstr(GetCommandLineW(),L"--no-ec")&&(deviceProfile(readBoardDescriptor()).id=="huawei-hlyl-wxx9"||gmAmdProfile(deviceProfile(readBoardDescriptor()).id));}
struct Handle {
    HANDLE value{INVALID_HANDLE_VALUE};
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
template<class T> T number(const std::vector<std::uint8_t>& data, std::size_t offset) {
    T value{};
    if (offset + sizeof(T) <= data.size()) std::memcpy(&value, data.data() + offset, sizeof(T));
    return value;
}
bool query(HANDLE handle, DWORD code, const void* input, DWORD inputSize,
           std::vector<std::uint8_t>& output, DWORD minimum) {
    DWORD returned{};
    if (!DeviceIoControl(handle, code, const_cast<void*>(input), inputSize, output.data(),
                         static_cast<DWORD>(output.size()), &returned, nullptr)) return false;
    if(returned<minimum||returned>output.size()){SetLastError(ERROR_INVALID_DATA);return false;}
    output.resize(returned);
    return true;
}
std::string utf8(const wchar_t* value) {
    const int size = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) return {};
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), size, nullptr, nullptr);
    result.pop_back();
    const auto first = result.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    return result.substr(first, result.find_last_not_of(" \t\r\n") - first + 1);
}
std::array<std::string, 2> processorModels() {
    std::array<std::string, 2> models;
    wchar_t cpuName[512]{}; DWORD bytes = sizeof(cpuName);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
            L"ProcessorNameString", RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, nullptr, cpuName, &bytes) == ERROR_SUCCESS)
        models[0] = utf8(cpuName);
    for (DWORD index = 0; index < 64; ++index) {
        DISPLAY_DEVICEW device{}; device.cb = sizeof(device);
        if (!EnumDisplayDevicesW(nullptr, index, &device, 0)) break;
        if ((device.StateFlags & DISPLAY_DEVICE_MIRRORING_DRIVER) ||
            std::wstring_view(device.DeviceID).find(L"PCI\\") != 0) continue;
        if (models[1].empty() || (device.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE))
            models[1] = utf8(device.DeviceString);
        if (device.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) break;
    }
    return models;
}
windows::NpuPowerResult readNpuPower() {
    wchar_t system[32768]{};
    const auto count=GetSystemDirectoryW(system,32768);
    if(!count||count>=32768)return {{},"AMD XRT: системный каталог недоступен"};
    const auto executable=std::wstring(system,count)+L"\\AMD\\xrt-smi.exe";
    if(GetFileAttributesW(executable.c_str())==INVALID_FILE_ATTRIBUTES)
        return {{},"AMD XRT: xrt-smi не установлен"};
    SECURITY_ATTRIBUTES attributes{sizeof(attributes),nullptr,TRUE};
    Handle output,writer,input;
    if(!CreatePipe(&output.value,&writer.value,&attributes,65536)||
       !SetHandleInformation(output.value,HANDLE_FLAG_INHERIT,0))
        return {{},"AMD XRT: не удалось открыть канал отчёта"};
    input.value=CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&attributes,OPEN_EXISTING,0,nullptr);
    if(input.value==INVALID_HANDLE_VALUE)return {{},"AMD XRT: не удалось открыть входной канал"};
    STARTUPINFOW startup{};startup.cb=sizeof(startup);
    startup.dwFlags=STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;
    startup.hStdInput=input.value;startup.hStdOutput=startup.hStdError=writer.value;
    PROCESS_INFORMATION process{};
    auto command=L"\""+executable+L"\" examine --report platform --batch";
    const auto working=portableSettingsPath("startup.log").parent_path();
    if(!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,
            nullptr,working.c_str(),&startup,&process))
        return {{},"AMD XRT: не удалось прочитать отчёт, код "+std::to_string(GetLastError())};
    Handle running{process.hProcess},thread{process.hThread};
    CloseHandle(std::exchange(writer.value,INVALID_HANDLE_VALUE));
    // This child only runs 'examine'. Bound its lifetime so a stalled vendor
    // query never leaves the FANs worker or application shutdown waiting forever.
    if(WaitForSingleObject(running.value,3000)!=WAIT_OBJECT_0) {
        TerminateProcess(running.value,ERROR_TIMEOUT);
        return {{},"AMD XRT: превышено время чтения мощности NPU"};
    }
    DWORD exitCode{};
    if(!GetExitCodeProcess(running.value,&exitCode)||exitCode!=0)
        return {{},"AMD XRT: отчёт мощности NPU недоступен"};
    std::string report;char buffer[4096];DWORD bytes{},available{};
    while(PeekNamedPipe(output.value,nullptr,0,nullptr,&available,nullptr)&&available) {
        if(!ReadFile(output.value,buffer,std::min(available,static_cast<DWORD>(sizeof(buffer))),&bytes,nullptr)||!bytes)break;
        if(report.size()+bytes>65536)return {{},"AMD XRT: слишком большой отчёт"};
        report.append(buffer,bytes);
    }
    return windows::parseNpuPowerReport(report);
}
std::array<std::string, 2> memorySerials(bool& ordinaryDdr5,bool& ordinaryDdr4) {
    ordinaryDdr5=false;
    ordinaryDdr4=false;
    constexpr DWORD rsmb = 0x52534D42;
    const UINT size = GetSystemFirmwareTable(rsmb, 0, nullptr, 0);
    std::array<std::string, 2> result{};
    if (size < 8 || size > 1024 * 1024) return result;
    std::vector<std::uint8_t> data(size);
    if (GetSystemFirmwareTable(rsmb, 0, data.data(), size) != size) return result;
    const auto end = std::min<std::size_t>(size, 8 + number<std::uint32_t>(data, 4));
    std::vector<std::pair<std::string, std::string>> modules;
    bool allDdr5=true,allDdr4=true;
    for (std::size_t p = 8; p + 4 <= end;) {
        const auto length = data[p + 1];
        if (length < 4 || p + length > end) break;
        std::size_t next = p + length;
        while (next + 1 < end && (data[next] || data[next + 1])) ++next;
        if (next + 1 >= end) break;
        const auto field = [&](std::size_t offset) -> std::string {
            if (offset >= length || !data[p + offset]) return {};
            std::size_t start = p + length;
            for (int n = 1; n < data[p + offset] && start < next; ++n) {
                while (start < next && data[start]) ++start;
                ++start;
            }
            std::size_t finish = start;
            while (finish <= next && data[finish]) ++finish;
            return start <= next ? std::string(reinterpret_cast<const char*>(data.data() + start), finish - start) : std::string{};
        };
        if (data[p] == 17 && length > 24 && number<std::uint16_t>(data, p + 12) != 0){
            modules.emplace_back(field(17) + "/" + field(16), field(24));
            allDdr5=allDdr5&&data[p+18]==34; // SMBIOS DDR5, excludes LPDDR5.
            allDdr4=allDdr4&&data[p+18]==26; // SMBIOS DDR4, excludes LPDDR4.
        }
        if (data[p] == 127) break;
        p = next + 2;
    }
    std::stable_sort(modules.begin(), modules.end());
    ordinaryDdr5=!modules.empty()&&allDdr5;
    ordinaryDdr4=!modules.empty()&&allDdr4;
    for (std::size_t i = 0; i < std::min(result.size(), modules.size()); ++i) result[i] = modules[i].second;
    return result;
}

}

struct MonitoringReader::Impl {
    const ProcessorVendor cpuVendor{windows::nativeCpuVendor()};
    windows::IntelPchReader pch;
    const bool hasNvidia{nvidiaGpuPresent()};
    NvidiaGpu nvidia;
    IntelGpu displayIntel;
    RadeonGpu displayRadeon;
    windows::IntelTelemetryReader intel;
    windows::IntelMemoryReader intelMemory;
    windows::AmdTelemetryReader amd;
    windows::PawnTelemetryReader pawn;
    windows::HpbsdTelemetryReader hpbsd;
    EcAccess* ecAccess{};
    bool diagnostic{};
    bool collectGpuPresentation{};
    std::vector<SensorReading> diskSensors;
    explicit Impl(EcAccess* access, bool detailed,bool presentation) : pawn((access && access->boardVerified())||hlylCpu()||(gm5Profile(deviceProfile(readBoardDescriptor()).id)||l16Profile(deviceProfile(readBoardDescriptor()).id))||(!wcsstr(GetCommandLineW(),L"--no-ec")&&(windows::HpbsdTelemetryReader::supportedHost()||windows::pawnAmdCpuSupported())),!wcsstr(GetCommandLineW(),L"--no-ec"),(access && access->boardVerified())||gm5Profile(deviceProfile(readBoardDescriptor()).id)||l16Profile(deviceProfile(readBoardDescriptor()).id)), ecAccess(access),diagnostic(detailed),collectGpuPresentation(presentation),oemControl(createLaptopControl()) {}
    std::unique_ptr<LaptopControl> oemControl;
    std::optional<std::array<std::string, 2>> models;
    windows::battery::Enumeration batteries;
    std::vector<BatteryReading> lastBatteries;
    std::map<std::string,std::string> batteryModels;
    std::map<std::string,BatteryReading> batteryDetails;
    std::string primaryBatteryId;
    BatteryReading batteryCache;
    std::optional<double> systemPowerCache;
    Clock::time_point systemPowerAt{};
    Clock::time_point nextSystemPowerErrorLog{};
    std::optional<std::array<ULONGLONG,3>> cpuTimes;
    std::array<std::string, 2> serials{};
    bool ordinaryDdr5{},ordinaryDdr4{};
    Clock::time_point nextMetadata{}, nextDisks{}, retryBattery{};
    std::array<DeviceTemperature, 2> diskCache{}, memoryCache{};
    ULONGLONG nextNpuPower{}, npuSampledAt{};
    std::optional<double> npuPower;
    std::string npuPowerError;
    void readNpu(MonitoringSnapshot& sample) {
        if(GetTickCount64()>=nextNpuPower) {
            try {
                auto result=readNpuPower();npuPower=result.watts;npuPowerError=std::move(result.error);
            } catch(const std::exception& error) {
                npuPower.reset();npuPowerError=std::string("AMD XRT: ")+error.what();
            }
            npuSampledAt=GetTickCount64();nextNpuPower=npuSampledAt+(npuPower?5000:30000);
        }
        sample.npuPowerWatts=sample.diagnostic?std::nullopt:npuPower;sample.npuPowerSource="AMD XRT";
        if(sample.diagnostic)sample.additionalSensors.push_back({"NPU","power","W","AMD XRT",npuPowerError.empty()?"last provider sample; 5s refresh":npuPowerError,npuPower,{},npuSampledAt});
        if(!npuPowerError.empty())sample.processorStatus+=" "+npuPowerError;
    }

    BatteryReading powerStatus() {
        BatteryReading result;result.deviceId="Windows/PowerStatus/aggregate";
        result.source="Windows · PowerStatus";result.temperatureStatus="Battery IOCTL: интерфейс батареи недоступен";
        SYSTEM_POWER_STATUS power{};
        if(GetSystemPowerStatus(&power)){
            result.powerStatus=std::format("GetSystemPowerStatus: ACLineStatus={} BatteryFlag={} BatteryLifePercent={}",power.ACLineStatus,power.BatteryFlag,power.BatteryLifePercent);
            if(power.BatteryFlag!=255)result.present=!(power.BatteryFlag&128);
            result.onlineKnown=power.ACLineStatus==0||power.ACLineStatus==1;
            if(result.onlineKnown)result.online=power.ACLineStatus==1;
            result.flow=(power.BatteryFlag!=255&&(power.BatteryFlag&8))?BatteryFlow::Charging:
                result.onlineKnown?(result.online?BatteryFlow::Idle:BatteryFlow::Discharging):BatteryFlow::Unknown;
            if(power.BatteryLifePercent<=100)result.percent=power.BatteryLifePercent;
        }else {result.powerStatus="GetSystemPowerStatus Win32="+std::to_string(GetLastError());result.temperatureStatus+="; "+result.powerStatus;}
        stampBatteryReading(result);return result;
    }
    std::vector<BatteryReading> readBatteryGroup() {
        if(Clock::now()>=retryBattery){
            batteries=windows::battery::enumerate();retryBattery=Clock::now()+std::chrono::seconds(10);
            batteryModels.clear();batteryDetails.clear();
            for(const auto& battery:batteries.devices){
                batteryModels[battery.id]=battery.string(BatteryDeviceName);
                auto& detail=batteryDetails[battery.id];
                detail.manufacturer=battery.string(BatteryManufactureName);detail.serial=battery.string(BatterySerialNumber);
                detail.uniqueId=battery.string(BatteryUniqueID);
                DWORD returned{};BATTERY_MANUFACTURE_DATE date{};
                if(battery.query(BatteryManufactureDate,&date,sizeof(date),returned)&&returned>=sizeof(date)&&date.Year&&date.Month>=1&&date.Month<=12&&date.Day>=1&&date.Day<=31)
                    detail.manufactured=std::format("{:04}-{:02}-{:02}",date.Year,date.Month,date.Day);
            }
        }
        std::vector<BatteryReading> results;
        for(auto& battery:batteries.devices){
            BatteryReading result=batteryDetails[battery.id];result.deviceId=battery.id;result.model=batteryModels[battery.id];
            result.source="Windows · Battery Class IOCTL";result.temperatureSource="Windows · BatteryTemperature";
            DWORD returned{};BATTERY_INFORMATION info{};
            const bool metadataRead=battery.query(BatteryInformation,&info,sizeof(info),returned)&&returned>=sizeof(info);
            if(metadataRead)battery.information=info;
            else{result.temperatureStatus="BatteryInformation Win32="+std::to_string(GetLastError());retryBattery=Clock::now();}
            const auto& metadata=battery.information;
            result.capacityRelative=(metadata.Capabilities&BATTERY_CAPACITY_RELATIVE)!=0;
            result.capacityUnit=result.capacityRelative?"relative":"mWh";
            result.capabilities=metadata.Capabilities;result.technology=metadata.Technology;
            result.alert1=metadata.DefaultAlert1;result.alert2=metadata.DefaultAlert2;result.criticalBias=metadata.CriticalBias;
            result.chemistry=std::string(reinterpret_cast<const char*>(metadata.Chemistry),4);
            if(const auto end=result.chemistry.find('\0');end!=std::string::npos)result.chemistry.resize(end);
            if(metadata.DesignedCapacity&&metadata.DesignedCapacity!=BATTERY_UNKNOWN_CAPACITY)result.designCapacity=metadata.DesignedCapacity;
            if(metadata.FullChargedCapacity&&metadata.FullChargedCapacity!=BATTERY_UNKNOWN_CAPACITY)result.fullCapacity=metadata.FullChargedCapacity;
            if(metadata.DesignedCapacity&&metadata.DesignedCapacity!=BATTERY_UNKNOWN_CAPACITY&&metadata.FullChargedCapacity&&metadata.FullChargedCapacity!=BATTERY_UNKNOWN_CAPACITY)
                result.healthPercent=100.*metadata.FullChargedCapacity/metadata.DesignedCapacity;
            result.healthReadUnavailable=!metadataRead;
            result.healthUnavailable=metadataRead&&!result.healthPercent.has_value();
            if(metadata.CycleCount&&metadata.CycleCount!=MAXDWORD)result.cycles=metadata.CycleCount;
            BATTERY_WAIT_STATUS wait{battery.tag,0,0,0,0};BATTERY_STATUS status{};
            if(DeviceIoControl(battery.handle,IOCTL_BATTERY_QUERY_STATUS,&wait,sizeof(wait),&status,sizeof(status),&returned,nullptr)&&returned>=sizeof(status)){
                result.powerStatus=std::format("Battery Class: PowerState=0x{:08X} tag={}",status.PowerState,battery.tag);
                result.present=true;result.onlineKnown=true;result.online=(status.PowerState&BATTERY_POWER_ON_LINE)!=0;
                result.powerState=status.PowerState;
                if(status.Capacity!=BATTERY_UNKNOWN_CAPACITY)result.remainingCapacity=status.Capacity;
                result.flow=(status.PowerState&BATTERY_CHARGING)?BatteryFlow::Charging:(status.PowerState&BATTERY_DISCHARGING)?BatteryFlow::Discharging:BatteryFlow::Idle;
                if(status.Capacity!=BATTERY_UNKNOWN_CAPACITY&&metadata.FullChargedCapacity&&metadata.FullChargedCapacity!=BATTERY_UNKNOWN_CAPACITY)
                    result.percent=std::clamp(100.*status.Capacity/metadata.FullChargedCapacity,0.,100.);
                if(!(metadata.Capabilities&BATTERY_CAPACITY_RELATIVE)&&status.Rate!=std::numeric_limits<LONG>::min())result.watts=status.Rate/1000.;
                if(status.Voltage&&status.Voltage!=BATTERY_UNKNOWN_VOLTAGE)result.volts=status.Voltage/1000.;
                if(result.watts&&result.volts)result.milliamps=*result.watts*1000. / (*result.volts);
            }else{result.powerStatus="BatteryStatus Win32="+std::to_string(GetLastError());result.temperatureStatus+="; "+result.powerStatus;retryBattery=Clock::now();}
            ULONG raw{};
            if(result.flow==BatteryFlow::Discharging&&battery.query(BatteryEstimatedTime,&raw,sizeof(raw),returned)&&returned>=sizeof(raw)&&raw!=std::numeric_limits<ULONG>::max())
                result.estimatedDischargeSeconds=raw;
            if(battery.query(BatteryTemperature,&raw,sizeof(raw),returned)&&returned>=sizeof(raw)){
                result.temperatureRaw=raw;result.celsius=batteryTemperatureCelsius(raw);
                result.temperatureStatus+=result.celsius?"; BatteryTemperature: ok":"; BatteryTemperature: invalid value";
            }else{const auto error=GetLastError();result.temperatureStatus+="; BatteryTemperature Win32="+std::to_string(error);
                if(error==ERROR_INVALID_FUNCTION)result.temperatureStatus+="; driver does not export temperature";}
            stampBatteryReading(result);results.push_back(std::move(result));
        }
        if(results.empty()){
            auto aggregate=powerStatus();
            for(const auto& error:batteries.errors)aggregate.temperatureStatus+="; "+error;
            results.push_back(std::move(aggregate));
        }
        if(!batteries.complete)for(auto& result:results){result.powerStatus+="; battery interface enumeration incomplete";
            for(const auto& error:batteries.errors)result.powerStatus+="; "+error;}
        for(const auto& result:results)journalBattery(result,"OS");
        return results;
    }
    BatteryReading readBattery() {
        lastBatteries=readBatteryGroup();
        // The one existing Wujie card keeps a stable primary interface. Aggregate
        // Windows percentages never replace a physical battery's individual IOCTL.
        auto primary=std::find_if(lastBatteries.begin(),lastBatteries.end(),[&](const auto& b){return b.deviceId==primaryBatteryId;});
        if(primary==lastBatteries.end())primary=lastBatteries.begin();
        primaryBatteryId=primary->deviceId;return *primary;
    }
    void readBatteryEc(BatteryReading& result) {
        // Exact GM6 supplemental read-only telemetry uses the existing global
        // transport gate, never the Wujie map or a controller/restore worker.
        windows::readGm6BatteryTelemetry(result,batteries.complete&&lastBatteries.size()==1);
        windows::readHydrocBatteryTelemetry(result,batteries.complete&&lastBatteries.size()==1);
        if(result.present!=false&&batteries.complete&&lastBatteries.size()==1){
            const auto lenovo=windows::readLegionEcBatteryOnly();
            if(lenovo.coherent&&lenovo.battery.present==true){
                const auto& ec=lenovo.battery;
                if(!result.celsius&&ec.celsius){result.celsius=ec.celsius;result.temperatureRaw=ec.temperatureRaw;result.temperatureSource=ec.temperatureSource;
                    result.temperatureStatus=ec.temperatureStatus;result.dynamicAtMs[2]=ec.dynamicAtMs[2];}
                if(result.manufactured.empty())result.manufactured=ec.manufactured;
                if(!diagnostic&&!ec.manufactured.empty())observeBatteryIdentity(ec.manufactured,ec.source,true);
            }
        }
        if(result.present==false)static_cast<void>(wujieBatteryDate(ecAccess,true));
        if (!ecAccess || !ecAccess->boardVerified() || result.present == false)
            return;
        // The ordinary dashboard still skips a busy poll. A diagnostic reader
        // waits briefly for a fresh sample instead of immediately losing the
        // only available temperature source. No cached value is substituted.
        const auto deadline=Clock::now()+std::chrono::milliseconds(250);
        const auto ready=[&]{
            if(deviceOperations().passiveEcReadsPaused())return false;
            while(ecAccess->observationsPaused()){
                if(deviceOperations().passiveEcReadsPaused())return false;
                if(!diagnostic||Clock::now()>=deadline){
                    if(diagnostic&&!result.celsius)result.temperatureStatus+="; EC: истекло ожидание временной паузы";
                    return false;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            return true;
        };
        if(!ready())return;
        // Reuse the process-wide EcAccess gate; never open a second EC transport.
        const auto word = [&](std::uint16_t address) -> std::optional<std::uint16_t> {
            try {
                for (int attempt = 0; attempt < 3; ++attempt) {
                    if (!ready()) return {};
                    const auto first = ecAccess->readBlock(address, 2);
                    const auto second = ecAccess->readBlock(address, 2);
                    if (first.size() == 2 && first == second)
                        return static_cast<std::uint16_t>(first[0] | (static_cast<std::uint16_t>(first[1]) << 8));
                }
            } catch (const std::exception&) {}
            return {};
        };
        bool supplied{};
        try{
            const auto state=ecAccess->readStable(0x490);
            if(state==255||(state&6)!=6){result.temperatureStatus+="; EC battery not ready";return;}
        }catch(const std::exception&){return;}
        // FANs is polled on startup even with battery logging disabled.
        static_cast<void>(wujieBatteryDate(ecAccess));
        // Wujie's exact _BIF fixes Power Unit to mAh/mA independently of XIF0.
        // Preserve the previous unit guard for all other verified profiles.
        const bool currentUnitsKnown=deviceProfile(ecAccess->verifyBoard().board).id=="wu-jie-15-xpro"||word(0x400)==1;
        if (const auto raw = word(ec::kBatteryTemperature); raw && *raw && *raw != 0xFFFF) {
            if (const auto celsius=batteryTemperatureCelsius(*raw)) { result.celsius=celsius; result.temperatureRaw=*raw; result.temperatureSource="EC · X5SP4NAG"; result.temperatureStatus="ok"; supplied = true; }
        }
        if (const auto raw = word(ec::kBatteryCycleCount); raw && *raw != 0xFFFF) {
            result.cycles = *raw; supplied = true;
        }
        // Keep the OS W/V-derived current with its OS power/voltage tuple.
        // An independently timed EC zero must not turn 32W into 0mA. The full
        // EC decoder exports its own voltage/current/power in the EC stream.
        // Only supplement a missing tuple when both EC quantities are known.
        if(!result.watts&&!result.volts&&!result.milliamps){
            const auto voltage=word(ec::kBatteryVoltage);
            const auto current=currentUnitsKnown?word(ec::kBatteryCurrent):std::nullopt;
            if(voltage&&*voltage>=5000&&*voltage<=25000&&current&&*current<=30000){
                result.volts=*voltage/1000.;result.milliamps=*current;supplied=true;
            }
        }
        if (supplied) result.source += " · EC";
    }
    void readMemory() {
        const auto now = Clock::now();
        if (now >= nextMetadata) { serials = memorySerials(ordinaryDdr5,ordinaryDdr4); nextMetadata = now + std::chrono::minutes(1); }
        for (std::size_t i = 0; i < 2; ++i) {
            memoryCache[i].serial = serials[i];
            // SMBIOS supplies identity, not a live DIMM temperature. Keep the
            // temperature empty until a verified source without PawnIO exists.
            memoryCache[i].source = "Температура недоступна";
        }
    }
    void readDisks() {
        if (!diagnostic && Clock::now() < nextDisks) return;
        nextDisks = Clock::now() + std::chrono::seconds(5);
        diskCache = {}; diskSensors.clear();
        int found{};
        bool uncertain{};
        for (int disk = 0; disk < (diagnostic ? 64 : 8) && (diagnostic || found < 2); ++disk) {
            Handle handle;
            const auto path = L"\\\\.\\PhysicalDrive" + std::to_wstring(disk);
            handle.value = CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
            if (handle.value == INVALID_HANDLE_VALUE) {
                const DWORD error = GetLastError();
                if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) {
                    uncertain = true;
                    if(diagnostic)diskSensors.push_back({"PhysicalDrive"+std::to_string(disk),"temperature","C","Windows · Storage IOCTL","open: Win32="+std::to_string(error),{},{},GetTickCount64()});
                }
                continue;
            }
            DeviceTemperature extra;bool nvme{};
            auto& item = found < 2 ? diskCache[found] : extra; ++found; item.present = true; item.source = "Windows · накопители";
            DWORD request[]{0, 0, 0};
            std::vector<std::uint8_t> descriptor(4096);
            if (query(handle.value, IOCTL_STORAGE_QUERY_PROPERTY, request, sizeof(request), descriptor, 36)) {
                const auto size = number<DWORD>(descriptor, 4);
                if (size >= 36 && size <= descriptor.size()) {
                    const auto stringAt = [&](std::size_t field) {
                        const auto offset = number<DWORD>(descriptor, field);
                        if (offset < 36 || offset >= size) return std::string{};
                        auto end = offset;
                        while (end < size && descriptor[end]) ++end;
                        if (end == size) return std::string{};
                        std::string text(reinterpret_cast<const char*>(descriptor.data() + offset), end - offset);
                        while (!text.empty() && text.back() == ' ') text.pop_back();
                        return text;
                    };
                    item.model = stringAt(16); item.serial = stringAt(24);
                    if (number<DWORD>(descriptor, 28) == BusTypeNvme) {nvme=true;item.source = "Windows · NVMe";}
                }
            }
            for (DWORD property : {52u, 51u}) {
                request[0] = property;
                std::vector<std::uint8_t> temperatures(4096);
                if (!query(handle.value, IOCTL_STORAGE_QUERY_PROPERTY, request, sizeof(request), temperatures, 40)) {
                    const auto error=GetLastError();item.status+=" property"+std::to_string(property)+": Win32="+std::to_string(error);
                    if(diagnostic)diskSensors.push_back({"PhysicalDrive"+std::to_string(disk),"temperature","C","Windows · Storage property "+std::to_string(property),"Win32="+std::to_string(error),{},{},GetTickCount64()});continue;
                }
                if(diagnostic){
                    const auto size=sensorField<DWORD>(temperatures,4);const auto count=sensorField<WORD>(temperatures,12);
                    if(size&&count&&*size<=temperatures.size()&&*size>=24&&*count<=(*size-24)/16){
                        for(unsigned i=0;i<*count;++i){const auto offset=24+16*i;
                            const auto index=*sensorField<WORD>(temperatures,offset);const auto raw=*sensorField<SHORT>(temperatures,offset+2);
                            const std::optional<double> value=raw>=-40&&raw<=150?std::optional<double>(raw):std::nullopt;
                            diskSensors.push_back({"PhysicalDrive"+std::to_string(disk)+" / "+item.model,"temperature/"+std::to_string(index),"C","Windows · Storage property "+std::to_string(property),value?"ok":"unavailable/invalid sentinel",value,double(raw),GetTickCount64()});
                        }
                    }else diskSensors.push_back({"PhysicalDrive"+std::to_string(disk),"temperature","C","Windows · Storage property "+std::to_string(property),"invalid descriptor",{},{},GetTickCount64()});
                }
                const auto decoded=storageTemperatureCelsius(temperatures);if(decoded&&!item.celsius){item.celsius=decoded;item.source="Windows · Storage property "+std::to_string(property);}
                if(!item.celsius)item.status+=" property"+std::to_string(property)+": температура не сообщается";
                if (item.celsius && !diagnostic) break;
            }
            if((diagnostic||!item.celsius)&&nvme){
                // Standard NVMe Get Log Page (SMART/Health). No device commands
                // that change firmware, features, thresholds or power limits.
                std::array<DWORD,140> smart{};
                smart[0]=50; // StorageDeviceProtocolSpecificProperty; QueryStandard=0.
                smart[2]=3;smart[3]=2;smart[4]=2; // NVMe / LogPage / SMART Health.
                smart[6]=40;smart[7]=512; // Offset relative to protocol structure.
                std::vector<std::uint8_t> response(sizeof(smart));
                if(query(handle.value,IOCTL_STORAGE_QUERY_PROPERTY,smart.data(),sizeof(smart),response,48)){
                    const auto composite=nvmeSmartTemperatureCelsius(response);
                    if(diagnostic){
                        diskSensors.push_back({"PhysicalDrive"+std::to_string(disk)+" / "+item.model,"temperature/composite","C","Windows · NVMe SMART",composite?"ok":"invalid/unavailable response",composite,{},GetTickCount64()});
                        // Use only a validated protocol descriptor. NVMe sensor1..8
                        // are independent Kelvin words at SMART offsets200..215.
                        if(composite){const auto start=8+*sensorField<DWORD>(response,24);
                            for(unsigned i=0;i<8;++i){const auto raw=sensorField<WORD>(response,start+200+2*i);std::optional<double> value;
                                if(raw&&*raw&&*raw!=65535&&*raw>=233&&*raw<=423)value=*raw-273.15;
                                diskSensors.push_back({"PhysicalDrive"+std::to_string(disk)+" / "+item.model,"temperature/sensor"+std::to_string(i+1),"C","Windows · NVMe SMART",value?"ok":"not reported/invalid",value,raw?std::optional<double>(*raw):std::nullopt,GetTickCount64()});
                            }
                        }
                    }
                    if(!item.celsius&&composite){item.celsius=composite;item.source="Windows · NVMe SMART";}
                    if(!composite)item.status+=" NVMe SMART: температура/ответ недействительны";
                }else {const auto error=GetLastError();item.status+=" NVMe SMART: Win32="+std::to_string(error);
                    if(diagnostic)diskSensors.push_back({"PhysicalDrive"+std::to_string(disk),"temperature","C","Windows · NVMe SMART","Win32="+std::to_string(error),{},{},GetTickCount64()});}
            }
            if(item.celsius)item.status="ok";
        }
        while (found < 2) {
            auto& item = diskCache[found++];
            if (!uncertain) item.present = false;
            item.source = "Windows · накопители";
        }
    }
    MonitoringSnapshot read() {
        if (!models) models = processorModels();
        auto batteryReading = readBattery();
        const auto windowsBattery=batteryReading;
        readBatteryEc(batteryReading);
        stampBatteryReading(batteryReading);
        if(!diagnostic)retainBatteryReadings(batteryReading, batteryCache);
        batteryCache = batteryReading;
        readMemory(); readDisks();
        MonitoringSnapshot sample;sample.diagnostic=diagnostic;sample.additionalSensors=diskSensors;
        if(diagnostic){
            sample.additionalSensors.push_back({"Battery / Windows","temperature","C",windowsBattery.temperatureSource,windowsBattery.temperatureStatus,windowsBattery.celsius,windowsBattery.temperatureRaw?std::optional<double>(*windowsBattery.temperatureRaw):std::nullopt,GetTickCount64()});
            sample.additionalSensors.push_back({"Battery / Windows","power","W",windowsBattery.source,windowsBattery.watts?"ok":"not reported",windowsBattery.watts,{},GetTickCount64()});
            sample.additionalSensors.push_back({"Battery / Windows","voltage","V",windowsBattery.source,windowsBattery.volts?"ok":"not reported",windowsBattery.volts,{},GetTickCount64()});
        }
        sample.systemPowerStale = true;
        if (ecAccess && ecAccess->boardVerified() && !ecAccess->observationsPaused() && !deviceOperations().passiveEcReadsPaused()) {
            try {
                systemPowerCache = static_cast<double>(ecAccess->readStable(0x44C));
                systemPowerAt=Clock::now();
                sample.systemPowerStale = false;
            } catch (const std::exception& error) {
                // A missed poll never refreshes the cache timestamp. Record a
                // bounded diagnostic so unstable reads can be distinguished
                // from a long EC pause without logging at the polling rate.
                if(Clock::now()>=nextSystemPowerErrorLog){
                    logMessage("power",std::string("EC 0x44C observation failed: ")+error.what());
                    nextSystemPowerErrorLog=Clock::now()+std::chrono::seconds(30);
                }
            }
        } else if(ecAccess&&ecAccess->boardVerified()&&Clock::now()>=nextSystemPowerErrorLog){
            logMessage("power",std::format("EC 0x44C observation paused: ec_pause={} operation_pause={}",
                ecAccess->observationsPaused(),deviceOperations().passiveEcReadsPaused()));
            nextSystemPowerErrorLog=Clock::now()+std::chrono::seconds(30);
        }
        if(systemPowerCache&&Clock::now()-systemPowerAt>std::chrono::seconds(10))systemPowerCache.reset();
        sample.systemPowerWatts = systemPowerCache;
        if(ecAccess&&ecAccess->boardVerified())sample.systemPowerSource = "EC 0x44C / experimental system power, assumed raw=W";
        sample.processorModels = *models;
        sample.battery = std::move(batteryReading); sample.memory = memoryCache; sample.disks = diskCache;
        sample.ordinaryDdr5=ordinaryDdr5;
        sample.ordinaryDdr4=ordinaryDdr4;
        sample.systemConsumptionWatts = sample.systemPowerWatts;
        if (sample.battery.flow == BatteryFlow::Charging) {
            if (sample.systemPowerWatts && sample.battery.watts && std::isfinite(*sample.battery.watts)) {
                // Different polling windows may briefly produce a negative difference.
                sample.systemConsumptionWatts = std::max(0.0, *sample.systemPowerWatts - std::max(0.0, *sample.battery.watts));
            } else sample.systemConsumptionWatts.reset();
        }
        sample.processorVendors[0]=cpuVendor;
        sample.processorSampledAtMs=GetTickCount64();
        if(cpuVendor==ProcessorVendor::Amd) {
            sample.processorVendors[1]=ProcessorVendor::Amd;
            amd.read(sample); pawn.read(sample); readNpu(sample);
            windows::readWindowsProcessorFrequency(sample.processors[0]);
            if(!wcsstr(GetCommandLineW(),L"--no-ec"))hpbsd.read(sample);
        } else if(cpuVendor==ProcessorVendor::Intel) {intel.read(sample);intelMemory.read(sample);}
        pch.read(sample);
        std::vector<GpuProviderReading> displayReadings;
        if(hasNvidia){if(collectGpuPresentation){const auto at=GetTickCount64();auto readings=nvidia.read();readNvidiaMonitoring(readings,sample);
            for(auto& row:readings)displayReadings.push_back({0x10de,std::move(row),at});}else readNvidiaMonitoring(nvidia,sample);}
        if (GetTickCount64() - sample.processorSampledAtMs > 10000) {
            sample.processors = {}; sample.socCelsius.reset(); sample.npuUsagePercent.reset(); sample.npuClockMhz.reset(); sample.npuPowerWatts.reset();
            sample.processorStatus = "Показания процессоров устарели во время чтения";
        }
        // Older Huawei AMD drivers may not expose ADLX CPU load. This native
        // fallback measures total busy time and does not require any EC driver.
        FILETIME idle{},kernel{},user{};
        if(GetSystemTimes(&idle,&kernel,&user)){
            const auto value=[](FILETIME t){return (ULONGLONG(t.dwHighDateTime)<<32)|t.dwLowDateTime;};
            const std::array<ULONGLONG,3> now{value(idle),value(kernel),value(user)};
            if(!sample.processors[0].usagePercent&&cpuTimes){const auto total=(now[1]-(*cpuTimes)[1])+(now[2]-(*cpuTimes)[2]);const auto unused=now[0]-(*cpuTimes)[0];
                if(total&&unused<=total){sample.processors[0].usagePercent=100.0*static_cast<double>(total-unused)/static_cast<double>(total);sample.processors[0].usageSource="Windows · GetSystemTimes";}}
            cpuTimes=now;
        }
        if(oemControl){const auto oem=diagnostic?oemControl->readTelemetry():oemControl->read();
            sample.additionalSensors.insert(sample.additionalSensors.end(),oem.temperatureSensors.begin(),oem.temperatureSensors.end());
            const bool legion=deviceProfile(readBoardDescriptor()).id=="lenovo-legion-kwcn";
            for(std::size_t i=0;i<2;++i)if(oem.fanRpm[i]){sample.processors[i].fanRpm=*oem.fanRpm[i];sample.processors[i].fanSource=legion?oem.source+" · fan "+std::to_string(i+1):"Huawei HWMI · fan "+std::to_string(i);}
            if(oem.cpuCelsius&&!sample.processors[0].tctlCelsius){sample.processors[0].celsius=oem.cpuCelsius;sample.processors[0].temperatureSource=legion?oem.source+" · CPUT":"Huawei HWMI · GTMP 0";}
            if(!sample.battery.celsius&&oem.batteryCelsius){sample.battery.celsius=oem.batteryCelsius;sample.battery.temperatureSource="Huawei HWMI";sample.battery.temperatureStatus="ok";sample.battery.dynamicAtMs[2]=batteryMonotonicMs();}
            if(!oem.status.empty())sample.processorStatus+=" "+oem.status;
        }
        sample.batteries=lastBatteries;
        for(auto& battery:sample.batteries)if(battery.deviceId==sample.battery.deviceId)battery=sample.battery;
        // GPU display cards are a separate projection. Fixed processors[1]
        // remains the discrete/shared-cooling temperature used by fan control.
        if(collectGpuPresentation){sample.gpuCards.initialized=true;
        try{
            const auto adapters=graphicsAdapters();
            const auto routes=windows::gpuDisplayRoutes(adapters);
            sample.gpuCards=gpuPresentation(adapters,{},routes);
            if(std::any_of(adapters.begin(),adapters.end(),[](const auto& a){return !a.software&&a.vendorId==0x8086;})){
                const auto at=GetTickCount64();auto rows=displayIntel.read();for(auto& row:rows)displayReadings.push_back({0x8086,std::move(row),at});}
            if(std::any_of(adapters.begin(),adapters.end(),[](const auto& a){return !a.software&&a.vendorId==0x1002;})){
                const auto at=GetTickCount64();auto rows=displayRadeon.read(true);for(auto& row:rows)displayReadings.push_back({0x1002,std::move(row),at});}
            sample.gpuCards=gpuPresentation(adapters,std::move(displayReadings),routes,GetTickCount64());
            // Preserve Wujie's established APU readings/NPU card when the OS
            // confirms exactly one AMD integrated device. No cross-GPU borrow.
            if(GetTickCount64()-sample.processorSampledAtMs<=10000&&sample.processorVendors[1]==ProcessorVendor::Amd&&std::count_if(sample.gpuCards.cards.begin(),sample.gpuCards.cards.end(),[](const auto& row){return row.vendorId==0x1002&&row.type==GpuType::Integrated;})==1)
                for(auto& row:sample.gpuCards.cards)if(row.vendorId==0x1002&&row.type==GpuType::Integrated){const auto& p=sample.processors[1];
                    const auto missing=[](auto& to,const auto& from,auto& source,const auto& fromSource){if(!to&&from){to=from;source=fromSource;}};
                    missing(row.celsius,p.celsius,row.temperatureSource,p.temperatureSource);
                    missing(row.usagePercent,p.usagePercent,row.usageSource,p.usageSource);
                    missing(row.clockMhz,p.clockMhz,row.clockSource,p.clockSource);
                    // Power is provided only by the identity-bound GPU rows.
                    // The legacy APU slot is not independent physical identity
                    // evidence and must not reintroduce broad ASIC power.
                    row.legacyApu=true;}
        }catch(const std::exception& error){sample.gpuCards.status=std::string("GPU display catalog unavailable: ")+error.what();}
        // Presentation providers must not extend the age of cooling readings.
        if(GetTickCount64()-sample.processorSampledAtMs>10000){sample.processors={};sample.socCelsius.reset();sample.npuUsagePercent.reset();sample.npuClockMhz.reset();sample.npuPowerWatts.reset();sample.processorStatus="Показания процессоров устарели во время чтения";}
        }
        return sample;
    }
};
MonitoringReader::MonitoringReader(EcAccess* access, bool diagnostic,bool collectGpuPresentation) : impl_(std::make_unique<Impl>(access,diagnostic,collectGpuPresentation)) {}
MonitoringReader::~MonitoringReader() = default;
BatteryPowerPolicy MonitoringReader::readBatteryPowerPolicy(){return windows::readBatteryPowerPolicy();}
MonitoringSnapshot MonitoringReader::read() { auto sample=impl_->read();journalBattery(sample.battery,"OS+EC");
    if(!impl_->diagnostic){observeBatteryIdentity(sample.battery.manufactured,"Windows Battery Class",false);
        if(batteryLoggingEnabled()||batteryIdentityHistoryNeedsEcSample())static_cast<void>(readEcBatteryOnly());}return sample; }
BatteryReading MonitoringReader::readBatteryOnly(BatteryReading* osObservation) {
    auto result = impl_->readBattery();
    if(osObservation)*osObservation=result;
    impl_->readBatteryEc(result); // Verified EC cycle counter, as on FANs.
    if(!result.celsius&&result.present!=false&&impl_->oemControl)try{
        result.celsius=impl_->oemControl->readBatteryTemperature();
        if(result.celsius){result.temperatureSource="Huawei HWMI · GTMP 14";result.temperatureStatus="ok";result.temperatureRaw.reset();}
    }catch(const std::exception& error){result.temperatureStatus+="; Huawei HWMI: ";result.temperatureStatus+=error.what();}
    stampBatteryReading(result);
    if(!impl_->diagnostic)retainBatteryReadings(result, impl_->batteryCache);
    impl_->batteryCache = result;
    journalBattery(result,"OS+EC");
    if(!impl_->diagnostic)observeBatteryIdentity(result.manufactured,"Windows Battery Class",false);
    return result;
}
BatteryReading MonitoringReader::readOsBatteryOnly(){return impl_->readBattery();}
EcBatteryReading MonitoringReader::readEcBatteryOnly(){
    if(auto lenovo=windows::readLegionEcBatteryOnly();lenovo.supported){
        if(lenovo.coherent){journalBattery(lenovo.battery,"EC",lenovo.status);journalBatteryRegisters(lenovo);}
        return lenovo;
    }
    if(auto gm6=windows::readGm6EcBatteryOnly();gm6.supported){
        if(gm6.coherent){journalBattery(gm6.battery,"EC",gm6.battery.powerStatus);journalBatteryRegisters(gm6);}
        return gm6;
    }
    EcBatteryReading result;
    auto* access=impl_->ecAccess;
    if(!access||!access->boardVerified())return result;
    result.supported=true;
    if(deviceOperations().passiveEcReadsPaused()||access->observationsPaused()){
        result.status="Опрос EC временно приостановлен";return result;
    }
    // FANs journal and SETs share one short saved sample rather than doubling
    // traffic. Preserve its original dynamic timestamps; never refresh on copy.
    static std::mutex sampleMutex;static EcAccess* lastAccess{};
    static EcBatteryReading last;static std::uint64_t lastAt{};
    std::lock_guard sampleLock(sampleMutex);
    const auto now=batteryMonotonicMs();
    if(lastAccess==access&&last.coherent&&now>=lastAt&&now-lastAt<800)return last;
    try{
        std::string context;
        const auto acquire=[&]{
        // Exact X5SP4NAG only. Reuse the shared interprocess transaction gate;
        // two identical short sets reject torn/different-time combinations.
        // Registers are from the exact DSDT and saved identity-checked dumps.
        const EcAccess::Transaction transaction(*access);
        if(access->read(0x454)!=82||access->read(0x456)!=72||access->read(0x740)!=26){
            result.status="Модель EC не подтверждена";return;
        }
        constexpr auto& addresses=uniwill::x5sp4nag::batteryCoreAddresses;
        using Raw=std::array<std::uint8_t,std::size(addresses)>;
        const auto sample=[&]{Raw raw{};for(std::size_t i=0;i<raw.size();++i)raw[i]=access->read(addresses[i]);return raw;};
        Raw raw{};bool stable=false;
        for(int attempt=0;attempt<3&&!stable;++attempt){const auto first=sample();raw=sample();stable=first==raw;}
        if(!stable){result.status="Согласованный снимок EC не получен";return;}
        const auto byte=[&](std::uint16_t a){return raw[static_cast<std::size_t>(std::find(std::begin(addresses),std::end(addresses),a)-std::begin(addresses))];};
        const auto word=[&](std::uint16_t a){return static_cast<std::uint16_t>(byte(a)|(static_cast<unsigned>(byte(a+1))<<8));};
        result=uniwill::x5sp4nag::decodeBatteryCore(raw);
        auto& battery=result.battery;
        if(batteryLoggingEnabled())for(const auto& candidate:uniwill::x5sp4nag::batteryCandidates){
            EcBatteryRegisterReading field;field.address=candidate.address;field.width=candidate.width;
            field.bigEndian=candidate.bigEndian;field.confirmed=candidate.confirmed;field.name=candidate.name;
            field.sampledTelemetry=candidate.sampledTelemetry;
            for(const auto& bit:uniwill::x5sp4nag::batteryCandidateBits)
                if(bit.address==candidate.address)field.name+="|"+std::string(bit.name)+".bit"+std::to_string(bit.bit);
            bool inCore=true;for(unsigned i=0;i<candidate.width;++i)
                inCore=inCore&&std::find(std::begin(addresses),std::end(addresses),candidate.address+i)!=std::end(addresses);
            try{
                if(inCore){
                    field.value=candidate.width==1?byte(candidate.address):candidate.bigEndian?
                        static_cast<std::uint16_t>((static_cast<unsigned>(byte(candidate.address))<<8)|byte(candidate.address+1)):word(candidate.address);
                    field.status="ok";
                }else if(deviceOperations().passiveEcReadsPaused())field.status="paused";
                else{
                    for(int attempt=0;attempt<2&&!field.value;++attempt){
                        const auto a=access->readBlock(candidate.address,candidate.width),b=access->readBlock(candidate.address,candidate.width);
                        if(a.size()==candidate.width&&a==b){
                            field.value=candidate.width==1?a[0]:candidate.bigEndian?
                                static_cast<std::uint16_t>((static_cast<unsigned>(a[0])<<8)|a[1]):
                                static_cast<std::uint16_t>(a[0]|(static_cast<unsigned>(a[1])<<8));
                            field.status="ok";
                        }
                    }
                    if(!field.value)field.status="unstable";
                }
            }catch(const std::exception& error){field.status=std::string("read_error: ")+error.what();}
            // Optional volatile candidates never invalidate the core sample.
            result.registers.push_back(std::move(field));
        }
        context=std::format("state432=0x{:04X} ready490=0x{:02X} isdb497=0x{:02X} gate742=0x{:02X} upper7B9=0x{:02X} lower7D0=0x{:02X} platform7C3=0x{:02X} policy7A6=0x{:02X} charger7CC=0x{:02X} ratingA={} ratingB={} configured_stop={} configured_lower={} lower_role=unverified reached={} critical={} raw520={} raw522={}",
            *result.state,*result.readyRaw,*result.isdbRaw,*result.gateRaw,*result.upperRaw,*result.restartRaw,*result.platformRaw,*result.voltagePolicyRaw,*result.chargerRaw,*result.typeCRatingA,*result.typeCRatingB,
            (*result.upperRaw&0x7F)?(*result.upperRaw&0x7F):100,*result.restartRaw&0x7F,bool(*result.upperRaw&0x80),bool(*result.state&4),*result.raw520,*result.raw522);
        if(!(*result.readyRaw&4)){result.status="Батарея не готова по EC";return;}
        stampBatteryReading(battery);
        };
        acquire(); // Releases the shared EC gate before formatting/writing logs.
        if(result.coherent){
            result.aging=observeBatteryAging(*access);
            if(result.aging.reading&&batteryLoggingEnabled()){
                EcBatteryRegisterReading field;field.address=0x9c9;field.width=2;field.bigEndian=true;
                field.confirmed=true;field.name="thermal_exposure_weighted_raw_I2EC";field.status="ok";
                field.value=result.aging.reading->history;result.registers.push_back(std::move(field));
            }
        }
        if(result.battery.present==false)static_cast<void>(wujieBatteryDate(access,true));
        else if(result.coherent&&result.battery.present==true&&result.readyRaw&&(*result.readyRaw&6)==6)
            result.battery.manufactured=wujieBatteryDate(access);
        if(!impl_->diagnostic&&result.coherent)
            observeBatteryIdentity(result.battery.manufactured,"EC X5SP4NAG 0x314/315",true,result.aging.realHistory);
        if(!result.coherent)return result;
        journalBattery(result.battery,"EC",context);
        journalBatteryRegisters(result);
        lastAccess=access;last=result;lastAt=now;
    }catch(const std::exception& error){result={};result.supported=true;result.status=error.what();}
    return result;
}
std::vector<BatteryReading> MonitoringReader::readBatteries(){
    const auto primary=readBatteryOnly();auto readings=impl_->lastBatteries;
    for(auto& battery:readings)if(battery.deviceId==primary.deviceId)battery=primary;
    return readings;
}
} // namespace mc
