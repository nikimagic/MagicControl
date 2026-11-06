// MagicControl - the authoritative embedded-controller register map.
//
// Sources: our C# reference Fan_X15Pro, OEM decompilation, ACPI and local logs.
// A recovered OEM name is not proof of support on this board. Read-only research
// candidates below must not become writable settings without device evidence.
// Detailed contracts: src/platform/windows/FAN_MODES.md and BATTERY.md.
// Shared bytes MUST use masked updates: preserve every bit owned by other features.
// A successful readback proves a write, not that firmware executed the requested action.
//
// Board under development: MECHREVO Wujie 15X Pro, board X5SP4NAG.
// The identity triple 0x454/0x456/0x740 = 82/72/26 gates all write access.
#pragma once

#include <cstdint>

namespace mc::ec {

// ---------------------------------------------------------------------------
// Board identity - read once at startup, gates every write
// ---------------------------------------------------------------------------
/// ecOEM_EC_VER. Expected 82 (0x52) on X5SP4NAG.
inline constexpr std::uint16_t kBoardEcVersion   = 0x454;
/// SystemID. Expected 72 (0x48) on X5SP4NAG.
inline constexpr std::uint16_t kBoardSystemId    = 0x456;
/// ProjectID. Expected 26 (0x1A) on X5SP4NAG.
inline constexpr std::uint16_t kBoardProjectId   = 0x740;

inline constexpr std::uint8_t kBoardEcVersionExpected = 82;
inline constexpr std::uint8_t kBoardSystemIdExpected  = 72;
inline constexpr std::uint8_t kBoardProjectIdExpected = 26;

// ---------------------------------------------------------------------------
// Fans and temperatures
// ---------------------------------------------------------------------------
/// CPU rotor tachometer, high byte. RPM = (high << 8) | low.
inline constexpr std::uint16_t kCpuRpmHigh = 0x464;
inline constexpr std::uint16_t kCpuRpmLow  = 0x465;
/// CPU duty cycle, raw. Percent = raw / 2; 0..200 -> 0..100%.
/// RPM/PWM are telemetry, never evidence of ownership loss or a restore trigger.
inline constexpr std::uint16_t kCpuPwm     = 0x75B;

/// Second rotor tachometer, high byte.
inline constexpr std::uint16_t kGpuRpmHigh = 0x46C;
inline constexpr std::uint16_t kGpuRpmLow  = 0x46D;
/// Second rotor duty cycle, raw / 2 = percent. Zero is a valid stopped rotor.
/// Hardware common mode does not imply identical real CPU/GPU PWM or RPM.
inline constexpr std::uint16_t kGpuPwm     = 0x75C;

/// CPU temperature in degrees Celsius, signed. EC's temperature may differ from
/// PawnIO Tctl/Tdie and ADL GPU readings; do not force them to agree.
inline constexpr std::uint16_t kCpuTemperature = 0x43E;

/// 0x46B sat at 25 while the second rotor moved 0 -> 2582 RPM, so it is
/// neither that tachometer's low byte nor its PWM. Now identified below as
/// the post-policy Slow limit on exact Wujie; never use the GM6 tach map here.
inline constexpr std::uint16_t kUnknown46B = 0x46B;

/// Read-only post-policy power cache, Wujie82/72/26 only. Embedded EC bank2
/// AA8A..AADE copies selected86B/86C/86E here and sets pending bits in9DD.
/// Five saved EC/SMU pairs agree; these are selected EC limits, not CPU ACK,
/// requested783..785, factory preset triples or measured power consumption.
inline constexpr std::uint16_t kAcceptedStapm = 0x46A;
inline constexpr std::uint16_t kAcceptedSlow = 0x46B;
inline constexpr std::uint16_t kAcceptedFast = 0x46F;

// ---------------------------------------------------------------------------
// Battery telemetry - read-only, verified X5SP4NAG register layout
// ---------------------------------------------------------------------------
/// OEM ECSpec.ecBt1Temperature=1186. uint16 LE in 0.1 K; C=raw/10-273.15.
/// Recorded mirrors 0x502/0x504 agreed in all 80 load/cooling samples.
inline constexpr std::uint16_t kBatteryTemperature = 0x4A2;
/// Current firmware DSDT CYCN field, uint16 LE.
inline constexpr std::uint16_t kBatteryCycleCount = 0x4A6;
/// DSDT XST1 (_BST present rate); _BIF Power Unit=1 means mA.
inline constexpr std::uint16_t kBatteryCurrent = 0x434;
/// DSDT XST3 (_BST voltage), uint16 LE in mV.
inline constexpr std::uint16_t kBatteryVoltage = 0x438;

// ---------------------------------------------------------------------------
// Performance mode
// ---------------------------------------------------------------------------
/// Raw performance-mode byte.
///   bits 7|4 -> preset: 0x80 office, 0x00 balanced, 0x10 turbo
///   bit 6    -> Fan Boost (maximum airflow), blocks software fan curves
///   bit 5    -> distinguishes observed office variants, firmware effect unknown
/// Software control is admitted only for 0x00, 0x10, 0x80 and 0xA0.
/// SETs owns mask 0xB0 only; preserve Fan Boost 0x40 and every other bit.
/// Max owns bit 0x40 only: no Default, table clearing or gate reactivation first.
inline constexpr std::uint16_t kPerformanceMode = 0x751;

inline constexpr std::uint8_t kPerformanceOffice   = 0xA0;
inline constexpr std::uint8_t kPerformanceBalanced = 0x00;
inline constexpr std::uint8_t kPerformanceTurbo    = 0x10;

/// Mask/bits decoding the preset group inside kPerformanceMode.
inline constexpr std::uint8_t kPerformancePresetMask = 0x90;
inline constexpr std::uint8_t kPerformanceBoostBit   = 0x40;
inline constexpr std::uint8_t kPerformanceBit5       = 0x20;

// ---------------------------------------------------------------------------
// Custom-mode power limits (PL1/PL2/PL4 style overrides)
// ---------------------------------------------------------------------------
// Mandatory contract: src/core/src/POWER_MODES.md. Exact-board observed mapping:
// 0x783=STAPM, 0x784=Slow, 0x785=Fast; do not use address order as SMU display order.
// These are byte values, not flag masks. Zero restores the firmware preset.
// Readback under idle is proven; loaded enforcement and a universal range are not.
/// OEM PL1-style override; zero means use the firmware preset, not a zero-watt cap.
/// SETs profile changes clear only nonzero 0x783..0x785; never fan tables/gates.
/// Exact-board probe 2026-10-16, PM 0x5D0009: writing 25 changed SMU STAPM
/// 35->25 W only; zero restored 35 W. Preset TABLE numbers are still not a
/// one-to-one promise of current SMU limits. Evidence: out/test_util/analysis/
/// ec-smu-limits-20261016/run1/summary.json (Turbo, reported USB-C 65 W, idle).
inline constexpr std::uint16_t kPowerLimitSustained = 0x783;
/// Historical PL2/"Short" name. Exact-board probe: writing 26 changed SMU SLOW
/// 35->26 W only, NOT Fast. Zero restored it. Keep this distinction in future UI.
inline constexpr std::uint16_t kPowerLimitShort     = 0x784;
/// OEM PL4-style override. OEM variants can scale it (0x727.7); do not assume
/// generic watts or enable manual power writes from the register name alone.
/// Exact-board probe with 0x727.7 CLEAR: raw 15 changed SMU FAST 40->15 W only.
/// Zero restored it. The case with 0x727.7 SET was not tested; never toggle that
/// shared fan/camera byte to force a scaling mode during power-limit research.
inline constexpr std::uint16_t kPowerLimitPeak      = 0x785;

// ---------------------------------------------------------------------------
// Fan control gates - the RAM fan-table protocol
// ---------------------------------------------------------------------------
/// Capability flag. Bit 6 (0x40) must be set or the EC has no RAM fan table.
inline constexpr std::uint16_t kRamTableCapability = 0x78E;
inline constexpr std::uint8_t  kRamTableSupportedBit = 0x40;

/// Application-present byte: OUR flag is bit 0 (0x01), not bit 7 (0x80).
/// Preserve all other bits; observed 0x81 is a whole byte, not the mask to write.
/// Explicit Default resets consumers through core ApplicationPresence and
/// releases bit0 LAST. Internal FAN cleanup and process exit retain presence.
/// Autonomous Manual pulses only bit0 during activation. Presence alone does
/// not select a fan mode; it also gates Copilot and disables native RGB idle.
inline constexpr std::uint16_t kAppPresence = 0x741;
inline constexpr std::uint8_t  kAppPresenceBit = 0x01;

/// Shared control byte. Bit 7 (0x80) is a fan custom-control gate used by both
/// autonomous Manual and Manual online: this bit cannot distinguish them alone.
/// Preserve AC-recovery bit 3 when changing fans, and vice versa.
inline constexpr std::uint16_t kCustomMode      = 0x726;
inline constexpr std::uint8_t  kCustomModeBit   = 0x80;
/// Bit 3 (0x08) is AC-recovery auto power-on.
inline constexpr std::uint8_t  kAcRecoveryBit   = 0x08;

/// Secondary control byte. Bit 6 (0x40) arms manual fan control; bit 4 (0x10)
/// is a camera command strobe.
/// Fan updates use mask 0x40 only. Camera updates use 0x18 only and preserve fans.
/// OEM bit 7 is a PL4 scaling candidate, not a verified setting for this build.
inline constexpr std::uint16_t kControl727      = 0x727;
inline constexpr std::uint8_t  kControl727FanBit = 0x40;
inline constexpr std::uint8_t  kControl727CamMask = 0x18;
inline constexpr std::uint8_t  kControl727CamOn   = 0x18;
inline constexpr std::uint8_t  kControl727CamOff  = 0x10;
/// After the command settles, bit 3 (0x08) is inverse electronic-camera state:
/// clear = enabled, set = electronically disabled. Bit 4 clears itself.
/// Historical name says "Present": this is inverse enable state, not physical
/// camera detection. Command 0x18 = enable, 0x10 = disable; await strobe clearing.
inline constexpr std::uint8_t  kCameraPresentBit  = 0x08;

/// "Fan control respective" - bit 7 (0x80) clear means common mode, set means
/// the two rotors may run at different duty cycles. Bits 2:0 select the
/// keyboard pulsation preset on verified X5SP4NAG: 0=off, 1=white, 2=red,
/// 3=green, 4=blue. Observed on the physical keyboard on 2026-10-14;
/// generic OEM XBF labels use another order. Static RGB is ignored.
/// Two independent owners share this byte: fans mask 0x80, pulsation mask 0x07.
/// Autonomous Manual requires hardware common (bit 7=0), CPU table only.
/// Manual online ALWAYS uses hardware split (bit 7=1), even when software
/// CPU=GPU computes a shared target from max(CPU,GPU). Do not confuse these links.
/// Default and Max leave this bit untouched. Unknown bits 6..3 are preserved.
inline constexpr std::uint16_t kFanControlRespective = 0x7C5;
inline constexpr std::uint8_t  kSplitModeBit         = 0x80;
inline constexpr std::uint8_t  kPulsationMask        = 0x07;

/// RAM table commit flag. Bit 2 (0x04) low = table writable, high = committed
/// and the EC is executing it.
/// This is the commit/activation request, not proof of physical PWM response.
/// Default clears this bit; Max leaves it alone; exit never clears it.
inline constexpr std::uint16_t kRamTableCommit = 0x7C6;
inline constexpr std::uint8_t  kRamTableCommitBit = 0x04;

// ---------------------------------------------------------------------------
// Fan curve tables - 96 bytes total, CPU first
// ---------------------------------------------------------------------------
/// CPU table base: Up[16] 0xF00..0xF0F, middle[16] 0xF10..0xF1F,
/// Duty[16] 0xF20..0xF2F. In autonomous Manual middle[] is DownT (UpT-3).
/// Preserve firmware-owned Down[0]; slot 0 Up/Duty=0 is a protocol marker.
/// Autonomous unused tail 255/255/200 is table encoding, not a separate erase.
/// Manual online uses its own RAM target encoding: do not mix the two formats.
/// Never clear tables for Default, Max or exit. Replace changed bytes when needed.
inline constexpr std::uint16_t kCpuTableBase    = 0xF00;
/// GPU table base: same layout shifted by 0x30.
inline constexpr std::uint16_t kGpuTableBase    = 0xF30;
inline constexpr std::uint16_t kTableStride     = 0x30;
/// Up to 16 curve points per table.
inline constexpr std::uint16_t kTablePointCount = 16;
/// Byte offset of the duty array inside a table.
inline constexpr std::uint16_t kTableDutyOffset = 0x20;
/// Middle-array offset; autonomous format uses DownT, preserving Down[0].
inline constexpr std::uint16_t kTableAuxOffset  = 0x10;

/// One past the last RAM table byte (0xF00 + 96).
inline constexpr std::uint16_t kTableEnd = 0xF60;

/// Historical native-status/control names. These addresses overlap the LAST
/// three bytes of the GPU RAM table; they are not "past" the tables. Do not
/// invent independent flag writes from these names. Table protocol owns its tail.
inline constexpr std::uint16_t kNativeStatus1 = 0xF5D;
inline constexpr std::uint16_t kNativeStatus2 = 0xF5E;
inline constexpr std::uint16_t kNativeControl = 0xF5F;

// ---------------------------------------------------------------------------
// Keyboard backlight
// ---------------------------------------------------------------------------
/// Capability flag: bit 2 (0x04) means the EC controls RGB backlight.
inline constexpr std::uint16_t kKeyboardCapability = 0x766;
inline constexpr std::uint8_t  kKeyboardRgbBit     = 0x04;
/// Bit 1 (0x02) is USB power share capability.
inline constexpr std::uint8_t  kUsbPowerShareBit   = 0x02;
/// USB charging ENABLE mask belongs to kKeyboardEffect (0x767), not capability
/// byte 0x766. The OEM exposes this setting even if the capability bit is clear.
inline constexpr std::uint8_t  kUsbChargingBit      = 0x10;

/// Backlight level and power switch.
///   bits 7:5 -> level 0..4
///   explicit Off uses mask0xE2 with selected level0 and power-disable bit1
/// A write must also raise strobe bit 4 (0x10).
/// Level is 0..4, not RGB strength. Off adds bit 1; preserve unrelated bits.
/// A wake notification must not replay cached brightness: keep the current level.
/// Native idle blank can set bit1 while retaining a non-zero selected level.
/// That effective blank is NOT a new desired Off preference. Restore preserves
/// a fresh disable bit; software idle may own bit1 independently of levelE0.
inline constexpr std::uint16_t kKeyboardLevel    = 0x78C;
inline constexpr std::uint8_t  kKeyboardMask     = 0xE2;
inline constexpr std::uint8_t  kKeyboardStrobe   = 0x10;
inline constexpr std::uint8_t  kKeyboardLevelMax = 4;

/// Shared byte: rainbow bit 7 (0x80), USB charging bit 4 (0x10), lightbar toggle
/// strobe bit 1 (0x02), static RGB apply strobe bit 5 (0x20) in the reference.
/// Each path owns only its mask; never overwrite this whole byte for one feature.
inline constexpr std::uint16_t kKeyboardEffect = 0x767;
inline constexpr std::uint8_t  kKeyboardRainbowBit = 0x80;
/// Windows-key lock, bit 0. Bit 1 is independent lightbar status; preserve it.
inline constexpr std::uint16_t kWindowsKeyLock = 0x768;
inline constexpr std::uint8_t  kWindowsKeyLockBit = 0x01;
/// Fn-lock, bit 4 (0x10).
inline constexpr std::uint16_t kFnLock = 0x74E;
inline constexpr std::uint8_t  kFnLockBit = 0x10;

/// Static RGB channels in DEVICE range 0..50 (not UI RGB 0..255).
/// White = 50/50/50; writing 255/255/255 extinguished the keyboard in user testing.
/// UI conversion is round(channel * 50 / 255); brightness remains an independent
/// 0x78C setting. Pulsation has its own palette in 0x7C5, not these RGB channels.
inline constexpr std::uint16_t kKeyboardRed   = 0x769;
inline constexpr std::uint16_t kKeyboardGreen = 0x76A;
inline constexpr std::uint16_t kKeyboardBlue  = 0x76B;

/// Screen lightbar: AC control and color registers.
inline constexpr std::uint16_t kLightbarAcControl = 0x748;
inline constexpr std::uint16_t kLightbarAcRed = 0x749;
inline constexpr std::uint16_t kLightbarAcGreen = 0x74A;
inline constexpr std::uint16_t kLightbarAcBlue = 0x74B;
inline constexpr std::uint8_t kLightbarAppExistsBit = 0x01;
inline constexpr std::uint8_t kLightbarS0OffBit = 0x04;
inline constexpr std::uint8_t kLightbarWelcomeBit = 0x80;
/// OEM lightbar capability, one-shot toggle trigger, and resulting status.
/// Capability 0x765.6 is read-only. Toggle 0x767.1 is a one-shot, not target state;
/// confirm 0x768.1 afterwards. Never blindly retry a toggle (it can undo itself).
/// Preserve rainbow/USB in 0x767 and Windows lock in 0x768.
inline constexpr std::uint16_t kLightbarSupport = 0x765;
inline constexpr std::uint16_t kLightbarTrigger = 0x767;
inline constexpr std::uint16_t kLightbarSwitchStatus = 0x768;
inline constexpr std::uint8_t kLightbarSupportBit = 0x40;
inline constexpr std::uint8_t kLightbarToggleTrigger = 0x02;
inline constexpr std::uint8_t kLightbarStatusBit = 0x02;

// ---------------------------------------------------------------------------
// Battery charge limiting
// ---------------------------------------------------------------------------
/// Separate OEM voltage/health-protection profile: bits 5:4 codes 0,1,2.
/// Old UI mapped these to 100/95/85, but that is NOT proof of percentage semantics.
/// Current percentage API does not write this field. Preserve unrelated bits.
inline constexpr std::uint16_t kBatteryProfileWmi = 0x7A6;
inline constexpr std::uint8_t  kBatteryProfileMask = 0x30;
inline constexpr std::uint8_t  kBatteryProfileShift = 4;
/// Windows OEM upper charge limit. Bits 0..6 store the requested percentage;
/// zero means 100%. Bit 7 is a firmware status flag and must be preserved.
/// Reference names bit 7 "Reached". A number of 85 here does NOT enable the cap:
/// also save via WMI 0x87F, select enforcement platform and inspect 0x742.2.
inline constexpr std::uint16_t kBatteryChargeControl = 0x7B9;
inline constexpr std::uint8_t  kBatteryChargeLimitMask = 0x7F;
/// Historical name retained. Reference reads bit 2 (0x04) as charge-limit Enabled.
/// This is a status/gate read, NOT a flag we set manually; other bits stay unknown.
inline constexpr std::uint16_t kBatteryVoltageProfileWmi = 0x742;
/// Stored charge limit via AcpiTest_MULong.GetSetULong WMI WORD interface.
/// NOT ordinary EC WriteEC(0x87F). Preserve adjacent byte and bit 7 on normal write.
inline constexpr std::uint16_t kBatteryStoredLimitWmi = 0x87F;
/// Enforcement platform (whole byte, not a bitfield). Exact-board reference:
/// WARNING: A/B 2026-10-16 proved a POWER side effect: DC Office changes
/// Fast/Slow/STAPM 30/25/25 at platform 7 to 45/40/40 at 4. Not battery-only.
/// AC 65W Office/Balanced/Turbo unchanged in the same test. See battery-platform-
/// smu-20261016/REPORT.md under research/test_util/analysis.
/// 7 -> 4 enables percentage enforcement when ROM ID=0xFF; 7 disables it again.
/// User policy 2026-10-17 on verified 4/7 + ROM FF: 100/95/85% select 4;
/// separate Auto preset selects 7 with upper 100%. Save original platform.
/// Lower=80 remains stored; its enforcement at platform 7 is unproven.
inline constexpr std::uint16_t kBatteryPlatformWmi = 0x7C3;
/// ROM-ID: read-only in Logger. Explicit battery policy owns FF<->04 in RAM,
/// with durable original/ownership record; never writes Set_ROMID/save markers.
inline constexpr std::uint16_t kBatteryRomIdWmi = 0x770;

// EC 0x7D0: lower charge-restart threshold, low 7 bits; preserve bit 7.
// OEM reference calls it Down; the exact consumer remains unknown. User's
// slow-charge-threshold interpretation is a hypothesis, not a verified name.
// Explicit request21.10.2026:97 for all Wujie upper presets. Reference setter
// accepts only1..95:97 is experimental and must not be silently clamped.
inline constexpr std::uint8_t kRequestedBatteryLowerPercent=97;

// ---------------------------------------------------------------------------
// USB-C / charger telemetry - read-only, decoded from the OEM/Uniwill path
// ---------------------------------------------------------------------------
/// ADPT field byte. Bits 3..6 select a generic OEM adapter class; this is not
/// a live watt meter and must not be used for writes.
inline constexpr std::uint16_t kAdapterClass = 0x49F;
/// PDIN field byte. Only bits 0..3 are named by the DSDT; the meaning is
/// platform-specific and read-only here.
inline constexpr std::uint16_t kPowerInput = 0x74C;
/// OEM complex power-status byte. Bit 0 is round-socket status; mask 0x36
/// contains Type-C status bits. Bit 7 priority semantics are not verified.
/// Observed USB-C channel 1 mask=0x12, channel 2 mask=0x24; 0x92/0xA4 with PD.
/// Disconnected 0x80 was observed: bit 7 alone does NOT mean external power.
/// Windows AC status can lag EC. Port fields are not an atomic snapshot of power.
inline constexpr std::uint16_t kTypeCStatus = 0x7CC;
/// Legacy TUXEDO charging-priority bit, NOT a confirmed Wujie mode selector.
/// N.1.20MRO15 bank2 A3F6->A3A3->98F8 forces this bit at9934..9937 when
/// AC+Type-C are present, then mirrors it into984.7. Native I2EC test on
/// USB-C65W also returnedA4 after24; DC write alone proves no priority mode.
/// Read-only on this firmware. Do not force it, write984, or infer that1
/// confirms a selected Performance mode. Native status supplies write admission.
inline constexpr std::uint8_t kTypeCPriorityBit = 0x80;
/// OEM Type-C channel A negotiated/reported power rating. OEM returns max(A,B).
/// This is NOT current draw; 65/100 here does not mean the laptop consumes that.
inline constexpr std::uint16_t kTypeCPowerA = 0x7CD;
/// OEM Type-C channel B power rating; observed B for USB-C channel 2.
/// Read-only; do not write charger power or infer input current from this byte.
inline constexpr std::uint16_t kTypeCPowerB = 0x7CE;

// Research-only candidates (OEM names do not prove implementation on this board):
// Linux/OEM power candidates: 0x7A5.7 HIGH_POWER, 0x7A5.4 OVERBOOST,
// 0x7A6.1 OVERBOOST_DYN_TEMP_OFF (inverse: 0 enables the named feature).
// Do not confuse 7A6.1 with 7A5.1 (lightbar/power LED field). RAM Fan 1.5 OEM
// methods exist, but no calls to its SetPowerStatus/SetOverBoostByDynamicTemp
// were found in that class; no exact-board power effect established. READ ONLY.
// Exact-board Turbo probe (DC/AC100, platform4/7, current FAN gates retained):
// 7A5.7 latched but did not change STAPM/Slow/Fast; 7A5.4 did not latch (30ms
// readback and later samples). Requesting both produced only bit7. No workload.
// Legacy Intel OEM also uses BIOS profile index 0x7AB with High Power; our
// readback stayed 0. These are not universal "enable full Turbo" controls.
// Evidence: research/test_util/analysis/highpower-overboost-20261016/REPORT.md.
// 0x742.5 is an old charging-priority capability in Linux. TUXEDO additionally
// restricts device support because ODM removed the feature in newer firmware.
// Our OEM support function returns false; a set capability bit is insufficient.
// 0x44C: raw consumption candidate, displayed assuming raw=W. Footer subtracts
// positive battery charging watts by user request; preserve raw value in logs.
// 0x44F: candidate GPU temperature, observed zero; never substitute zero for ADL.
// 0x721/0x72D/0x72E: NUC/OEM GPU-limit candidates, no confirmed control here.
// 0x786/0x787: OEM TCC/FanSwitchSpeed candidates; other EC versions use them for
// PWM. Do not enable writes merely because another model uses these addresses.
// 0x7C8 bit 4: OEM thermal-protection candidate; board semantics unverified.
// 0x7D2: OEM GPU-module candidate; no NVIDIA TGP support established on this APU.
// 0x7D8/0x7D9/0x7DA, 0x753/0x754: temperature/current candidates, uncalibrated.
// SmartAPC command 0xBB+offset reads, 0xAA writes in OEM code; only research,
// no feature support inferred and no setters enabled in current MagicControl.

// ---------------------------------------------------------------------------
// Reachability per transport
// ---------------------------------------------------------------------------
/// Smallest address this build may write.
inline constexpr std::uint16_t kMinWritable = 0x43E;
/// The RAM fan tables are the highest region we touch.
inline constexpr std::uint16_t kMaxWritable = 0xF5F;

/// Highest address the Windows driver's 8-byte packet can express. The EC
/// address space is 16-bit wide, which is why this project's registers sit far
/// above the 256-byte window a debugfs transport would offer.
inline constexpr std::uint16_t kMaxAddress16 = 0xFFFF;

/// The ec_sys debugfs driver (drivers/acpi/ec_sys.c) hard-codes
/// `#define EC_SPACE_SIZE 256`, so it exposes only EC addresses 0x00..0xFF.
///
/// CORRECTION, found by the unit tests: every register this project uses lives
/// at 0x43E or above, which is OUTSIDE that window. An earlier claim in this
/// file that all registers fit inside it was wrong - the values merely looked
/// small next to a 16-bit space. Nothing in this map is reachable through
/// ec_sys, not even the fan tachometers.
///
/// The Linux port therefore needs a different transport, chosen once the
/// hardware can be probed on Kubuntu:
///   1. The uniwill-laptop kernel driver, which already reaches this hardware
///      family through WMI and exposes fan and temperature data over hwmon.
///      Note the mainline documentation states platform-profile writes are not
///      implemented, so raw fan control would remain out of reach.
///   2. /dev/mem or an IOCTL on a driver that permits 16-bit EC access.
///   3. A small purpose-built kernel module.
///
/// Until that is settled, the Windows backend is the only working transport.
inline constexpr std::uint16_t kLinuxEcWindowSize = 0x100;

/// True when an address falls inside the 256-byte ec_sys window.
///
/// Every current register returns false. This helper exists so the Linux port
/// can report the limitation precisely instead of failing obscurely, and so the
/// tests keep the arithmetic honest.
[[nodiscard]] constexpr bool reachableViaEcSysWindow(std::uint16_t address) noexcept {
    return address < kLinuxEcWindowSize;
}

}  // namespace mc::ec
