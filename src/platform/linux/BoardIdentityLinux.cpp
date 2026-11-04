#include "MagicControl/BoardIdentity.h"
#include "MagicControl/DeviceProfile.h"

#include <algorithm>
#include <cctype>
#include <fstream>

namespace mc {
namespace {
std::string readFirstLine(const char* path) {
    std::ifstream file(path);
    std::string value;
    std::getline(file, value);
    return value;
}
std::string trim(std::string value) {
    const auto space = [](unsigned char c) { return std::isspace(c) != 0; };
    value.erase(value.begin(), std::find_if_not(value.begin(), value.end(), space));
    value.erase(std::find_if_not(value.rbegin(), value.rend(), space).base(), value.end());
    return value;
}
}

BoardDescriptor readBoardDescriptor() {
    BoardDescriptor descriptor;
    descriptor.manufacturer = trim(readFirstLine("/sys/class/dmi/id/sys_vendor"));
    descriptor.baseBoard = trim(readFirstLine("/sys/class/dmi/id/board_name"));
    descriptor.productName = trim(readFirstLine("/sys/class/dmi/id/product_name"));
    identifyDevice(descriptor);
    return descriptor;
}
}
