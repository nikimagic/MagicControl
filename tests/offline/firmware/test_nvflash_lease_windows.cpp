#include "NvflashPlatform.h"
#include <iostream>
int main(){
    // Kernel semaphore only: no NVML object, native preflight, service or reader.
    auto owner=mc::diagnostics::nvflash::tryTelemetryLease();
    if(!owner){std::cerr<<"Fixture requires idle capture semaphore\n";return 1;}
    if(mc::diagnostics::nvflash::tryTelemetryLease()){std::cerr<<"Concurrent GPU access admitted\n";return 1;}
    owner.reset();
    if(!mc::diagnostics::nvflash::tryTelemetryLease()){std::cerr<<"GPU access not released\n";return 1;}
    std::cout<<"PASS shared capture/telemetry semaphore; no vendor/hardware operations\n";
}
