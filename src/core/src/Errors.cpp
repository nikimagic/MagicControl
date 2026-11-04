#include "MagicControl/Errors.h"

#include <array>

namespace mc {
namespace {

/// Shared hex formatter. Kept allocation-light: EC errors are raised on hot
/// paths, and a formatting helper must never be the reason a fan update stalls.
std::string formatHex(std::uint32_t value) {
    std::array<char, 9> digits{};
    constexpr char kDigits[] = "0123456789ABCDEF";

    int length = 0;
    if (value == 0) {
        digits[length] = '0';
        ++length;
    } else {
        std::array<char, 8> reversed{};
        while (value != 0 && length < 8) {
            reversed[static_cast<std::size_t>(length)] = kDigits[value & 0xF];
            ++length;
            value >>= 4;
        }
        for (int i = 0; i < length; ++i) {
            digits[static_cast<std::size_t>(i)] = reversed[static_cast<std::size_t>(length - 1 - i)];
        }
    }
    return std::string(digits.data(), static_cast<std::size_t>(length));
}

}  // namespace

std::string toHex(std::uint32_t value) { return formatHex(value); }

std::string WriteNotConfirmed::toHex(std::uint32_t value) { return formatHex(value); }

std::string AddressNotReachable::toHex(std::uint32_t value) { return formatHex(value); }

}  // namespace mc
