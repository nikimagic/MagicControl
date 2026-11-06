// Link-time guardrails for the real INFO lifecycle test.  Unexpected inventory,
// EC, BIOS or power calls fail rather than reaching any platform backend.
#include "MagicControl/InfoResourceControl.h"
#include "MagicControl/ActionLog.h"
#include <atomic>
#include <stdexcept>

namespace {
std::atomic<unsigned> infoResults{},infoFinishes{};
[[noreturn]] void forbidden(){throw std::runtime_error("offline VBIOS fixture called an unrelated collector");}
}
unsigned fixtureInfoResults(){return infoResults.load(std::memory_order_acquire);}
unsigned fixtureInfoFinishes(){return infoFinishes.load(std::memory_order_acquire);}

namespace mc {
std::size_t HardwareInventory::rowCount()const noexcept{return 0;}
bool InventoryCache::save(const HardwareInventory&,std::string&){forbidden();}
const IHardwareProvider& defaultHardwareProvider(){forbidden();}
DiagnosticDumpResult createDiagnosticDump(std::shared_ptr<EcAccess>,std::stop_token,MaximumFanProbe,
    std::shared_ptr<std::mutex>,DiagnosticDumpMode,FirmwareFanDefault,std::string,std::filesystem::path){forbidden();}
DiagnosticDumpResult createBiosRomDump(std::stop_token,std::atomic<std::uint64_t>*,std::shared_ptr<TelemetryGate::FirmwarePause>){forbidden();}
DiagnosticDumpResult createPowerInputDump(PowerInputDumpSession&,const PowerInputPrompt&,
    std::stop_token,std::shared_ptr<EcAccess>,std::string){forbidden();}
namespace audit {
Id capture() noexcept{return 42;}
std::string quote(std::string_view){return "\"synthetic\"";}
void related(Id,std::string_view name,std::string_view) noexcept{
    if(name=="info.result")infoResults.fetch_add(1,std::memory_order_release);
}
TaskContext::TaskContext()noexcept=default;
TaskContext::~TaskContext()=default;
Scope::Scope(std::string_view command,std::string_view,Id,bool) noexcept:command_(command){}
Scope::~Scope(){if(command_=="info.vbios_dump")infoFinishes.fetch_add(1,std::memory_order_release);}
void Scope::finish(std::string_view,std::string_view) noexcept{}
void Scope::fail(std::string_view) noexcept{}
}
}
