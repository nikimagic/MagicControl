// MagicControl - unit tests for logic that does not need hardware.
//
// The register map is the most safety critical part of this project: a wrong
// mask means a wrong write to firmware we do not own. These tests pin down the
// encodings recovered from the verified C# implementation so a later edit
// cannot quietly change them.

#include <cstdint>
#include <exception>
#include <optional>
#include <cstdio>
#include <string>
#include <vector>

#include "TestSupport.h"
#include "MagicControl/BoardIdentity.h"
#include "MagicControl/HardwareInventory.h"
#include "MagicControl/EcRegister.h"
#include "MagicControl/Errors.h"
#include "MagicControl/Types.h"

namespace mc::tests {

// Shared by every test translation unit, so one binary prints one summary.
int& checkCount() noexcept {
    static int value = 0;
    return value;
}

int& failureCount() noexcept {
    static int value = 0;
    return value;
}

}  // namespace mc::tests

namespace {

// ---------------------------------------------------------------------------
// Board identity
// ---------------------------------------------------------------------------

void testBoardIdentityValues() {
    // The gate that protects every write. If these drift, the app would accept
    // a board it was never verified against.
    CHECK(mc::ec::kBoardEcVersionExpected == 82);
    CHECK(mc::ec::kBoardSystemIdExpected == 72);
    CHECK(mc::ec::kBoardProjectIdExpected == 26);

    CHECK(mc::ec::kBoardEcVersion == 0x454);
    CHECK(mc::ec::kBoardSystemId == 0x456);
    CHECK(mc::ec::kBoardProjectId == 0x740);
}

// ---------------------------------------------------------------------------
// Performance mode decoding
// ---------------------------------------------------------------------------

/// Mirrors the decoder in EcPerformanceMode from the C# reference.
std::uint8_t presetOf(std::uint8_t raw) { return raw & mc::ec::kPerformancePresetMask; }

bool allowsSoftwareControl(std::uint8_t raw) {
    return raw == 0x00 || raw == 0x10 || raw == 0x80 || raw == 0xA0;
}

void testPerformanceModeDecoding() {
    CHECK(presetOf(mc::ec::kPerformanceOffice) == 0x80);
    CHECK(presetOf(mc::ec::kPerformanceBalanced) == 0x00);
    CHECK(presetOf(mc::ec::kPerformanceTurbo) == 0x10);

    // Fan boost is bit 6 and must never be mistaken for a preset bit.
    CHECK((0x40 & mc::ec::kPerformancePresetMask) == 0);
    CHECK((0x40 & mc::ec::kPerformanceBoostBit) != 0);

    CHECK(allowsSoftwareControl(0x00));
    CHECK(allowsSoftwareControl(0x10));
    CHECK(allowsSoftwareControl(0x80));
    CHECK(allowsSoftwareControl(0xA0));

    // Fan boost blocks software curves, so a boosted mode must be refused.
    CHECK(!allowsSoftwareControl(0x40));
    CHECK(!allowsSoftwareControl(0x50));
    // Undocumented lower-bit combinations stay refused on purpose.
    CHECK(!allowsSoftwareControl(0x20));
    CHECK(!allowsSoftwareControl(0x01));
}

// ---------------------------------------------------------------------------
// Keyboard backlight encoding
// ---------------------------------------------------------------------------

/// Rebuilds the byte the EC expects for a backlight level.
std::uint8_t keyboardLevelBits(int level) {
    return static_cast<std::uint8_t>((level << 5) | (level == 0 ? 2 : 0));
}

void testKeyboardLevelEncoding() {
    CHECK(mc::ec::kKeyboardMask == 0xE2);
    CHECK(mc::ec::kKeyboardStrobe == 0x10);
    CHECK(mc::ec::kKeyboardLevelMax == 4);

    // Level 0 means off, and the EC expects the marker bit 1 in that case.
    CHECK(keyboardLevelBits(0) == 0x02);

    // Levels 1..4 sit in bits 7:5 with no marker bit.
    CHECK(keyboardLevelBits(1) == 0x20);
    CHECK(keyboardLevelBits(2) == 0x40);
    CHECK(keyboardLevelBits(3) == 0x60);
    CHECK(keyboardLevelBits(4) == 0x80);

    // Every encoded value must survive the mask the write uses, and the strobe
    // bit must stay outside that mask so it is never read back as a setting.
    for (int level = 0; level <= mc::ec::kKeyboardLevelMax; ++level) {
        const std::uint8_t bits = keyboardLevelBits(level);
        CHECK((bits & mc::ec::kKeyboardMask) == bits);
        CHECK((mc::ec::kKeyboardStrobe & mc::ec::kKeyboardMask) == 0);
    }
}

// ---------------------------------------------------------------------------
// Battery charge limit encoding
// ---------------------------------------------------------------------------

void testBatteryProfileEncoding() {
    constexpr std::uint8_t kRaw100 = 0;
    constexpr std::uint8_t kRaw95 = 1;
    constexpr std::uint8_t kRaw85 = 2;

    // The profile lives in bits 5:4, so each preset is one shift left.
    CHECK((static_cast<std::uint8_t>(kRaw100 << mc::ec::kBatteryProfileShift) &
           mc::ec::kBatteryProfileMask) == 0x00);
    CHECK((static_cast<std::uint8_t>(kRaw95 << mc::ec::kBatteryProfileShift) &
           mc::ec::kBatteryProfileMask) == 0x10);
    CHECK((static_cast<std::uint8_t>(kRaw85 << mc::ec::kBatteryProfileShift) &
           mc::ec::kBatteryProfileMask) == 0x20);

    // Decoding back must recover the preset index.
    for (std::uint8_t preset = 0; preset <= 2; ++preset) {
        const std::uint8_t raw =
            static_cast<std::uint8_t>(preset << mc::ec::kBatteryProfileShift);
        CHECK(((raw & mc::ec::kBatteryProfileMask) >> mc::ec::kBatteryProfileShift) == preset);
    }
}

// ---------------------------------------------------------------------------
// Fan curve table layout
// ---------------------------------------------------------------------------

void testFanCurveTableLayout() {
    // CPU temperatures 0xF00..0xF0F, duty cycles 0xF20..0xF2F.
    // GPU is the same block shifted by 0x30.
    CHECK(mc::ec::kCpuTableBase == 0xF00);
    CHECK(mc::ec::kGpuTableBase == 0xF30);
    CHECK(mc::ec::kTableStride == mc::ec::kGpuTableBase - mc::ec::kCpuTableBase);
    CHECK(mc::ec::kTablePointCount == 16);
    CHECK(mc::ec::kTableDutyOffset == 0x20);
    CHECK(mc::ec::kTableAuxOffset == 0x10);

    // The duty array must stay inside its own table.
    CHECK(mc::ec::kTableDutyOffset + mc::ec::kTablePointCount <= mc::ec::kTableStride);

    // Both tables plus their trailing status bytes fit well inside the 16-bit
    // EC address space the Windows driver accepts.
    CHECK(mc::ec::kTableEnd <= mc::ec::kMaxAddress16);
    CHECK(mc::ec::kNativeControl < mc::ec::kTableEnd);
    CHECK(mc::ec::kNativeControl > mc::ec::kGpuTableBase + mc::ec::kTableDutyOffset);

    // Every address this build writes fits a 16-bit EC address.
    CHECK(mc::ec::kMinWritable < mc::ec::kMaxWritable);
    CHECK(mc::ec::kMaxWritable <= mc::ec::kMaxAddress16);
}

/// The ec_sys debugfs driver caps its window at 256 bytes, which means none of
/// this project's registers are reachable through it.
///
/// This test exists because an earlier version of the register map claimed the
/// opposite, and the claim was reported to the user as fact. Pinning the real
/// arithmetic here means the mistake cannot be repeated silently: if a future
/// change moves a register below 0x100, or if the Linux transport changes, this
/// test fails and forces the documentation to be updated with it.
void testEcSysWindowDoesNotCoverOurRegisters() {
    CHECK(mc::ec::kLinuxEcWindowSize == 0x100);

    CHECK(!mc::ec::reachableViaEcSysWindow(mc::ec::kBoardEcVersion));
    CHECK(!mc::ec::reachableViaEcSysWindow(mc::ec::kCpuTemperature));
    CHECK(!mc::ec::reachableViaEcSysWindow(mc::ec::kCpuRpmHigh));
    CHECK(!mc::ec::reachableViaEcSysWindow(mc::ec::kGpuRpmHigh));
    CHECK(!mc::ec::reachableViaEcSysWindow(mc::ec::kCpuPwm));
    CHECK(!mc::ec::reachableViaEcSysWindow(mc::ec::kGpuPwm));
    CHECK(!mc::ec::reachableViaEcSysWindow(mc::ec::kPerformanceMode));
    CHECK(!mc::ec::reachableViaEcSysWindow(mc::ec::kKeyboardBlue));
    CHECK(!mc::ec::reachableViaEcSysWindow(mc::ec::kMinWritable));
    CHECK(!mc::ec::reachableViaEcSysWindow(mc::ec::kMaxWritable));
    CHECK(!mc::ec::reachableViaEcSysWindow(mc::ec::kCpuTableBase));

    // The lowest register in the whole map is still far above the window.
    CHECK(mc::ec::kMinWritable > mc::ec::kLinuxEcWindowSize);

    // The helper itself must still answer correctly at the boundary.
    CHECK(mc::ec::reachableViaEcSysWindow(0x000));
    CHECK(mc::ec::reachableViaEcSysWindow(0x0FF));
    CHECK(!mc::ec::reachableViaEcSysWindow(0x100));
    CHECK(!mc::ec::reachableViaEcSysWindow(0x101));
}

// ---------------------------------------------------------------------------
// Fan telemetry decoding
// ---------------------------------------------------------------------------

void testFanFeedbackAddresses() {
    // CPU and GPU must never share an address, or one fan would report the
    // other fan's readings.
    CHECK(mc::ec::kCpuRpmHigh != mc::ec::kGpuRpmHigh);
    CHECK(mc::ec::kCpuRpmLow != mc::ec::kGpuRpmLow);
    CHECK(mc::ec::kCpuPwm != mc::ec::kGpuPwm);

    // The tachometer halves must be adjacent, because the reader samples the
    // high byte before and after the low byte to detect a rollover.
    CHECK(mc::ec::kCpuRpmLow == mc::ec::kCpuRpmHigh + 1);
    CHECK(mc::ec::kGpuRpmLow == mc::ec::kGpuRpmHigh + 1);

    // 0x46B was proven NOT to be the second rotor's low byte.
    CHECK(mc::ec::kUnknown46B != mc::ec::kGpuRpmLow);
    CHECK(mc::ec::kUnknown46B != mc::ec::kGpuPwm);
}

void testRpmAssembly() {
    const auto rpm = [](std::uint8_t high, std::uint8_t low) {
        return (static_cast<int>(high) << 8) | static_cast<int>(low);
    };
    CHECK(rpm(0x00, 0x00) == 0);
    CHECK(rpm(0x03, 0xE8) == 1000);
    CHECK(rpm(0x0A, 0x16) == 2582);  // The value the C# notes recorded.
    CHECK(rpm(0xFF, 0xFF) == 65535);
}

void testDutyConversion() {
    // The EC stores duty as raw / 2, so a raw byte maps to 0..100 percent.
    const auto percent = [](std::uint8_t raw) { return static_cast<double>(raw) / 2.0; };
    CHECK(percent(0) == 0.0);
    CHECK(percent(20) == 10.0);   // The reading visible in the reference screenshot.
    CHECK(percent(200) == 100.0);

    // Raw values above 200 are treated as "not a duty value" by the reader and
    // make the sample be discarded.
    CHECK(!(201 <= 200));
}

// ---------------------------------------------------------------------------
// Error formatting
// ---------------------------------------------------------------------------

void testHexFormatting() {
    CHECK(mc::toHex(0) == "0");
    CHECK(mc::toHex(0x1A) == "1A");
    CHECK(mc::toHex(0x43E) == "43E");
    CHECK(mc::toHex(0xF5F) == "F5F");
    CHECK(mc::toHex(0xFFFF) == "FFFF");

    // Messages must stay uppercase so they match the log format of the
    // reference implementation.
    const mc::WriteNotConfirmed error(0x75B, 0x80, 0x00);
    CHECK(error.requested() == 0x80);
    CHECK(error.actual() == 0x00);
    const std::string message = error.what();
    CHECK(message.find("75B") != std::string::npos);
    CHECK(message.find("80") != std::string::npos);
}

void testTransportNames() {
    CHECK(std::string(toString(mc::EcTransport::WindowsAcpiDriver)) == "windows-acpi-driver");
    CHECK(std::string(toString(mc::EcTransport::LinuxEcSys)) == "linux-ec-sys");
    CHECK(std::string(toString(mc::EcTransport::Unknown)) == "unknown");
    CHECK(std::string(toString(mc::FanChannel::Cpu)) == "CPU");
    CHECK(std::string(toString(mc::FanChannel::Gpu)) == "GPU");
}

/// The board summary must print identity bytes as numbers.
///
/// This guards a real defect: streaming a std::uint8_t inserts it as a
/// character, so the expected values 82, 72 and 26 appeared in the startup log
/// as "R", "H" and a control byte, which made a passing board check look like
/// garbage. The check below fails if that conversion is ever reintroduced.
void testBoardCheckSummaryPrintsNumbers() {
    // The local is deliberately not called "check": that name would shadow the
    // check() function the CHECK macro expands to, and the compiler would then
    // report the variable as "not a function taking 4 arguments".
    mc::BoardCheck result;
    result.verified = true;
    result.bytes.push_back({mc::ec::kBoardEcVersion, "ecOEM_EC_VER",
                            mc::ec::kBoardEcVersionExpected, mc::ec::kBoardEcVersionExpected});
    result.bytes.push_back({mc::ec::kBoardSystemId, "SystemID", mc::ec::kBoardSystemIdExpected,
                            mc::ec::kBoardSystemIdExpected});
    result.bytes.push_back({mc::ec::kBoardProjectId, "ProjectID", mc::ec::kBoardProjectIdExpected,
                            mc::ec::kBoardProjectIdExpected});
    result.board.baseBoard = "WUJIE Series-X5SP4NAG";
    result.board.modelId = "wu-jie-15-xpro";

    const std::string summary = describeBoardCheck(result);

    // The search result is stored before being tested. A call such as
    // summary.find("...") passed straight into the CHECK macro would be torn
    // apart by the preprocessor, because the macro takes a single argument.
    const auto hasText = [](const std::string& text, const char* needle) {
        return text.find(needle) != std::string::npos;
    };

    CHECK(hasText(summary, "0x454=82"));
    CHECK(hasText(summary, "0x456=72"));
    CHECK(hasText(summary, "0x740=26"));

    // The character forms must not appear anywhere in the summary.
    CHECK(!hasText(summary, "0x454=R"));
    CHECK(!hasText(summary, "0x456=H"));

    // A failing check must report the mismatch in numbers too.
    mc::BoardCheck failed;
    failed.verified = false;
    failed.bytes.push_back({mc::ec::kBoardSystemId, "SystemID", 72, 65});
    const std::string failedSummary = describeBoardCheck(failed);
    CHECK(hasText(failedSummary, "0x456=65"));
    CHECK(hasText(failedSummary, "verified=no"));
}

/// The inventory cache must survive a write and read without changing any text.
///
/// This is a real defect guard, not a formality. An earlier build produced a
/// cache whose strings read back without their spaces, which made every caption
/// on the BIOS page run together. The cause was in the JSON layer, and the same
/// code is used on Linux, so the round trip is pinned here for both platforms.
void testInventoryJsonRoundTrip() {
    // The cache must survive a write and read without altering any text.
    //
    // This guards a real defect: a build produced a cache whose strings came back
    // without their spaces, which made every caption on the BIOS page run
    // together. The same JSON layer runs on Windows and Linux, so the round trip
    // is pinned for both.
    mc::HardwareInventory original;
    original.readAt = "10.10.2026 13:03:07";

    mc::InventorySection& section = original.addSection("РќРѕСѓС‚Р±СѓРє В· BIOS В· UEFI В· EC");
    section.addRow("РњРѕРґРµР»СЊ РёР· РїСЂРѕС€РёРІРєРё", "WUJIE Series");
    section.addRow("РРґРµРЅС‚РёС„РёРєР°С‚РѕСЂ СЃРёСЃС‚РµРјРЅРѕР№ РїСЂРѕС€РёРІРєРё UEFI",
                   "D9D24AA7-D080-5181-8A9A-597ADE95FE20");
    section.addRow("РњРѕРґРµР»СЊ", "AMD Ryzen AI 9 H 365 w/ Radeon 880M");
    section.addRow("Р§Р°СЃС‚РѕС‚Р° РёР· SMBIOS", "1996 РњР“С†");
    section.addRow("РњРѕРґРµР»СЊ РёР· EDID", "NE153QDM-NY1 В· BOE");
    section.addRow("Р—РЅР°С‡РµРЅРёРµ РёР· РїСЂРѕР±РµР»Р°", " ");
    section.addRow("РљР°РІС‹С‡РєРё Рё СЃР»СЌС€", "РїСѓС‚СЊ \"C:\\Temp\" Рё СЃР»СЌС€ \\ РєРѕРЅРµС†");

    const std::string json = mc::toJson(original);

    // The writer must not swallow spaces either.
    const bool writerKeepsSpaces = json.find("WUJIE Series") != std::string::npos;
    CHECK(writerKeepsSpaces);
    const bool writerKeepsLongValue =
        json.find("AMD Ryzen AI 9 H 365 w/ Radeon 880M") != std::string::npos;
    CHECK(writerKeepsLongValue);

    const std::optional<mc::HardwareInventory> parsed = mc::fromJson(json);
    const bool parsedOk = parsed.has_value();
    CHECK(parsedOk);
    if (!parsed.has_value()) {
        return;
    }

    const bool readAtSame = parsed->readAt == original.readAt;
    CHECK(readAtSame);
    const bool sectionCountSame = parsed->sections.size() == original.sections.size();
    CHECK(sectionCountSame);
    if (!sectionCountSame) {
        return;
    }

    // Each row is checked by name, so the report names the field that changed
    // rather than only its index.
    const mc::InventorySection& expectedRows = original.sections.front();
    const mc::InventorySection& actualRows = parsed->sections.front();

    const bool rowsSame = actualRows.rows.size() == expectedRows.rows.size();
    CHECK(rowsSame);
    if (!rowsSame) {
        return;
    }

    for (std::size_t index = 0; index < expectedRows.rows.size(); ++index) {
        const std::string expectedName = expectedRows.rows[index].name;
        const std::string actualName = actualRows.rows[index].name;
        const std::string expectedValue = expectedRows.rows[index].value;
        const std::string actualValue = actualRows.rows[index].value;

        if (expectedName != actualName || expectedValue != actualValue) {
            std::printf("  row %zu mismatch:\n    expected name  [%s]\n    actual   name  [%s]\n"
                        "    expected value [%s]\n    actual   value [%s]\n",
                        index, expectedName.c_str(), actualName.c_str(), expectedValue.c_str(),
                        actualValue.c_str());
        }

        const bool nameSame = expectedName == actualName;
        const bool valueSame = expectedValue == actualValue;
        CHECK(nameSame);
        CHECK(valueSame);
    }
}
// ---------------------------------------------------------------------------
// Write guard
// ---------------------------------------------------------------------------

void testWritableRange() {
    // The RAM fan table is the highest region written, and the keyboard colour
    // registers the lowest of the named settings.
    CHECK(mc::ec::kMinWritable < mc::ec::kMaxWritable);
    CHECK(mc::ec::kCpuTableBase >= mc::ec::kMinWritable);
    CHECK(mc::ec::kNativeControl <= mc::ec::kMaxWritable);
    CHECK(mc::ec::kKeyboardLevel >= mc::ec::kMinWritable);
    CHECK(mc::ec::kPerformanceMode >= mc::ec::kMinWritable);
}

}  // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc == 2 && std::string(argv[1]) == "--logging") {
        try { return mc::tests::runLoggingTests() == 0 ? 0 : 1; }
        catch (const std::exception& error) {
            std::printf("Logging test exception: %s\n", error.what());
            return 1;
        }
    }
    if (argc == 4 && std::string(argv[1]) == "--temperatures") {
        try { return mc::tests::runTemperatureTests(argv[2],argv[3]) == 0 ? 0 : 1; }
        catch (const std::exception& error) {
            std::printf("Temperature replay exception: %s\n", error.what());
            return 1;
        }
    }
    if (argc == 2 && std::string(argv[1]) == "--fan-presets") {
        std::setvbuf(stdout, nullptr, _IONBF, 0);
        try { static_cast<void>(mc::tests::runFanPresetTests()); }
        catch (const std::exception& error) {
            std::printf("Fan preset test exception: %s\n", error.what());
            return 1;
        }
        return mc::tests::failureCount() == 0 ? 0 : 1;
    }
    testBoardIdentityValues();
    testPerformanceModeDecoding();
    testKeyboardLevelEncoding();
    testBatteryProfileEncoding();
    testFanCurveTableLayout();
    testEcSysWindowDoesNotCoverOurRegisters();
    testFanFeedbackAddresses();
    testRpmAssembly();
    testDutyConversion();
    testHexFormatting();
    testTransportNames();
    testBoardCheckSummaryPrintsNumbers();
    testInventoryJsonRoundTrip();
    testWritableRange();

    // The scroll arithmetic lives in the core, so it is tested here rather than
    // behind a screen.
    static_cast<void>(mc::tests::runScrollModelTests());
    try { static_cast<void>(mc::tests::runCoreSafetyTests()); }
    catch (const std::exception& error) {
        std::printf("Core safety test exception: %s\n", error.what());
        return 1;
    }
    try { static_cast<void>(mc::tests::runFanPresetTests()); }
    catch (const std::exception& error) {
        std::printf("Fan preset test exception: %s\n", error.what());
        return 1;
    }

    std::printf("\n%d checks, %d failure(s)\n", mc::tests::checkCount(),
                mc::tests::failureCount());
    return mc::tests::failureCount() == 0 ? 0 : 1;
}
