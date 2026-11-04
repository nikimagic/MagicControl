#include "../common/DiagnosticDumpInternal.h"
#include "DiagnosticWmiWindows.h"
#include "ApuPowerWindows.h"
#include "MagicControl/BoardIdentity.h"
#include "MagicControl/DeviceProfile.h"
#include <chrono>
#include <cmath>
#include <format>
#include <intrin.h>
namespace mc::diagnostics {
struct PlatformSensors::Impl {
    windows::ApuPowerReader smu;
    std::vector<SensorReading> read(bool slow,std::stop_token token){
        std::vector<SensorReading> rows;
        const auto add=[&](std::string device,std::string metric,std::string unit,std::string source,std::optional<double> value,std::string status="",std::optional<double> raw={}){
            rows.push_back({std::move(device),std::move(metric),std::move(unit),std::move(source),status.empty()?(value?"ok":"not reported"):status,value,raw,GetTickCount64(),SensorTimestampOrigin::SoftwareObservation});
        };
        if(!slow){
            int cpuid[4]{};__cpuid(cpuid,1);const auto signature=static_cast<unsigned>(cpuid[0]);
            add("CPU CPUID","signature","raw","CPUID leaf1",double(signature));
            const auto family=(signature>>8)&15,model=((signature>>4)&15)|((signature>>12)&0xF0);
            add("CPU CPUID","family","id","CPUID leaf1",double(family==15?family+((signature>>20)&255):family));
            add("CPU CPUID","model","id","CPUID leaf1",double(model));
            add("CPU CPUID","stepping","id","CPUID leaf1",double(signature&15));
            MEMORYSTATUSEX memory{sizeof(memory)};
            if(GlobalMemoryStatusEx(&memory)){
                add("System RAM","used","bytes","Windows · GlobalMemoryStatusEx",double(memory.ullTotalPhys-memory.ullAvailPhys));
                add("System RAM","available","bytes","Windows · GlobalMemoryStatusEx",double(memory.ullAvailPhys));
                add("System RAM","load","percent","Windows · GlobalMemoryStatusEx",double(memory.dwMemoryLoad));
            }else add("System RAM","load","percent","Windows · GlobalMemoryStatusEx",{},"Win32="+std::to_string(GetLastError()));
            const auto id=deviceProfile(readBoardDescriptor()).id;
            if(!wcsstr(GetCommandLineW(),L"--no-ec")&&(id=="wu-jie-15-xpro"||id=="huawei-hlyl-wxx9"||id=="minisforum-hpbsd")){
                const auto s=smu.read(true);
                // A stale SMU cache must not become a fresh diagnostic reading.
                const auto valid=[&](auto value){return s.stale?std::optional<double>{}:value;};
                const auto source="PawnIO · RyzenSMU table "+std::to_string(s.tableVersion);
                add("APU","fast_window_power","W",source,valid(s.fastPowerWatts),s.status);
                add("APU","STAPM_limit","W",source,valid(s.stapmLimitWatts),s.status);
                add("APU","Slow_limit","W",source,valid(s.slowLimitWatts),s.status);
                add("APU","Fast_limit","W",source,valid(s.fastLimitWatts),s.status);
            }
            return rows;
        }
        const auto query=[&](const wchar_t* sql,std::initializer_list<const wchar_t*> fields,const wchar_t* space,auto consume){
            if(token.stop_requested())return;
            const auto source="Windows WMI · "+windows::wmi::utf8(space);
            // Performance providers may need several seconds for their first
            // sample. This worker is separate from the fast sensor timeline.
            try{windows::wmi::QueryCompletion completion;
                const auto values=windows::wmi::isolatedQuery(sql,fields,space,std::chrono::seconds(8),token,completion);
                const auto queryName=windows::wmi::utf8(sql);
                add(queryName,"provider_records","records",source,double(completion.rows),
                    completion.complete?"complete":"partial; "+completion.status);
                if(!completion.complete)add(queryName,"provider_status","",source,{},completion.status);
                else if(values.empty())add(queryName,"provider_status","",source,{},"no instances");
                for(const auto& value:values)consume(value,source);
            }catch(const std::exception& e){add(windows::wmi::utf8(sql),"provider_status","",source,{},e.what());}
        };
        // ACPI thermal zones do not necessarily represent CPU package temperature.
        query(L"SELECT InstanceName,CurrentTemperature FROM MSAcpi_ThermalZoneTemperature",{L"InstanceName",L"CurrentTemperature"},L"ROOT\\WMI",[&](const auto& r,const auto& source){
            const auto raw=r.number(L"CurrentTemperature");std::optional<double> value;
            if(raw&&*raw>=2331&&*raw<=4731)value=*raw/10.-273.15;
            add(r.get(L"InstanceName"),"thermal_zone_temperature","C",source,value,"ACPI thermal zone; no CPU/GPU assignment",raw);
        });
        // Read optional sensor providers only if the owner already runs them.
        // Sensor identifiers retain CPU/core/GPU/DIMM/disk ownership and avoid
        // assigning a third-party temperature to a guessed dashboard slot.
        for(const auto* space:{L"ROOT\\LibreHardwareMonitor",L"ROOT\\OpenHardwareMonitor"})
            query(L"SELECT Identifier,Name,SensorType,Value,Min,Max FROM Sensor",{L"Identifier",L"Name",L"SensorType",L"Value",L"Min",L"Max"},space,[&](const auto& r,const auto& source){
                const auto type=r.get(L"SensorType");
                const std::string unit=type=="Temperature"?"C":type=="Load"||type=="Control"?"percent":type=="Clock"?"MHz":type=="Power"?"W":type=="Voltage"?"V":type=="Fan"?"RPM":"provider units";
                const auto device=r.get(L"Identifier")+" / "+r.get(L"Name");
                add(device,type,unit,source,r.number(L"Value"));
                add(device,type+"/provider_min",unit,source,r.number(L"Min"),"provider lifetime minimum; not this dump");
                add(device,type+"/provider_max",unit,source,r.number(L"Max"),"provider lifetime maximum; not this dump");
            });
        query(L"SELECT Name,PercentProcessorTime,ProcessorFrequency,PercentofMaximumFrequency FROM Win32_PerfFormattedData_Counters_ProcessorInformation",{L"Name",L"PercentProcessorTime",L"ProcessorFrequency",L"PercentofMaximumFrequency"},L"ROOT\\CIMV2",[&](const auto& r,const auto& source){
            add("CPU logical "+r.get(L"Name"),"usage","percent",source,r.number(L"PercentProcessorTime"));
            add("CPU logical "+r.get(L"Name"),"frequency","MHz",source,r.number(L"ProcessorFrequency"));
            add("CPU logical "+r.get(L"Name"),"maximum_frequency_ratio","percent",source,r.number(L"PercentofMaximumFrequency"));
        });
        query(L"SELECT Name,UtilizationPercentage FROM Win32_PerfFormattedData_GPUPerformanceCounters_GPUEngine",{L"Name",L"UtilizationPercentage"},L"ROOT\\CIMV2",[&](const auto& r,const auto& source){
            add(r.get(L"Name"),"engine_usage","percent",source,r.number(L"UtilizationPercentage"),"per process/engine counter; do not sum into total GPU load");
        });
        query(L"SELECT Name,DedicatedUsage,SharedUsage,TotalCommitted FROM Win32_PerfFormattedData_GPUPerformanceCounters_GPUAdapterMemory",{L"Name",L"DedicatedUsage",L"SharedUsage",L"TotalCommitted"},L"ROOT\\CIMV2",[&](const auto& r,const auto& source){
            add(r.get(L"Name"),"dedicated_usage","bytes",source,r.number(L"DedicatedUsage"));
            add(r.get(L"Name"),"shared_usage","bytes",source,r.number(L"SharedUsage"));
            add(r.get(L"Name"),"committed","bytes",source,r.number(L"TotalCommitted"));
        });
        return rows;
    }
};
PlatformSensors::PlatformSensors():impl_(std::make_unique<Impl>()){}
PlatformSensors::~PlatformSensors()=default;
std::vector<SensorReading> PlatformSensors::read(bool slow,std::stop_token token){return impl_->read(slow,token);}
}
