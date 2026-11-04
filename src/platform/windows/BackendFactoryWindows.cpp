#include "MagicControl/IEcBackend.h"
#include "MagicControl/BoardIdentity.h"
#include "MagicControl/DeviceProfile.h"
#include "MagicControl/Errors.h"
#include "MagicControl/LaptopControl.h"
#include "MagicControl/ControlAudit.h"
#include "../../devices/minisforum/hpbsd/PowerControlWindows.h"
#include "../../devices/lenovo/legion-kwcn/ControlWindows.h"
#include <windows.h>

namespace mc {
std::unique_ptr<LaptopControl> createHlylControl();
std::unique_ptr<LaptopControl> createLaptopControl(){
    if(wcsstr(GetCommandLineW(),L"--no-ec"))return {};
    const auto& profile=deviceProfile(readBoardDescriptor());
    if(profile.id=="huawei-hlyl-wxx9")return auditedLaptopControl(createHlylControl());
    if(profile.id=="minisforum-hpbsd")return auditedLaptopControl(windows::createHpbsdPowerControl());
    if(profile.id=="lenovo-legion-kwcn")return auditedLaptopControl(windows::createLegionControl());
    return {};
}
std::unique_ptr<IEcBackend> createDefaultBackend() {
    const auto& profile=deviceProfile(readBoardDescriptor());
    // GM6IR0C owns a typed controller. Never expose it to legacy X5 startup
    // restoration, battery, keyboard or fan handlers through MainWindow::ec_.
    if(profile.ecBackendId=="windows-acpi-driver" && !gmRamFanProfile(profile.id)) return createWindowsAcpiDriverBackend();
    throw BackendUnavailable("No verified EC transport for this laptop",
        "System monitoring remains available. This board needs its own verified device profile.");
}
}
