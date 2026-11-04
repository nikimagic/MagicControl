#include "MagicControl/BoardIdentity.h"

#include <chrono>
#include <iterator>
#include <sstream>
#include <string>

#include "MagicControl/DeviceProfile.h"

namespace mc {
namespace {

std::string elapsedMillisecondsSince(std::chrono::steady_clock::time_point start) {
    const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start);
    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(1);
    out << elapsed.count();
    return out.str();
}

}  // namespace

BoardCheck runBoardCheck(EcAccess& access) {
    const auto start = std::chrono::steady_clock::now();
    BoardCheck check;
    check.board = readBoardDescriptor();

    try {
        const auto& profile = deviceProfile(check.board);
        if (!profile.capabilities.anyEcControl() || profile.identity.empty()) {
            check.error = "This board has no verified EC control profile";
        } else if (profile.ecBackendId != access.info().id) {
            check.error = "The device profile does not support this EC transport";
        }
        if (!check.error.empty()) {
            check.elapsedMilliseconds = std::stod(elapsedMillisecondsSince(start));
            return check;
        }
        check.bytes.reserve(profile.identity.size());
        for (const DeviceIdentityProbe& probe : profile.identity) {
            BoardIdentityByte entry;
            entry.address = probe.address;
            entry.name = probe.name;
            entry.expected = probe.expected;
            entry.mask = probe.mask;
            entry.actual = access.readStable(probe.address);
            check.bytes.push_back(entry);
            if (!entry.matches()) {
                check.error = entry.name + ": EC 0x" + toHex(entry.address) + " = " +
                              std::to_string(entry.actual) + ", expected " +
                              std::to_string(entry.expected);
                break;
            }
        }
        check.verified = check.error.empty() && check.bytes.size() == profile.identity.size();
    } catch (const std::exception& error) {
        check.verified = false;
        if (check.error.empty()) check.error = error.what();
    }

    check.elapsedMilliseconds = std::stod(elapsedMillisecondsSince(start));
    return check;
}

std::string describeBoardCheck(const BoardCheck& check) {
    std::ostringstream out;
    out << "board verified=" << (check.verified ? "yes" : "no");
    if (!check.bytes.empty()) {
        out << " [";
        for (std::size_t i = 0; i < check.bytes.size(); ++i) {
            if (i != 0) out << ' ';
            out << "0x" << toHex(check.bytes[i].address) << '='
                << static_cast<int>(check.bytes[i].actual);
        }
        out << ']';
    }
    out << " model=" << (check.board.modelId.empty() ? "unknown" : check.board.modelId);
    out << " board=" << (check.board.baseBoard.empty() ? "unknown" : check.board.baseBoard);
    if (!check.error.empty()) out << " error=" << check.error;
    return out.str();
}

}  // namespace mc
