#pragma once
#include "MagicControl/InfoResourceControl.h"

namespace mc::windows {
class FanController;
// Exact existing controller ownership; factories do not touch hardware.
MaximumFanProbe makeInfoMaximumFanProbe(std::shared_ptr<FanController>);
FirmwareFanDefault makeInfoFirmwareFanDefault(std::shared_ptr<FanController>);
// The only OS-specific INFO workflow: verified preparation, interactive launch
// on the UI apartment, then actual installer completion/version confirmation.
InfoOperationServices infoOperationServices(InfoResourceOperation operation=InfoResourceOperation::PawnIoPrepare);
}
