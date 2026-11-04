#include "MagicControl/DeviceProfile.h"
#include "MagicControl/EcAccess.h"
#include <vector>
#include <algorithm>

namespace mc::devices {
const DeviceProfile& x5sp4nagProfile() noexcept;
const DeviceProfile& unknownProfile() noexcept;
const DeviceProfile& hlylProfile() noexcept;
const DeviceProfile& gm6ir0cProfile() noexcept;
const DeviceProfile& hydroc16g2Profile() noexcept;
const DeviceProfile& l16Profile() noexcept;
const DeviceProfile& hpbsdProfile() noexcept;
const DeviceProfile& legionKwcnProfile() noexcept;
std::span<const DeviceProfile* const> reportedMechrevoProfiles() noexcept;
}
namespace mc {
std::span<const DeviceProfile* const> deviceProfiles() noexcept {
    static const auto profiles=[] {
        std::vector<const DeviceProfile*> values{&devices::x5sp4nagProfile(),&devices::hlylProfile(),&devices::gm6ir0cProfile(),&devices::hydroc16g2Profile()};
        const auto reported=devices::reportedMechrevoProfiles();
        values.insert(values.end(),reported.begin(),reported.end());
        values.push_back(&devices::l16Profile());
        values.push_back(&devices::hpbsdProfile());
        values.push_back(&devices::legionKwcnProfile());
        return values;
    }();
    return profiles;
}
const RamFanProfileData* ramFanDataForProfile(std::string_view id) noexcept {
    for(const auto* profile:deviceProfiles())if(profile&&profile->id==id)return profile->ramFanData;
    return nullptr;
}
const DeviceProfile& deviceProfile(const BoardDescriptor& board) noexcept {
    // Exact SMBIOS matching: never infer register compatibility from a marketing
    // name, a shared CPU, ProjectID, installed driver or a substring match.
    for(const auto* profile:deviceProfiles())
        if(profile && board.manufacturer==profile->manufacturer && board.baseBoard==profile->baseBoard &&
            (profile->productName.empty()||board.productName==profile->productName||
             std::find(profile->productVariants.begin(),profile->productVariants.end(),board.productName)!=profile->productVariants.end()))
            return *profile;
    return devices::unknownProfile();
}
void identifyDevice(BoardDescriptor& board) { board.modelId=deviceProfile(board).id; }
}
