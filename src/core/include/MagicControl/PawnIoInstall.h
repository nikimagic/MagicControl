#pragma once
#include <filesystem>
#include <cstdint>
#include <stop_token>
#include <string>
#include <array>
#include <optional>
#include <functional>
#include <string_view>

namespace mc::dependencies {
struct PawnIoDriverStatus {
    bool installed{}, running{};
    std::uint32_t serviceState{}, win32ExitCode{}, serviceExitCode{};
    std::uint32_t serviceType{}, startType{};
    std::wstring binaryPath;
    std::wstring resolvedBinaryPath;
    std::string fileVersion,versionStatus;
    std::optional<std::array<unsigned,4>> fileVersionNumbers;
    // Keep file metadata, the loaded kernel's reply and the bundled installer
    // separate. PawnIOLib's pawnio_version() is the DLL ABI, NOT this driver.
    std::string loadedVersion,loadedVersionStatus;
    std::optional<std::uint32_t> loadedVersionRaw;
    std::string_view version() const {return loadedVersion.empty()?fileVersion:loadedVersion;}
    std::string_view versionSource() const {
        return !loadedVersion.empty()?"loaded driver IOCTL_PIO_VERSION":
            !fileVersion.empty()?"installed SYS VERSIONINFO":"unavailable";
    }
};
enum class PawnIoVersionQuery { FileMetadataOnly, LoadedDriver };
// Default remains passive SCM/file metadata for installation decisions. Explicit
// LoadedDriver adds only the official version IOCTL on an already running driver:
// no module load/execute, service start, EC/port/MSR access or installation.
PawnIoDriverStatus pawnIoDriverStatus(PawnIoVersionQuery query=PawnIoVersionQuery::FileMetadataOnly);
// Passive SCM and installed SYS VERSIONINFO. This is not a loaded-image query.
PawnIoDriverStatus lenovoEnergyDriverStatus();
// A stopped driver still counts as installed. No start/open device IO.
bool pawnIoInstalled();
inline constexpr char bundledPawnIoVersion[]="2.2.0";
inline constexpr std::array<unsigned,4> bundledPawnIoVersionNumbers{2,2,0,0};
enum class PawnIoSetupAction { Install, Upgrade, Repair, Current, UnknownVersion };
// One decision for the button, click guard and final pre-launch recheck.
inline PawnIoSetupAction pawnIoSetupAction(const PawnIoDriverStatus& driver){
    if(!driver.installed)return PawnIoSetupAction::Install;
    // Compare the newest proven file/loaded version. In particular, missing
    // VERSIONINFO must not disable an upgrade when the kernel answers. An old
    // loaded image must not downgrade a newer SYS awaiting restart (or vice versa).
    auto version=driver.fileVersionNumbers;
    if(driver.loadedVersionRaw){const auto packed=*driver.loadedVersionRaw;
        const std::array<unsigned,4> loaded{packed>>16,(packed>>8)&0xff,packed&0xff,0};
        if(!version||*version<loaded)version=loaded;
    }
    if(!version)return PawnIoSetupAction::UnknownVersion;
    if(*version<bundledPawnIoVersionNumbers)return PawnIoSetupAction::Upgrade;
    if(*version==bundledPawnIoVersionNumbers&&!driver.running)return PawnIoSetupAction::Repair;
    return PawnIoSetupAction::Current;
}
PawnIoSetupAction pawnIoSetupAction();

// Windows-only explicit setup. Preparation downloads and verifies without running
// the installer. Launch opens the normal official wizard; no silent parameters.
std::filesystem::path preparePawnIoInstaller(std::stop_token stop = {});
void launchPawnIoInstaller(const std::filesystem::path& installer,std::function<void(unsigned)> onExit={});
std::filesystem::path prepareLenovoEnergyInstaller(std::stop_token stop = {});
void launchLenovoEnergyInstaller(const std::filesystem::path&,std::function<void(unsigned)> onExit={});
}
