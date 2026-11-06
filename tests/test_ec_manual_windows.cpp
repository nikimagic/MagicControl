#include "MagicControl/EcAccess.h"
#include "MagicControl/EcRegister.h"
#include "MagicControl/FanCurve.h"
#include "MagicControl/Logging.h"
#include "MagicControl/TelemetryGate.h"
#include "../src/platform/windows/FanControlWindows.h"
#include <windows.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>
using namespace std::chrono_literals;
namespace {
int failures{}, checks{};
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::printf("FAIL %d: %s\n",__LINE__,#x); } } while(false)
struct Memory {
 std::recursive_mutex gate; std::array<std::uint8_t,65536> bytes{};
 std::atomic<int> tableWrites{}, temperatureReads{}, totalWrites{}, commitWrites{};
 std::atomic<bool> failOwnership{};
 Memory() {
  bytes[mc::ec::kBoardEcVersion]=82; bytes[mc::ec::kBoardSystemId]=72; bytes[mc::ec::kBoardProjectId]=26;
  bytes[mc::ec::kRamTableCapability]=0x40; bytes[mc::ec::kPerformanceMode]=0xA0;
  bytes[0xF10]=19; bytes[0xF40]=21; // Firmware-owned Down[0] must survive.
 }
};
class Backend final : public mc::IEcBackend {
 std::shared_ptr<Memory> m; mc::BackendInfo info_;
public:
 explicit Backend(std::shared_ptr<Memory> p):m(p){info_.id="windows-acpi-driver";info_.transport=mc::EcTransport::WindowsAcpiDriver;info_.writable=true;info_.maxAddress=0xFFFF;}
 const mc::BackendInfo& info() const noexcept override {return info_;}
 std::uint8_t read(std::uint16_t a) override {std::lock_guard lock(m->gate);return m->bytes[a];}
 void write(std::uint16_t a,std::uint8_t v) override {std::lock_guard lock(m->gate);m->bytes[a]=v;++m->totalWrites;if(a==mc::ec::kRamTableCommit)++m->commitWrites;if(a>=0xF00 && a<0xF60)++m->tableWrites;}
 void lockTransaction() override {m->gate.lock();} void unlockTransaction() noexcept override {m->gate.unlock();}
};
class Owner final : public mc::windows::FanOwnership {public:void restoreService()override{}};
class Environment final : public mc::windows::FanControlEnvironment {
 std::shared_ptr<Memory> m;
public:
 explicit Environment(std::shared_ptr<Memory> p):m(p){}
 mc::MonitoringSnapshot readTemperatures()override {++m->temperatureReads;mc::MonitoringSnapshot s;s.processorSampledAtMs=GetTickCount64();s.processors[0].celsius=45;s.processors[1].celsius=40;return s;}
 std::unique_ptr<mc::windows::FanOwnership> acquireOwnership(bool)override{if(m->failOwnership)throw std::runtime_error("fake ownership failure");return std::make_unique<Owner>();}
};
template<class F> bool wait(F f,int seconds=10){auto until=std::chrono::steady_clock::now()+std::chrono::seconds(seconds);while(!f() && std::chrono::steady_clock::now()<until)std::this_thread::sleep_for(10ms);return f();}
}
int main(int argc,char** argv){try{
 mc::setLoggingEnabled(false);
 const bool barrierOnly=argc==2&&std::string(argv[1])=="--barrier-only";
 const std::vector<mc::FanCurvePoint> points{{32,7},{40,30},{60,100}};
 auto table=mc::buildAutonomousFanTable(points);
 CHECK(table[0]==0 && table[32]==0);CHECK(table[1]==32 && table[17]==29 && table[33]==14);
 CHECK(table[4]==85 && table[20]==82 && table[36]==200);CHECK(table[5]==255 && table[37]==200);
 std::string error;
 CHECK(mc::validateFanCurve({{20,5},{100,100}},error));
 CHECK(mc::validateFanCurve({{20,5},{85,5},{100,5}},error));
 CHECK(!mc::validateFanCurve({{101,100}},error));
 CHECK(!mc::validateFanCurve({{20,4}},error));
 CHECK(!mc::validateFanCurve({{20,101}},error));
 const auto expanded=mc::buildAutonomousFanTable({{20,5},{80,60},{100,60}});
 CHECK(expanded[1]==20 && expanded[33]==10);
 CHECK(expanded[2]==80 && expanded[34]==120);
 CHECK(expanded[3]==85 && expanded[35]==200 && expanded[4]==255);
 const auto protected85=mc::buildAutonomousFanTable({{20,5},{85,5},{100,5}});
 CHECK(protected85[2]==85 && protected85[34]==200 && protected85[3]==255);
 bool rejected=false;try{static_cast<void>(mc::buildAutonomousFanTable({{40,30},{35,40}}));}catch(const std::invalid_argument&){rejected=true;}CHECK(rejected);
 std::vector<mc::FanCurvePoint> tooLong;for(int i=0;i<15;++i)tooLong.push_back({10+i*4,20});
 rejected=false;try{static_cast<void>(mc::buildAutonomousFanTable(tooLong));}catch(const std::invalid_argument&){rejected=true;}CHECK(rejected);
 if(argc==2&&std::string(argv[1])=="--codec-only"){
  std::printf("Fan input codec: %d checks, %d failures (no EC/controller created)\n",checks,failures);return failures?1:0;
 }
 auto memory=std::make_shared<Memory>();mc::EcAccess access(std::make_unique<Backend>(memory));CHECK(access.verifyBoard().verified);
 mc::windows::FanController control(access,std::make_unique<Environment>(memory));
 const auto captureAllowed=[] {try{auto capture=mc::telemetryGate().beginFirmwareCapture({},500ms);return bool(capture);}catch(const std::exception&){return false;}};
 mc::FanCurveTable curve{};curve[0]={32,7};curve[1]={40,30};curve[2]={60,100};control.ecManual(curve,curve);
 CHECK(wait([&]{return control.status().ecManual && !control.status().applying;}));CHECK(!control.status().error);
 CHECK(wait([&]{return !mc::deviceOperations().busy();},5));
 CHECK(captureAllowed()); // EC Manual is autonomous, not a host thermal loop.
 CHECK(access.readStable(0x751)==0xA0);CHECK(access.readStable(0xF10)==19 && access.readStable(0xF40)==21);
 CHECK(access.readStable(0xF00)==0 && access.readStable(0xF20)==0);CHECK(access.readStable(0xF01)==32 && access.readStable(0xF21)==14);
 CHECK(access.readStable(0xF04)==85 && access.readStable(0xF24)==200);
 int writes=memory->tableWrites;std::this_thread::sleep_for(2200ms);CHECK(memory->tableWrites==writes);CHECK(memory->temperatureReads==0);
 bool linkRejected=false;try{control.link(true);}catch(const std::exception&){linkRejected=true;}
 CHECK(linkRejected);CHECK(control.status().ecManual);CHECK(access.readStable(mc::ec::kRamTableCommit)==4);
 CHECK(memory->tableWrites==writes);
 CHECK(!control.status().target[0].has_value()); // No invented current PWM target in autonomous mode.
 if(!barrierOnly){
  const int writesBefore=memory->totalWrites, commitsBefore=memory->commitWrites, tablesBefore=memory->tableWrites;
  {std::lock_guard lock(memory->gate);memory->bytes[mc::ec::kRamTableCommit]=0;}
  CHECK(wait([&]{const auto s=control.status();return s.error&&!s.owned&&!s.applying;},12));
  std::this_thread::sleep_for(1500ms);const auto state=control.status();
  CHECK(state.message.find("извне")!=std::string::npos&&!state.confirmed[0]&&!state.confirmed[1]);
  CHECK(access.readStable(mc::ec::kRamTableCommit)==0);
  CHECK(memory->totalWrites==writesBefore&&memory->commitWrites==commitsBefore&&memory->tableWrites==tablesBefore);
  CHECK(memory->temperatureReads==0);
 }
 auto done=control.nativeAndWait();CHECK(done.wait_for(10s)==std::future_status::ready);done.get();
 CHECK(wait([&]{return !mc::deviceOperations().busy();},5));
 CHECK(!control.status().ecManual);CHECK((access.readStable(0x7C6)&4)==0);
 control.maximum();CHECK(wait([&]{return control.status().maximum&&!control.status().applying;}));CHECK(captureAllowed());
 CHECK(wait([&]{return !mc::deviceOperations().busy();},5));
 auto maxDone=control.nativeAndWait();CHECK(maxDone.wait_for(10s)==std::future_status::ready);maxDone.get();
 CHECK(wait([&]{return !mc::deviceOperations().busy();},5));
 memory->failOwnership=true;control.apply(0,curve,true);CHECK(wait([&]{return control.status().error&&!control.status().applying;}));
 CHECK(wait([&]{return !mc::deviceOperations().busy();},5));
 CHECK(captureAllowed()); // Failed acquisition must not leak a cooling lease.
 memory->failOwnership=false;
 control.ecManual(curve,curve);CHECK(wait([&]{return control.status().ecManual && !control.status().applying;}));
 CHECK(wait([&]{return !mc::deviceOperations().busy();},5));
 control.apply(0,curve,true);CHECK(wait([&]{return !control.status().ecManual && !control.status().applying;}));CHECK(memory->temperatureReads>0);
 CHECK(wait([&]{return !mc::deviceOperations().busy();},5));
 CHECK(!captureAllowed()); // Software-driven FANs must keep temperature sampling.
 auto stop=control.nativeAndWait();stop.get();
 CHECK(captureAllowed());
 std::printf("EC_manual: %d checks, %d failures (simulated EC only; no hardware access)\n",checks,failures);
 return failures?1:0;
}catch(const std::exception& error){std::fprintf(stderr,"EC_manual test exception: %s\n",error.what());return 2;}}
