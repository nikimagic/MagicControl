#include "MagicControl/BiosRomDump.h"
namespace mc {
DiagnosticDumpResult createBiosRomDump(std::stop_token token,std::atomic<std::uint64_t>* progress,std::shared_ptr<TelemetryGate::FirmwarePause> pause){
    if(pause&&!telemetryGate().owns(pause))throw std::invalid_argument("BIOS: foreign telemetry pause");
    if(!pause)pause=telemetryGate().requestFirmwareCapture(token);
    pause->waitForQuiescence(token,std::chrono::seconds(10));
    auto operation=deviceOperations().tryBeginFirmware(pause);
    if(!operation)throw std::runtime_error("Device operation busy; BIOS reader not started");
    auto session=std::make_shared<BiosCaptureSession>(BiosCaptureSession{std::move(pause),std::move(operation)});
    return createPlatformBiosRomDump(token,progress,session);
}
}
