#pragma once
#include "MagicControl/PawnIoInstall.h"
#include "MagicControl/ResourceCatalog.h"
#include "MagicControl/ResourceProvider.h"
#include <span>
#include <string>
#include <vector>

namespace mc::windows {
struct UniwillBindingEvidence {std::string instance,service,packageVersion;};
ResourceDriverObservation projectPawnIoDriver(const dependencies::PawnIoDriverStatus&);
std::vector<ResourceDriverObservation> projectUniwillBindings(std::span<const UniwillBindingEvidence>,bool complete,std::string status);
// Read-only SCM, installed VERSIONINFO, loaded PawnIO version and PnP metadata.
// Only the official version IOCTL is sent to an already running PawnIO; never
// starts/installs a driver, loads modules, or accesses/authorizes hardware.
std::vector<ResourceDriverObservation> observeResourceDrivers();
class WindowsDriverResourceProvider final:public ResourceProvider {
public:
    WindowsDriverResourceProvider(const BoardDescriptor&,std::function<std::vector<ResourceDriverObservation>()>);
    ResourceCatalog observe() override;
private:
    BoardDescriptor board_;
    std::function<std::vector<ResourceDriverObservation>()> read_;
};
}
