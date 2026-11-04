// Controlled hardware A/B probe. Never registered as an automatic CTest test.
#include "MagicControl/EcAccess.h"
#include "MagicControl/EcRegister.h"
#include "MagicControl/FanCurve.h"
#include "MagicControl/PortableSettings.h"
#include "../src/platform/windows/FanControlWindows.h"
#include <windows.h>
#include <objbase.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <thread>
#include <vector>
using namespace std::chrono_literals;
namespace {
std::atomic<bool> cancel{};
BOOL WINAPI handler(DWORD event) {if(event==CTRL_C_EVENT || event==CTRL_BREAK_EVENT){cancel=true;return TRUE;}return FALSE;}
struct PackageEnergy {
 HMODULE lib{};void* module{};
 using Open=HRESULT(WINAPI*)(void**);using Load=HRESULT(WINAPI*)(void*,const void*,std::size_t);
 using Exec=HRESULT(WINAPI*)(void*,const char*,const std::uint64_t*,std::size_t,std::uint64_t*,std::size_t,std::size_t*);
 using Close=HRESULT(WINAPI*)(void*);Exec execute{};Close close{};double unit{};
 ~PackageEnergy(){if(module&&close)close(module);if(lib)FreeLibrary(lib);}
 std::uint64_t msr(std::uint64_t id){std::uint64_t v{};std::size_t n{};if(FAILED(execute(module,"ioctl_read_msr",&id,1,&v,1,&n))||n!=1)throw std::runtime_error("MSR read failed");return v;}
 void init(const wchar_t* moduleName=L"AMDFamily17.bin", bool energyMode=true){
  auto dir=mc::portableSettingsPath("PawnIO");lib=LoadLibraryExW((dir/"PawnIOLib.dll").c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
  if(!lib)throw std::runtime_error("PawnIO DLL unavailable");
  auto open=reinterpret_cast<Open>(GetProcAddress(lib,"pawnio_open"));auto load=reinterpret_cast<Load>(GetProcAddress(lib,"pawnio_load"));
  execute=reinterpret_cast<Exec>(GetProcAddress(lib,"pawnio_execute"));close=reinterpret_cast<Close>(GetProcAddress(lib,"pawnio_close"));
  if(!open||!load||!execute||!close||FAILED(open(&module)))throw std::runtime_error("PawnIO driver unavailable or elevation required");
  std::ifstream f(dir/moduleName,std::ios::binary);std::vector<char>b{std::istreambuf_iterator<char>(f),{}};
  if(b.empty()||FAILED(load(module,b.data(),b.size())))throw std::runtime_error("AMD module load failed");
  if(energyMode)unit=std::pow(.5,static_cast<int>((msr(0xC0010299)>>8)&31));
 }
 std::uint32_t energy(){return static_cast<std::uint32_t>(msr(0xC001029B));}
};
struct Handle {HANDLE value{};~Handle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}};
}
int readSmu(const std::filesystem::path& output) {
 try {
  Handle single{CreateMutexW(nullptr,TRUE,L"MagicControl.SingleInstance")};
  if(!single.value||GetLastError()==ERROR_ALREADY_EXISTS)throw std::runtime_error("Close MagicControl before SMU diagnostic");
  mc::EcAccess ec(mc::createDefaultBackend());if(!ec.verifyBoard().verified)throw std::runtime_error("Exact board verification failed");
  if(std::filesystem::exists(output))throw std::runtime_error("Output already exists");
  Handle pci{CreateMutexW(nullptr,FALSE,L"Global\\Access_PCI")};if(!pci.value)throw std::runtime_error("PCI mutex failed");
  DWORD acquired=WaitForSingleObject(pci.value,2000);if(acquired!=WAIT_OBJECT_0&&acquired!=WAIT_ABANDONED)throw std::runtime_error("PCI access busy");
  try {
   PackageEnergy module;module.init(L"RyzenSMU.bin",false);
   auto command=[&](const char* name,std::uint64_t* out,std::size_t size){std::size_t n{};const auto hr=module.execute(module.module,name,nullptr,0,out,size,&n);if(FAILED(hr))throw std::runtime_error(std::string(name)+" failed HRESULT="+std::to_string(static_cast<unsigned long>(hr)));};
   std::uint64_t resolved[2]{};command("ioctl_resolve_pm_table",resolved,2);
   command("ioctl_update_pm_table",nullptr,0);
   std::array<std::uint64_t,128> table{};command("ioctl_read_pm_table",table.data(),table.size());
   std::array<float,256> values{};std::memcpy(values.data(),table.data(),sizeof(table));
   std::filesystem::create_directories(output);
   std::ofstream binary(output/"pm-table-prefix.bin",std::ios::binary);binary.write(reinterpret_cast<const char*>(table.data()),sizeof(table));
   std::ofstream data(output/"pm-table.csv");data<<"float_index,byte_offset,value\n";for(std::size_t i=0;i<values.size();++i)data<<i<<','<<i*4<<','<<values[i]<<'\n';
   std::ofstream info(output/"info.txt");info<<"Read-only power metric diagnostic; no EC or SMU limit-setting commands.\nPM table version="<<std::hex<<resolved[0]<<"\n";
   const char* names[]{"STAPM_LIMIT","STAPM_VALUE","PPT_FAST_LIMIT","PPT_FAST_VALUE","PPT_SLOW_LIMIT","PPT_SLOW_VALUE"};
   for(int i=0;i<6;++i){info<<names[i]<<'='<<values[i]<<" W\n";std::printf("%s=%.3f W\n",names[i],values[i]);}
   std::printf("PM table version=0x%llX, raw prefix saved; no power settings changed\n",static_cast<unsigned long long>(resolved[0]));
  } catch(...) {ReleaseMutex(pci.value);throw;}
  ReleaseMutex(pci.value);return 0;
 }catch(const std::exception& error){std::fprintf(stderr,"SMU READ STOP: %s\n",error.what());return 1;}
}

int windowsAb(const std::filesystem::path& output, bool presenceTest=false, BYTE expectedAc=1) {
 HMODULE powerLib{};using GetOverlay=DWORD(WINAPI*)(GUID*);using SetOverlay=DWORD(WINAPI*)(GUID);SetOverlay set{};GUID original{};bool originalKnown{};
 std::unique_ptr<mc::EcAccess> ecHolder; std::uint8_t originalPresence{}; bool presenceKnown{};
 std::vector<std::jthread> load;auto stopLoad=[&]{for(auto& t:load)t.request_stop();load.clear();};
 try {
  Handle single{CreateMutexW(nullptr,TRUE,L"MagicControl.SingleInstance")};if(!single.value||GetLastError()==ERROR_ALREADY_EXISTS)throw std::runtime_error("Close MagicControl first");
  SYSTEM_POWER_STATUS power{};if(!GetSystemPowerStatus(&power)||power.ACLineStatus!=expectedAc)throw std::runtime_error("Requested power source required");
  ecHolder=std::make_unique<mc::EcAccess>(mc::createDefaultBackend());auto& ec=*ecHolder;if(!ec.verifyBoard().verified)throw std::runtime_error("Exact board check failed");
  if((ec.readStable(0x726)&128)||(ec.readStable(0x7C6)&4))throw std::runtime_error("Native cooling required");
  originalPresence=ec.readStable(mc::ec::kAppPresence)&1;presenceKnown=true;
  if(presenceTest && originalPresence)throw std::runtime_error("Start with APP presence cleared (MagicControl closed)");
  if(std::filesystem::exists(output))throw std::runtime_error("Output already exists");
  powerLib=LoadLibraryW(L"powrprof.dll");if(!powerLib)throw std::runtime_error("Power library unavailable");
  auto get=reinterpret_cast<GetOverlay>(GetProcAddress(powerLib,"PowerGetActualOverlayScheme"));set=reinterpret_cast<SetOverlay>(GetProcAddress(powerLib,"PowerSetActiveOverlayScheme"));
  if(!get||!set||get(&original)!=0)throw std::runtime_error("Windows overlay API unavailable");originalKnown=true;
  PackageEnergy energy;energy.init();Handle pci{CreateMutexW(nullptr,FALSE,L"Global\\Access_PCI")};if(!pci.value)throw std::runtime_error("PCI mutex failed");
  PackageEnergy smu;if(expectedAc==1)smu.init(L"RyzenSMU.bin",false);
  auto cmd=[&](const char* name,std::uint64_t*out,std::size_t size){std::size_t n{};if(FAILED(smu.execute(smu.module,name,nullptr,0,out,size,&n)))throw std::runtime_error(std::string(name)+" failed");};
  auto table=[&]{if(expectedAc==0){std::array<float,16> unavailable{};unavailable.fill(std::nanf(""));return unavailable;}DWORD locked=WaitForSingleObject(pci.value,2000);if(locked!=WAIT_OBJECT_0&&locked!=WAIT_ABANDONED)throw std::runtime_error("PCI busy");std::array<float,16> v{};try{std::uint64_t resolved[2]{};cmd("ioctl_resolve_pm_table",resolved,2);cmd("ioctl_update_pm_table",nullptr,0);std::uint64_t raw[8]{};cmd("ioctl_read_pm_table",raw,8);std::memcpy(v.data(),raw,sizeof(raw));}catch(...){ReleaseMutex(pci.value);throw;}ReleaseMutex(pci.value);return v;};
  table();std::filesystem::create_directories(output);std::ofstream data(output/"samples.csv");data<<"phase,second,msr_package_watts,smu_stapm_limit,smu_stapm_value,smu_fast_limit,smu_fast_value,smu_slow_limit,smu_slow_value,smu_apu_limit,cpu_ec_c,ec_mode,app_presence_raw,ec_write_count\n";
  std::ofstream originalFile(output/"original-overlay.txt");wchar_t guidText[64];StringFromGUID2(original,guidText,64);originalFile<<std::string(guidText,guidText+wcslen(guidText))<<'\n';originalFile.close();
  const DWORD workers=GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);std::atomic<double> sink{};
  for(DWORD i=0;i<workers;++i)load.emplace_back([&,i](std::stop_token stop){double a=1.0+i,b=.999999;while(!stop.stop_requested()){for(int j=0;j<8192;++j){a=a*b+.000001;b=1.999999-b;}sink.store(a,std::memory_order_relaxed);}});
  auto phase=[&](const char* name){std::printf("PHASE %s\n",name);auto before=energy.energy();auto at=GetTickCount64();for(int second=1;second<=15;++second){std::this_thread::sleep_for(1s);if(cancel||std::filesystem::exists(output/"STOP"))throw std::runtime_error("Cancelled");if(!GetSystemPowerStatus(&power)||power.ACLineStatus!=expectedAc)throw std::runtime_error("Power source changed");int temp=ec.readTemperature(mc::ec::kCpuTemperature);if(temp>=75)throw std::runtime_error("75 C test stop");auto current=energy.energy();auto now=GetTickCount64();double watts=static_cast<std::uint32_t>(current-before)*energy.unit/((now-at)/1000.0);before=current;at=now;auto v=table();data<<name<<','<<second<<','<<watts;for(int i=0;i<7;++i)data<<','<<v[i];data<<','<<temp<<','<<static_cast<int>(ec.readStable(0x751))<<','<<static_cast<int>(ec.readStable(mc::ec::kAppPresence))<<','<<ec.writeCount()<<'\n';data.flush();if(second%5==0)std::printf("%s %ds: MSR %.2f W, SMU fast %.2f W, limits %.1f/%.1f/%.1f, CPU %d C\n",name,second,watts,v[3],v[0],v[2],v[4],temp);}};
  if(presenceTest) {
   phase("presence_off_before");
   static_cast<void>(ec.updateBitsVerified(mc::ec::kAppPresence,1,1));phase("presence_on");
   static_cast<void>(ec.updateBitsVerified(mc::ec::kAppPresence,1,originalPresence));phase("presence_off_restored");
  } else {
   phase("original_windows");
   const GUID best{0xded574b5,0x45a0,0x4f42,{0x87,0x37,0x46,0x34,0x5c,0x09,0xc2,0x38}};
   if(set(best)!=0)throw std::runtime_error("Cannot set Windows best performance");phase("best_performance");
   if(set(original)!=0)throw std::runtime_error("Cannot restore Windows overlay");phase("original_restored");
  }
  stopLoad();
  if((ec.readStable(mc::ec::kAppPresence)&1)!=originalPresence)throw std::runtime_error("App presence restoration failed");
  GUID after{};if(get(&after)!=0||!IsEqualGUID(original,after))throw std::runtime_error("Windows overlay restoration not confirmed");
  std::ofstream report(output/"restored.txt");report<<"Original Windows overlay restored. EC write count="<<ec.writeCount()<<". No SMU limit-setting commands.\n";
  FreeLibrary(powerLib);std::puts(presenceTest?"DONE: CPU load stopped; original APP presence and Windows mode verified restored.":"DONE: CPU load stopped, original Windows power mode restored, no EC writes.");return 0;
 }catch(const std::exception& error){stopLoad();if(presenceTest&&presenceKnown&&ecHolder){try{static_cast<void>(ecHolder->updateBitsVerified(mc::ec::kAppPresence,1,originalPresence));}catch(...){std::fprintf(stderr,"APP presence restoration FAILED\n");}}if(originalKnown&&set){DWORD restored=set(original);std::fprintf(stderr,"Overlay restoration status=%lu\n",restored);}if(powerLib)FreeLibrary(powerLib);std::fprintf(stderr,"WINDOWS AB STOP: %s\n",error.what());return 1;}
}

int fanTableAb(const std::filesystem::path& output) {
 std::unique_ptr<mc::EcAccess> access; std::vector<std::uint8_t> savedTable;
 const std::array<std::pair<std::uint16_t,std::uint8_t>,5> flags{{{0x726,128},{0x727,64},{0x741,1},{0x7C5,128},{0x7C6,4}}};
 std::array<std::uint8_t,5> savedFlags{};bool captured=false;
 auto restore=[&]{if(!captured)return;auto& ec=*access;
  static_cast<void>(ec.updateBitsVerified(0x7C6,4,0));
  for(std::size_t i=0;i<savedTable.size();++i)ec.writeVerified(static_cast<std::uint16_t>(0xF00+i),savedTable[i]);
  for(std::size_t i=0;i<flags.size();++i)static_cast<void>(ec.updateBitsVerified(flags[i].first,flags[i].second,savedFlags[i]));
  for(std::size_t i=0;i<savedTable.size();++i)if(ec.readStable(static_cast<std::uint16_t>(0xF00+i))!=savedTable[i])throw std::runtime_error("Table restoration mismatch");
  for(std::size_t i=0;i<flags.size();++i)if((ec.readStable(flags[i].first)&flags[i].second)!=(savedFlags[i]&flags[i].second))throw std::runtime_error("Flag restoration mismatch");
  std::ofstream(output/"restored.txt")<<"Original 96 table bytes and five owned flag fields verified restored. Performance mode and power limits not written.\n";
 };
 try {
  Handle single{CreateMutexW(nullptr,TRUE,L"MagicControl.SingleInstance")};if(!single.value||GetLastError()==ERROR_ALREADY_EXISTS)throw std::runtime_error("Close MagicControl first");
  if(std::filesystem::exists(output))throw std::runtime_error("Output already exists");
  access=std::make_unique<mc::EcAccess>(mc::createDefaultBackend());auto& ec=*access;
  if(!ec.verifyBoard().verified)throw std::runtime_error("Exact board check failed");
  if(ec.readTemperature(0x43E)>65)throw std::runtime_error("Stop load and let CPU cool below 65 C");
  if((ec.readStable(0x751)&64)||(ec.readStable(0x726)&128)||(ec.readStable(0x7C6)&4))throw std::runtime_error("Native cooling required after closing MagicControl");
  SYSTEM_POWER_STATUS originalPower{};if(!GetSystemPowerStatus(&originalPower))throw std::runtime_error("Power status unavailable");
  std::filesystem::create_directories(output);savedTable=ec.readBlock(0xF00,96);
  std::ofstream before(output/"before.csv");before<<"address,value\n";
  for(std::size_t i=0;i<flags.size();++i){savedFlags[i]=ec.readStable(flags[i].first);before<<flags[i].first<<','<<static_cast<int>(savedFlags[i])<<'\n';}
  for(std::size_t i=0;i<savedTable.size();++i)before<<0xF00+i<<','<<static_cast<int>(savedTable[i])<<'\n';before.close();captured=true;
  std::ofstream data(output/"samples.csv");data<<"phase,second,target_percent,cpu_pwm,gpu_pwm,cpu_rpm,gpu_rpm,cpu_ec_c,split_raw,commit_raw,cpu_target_raw,gpu_target_raw\n";
  auto phase=[&](const char* name,int duty,bool split){
   std::printf("PHASE %s\n",name);
   static_cast<void>(ec.updateBitsVerified(0x7C6,4,0));
   static_cast<void>(ec.updateBitsVerified(0x727,64,0));std::this_thread::sleep_for(50ms);
   static_cast<void>(ec.updateBitsVerified(0x727,64,64));
   static_cast<void>(ec.updateBitsVerified(0x726,128,128));
   static_cast<void>(ec.updateBitsVerified(0x7C5,128,split?128:0));
   auto table=mc::buildAutonomousFanTable({{20,duty},{80,duty},{85,100}});
   for(auto base:{0xF00,0xF30})for(int i=0;i<48;++i){if(i==16)continue;ec.writeVerified(static_cast<std::uint16_t>(base+i),table[i]);std::this_thread::sleep_for(10ms);}
   static_cast<void>(ec.updateBitsVerified(0x7C6,4,4));
   static_cast<void>(ec.updateBitsVerified(0x741,1,0));std::this_thread::sleep_for(300ms);static_cast<void>(ec.updateBitsVerified(0x741,1,1));
   for(int s=1;s<=12;++s){std::this_thread::sleep_for(1s);
    if(cancel||std::filesystem::exists(output/"STOP"))throw std::runtime_error("Cancelled");
    SYSTEM_POWER_STATUS power{};if(!GetSystemPowerStatus(&power)||power.ACLineStatus!=originalPower.ACLineStatus)throw std::runtime_error("Power source changed");
    int t=ec.readTemperature(0x43E);if(t>=70)throw std::runtime_error("70 C stop");
    auto c=ec.readFanFeedback(mc::FanChannel::Cpu),g=ec.readFanFeedback(mc::FanChannel::Gpu);
    auto splitRaw=ec.readStable(0x7C5),commit=ec.readStable(0x7C6);
    data<<name<<','<<s<<','<<duty<<','<<c.dutyPercent<<','<<g.dutyPercent<<','<<c.rpm<<','<<g.rpm<<','<<t<<','<<static_cast<int>(splitRaw)<<','<<static_cast<int>(commit)<<','<<static_cast<int>(ec.readStable(0x461))<<','<<static_cast<int>(ec.readStable(0x469))<<'\n';data.flush();
    if(s%4==0)std::printf("%s %ds: CPU %.1f%%/%d rpm GPU %.1f%%/%d rpm T=%d C split=0x%02X\n",name,s,c.dutyPercent,c.rpm,g.dutyPercent,g.rpm,t,splitRaw);
   }
  };
  phase("split_50",50,true);phase("linked_50",50,false);phase("linked_70",70,false);phase("split_70",70,true);
  restore();std::puts("DONE: original EC table and flags verified restored");return 0;
 }catch(const std::exception& e){std::fprintf(stderr,"FAN AB STOP: %s\n",e.what());try{restore();}catch(const std::exception& r){std::fprintf(stderr,"RESTORATION FAILED: %s\n",r.what());}return 1;}
}

int wmain(int argc,wchar_t**argv){
 std::setvbuf(stdout,nullptr,_IONBF,0);
 if(argc==3&&std::wstring(argv[1])==L"--fan-table-ab")return fanTableAb(argv[2]);
 if(argc==3&&std::wstring(argv[1])==L"--presence-ab-battery")return windowsAb(argv[2],true,0);
 if(argc==3&&std::wstring(argv[1])==L"--presence-ab")return windowsAb(argv[2],true);
 if(argc==3&&std::wstring(argv[1])==L"--windows-ab")return windowsAb(argv[2]);
 if(argc==3&&std::wstring(argv[1])==L"--smu")return readSmu(argv[2]);
 if(argc!=3||std::wstring(argv[1])!=L"--run"){std::puts("PowerAbProbe --run OUTPUT_DIRECTORY (real hardware; AC required; close MagicControl)");return 0;}
 std::setvbuf(stdout,nullptr,_IONBF,0);SetConsoleCtrlHandler(handler,TRUE);
 try {
  Handle single{CreateMutexW(nullptr,TRUE,L"MagicControl.SingleInstance")};
  if(!single.value||GetLastError()==ERROR_ALREADY_EXISTS)throw std::runtime_error("Close MagicControl via tray Exit before the hardware test; no EC writes performed");
  SYSTEM_POWER_STATUS power{};if(!GetSystemPowerStatus(&power)||power.ACLineStatus!=1)throw std::runtime_error("AC power required; no EC writes performed");
  const std::filesystem::path output(argv[2]);if(std::filesystem::exists(output))throw std::runtime_error("Output already exists; refusing overwrite");
  mc::EcAccess ec(mc::createDefaultBackend());if(!ec.verifyBoard().verified)throw std::runtime_error("Exact board verification failed");
  const auto mode=ec.readStable(mc::ec::kPerformanceMode);
  if((mode&0x40)||(ec.readStable(0x726)&0x80)||(ec.readStable(0x727)&0x40)||(ec.readStable(0x7C6)&4))throw std::runtime_error("Start from native Default cooling; no EC writes performed");
  for(int i=0;i<96;++i)if(ec.readStable(static_cast<std::uint16_t>(0xF00+i)))throw std::runtime_error("Inactive fan tables are not empty; refusing to change unowned state");
  const auto presence=ec.readStable(mc::ec::kAppPresence)&1;
  std::array<std::uint8_t,3> originalLimits{};for(int i=0;i<3;++i)originalLimits[i]=ec.readStable(static_cast<std::uint16_t>(0x783+i));
  PackageEnergy package;package.init(); // Driver/energy access verified before changing EC.
  std::filesystem::create_directories(output);
  std::ofstream snapshot(output/"before.csv");snapshot<<"address,value\n";
  for(auto a:{0x751,0x726,0x727,0x741,0x7C5,0x7C6,0x783,0x784,0x785})snapshot<<a<<','<<static_cast<int>(ec.readStable(static_cast<std::uint16_t>(a)))<<'\n';snapshot.close();
  std::ofstream data(output/"samples.csv");data<<"utc,phase,phase_second,package_watts,cpu_ec_c,mode,custom726,gate727,ram7c6,cpu_pwm,gpu_pwm,cpu_rpm,gpu_rpm,pl1,pl2,pl4,ec_write_count\n";
  std::vector<std::jthread> load;
  const DWORD workers=GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
  std::atomic<double> sink{};
  for(DWORD i=0;i<workers;++i)load.emplace_back([&,i](std::stop_token stop){double a=1.0+i,b=.999999,c=.000001;while(!stop.stop_requested()&&!cancel){for(int j=0;j<8192;++j){a=a*b+c;b=1.999999-b;}sink.store(a,std::memory_order_relaxed);}});
  auto stopLoad=[&]{for(auto& t:load)t.request_stop();load.clear();};
  std::puts("Fixed CPU load started; no Windows power policy or PL-register writes.");
  {
   mc::windows::FanController controller(ec);
   auto native=[&]{auto f=controller.nativeAndWait();if(f.wait_for(30s)!=std::future_status::ready)throw std::runtime_error("Native transition timeout");f.get();};
   auto waitApplied=[&]{auto end=std::chrono::steady_clock::now()+30s;for(;;){auto s=controller.status();if(s.error)throw std::runtime_error(s.message);if(!s.applying&&!s.busy)break;if(cancel||std::chrono::steady_clock::now()>end)throw std::runtime_error("Apply cancelled/timed out");std::this_thread::sleep_for(100ms);}};
   mc::FanCurveTable curve{};curve[0]={32,20};curve[1]={40,30};curve[2]={50,50};curve[3]={60,100};
   auto phase=[&](const char* name){
    std::printf("PHASE %s\n",name);auto before=package.energy();auto at=GetTickCount64();
    for(int second=1;second<=20;++second){
     std::this_thread::sleep_for(1s);if(cancel || std::filesystem::exists(output/"STOP"))throw std::runtime_error("Cancelled");
     if(!GetSystemPowerStatus(&power)||power.ACLineStatus!=1)throw std::runtime_error("Power source changed; aborting");
     const int temp=ec.readTemperature(mc::ec::kCpuTemperature);if(temp>=75)throw std::runtime_error("75 C test stop reached");
     if((ec.readStable(0x751)&0x90)!=(mode&0x90))throw std::runtime_error("External performance preset change");
     for(int i=0;i<3;++i)if(ec.readStable(static_cast<std::uint16_t>(0x783+i))!=originalLimits[i])throw std::runtime_error("External PL override change");
     const auto energy=package.energy();const auto now=GetTickCount64();const double watts=static_cast<std::uint32_t>(energy-before)*package.unit/((now-at)/1000.0);before=energy;at=now;
     auto cpu=ec.readFanFeedback(mc::FanChannel::Cpu);auto gpu=ec.readFanFeedback(mc::FanChannel::Gpu);
     SYSTEMTIME utc{};GetSystemTime(&utc);char stamp[64];std::snprintf(stamp,sizeof(stamp),"%04u-%02u-%02uT%02u:%02u:%02uZ",utc.wYear,utc.wMonth,utc.wDay,utc.wHour,utc.wMinute,utc.wSecond);
     data<<stamp<<','<<name<<','<<second<<','<<watts<<','<<temp;
     for(auto a:{0x751,0x726,0x727,0x7C6})data<<','<<static_cast<int>(ec.readStable(static_cast<std::uint16_t>(a)));
     data<<','<<cpu.dutyPercent<<','<<gpu.dutyPercent<<','<<cpu.rpm<<','<<gpu.rpm;
     for(int i=0;i<3;++i)data<<','<<static_cast<int>(ec.readStable(static_cast<std::uint16_t>(0x783+i)));
     data<<','<<ec.writeCount()<<'\n';data.flush();
     if(second%5==0)std::printf("%s %ds: %.2f W, CPU EC %d C\n",name,second,watts,temp);
    }
   };
   try {
    native();phase("default_before");
    controller.apply(0,curve,true);waitApplied();phase("manual_custom_on");
    {const mc::EcAccess::Transaction lock(ec);static_cast<void>(ec.updateBitsVerified(0x726,0x80,0));}
    phase("manual_custom_off");
    {const mc::EcAccess::Transaction lock(ec);static_cast<void>(ec.updateBitsVerified(0x726,0x80,0x80));}
    phase("manual_custom_on_again");
    native();phase("default_after");
   } catch(...) {stopLoad();try{native();}catch(...){std::puts("ERROR: native restoration failed; inspect result before continuing");}throw;}
   stopLoad();native();
  }
  static_cast<void>(ec.updateBitsVerified(mc::ec::kAppPresence,1,static_cast<std::uint8_t>(presence)));
  std::ofstream after(output/"after.csv");after<<"address,value\n";for(auto a:{0x751,0x726,0x727,0x741,0x7C5,0x7C6,0x783,0x784,0x785})after<<a<<','<<static_cast<int>(ec.readStable(static_cast<std::uint16_t>(a)))<<'\n';
  std::puts("DONE: load stopped; native cooling restored. See samples.csv, before.csv, after.csv.");return 0;
 }catch(const std::exception&e){std::fprintf(stderr,"STOP: %s\n",e.what());return 1;}
}



