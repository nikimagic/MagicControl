#include "DiagnosticWmiWindows.h"
#include "NativeInterfaceInventoryWindows.h"
#include "BatteryInterfacesWindows.h"
#include "IteLightingWindows.h"
#include "LiquidCoolingWindows.h"
#include <charconv>
#include <cstring>
#include <thread>
#include <limits>
#include <atomic>
namespace mc::windows::wmi {
namespace {
struct Spec {const wchar_t* sql;std::initializer_list<const wchar_t*> fields;const wchar_t* space;};
std::atomic<bool> hostHasHelperEntryPoint{};
const Spec specs[]{
 {L"SELECT InstanceName,CurrentTemperature FROM MSAcpi_ThermalZoneTemperature",{L"InstanceName",L"CurrentTemperature"},L"ROOT\\WMI"},
 {L"SELECT Identifier,Name,SensorType,Value,Min,Max FROM Sensor",{L"Identifier",L"Name",L"SensorType",L"Value",L"Min",L"Max"},L"ROOT\\LibreHardwareMonitor"},
 {L"SELECT Identifier,Name,SensorType,Value,Min,Max FROM Sensor",{L"Identifier",L"Name",L"SensorType",L"Value",L"Min",L"Max"},L"ROOT\\OpenHardwareMonitor"},
 {L"SELECT Name,PercentProcessorTime,ProcessorFrequency,PercentofMaximumFrequency FROM Win32_PerfFormattedData_Counters_ProcessorInformation",{L"Name",L"PercentProcessorTime",L"ProcessorFrequency",L"PercentofMaximumFrequency"},L"ROOT\\CIMV2"},
 {L"SELECT Name,UtilizationPercentage FROM Win32_PerfFormattedData_GPUPerformanceCounters_GPUEngine",{L"Name",L"UtilizationPercentage"},L"ROOT\\CIMV2"},
 {L"SELECT Name,DedicatedUsage,SharedUsage,TotalCommitted FROM Win32_PerfFormattedData_GPUPerformanceCounters_GPUAdapterMemory",{L"Name",L"DedicatedUsage",L"SharedUsage",L"TotalCommitted"},L"ROOT\\CIMV2"},
 {L"SELECT ManufacturerIdTxt,ManufacturerVersion,SpecVersion,IsEnabled_InitialValue,IsActivated_InitialValue FROM Win32_Tpm",{L"ManufacturerIdTxt", L"ManufacturerVersion", L"SpecVersion", L"IsEnabled_InitialValue", L"IsActivated_InitialValue"},L"ROOT\\CIMV2\\Security\\MicrosoftTpm"},
 {L"SELECT SMBIOSMajorVersion,SMBIOSMinorVersion FROM Win32_BIOS",{L"SMBIOSMajorVersion",L"SMBIOSMinorVersion"},L"ROOT\\CIMV2"},
 {L"SELECT Name,Manufacturer,NumberOfCores,NumberOfLogicalProcessors,MaxClockSpeed,SocketDesignation,L2CacheSize,L3CacheSize,VirtualizationFirmwareEnabled FROM Win32_Processor",{L"Name",L"Manufacturer",L"NumberOfCores",L"NumberOfLogicalProcessors",L"MaxClockSpeed",L"SocketDesignation",L"L2CacheSize",L"L3CacheSize",L"VirtualizationFirmwareEnabled"},L"ROOT\\CIMV2"},
 {L"SELECT Name,AdapterCompatibility,VideoProcessor,DriverVersion,DriverDate,PNPDeviceID FROM Win32_VideoController",{L"Name",L"AdapterCompatibility",L"VideoProcessor",L"DriverVersion",L"DriverDate",L"PNPDeviceID"},L"ROOT\\CIMV2"},
 {L"SELECT Manufacturer,PartNumber,SerialNumber,Capacity,Speed,ConfiguredClockSpeed,SMBIOSMemoryType,FormFactor,DeviceLocator,BankLabel,DataWidth,TotalWidth,ConfiguredVoltage FROM Win32_PhysicalMemory",{L"Manufacturer",L"PartNumber",L"SerialNumber",L"Capacity",L"Speed",L"ConfiguredClockSpeed",L"SMBIOSMemoryType",L"FormFactor",L"DeviceLocator",L"BankLabel",L"DataWidth",L"TotalWidth",L"ConfiguredVoltage"},L"ROOT\\CIMV2"},
 {L"SELECT DeviceId,FriendlyName,FirmwareVersion,Size,BusType,MediaType,HealthStatus FROM MSFT_PhysicalDisk",{L"DeviceId",L"FriendlyName",L"FirmwareVersion",L"Size",L"BusType",L"MediaType",L"HealthStatus"},L"ROOT\\Microsoft\\Windows\\Storage"},
 {L"SELECT Model,FirmwareRevision,Size,InterfaceType,Status FROM Win32_DiskDrive",{L"Model",L"FirmwareRevision",L"Size",L"InterfaceType",L"Status"},L"ROOT\\CIMV2"},
 {L"SELECT DeviceName,Manufacturer,DriverVersion FROM Win32_PnPSignedDriver WHERE DeviceClass='NET' OR DeviceClass='MEDIA' OR DeviceClass='CAMERA' OR DeviceClass='BLUETOOTH' OR DeviceClass='COMPUTEACCELERATOR' OR DeviceName LIKE '%IPU%' OR DeviceName LIKE '%NPU%'",{L"DeviceName",L"Manufacturer",L"DriverVersion"},L"ROOT\\CIMV2"},
 {L"SELECT Caption,Version,BuildNumber,OSArchitecture,LastBootUpTime FROM Win32_OperatingSystem",{L"Caption",L"Version",L"BuildNumber",L"OSArchitecture",L"LastBootUpTime"},L"ROOT\\CIMV2"},
 // Fixed metadata-only operations use the same bounded process/wire as WMI.
 // These names are never passed to ExecQuery or accepted as arbitrary SQL.
 {L"SELECT Name,Value FROM MagicControlNativeInterfaces",{L"Name",L"Value"},L"MAGICCONTROL\\READONLY"},
 {L"SELECT Name,Value FROM MagicControlSystemBatteries",{L"Name",L"Value"},L"MAGICCONTROL\\READONLY"},
 // Explicit HYDROC dump getter. Unlike descriptor-only metadata, it sends
 // fixed HID80H to request firmware, never effects/brightness/save commands.
 {L"SELECT Name,Value FROM MagicControlIteLightingEvidence",{L"Name",L"Value"},L"MAGICCONTROL\\HID_VERSION_QUERY"},
 // Explicit LC control bridge, never invoked by ordinary inventory. Both fixed
 // actions notify firmware of its current verified EC presence; no EC/BLE setter.
 {L"SELECT Name,Value FROM MagicControlLppConditionOn",{L"Name",L"Value"},L"MAGICCONTROL\\LPP_NATIVE_POLICY_NOTIFY"},
 {L"SELECT Name,Value FROM MagicControlLppConditionOff",{L"Name",L"Value"},L"MAGICCONTROL\\LPP_NATIVE_POLICY_NOTIFY"}
};
struct Handle {HANDLE value{};~Handle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}};
using Bytes=std::vector<unsigned char>;
constexpr std::uint32_t maxFrame=2*1024*1024;
template<class T>void put(Bytes& out,T value){const auto* p=reinterpret_cast<const unsigned char*>(&value);out.insert(out.end(),p,p+sizeof(value));}
void putString(Bytes& out,const std::string& s){if(s.size()>maxFrame/2)throw std::runtime_error("WMI field exceeds diagnostic wire limit");put(out,static_cast<std::uint32_t>(s.size()));out.insert(out.end(),s.begin(),s.end());}
struct Reader {const Bytes& b;std::size_t at{};
 template<class T>T get(){if(at>b.size()||sizeof(T)>b.size()-at)throw std::runtime_error("Invalid WMI frame");T v{};std::memcpy(&v,b.data()+at,sizeof(v));at+=sizeof(v);return v;}
 std::string string(){const auto n=get<std::uint32_t>();if(n>b.size()-at)throw std::runtime_error("Invalid WMI text");std::string s(reinterpret_cast<const char*>(b.data()+at),n);at+=n;return s;}
};
std::wstring wide(const std::string& s){const int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);if(!n&&!s.empty())throw std::runtime_error("Invalid WMI UTF8");std::wstring w(n,0);if(n)MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),w.data(),n);return w;}
void send(const Bytes& body){if(body.size()>maxFrame)throw std::runtime_error("WMI frame too large");Bytes frame;put(frame,static_cast<std::uint32_t>(body.size()));frame.insert(frame.end(),body.begin(),body.end());std::size_t at{};while(at<frame.size()){DWORD written{};if(!WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),frame.data()+at,static_cast<DWORD>(frame.size()-at),&written,nullptr)||!written)throw std::runtime_error("WMI helper pipe closed");at+=written;}}
void sendRow(const Record& row){Bytes body;put<std::uint8_t>(body,1);put(body,static_cast<std::uint32_t>(row.fields.size()+row.numericFields.size()));
 for(const auto& [key,value]:row.fields){putString(body,utf8(key));put<std::uint8_t>(body,0);putString(body,value);}
 for(const auto& [key,value]:row.numericFields){putString(body,utf8(key));put<std::uint8_t>(body,1);put(body,value);}send(body);}
void sendEnd(const QueryCompletion& end){Bytes body;put<std::uint8_t>(body,2);put<std::uint8_t>(body,end.complete?1:0);putString(body,end.status);send(body);}
}
std::optional<int> runDiagnosticHelper(const std::vector<std::string>& args){
 hostHasHelperEntryPoint.store(true);
 if(args.size()<2||args[1]!="--diagnostic-wmi")return {};
 if(args.size()!=3&&(args.size()!=4||args[3]!="--no-ec"))return 2;unsigned index{};const auto parsed=std::from_chars(args[2].data(),args[2].data()+args[2].size(),index);
 if(parsed.ec!=std::errc{}||parsed.ptr!=args[2].data()+args[2].size()||index>=std::size(specs))return 2;
 // Entry point is dispatched BEFORE GUI, driver checks, EC and single-instance.
 // Only fixed typed operations are supported; no arbitrary SQL/method/data.
 // Native LC condition notifications have their own explicit control scope.
 try{QueryCompletion completion;
    const auto& spec=specs[index];
    if(!wcsncmp(spec.space,L"MAGICCONTROL\\",13)){
        const auto emit=[&](const InventoryRow& row){Record record;record.fields.emplace(L"Name",row.name);record.fields.emplace(L"Value",row.value);sendRow(record);++completion.rows;};
        if(!wcscmp(spec.sql,L"SELECT Name,Value FROM MagicControlNativeInterfaces"))completion.complete=inventory::nativeInterfaces({},emit).complete;
        else if(!wcscmp(spec.sql,L"SELECT Name,Value FROM MagicControlSystemBatteries"))completion.complete=battery::metadata(emit);
        else if(!wcscmp(spec.sql,L"SELECT Name,Value FROM MagicControlIteLightingEvidence"))completion.complete=captureIteLightingEvidence(emit);
        else completion.complete=invokeHydrocLppCondition(!wcscmp(spec.sql,L"SELECT Name,Value FROM MagicControlLppConditionOn"),emit);
        if(!completion.complete)completion.status="Partial native metadata; per-interface status retained";
    }else{Wmi wmi({},true);const auto& s=specs[index];wmi.query(s.sql,s.fields,s.space,std::chrono::seconds(8),&completion,sendRow);}
    sendEnd(completion);return 0;}
 catch(const std::exception& e){try{QueryCompletion failed;failed.complete=false;failed.status=e.what();sendEnd(failed);}catch(...){}return 1;}
}
std::vector<Record> isolatedQuery(const wchar_t* sql,std::initializer_list<const wchar_t*> fields,const wchar_t* space,std::chrono::seconds timeout,std::stop_token stop,QueryCompletion& completion){
 completion={};completion.complete=false;std::vector<Record> rows;
 if(stop.stop_requested()){completion.status="Сбор отменён";return rows;}
 unsigned index=0;for(;index<std::size(specs);++index)if(!wcscmp(sql,specs[index].sql)&&!wcscmp(space,specs[index].space)&&fields.size()==specs[index].fields.size()&&std::equal(fields.begin(),fields.end(),specs[index].fields.begin(),[](auto a,auto b){return wcscmp(a,b)==0;}))break;
 if(index==std::size(specs))throw std::runtime_error("Diagnostic WMI query outside allowlist");
 const auto deadline=std::chrono::steady_clock::now()+timeout;
 SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};HANDLE input{},output{};
 if(!CreatePipe(&input,&output,&sa,0))throw std::runtime_error("Cannot create WMI helper pipe");
 Handle pipe{input},writer{output};SetHandleInformation(pipe.value,HANDLE_FLAG_INHERIT,0);
 Handle nullInput{CreateFileW(L"NUL",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,OPEN_EXISTING,0,nullptr)};
 Handle job{CreateJobObjectW(nullptr,nullptr)};JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
 if(!job.value||!SetInformationJobObject(job.value,JobObjectExtendedLimitInformation,&limits,sizeof(limits)))throw std::runtime_error("Cannot bound WMI helper lifetime");
 std::wstring exe(32768,0);const auto n=GetModuleFileNameW(nullptr,exe.data(),static_cast<DWORD>(exe.size()));if(!n||n>=exe.size())throw std::runtime_error("Cannot resolve WMI helper executable");exe.resize(n);
 // Only an executable that registered this early entry point may spawn
 // itself. Never invoke a library consumer or an older sibling application.
 if(!hostHasHelperEntryPoint.load())throw std::runtime_error("Read-only WMI helper entry point was not registered by this executable");
 auto command=L"\""+exe+L"\" --diagnostic-wmi "+std::to_wstring(index);STARTUPINFOW startup{sizeof(startup)};startup.dwFlags=STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;startup.hStdInput=nullInput.value;startup.hStdOutput=writer.value;startup.hStdError=nullInput.value;
 if(wcsstr(GetCommandLineW(),L"--no-ec"))command+=L" --no-ec";
 PROCESS_INFORMATION pi{};if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|CREATE_SUSPENDED,nullptr,nullptr,&startup,&pi))throw std::runtime_error("Cannot start read-only WMI helper");
 Handle process{pi.hProcess},thread{pi.hThread};
 if(!AssignProcessToJobObject(job.value,process.value)){TerminateProcess(process.value,ERROR_CANCELLED);WaitForSingleObject(process.value,1000);throw std::runtime_error("Cannot isolate read-only WMI helper");}
 ResumeThread(thread.value);CloseHandle(writer.value);writer.value=nullptr;
 Bytes pending;bool ended=false;
 try{for(;;){
  DWORD available{};while(PeekNamedPipe(pipe.value,nullptr,0,nullptr,&available,nullptr)&&available){
   unsigned char buffer[65536];DWORD got{};if(!ReadFile(pipe.value,buffer,std::min<DWORD>(available,sizeof(buffer)),&got,nullptr)||!got)break;pending.insert(pending.end(),buffer,buffer+got);
   std::size_t consumed{};
   while(pending.size()-consumed>=4){std::uint32_t size{};std::memcpy(&size,pending.data()+consumed,4);if(!size||size>maxFrame)throw std::runtime_error("Invalid WMI helper frame size");if(pending.size()-consumed<4+size)break;
    Bytes body(pending.begin()+consumed+4,pending.begin()+consumed+4+size);Reader reader{body};const auto type=reader.get<std::uint8_t>();
    if(type==1){if(rows.size()>=kMaximumQueryRecords)throw std::runtime_error("WMI record limit reached");Record row;const auto count=reader.get<std::uint32_t>();if(count>64)throw std::runtime_error("Invalid WMI field count");
     for(unsigned i=0;i<count;++i){auto key=wide(reader.string());const auto kind=reader.get<std::uint8_t>();if(kind==0)row.fields.emplace(std::move(key),reader.string());else if(kind==1){const auto number=reader.get<double>();if(std::isfinite(number))row.numericFields.emplace(std::move(key),number);}else throw std::runtime_error("Invalid WMI field kind");}rows.push_back(std::move(row));
    }else if(type==2){completion.complete=reader.get<std::uint8_t>()!=0;completion.status=reader.string();ended=true;}else throw std::runtime_error("Invalid WMI frame kind");
    if(reader.at!=body.size())throw std::runtime_error("Trailing WMI frame data");consumed+=4+size;
   }
   if(consumed)pending.erase(pending.begin(),pending.begin()+consumed);
   if(stop.stop_requested()||std::chrono::steady_clock::now()>=deadline)break;
  }
  if(WaitForSingleObject(process.value,0)==WAIT_OBJECT_0){DWORD remaining{};if(PeekNamedPipe(pipe.value,nullptr,0,nullptr,&remaining,nullptr)&&remaining)continue;if(!ended||!pending.empty()){completion.complete=false;completion.status="WMI helper exited before complete response";}break;}
  if(stop.stop_requested()||std::chrono::steady_clock::now()>=deadline){completion.complete=false;completion.status=stop.stop_requested()?"Сбор отменён; сохранены полученные строки":"WMI total deadline (connect/execute/enumerate); partial rows retained";TerminateJobObject(job.value,ERROR_TIMEOUT);WaitForSingleObject(process.value,1000);break;}
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
 }}catch(const std::exception& e){completion.complete=false;completion.status=e.what();TerminateJobObject(job.value,ERROR_CANCELLED);WaitForSingleObject(process.value,1000);}
 completion.rows=rows.size();return rows;
}
}
