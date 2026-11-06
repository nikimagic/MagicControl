#pragma once
#include "Probe.h"
#include <array>
namespace probe {
// SMBIOS type 17 summary. Every byte remains available in the original DMI blob.
inline Json memoryModulesFromDmi(const std::string &data) {
    Json result = Json::array();
    std::size_t offset{};
    for (unsigned record = 0; record < 4096 && offset + 4 <= data.size(); ++record) {
        const auto byte = [&](std::size_t p) { return static_cast<unsigned char>(data[p]); };
        const unsigned type = byte(offset), length = byte(offset + 1);
        if (length < 4 || offset + length + 2 > data.size())
            break;
        std::size_t end = offset + length;
        while (end + 1 < data.size() && (data[end] != 0 || data[end + 1] != 0))
            ++end;
        if (end + 1 >= data.size())
            break;
        std::vector<std::string> strings;
        for (std::size_t at = offset + length; at < end;) {
            const auto next = data.find('\0', at);
            const auto stop = next == std::string::npos || next > end ? end : next;
            strings.push_back(data.substr(at, stop - at));
            at = stop + 1;
        }
        const auto word = [&](unsigned p) -> std::optional<unsigned> {
            if (p + 2 > length)
                return {};
            return byte(offset + p) | (byte(offset + p + 1) << 8);
        };
        const auto dword = [&](unsigned p) -> std::optional<std::uint32_t> {
            if (p + 4 > length)
                return {};
            std::uint32_t value{};
            for (unsigned i = 0; i < 4; ++i)
                value |= static_cast<std::uint32_t>(byte(offset + p + i)) << (8 * i);
            return value;
        };
        const auto text = [&](unsigned p) -> std::string {
            if (p >= length)
                return {};
            const auto index = byte(offset + p);
            return index && index <= strings.size() ? strings[index - 1] : std::string{};
        };
        if (type == 17) {
            const auto size = word(12);
            Json capacity, present;
            if (size && *size != 0xFFFF) {
                present = *size != 0;
                if (*size == 0x7FFF) {
                    const auto ext = dword(28);
                    if (ext)
                        capacity = static_cast<std::uint64_t>(*ext & 0x7FFFFFFF) * 1024 * 1024;
                } else
                    capacity = static_cast<std::uint64_t>(*size & 0x7FFF) *
                               ((*size & 0x8000) ? 1024ull : 1024ull * 1024ull);
            }
            Json item{{"source", "SMBIOS type 17"},
                      {"handle", word(2) ? Json(*word(2)) : Json{}},
                      {"present", present},
                      {"capacity_bytes", capacity},
                      {"device_locator", text(16)},
                      {"bank_locator", text(17)},
                      {"manufacturer", text(23)},
                      {"serial_number", text(24)},
                      {"part_number", text(26)},
                      {"memory_type_raw", length > 18 ? Json(byte(offset + 18)) : Json{}},
                      {"speed_reported_raw", word(21) ? Json(*word(21)) : Json{}},
                      {"configured_speed_reported_raw", word(32) ? Json(*word(32)) : Json{}},
                      {"formatted_hex", bytesHex(data.data() + offset, length)},
                      {"comment", "Firmware inventory, not live clock or temperature; raw speed fields use "
                                  "the applicable SMBIOS version"}};
            for (const auto &field : std::array<std::pair<const char *, unsigned>, 3>{
                     {{"minimum_voltage_V", 34}, {"maximum_voltage_V", 36}, {"configured_voltage_V", 38}}}) {
                const auto v = word(field.second);
                item[field.first] = v && *v && *v != 0xFFFF ? Json(*v / 1000.0) : Json{};
            }
            result.push(std::move(item));
        }
        offset = end + 2;
        if (type == 127)
            break;
    }
    return result;
}
} // namespace probe
