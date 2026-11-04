#pragma once
// File-only validation. Never opens hardware, pads a region or repairs bytes.
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace mc::diagnostics::bios {
inline std::uint32_t word(std::span<const char> bytes, std::size_t at) {
    if (at > bytes.size() || bytes.size() - at < 4) throw std::runtime_error("Truncated BIOS header");
    const auto* p = reinterpret_cast<const unsigned char*>(bytes.data() + at);
    return p[0] | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}
inline bool hasData(std::span<const char> bytes) {
    return std::any_of(bytes.begin(), bytes.end(), [](unsigned char c) { return c != 0 && c != 255; });
}
// File-only recognition of the reviewed 16-bit reset stubs. GM6 stock and
// patched images start90 90 E9..., so checking only the first two bytes rejects
// complete valid captures. Preserve the existing opcode0/1 admission, and also
// allow leading single-byte NOPs; require a whole E9 rel16 or EA ptr16:16
// instruction inside the final16bytes. This is a limited
// format heuristic, not x86 emulation or proof that the firmware boots.
inline bool resetJump(std::span<const char> bytes) {
    if (bytes.size() < 16) return false;
    const auto stub = bytes.last(16);
    const auto wholeJump = [&](std::size_t at) {
        if (at >= stub.size()) return false;
        const auto opcode = static_cast<unsigned char>(stub[at]);
        return (opcode == 0xe9 && stub.size() - at >= 3) || (opcode == 0xea && stub.size() - at >= 5);
    };
    if (wholeJump(0) || wholeJump(1)) return true;
    std::size_t at{};
    while (at < stub.size() && static_cast<unsigned char>(stub[at]) == 0x90) ++at;
    return wholeJump(at);
}
inline unsigned firmwareVolumes(std::span<const char> bytes) {
    unsigned count{};
    for (std::size_t at = 0; at + 56 <= bytes.size(); at += 8) {
        if (word(bytes, at + 40) != 0x4856465f) continue;
        const std::uint64_t length = word(bytes, at + 32) | (std::uint64_t(word(bytes, at + 36)) << 32);
        const auto header = word(bytes, at + 48) & 0xffff;
        if (header < 56 || header > 4096 || (header & 1) || length < header || length > bytes.size() - at) continue;
        unsigned sum{};
        for (unsigned i = 0; i < header; i += 2)
            sum += static_cast<unsigned char>(bytes[at + i]) | (unsigned(static_cast<unsigned char>(bytes[at + i + 1])) << 8);
        if (sum & 0xffff) throw std::runtime_error("UEFI FV header checksum invalid at " + std::to_string(at));
        ++count;
    }
    return count;
}
struct Region {
    unsigned index{};
    bool enabled{}, hostRead{}, nonblank{};
    std::uint64_t base{}, end{}; // Inclusive.
};
struct Shape {
    std::string kind;
    unsigned firmwareVolumes{};
    std::uint64_t descriptorBytes{};
    unsigned components{};
    std::uint32_t hostMaster{};
    std::vector<Region> regions;
};
// Exact LPC ranges in the reviewed FPT16.1.27.2181 internal platform table.
// All31 range pairs match16.1.25.1871; no new host is admitted by this update.
// Source admission only; FPT -I must additionally accept the actual system.
inline bool reviewedFpt16Lpc(std::uint16_t id) {
    return (id >= 0x7a00 && id <= 0x7aff) || (id >= 0x5180 && id <= 0x51ff) ||
        (id >= 0x5480 && id <= 0x54ff);
}
// Only the owner-requested Ultra275HX profiles. The caller obtains profileId
// from the actual SMBIOS through deviceProfile(), never the UI preview.
inline bool fpt19Ultra275Profile(std::string_view profileId, std::string_view cpuName) {
    if (profileId != "eluktronics-hydroc16g2" && profileId != "mechrevo-yaoshi-series-x6ar55xy") return false;
    std::string normalized;
    for (unsigned char c : cpuName) if (std::isalnum(c)) normalized += static_cast<char>(std::toupper(c));
    return normalized.ends_with("ULTRA9275HX");
}
inline bool reviewedFpt19Lpc(std::uint16_t id) {
    // Reviewed 19.0.5.1948 records: 7F00..7F7F (internal1, retains Intel's
    // "Meteor Lake Platform" label) and7700..777F (Arrow Lake Platform).
    // Do not admit the other table entries just because the tool lists them.
    return (id >= 0x7f00 && id <= 0x7f7f) || (id >= 0x7700 && id <= 0x777f);
}
inline Shape validate(std::span<const char> bytes, std::uint64_t smbiosBytes, bool intelFpt) {
    if (bytes.size() < 1024 * 1024 || bytes.size() > 64 * 1024 * 1024 || !hasData(bytes))
        throw std::runtime_error("BIOS image is empty, outside1–64MiB or contains only00/FF");
    if (!intelFpt) {
        if (!smbiosBytes || bytes.size() != smbiosBytes)
            throw std::runtime_error("AFU size differs from SMBIOS; declared ROM image not complete");
        return {"declared_rom_image"}; // Does not claim a separately proven physical SPI layout.
    }
    if (word(bytes, 16) != 0x0ff0a55a)
        throw std::runtime_error("Intel: Flash Descriptor missing; BIOS region alone is not full SPI");
    const auto map0 = word(bytes, 20), map1 = word(bytes, 24);
    const auto fcba = (map0 & 255) * 16, frba = ((map0 >> 16) & 255) * 16, fmba = (map1 & 255) * 16;
    if (fcba < 32 || fcba + 12 > 4096 || frba < 32 || frba + 64 > 4096 || fmba < 32 || fmba + 4 > 4096)
        throw std::runtime_error("Intel descriptor table offsets invalid");
    Shape shape{"intel_spi"};
    shape.components = ((map0 >> 8) & 3) + 1;
    if (shape.components > 2) throw std::runtime_error("Unsupported descriptor component count");
    const auto component = word(bytes, fcba);
    for (unsigned n = 0; n < shape.components; ++n) {
        const auto density = (component >> (n * 4)) & 15;
        if (density > 7) throw std::runtime_error("Unknown flash component density");
        shape.descriptorBytes += (512ull * 1024) << density;
    }
    if (bytes.size() != shape.descriptorBytes)
        throw std::runtime_error("FPT size differs from Descriptor flash capacity; full SPI not confirmed");
    shape.hostMaster = word(bytes, fmba);
    std::vector<Region> enabled;
    for (unsigned n = 0; n < 16; ++n) {
        const auto value = word(bytes, frba + n * 4);
        Region region;
        region.index = n;
        region.base = std::uint64_t(value & 0x7fff) << 12;
        region.end = (std::uint64_t((value >> 16) & 0x7fff) << 12) | 4095;
        region.enabled = region.end >= region.base;
        // Reviewed IFD2 region0..11 read bits8..19. Extended read-bit
        // layouts are not admitted by this backend; keep enabled12..15
        // visible in evidence but refuse to declare such a capture complete.
        region.hostRead = n < 12 && (shape.hostMaster & (1u << (n + 8))) != 0;
        if (region.enabled) {
            if (region.end >= bytes.size()) throw std::runtime_error("Intel region outside captured image: " + std::to_string(n));
            region.nonblank = hasData(bytes.subspan(static_cast<std::size_t>(region.base), static_cast<std::size_t>(region.end - region.base + 1)));
            enabled.push_back(region);
        } else if (n < 3) throw std::runtime_error("Required Descriptor/BIOS/ME region disabled");
        shape.regions.push_back(region);
    }
    if (shape.regions[0].base != 0 || shape.regions[0].end != 4095)
        throw std::runtime_error("Intel Descriptor region extent invalid");
    std::sort(enabled.begin(), enabled.end(), [](const auto& a, const auto& b) { return a.base < b.base; });
    std::uint64_t next{};
    for (const auto& region : enabled) {
        if (region.base != next) throw std::runtime_error("SPI regions overlap or leave unverified gaps");
        next = region.end + 1;
    }
    if (next != bytes.size()) throw std::runtime_error("SPI region map does not cover flash capacity");
    for (unsigned n : {0u, 1u, 2u, 3u})
        if (shape.regions[n].enabled && !shape.regions[n].nonblank)
            throw std::runtime_error("Required Intel region contains only00/FF: " + std::to_string(n));
    const auto& meRegion = shape.regions[2];
    const auto me = bytes.subspan(static_cast<std::size_t>(meRegion.base), static_cast<std::size_t>(meRegion.end - meRegion.base + 1));
    const auto meHeader = me.first(std::min<std::size_t>(me.size(), 65536));
    const auto contains = [&](const std::string& tag) { return std::search(meHeader.begin(), meHeader.end(), tag.begin(), tag.end()) != meHeader.end(); };
    if (!contains("$FPT") && !contains("$CPD")) throw std::runtime_error("Intel ME partition header missing");
    const auto& bios = shape.regions[1];
    const auto uefi = bytes.subspan(static_cast<std::size_t>(bios.base), static_cast<std::size_t>(bios.end - bios.base + 1));
    shape.firmwareVolumes = firmwareVolumes(uefi);
    // Two FV and this reset form are this decoder's conservative admission
    // policy. Refusal means unsupported/unconfirmed, not damaged firmware.
    if (shape.firmwareVolumes < 2 || !resetJump(uefi))
        throw std::runtime_error("Intel BIOS lacks checked UEFI FV headers or reset jump");
    return shape;
}
inline bool allRegionsReadable(const Shape& shape) {
    return std::all_of(shape.regions.begin(), shape.regions.end(), [](const auto& r) { return !r.enabled || r.hostRead; });
}
}
