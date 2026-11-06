#pragma once
#include <array>
#include <string_view>
// Field names and descriptions from the included NVIDIA NVML header.
// Original copyright/license notices are preserved in vendor/NVIDIA/nvml.h.
namespace probe {
struct NvmlFieldDescription {
    std::string_view name, comment;
};
inline const auto kNvmlFields = [] {
    std::array<NvmlFieldDescription, 308> result{};
    result[1] = {"NVML_FI_DEV_ECC_CURRENT", "Current ECC mode. 1=Active. 0=Inactive"};
    result[2] = {"NVML_FI_DEV_ECC_PENDING", "Pending ECC mode. 1=Active. 0=Inactive"};
    result[3] = {"NVML_FI_DEV_ECC_SBE_VOL_TOTAL", "Total single bit volatile ECC errors"};
    result[4] = {"NVML_FI_DEV_ECC_DBE_VOL_TOTAL", "Total double bit volatile ECC errors"};
    result[5] = {"NVML_FI_DEV_ECC_SBE_AGG_TOTAL", "Total single bit aggregate (persistent) ECC errors"};
    result[6] = {"NVML_FI_DEV_ECC_DBE_AGG_TOTAL", "Total double bit aggregate (persistent) ECC errors"};
    result[7] = {"NVML_FI_DEV_ECC_SBE_VOL_L1", "L1 cache single bit volatile ECC errors"};
    result[8] = {"NVML_FI_DEV_ECC_DBE_VOL_L1", "L1 cache double bit volatile ECC errors"};
    result[9] = {"NVML_FI_DEV_ECC_SBE_VOL_L2", "L2 cache single bit volatile ECC errors"};
    result[10] = {"NVML_FI_DEV_ECC_DBE_VOL_L2", "L2 cache double bit volatile ECC errors"};
    result[11] = {"NVML_FI_DEV_ECC_SBE_VOL_DEV", "Device memory single bit volatile ECC errors"};
    result[12] = {"NVML_FI_DEV_ECC_DBE_VOL_DEV", "Device memory double bit volatile ECC errors"};
    result[13] = {"NVML_FI_DEV_ECC_SBE_VOL_REG", "Register file single bit volatile ECC errors"};
    result[14] = {"NVML_FI_DEV_ECC_DBE_VOL_REG", "Register file double bit volatile ECC errors"};
    result[15] = {"NVML_FI_DEV_ECC_SBE_VOL_TEX", "Texture memory single bit volatile ECC errors"};
    result[16] = {"NVML_FI_DEV_ECC_DBE_VOL_TEX", "Texture memory double bit volatile ECC errors"};
    result[17] = {"NVML_FI_DEV_ECC_DBE_VOL_CBU", "CBU double bit volatile ECC errors"};
    result[18] = {"NVML_FI_DEV_ECC_SBE_AGG_L1", "L1 cache single bit aggregate (persistent) ECC errors"};
    result[19] = {"NVML_FI_DEV_ECC_DBE_AGG_L1", "L1 cache double bit aggregate (persistent) ECC errors"};
    result[20] = {"NVML_FI_DEV_ECC_SBE_AGG_L2", "L2 cache single bit aggregate (persistent) ECC errors"};
    result[21] = {"NVML_FI_DEV_ECC_DBE_AGG_L2", "L2 cache double bit aggregate (persistent) ECC errors"};
    result[22] = {"NVML_FI_DEV_ECC_SBE_AGG_DEV",
                  "Device memory single bit aggregate (persistent) ECC errors"};
    result[23] = {"NVML_FI_DEV_ECC_DBE_AGG_DEV",
                  "Device memory double bit aggregate (persistent) ECC errors"};
    result[24] = {"NVML_FI_DEV_ECC_SBE_AGG_REG",
                  "Register File single bit aggregate (persistent) ECC errors"};
    result[25] = {"NVML_FI_DEV_ECC_DBE_AGG_REG",
                  "Register File double bit aggregate (persistent) ECC errors"};
    result[26] = {"NVML_FI_DEV_ECC_SBE_AGG_TEX",
                  "Texture memory single bit aggregate (persistent) ECC errors"};
    result[27] = {"NVML_FI_DEV_ECC_DBE_AGG_TEX",
                  "Texture memory double bit aggregate (persistent) ECC errors"};
    result[28] = {"NVML_FI_DEV_ECC_DBE_AGG_CBU", "CBU double bit aggregate ECC errors"};
    result[29] = {"NVML_FI_DEV_RETIRED_SBE", "Number of retired pages because of single bit errors"};
    result[30] = {"NVML_FI_DEV_RETIRED_DBE", "Number of retired pages because of double bit errors"};
    result[31] = {"NVML_FI_DEV_RETIRED_PENDING", "If any pages are pending retirement. 1=yes. 0=no."};
    result[32] = {"NVML_FI_DEV_NVLINK_CRC_FLIT_ERROR_COUNT_L0",
                  "NVLink flow control CRC  Error Counter for Lane 0"};
    result[33] = {"NVML_FI_DEV_NVLINK_CRC_FLIT_ERROR_COUNT_L1",
                  "NVLink flow control CRC  Error Counter for Lane 1"};
    result[34] = {"NVML_FI_DEV_NVLINK_CRC_FLIT_ERROR_COUNT_L2",
                  "NVLink flow control CRC  Error Counter for Lane 2"};
    result[35] = {"NVML_FI_DEV_NVLINK_CRC_FLIT_ERROR_COUNT_L3",
                  "NVLink flow control CRC  Error Counter for Lane 3"};
    result[36] = {"NVML_FI_DEV_NVLINK_CRC_FLIT_ERROR_COUNT_L4",
                  "NVLink flow control CRC  Error Counter for Lane 4"};
    result[37] = {"NVML_FI_DEV_NVLINK_CRC_FLIT_ERROR_COUNT_L5",
                  "NVLink flow control CRC  Error Counter for Lane 5"};
    result[38] = {"NVML_FI_DEV_NVLINK_CRC_FLIT_ERROR_COUNT_TOTAL",
                  "NVLink flow control CRC  Error Counter total for all Lanes"};
    result[39] = {"NVML_FI_DEV_NVLINK_CRC_DATA_ERROR_COUNT_L0", "NVLink data CRC Error Counter for Lane 0"};
    result[40] = {"NVML_FI_DEV_NVLINK_CRC_DATA_ERROR_COUNT_L1", "NVLink data CRC Error Counter for Lane 1"};
    result[41] = {"NVML_FI_DEV_NVLINK_CRC_DATA_ERROR_COUNT_L2", "NVLink data CRC Error Counter for Lane 2"};
    result[42] = {"NVML_FI_DEV_NVLINK_CRC_DATA_ERROR_COUNT_L3", "NVLink data CRC Error Counter for Lane 3"};
    result[43] = {"NVML_FI_DEV_NVLINK_CRC_DATA_ERROR_COUNT_L4", "NVLink data CRC Error Counter for Lane 4"};
    result[44] = {"NVML_FI_DEV_NVLINK_CRC_DATA_ERROR_COUNT_L5", "NVLink data CRC Error Counter for Lane 5"};
    result[45] = {"NVML_FI_DEV_NVLINK_CRC_DATA_ERROR_COUNT_TOTAL",
                  "NvLink data CRC Error Counter total for all Lanes"};
    result[46] = {"NVML_FI_DEV_NVLINK_REPLAY_ERROR_COUNT_L0", "NVLink Replay Error Counter for Lane 0"};
    result[47] = {"NVML_FI_DEV_NVLINK_REPLAY_ERROR_COUNT_L1", "NVLink Replay Error Counter for Lane 1"};
    result[48] = {"NVML_FI_DEV_NVLINK_REPLAY_ERROR_COUNT_L2", "NVLink Replay Error Counter for Lane 2"};
    result[49] = {"NVML_FI_DEV_NVLINK_REPLAY_ERROR_COUNT_L3", "NVLink Replay Error Counter for Lane 3"};
    result[50] = {"NVML_FI_DEV_NVLINK_REPLAY_ERROR_COUNT_L4", "NVLink Replay Error Counter for Lane 4"};
    result[51] = {"NVML_FI_DEV_NVLINK_REPLAY_ERROR_COUNT_L5", "NVLink Replay Error Counter for Lane 5"};
    result[52] = {"NVML_FI_DEV_NVLINK_REPLAY_ERROR_COUNT_TOTAL",
                  "NVLink Replay Error Counter total for all Lanes"};
    result[53] = {"NVML_FI_DEV_NVLINK_RECOVERY_ERROR_COUNT_L0", "NVLink Recovery Error Counter for Lane 0"};
    result[54] = {"NVML_FI_DEV_NVLINK_RECOVERY_ERROR_COUNT_L1", "NVLink Recovery Error Counter for Lane 1"};
    result[55] = {"NVML_FI_DEV_NVLINK_RECOVERY_ERROR_COUNT_L2", "NVLink Recovery Error Counter for Lane 2"};
    result[56] = {"NVML_FI_DEV_NVLINK_RECOVERY_ERROR_COUNT_L3", "NVLink Recovery Error Counter for Lane 3"};
    result[57] = {"NVML_FI_DEV_NVLINK_RECOVERY_ERROR_COUNT_L4", "NVLink Recovery Error Counter for Lane 4"};
    result[58] = {"NVML_FI_DEV_NVLINK_RECOVERY_ERROR_COUNT_L5", "NVLink Recovery Error Counter for Lane 5"};
    result[59] = {"NVML_FI_DEV_NVLINK_RECOVERY_ERROR_COUNT_TOTAL",
                  "NVLink Recovery Error Counter total for all Lanes"};
    result[60] = {"NVML_FI_DEV_NVLINK_BANDWIDTH_C0_L0", "NVLink Bandwidth Counter for Counter Set 0, Lane 0"};
    result[61] = {"NVML_FI_DEV_NVLINK_BANDWIDTH_C0_L1", "NVLink Bandwidth Counter for Counter Set 0, Lane 1"};
    result[62] = {"NVML_FI_DEV_NVLINK_BANDWIDTH_C0_L2", "NVLink Bandwidth Counter for Counter Set 0, Lane 2"};
    result[63] = {"NVML_FI_DEV_NVLINK_BANDWIDTH_C0_L3", "NVLink Bandwidth Counter for Counter Set 0, Lane 3"};
    result[64] = {"NVML_FI_DEV_NVLINK_BANDWIDTH_C0_L4", "NVLink Bandwidth Counter for Counter Set 0, Lane 4"};
    result[65] = {"NVML_FI_DEV_NVLINK_BANDWIDTH_C0_L5", "NVLink Bandwidth Counter for Counter Set 0, Lane 5"};
    result[66] = {"NVML_FI_DEV_NVLINK_BANDWIDTH_C0_TOTAL",
                  "NVLink Bandwidth Counter Total for Counter Set 0, All Lanes"};
    result[67] = {"NVML_FI_DEV_NVLINK_BANDWIDTH_C1_L0", "NVLink Bandwidth Counter for Counter Set 1, Lane 0"};
    result[68] = {"NVML_FI_DEV_NVLINK_BANDWIDTH_C1_L1", "NVLink Bandwidth Counter for Counter Set 1, Lane 1"};
    result[69] = {"NVML_FI_DEV_NVLINK_BANDWIDTH_C1_L2", "NVLink Bandwidth Counter for Counter Set 1, Lane 2"};
    result[70] = {"NVML_FI_DEV_NVLINK_BANDWIDTH_C1_L3", "NVLink Bandwidth Counter for Counter Set 1, Lane 3"};
    result[71] = {"NVML_FI_DEV_NVLINK_BANDWIDTH_C1_L4", "NVLink Bandwidth Counter for Counter Set 1, Lane 4"};
    result[72] = {"NVML_FI_DEV_NVLINK_BANDWIDTH_C1_L5", "NVLink Bandwidth Counter for Counter Set 1, Lane 5"};
    result[73] = {"NVML_FI_DEV_NVLINK_BANDWIDTH_C1_TOTAL",
                  "NVLink Bandwidth Counter Total for Counter Set 1, All Lanes"};
    result[74] = {"NVML_FI_DEV_PERF_POLICY_POWER", "Perf Policy Counter for Power Policy"};
    result[75] = {"NVML_FI_DEV_PERF_POLICY_THERMAL", "Perf Policy Counter for Thermal Policy"};
    result[76] = {"NVML_FI_DEV_PERF_POLICY_SYNC_BOOST", "Perf Policy Counter for Sync boost Policy"};
    result[77] = {"NVML_FI_DEV_PERF_POLICY_BOARD_LIMIT", "Perf Policy Counter for Board Limit"};
    result[78] = {"NVML_FI_DEV_PERF_POLICY_LOW_UTILIZATION",
                  "Perf Policy Counter for Low GPU Utilization Policy"};
    result[79] = {"NVML_FI_DEV_PERF_POLICY_RELIABILITY", "Perf Policy Counter for Reliability Policy"};
    result[80] = {"NVML_FI_DEV_PERF_POLICY_TOTAL_APP_CLOCKS",
                  "Perf Policy Counter for Total App Clock Policy"};
    result[81] = {"NVML_FI_DEV_PERF_POLICY_TOTAL_BASE_CLOCKS",
                  "Perf Policy Counter for Total Base Clocks Policy"};
    result[82] = {"NVML_FI_DEV_MEMORY_TEMP", "Memory temperature for the device"};
    result[83] = {"NVML_FI_DEV_TOTAL_ENERGY_CONSUMPTION",
                  "Total energy consumption for the GPU in mJ since the driver was last reloaded"};
    result[84] = {"NVML_FI_DEV_NVLINK_SPEED_MBPS_L0", "NVLink Speed in MBps for Link 0"};
    result[85] = {"NVML_FI_DEV_NVLINK_SPEED_MBPS_L1", "NVLink Speed in MBps for Link 1"};
    result[86] = {"NVML_FI_DEV_NVLINK_SPEED_MBPS_L2", "NVLink Speed in MBps for Link 2"};
    result[87] = {"NVML_FI_DEV_NVLINK_SPEED_MBPS_L3", "NVLink Speed in MBps for Link 3"};
    result[88] = {"NVML_FI_DEV_NVLINK_SPEED_MBPS_L4", "NVLink Speed in MBps for Link 4"};
    result[89] = {"NVML_FI_DEV_NVLINK_SPEED_MBPS_L5", "NVLink Speed in MBps for Link 5"};
    result[90] = {"NVML_FI_DEV_NVLINK_SPEED_MBPS_COMMON", "Common NVLink Speed in MBps for active links"};
    result[91] = {"NVML_FI_DEV_NVLINK_LINK_COUNT", "Number of NVLinks present on the device"};
    result[92] = {"NVML_FI_DEV_RETIRED_PENDING_SBE",
                  "If any pages are pending retirement due to SBE. 1=yes. 0=no."};
    result[93] = {"NVML_FI_DEV_RETIRED_PENDING_DBE",
                  "If any pages are pending retirement due to DBE. 1=yes. 0=no."};
    result[94] = {"NVML_FI_DEV_PCIE_REPLAY_COUNTER", "PCIe replay counter"};
    result[95] = {"NVML_FI_DEV_PCIE_REPLAY_ROLLOVER_COUNTER", "PCIe replay rollover counter"};
    result[96] = {"NVML_FI_DEV_NVLINK_CRC_FLIT_ERROR_COUNT_L6",
                  "NVLink flow control CRC  Error Counter for Lane 6"};
    result[97] = {"NVML_FI_DEV_NVLINK_CRC_FLIT_ERROR_COUNT_L7",
                  "NVLink flow control CRC  Error Counter for Lane 7"};
    result[98] = {"NVML_FI_DEV_NVLINK_CRC_FLIT_ERROR_COUNT_L8",
                  "NVLink flow control CRC  Error Counter for Lane 8"};
    result[99] = {"NVML_FI_DEV_NVLINK_CRC_FLIT_ERROR_COUNT_L9",
                  "NVLink flow control CRC  Error Counter for Lane 9"};
    result[100] = {"NVML_FI_DEV_NVLINK_CRC_FLIT_ERROR_COUNT_L10",
                   "NVLink flow control CRC  Error Counter for Lane 10"};
    result[101] = {"NVML_FI_DEV_NVLINK_CRC_FLIT_ERROR_COUNT_L11",
                   "NVLink flow control CRC  Error Counter for Lane 11"};
    result[102] = {"NVML_FI_DEV_NVLINK_CRC_DATA_ERROR_COUNT_L6", "NVLink data CRC Error Counter for Lane 6"};
    result[103] = {"NVML_FI_DEV_NVLINK_CRC_DATA_ERROR_COUNT_L7", "NVLink data CRC Error Counter for Lane 7"};
    result[104] = {"NVML_FI_DEV_NVLINK_CRC_DATA_ERROR_COUNT_L8", "NVLink data CRC Error Counter for Lane 8"};
    result[105] = {"NVML_FI_DEV_NVLINK_CRC_DATA_ERROR_COUNT_L9", "NVLink data CRC Error Counter for Lane 9"};
    result[106] = {"NVML_FI_DEV_NVLINK_CRC_DATA_ERROR_COUNT_L10",
                   "NVLink data CRC Error Counter for Lane 10"};
    result[107] = {"NVML_FI_DEV_NVLINK_CRC_DATA_ERROR_COUNT_L11",
                   "NVLink data CRC Error Counter for Lane 11"};
    result[108] = {"NVML_FI_DEV_NVLINK_REPLAY_ERROR_COUNT_L6", "NVLink Replay Error Counter for Lane 6"};
    result[109] = {"NVML_FI_DEV_NVLINK_REPLAY_ERROR_COUNT_L7", "NVLink Replay Error Counter for Lane 7"};
    result[110] = {"NVML_FI_DEV_NVLINK_REPLAY_ERROR_COUNT_L8", "NVLink Replay Error Counter for Lane 8"};
    result[111] = {"NVML_FI_DEV_NVLINK_REPLAY_ERROR_COUNT_L9", "NVLink Replay Error Counter for Lane 9"};
    result[112] = {"NVML_FI_DEV_NVLINK_REPLAY_ERROR_COUNT_L10", "NVLink Replay Error Counter for Lane 10"};
    result[113] = {"NVML_FI_DEV_NVLINK_REPLAY_ERROR_COUNT_L11", "NVLink Replay Error Counter for Lane 11"};
    result[114] = {"NVML_FI_DEV_NVLINK_RECOVERY_ERROR_COUNT_L6", "NVLink Recovery Error Counter for Lane 6"};
    result[115] = {"NVML_FI_DEV_NVLINK_RECOVERY_ERROR_COUNT_L7", "NVLink Recovery Error Counter for Lane 7"};
    result[116] = {"NVML_FI_DEV_NVLINK_RECOVERY_ERROR_COUNT_L8", "NVLink Recovery Error Counter for Lane 8"};
    result[117] = {"NVML_FI_DEV_NVLINK_RECOVERY_ERROR_COUNT_L9", "NVLink Recovery Error Counter for Lane 9"};
    result[118] = {"NVML_FI_DEV_NVLINK_RECOVERY_ERROR_COUNT_L10",
                   "NVLink Recovery Error Counter for Lane 10"};
    result[119] = {"NVML_FI_DEV_NVLINK_RECOVERY_ERROR_COUNT_L11",
                   "NVLink Recovery Error Counter for Lane 11"};
    result[120] = {"NVML_FI_DEV_NVLINK_BANDWIDTH_C0_L6",
                   "NVLink Bandwidth Counter for Counter Set 0, Lane 6"};
    result[121] = {"NVML_FI_DEV_NVLINK_BANDWIDTH_C0_L7",
                   "NVLink Bandwidth Counter for Counter Set 0, Lane 7"};
    result[122] = {"NVML_FI_DEV_NVLINK_BANDWIDTH_C0_L8",
                   "NVLink Bandwidth Counter for Counter Set 0, Lane 8"};
    result[123] = {"NVML_FI_DEV_NVLINK_BANDWIDTH_C0_L9",
                   "NVLink Bandwidth Counter for Counter Set 0, Lane 9"};
    result[124] = {"NVML_FI_DEV_NVLINK_BANDWIDTH_C0_L10",
                   "NVLink Bandwidth Counter for Counter Set 0, Lane 10"};
    result[125] = {"NVML_FI_DEV_NVLINK_BANDWIDTH_C0_L11",
                   "NVLink Bandwidth Counter for Counter Set 0, Lane 11"};
    result[126] = {"NVML_FI_DEV_NVLINK_BANDWIDTH_C1_L6",
                   "NVLink Bandwidth Counter for Counter Set 1, Lane 6"};
    result[127] = {"NVML_FI_DEV_NVLINK_BANDWIDTH_C1_L7",
                   "NVLink Bandwidth Counter for Counter Set 1, Lane 7"};
    result[128] = {"NVML_FI_DEV_NVLINK_BANDWIDTH_C1_L8",
                   "NVLink Bandwidth Counter for Counter Set 1, Lane 8"};
    result[129] = {"NVML_FI_DEV_NVLINK_BANDWIDTH_C1_L9",
                   "NVLink Bandwidth Counter for Counter Set 1, Lane 9"};
    result[130] = {"NVML_FI_DEV_NVLINK_BANDWIDTH_C1_L10",
                   "NVLink Bandwidth Counter for Counter Set 1, Lane 10"};
    result[131] = {"NVML_FI_DEV_NVLINK_BANDWIDTH_C1_L11",
                   "NVLink Bandwidth Counter for Counter Set 1, Lane 11"};
    result[132] = {"NVML_FI_DEV_NVLINK_SPEED_MBPS_L6", "NVLink Speed in MBps for Link 6"};
    result[133] = {"NVML_FI_DEV_NVLINK_SPEED_MBPS_L7", "NVLink Speed in MBps for Link 7"};
    result[134] = {"NVML_FI_DEV_NVLINK_SPEED_MBPS_L8", "NVLink Speed in MBps for Link 8"};
    result[135] = {"NVML_FI_DEV_NVLINK_SPEED_MBPS_L9", "NVLink Speed in MBps for Link 9"};
    result[136] = {"NVML_FI_DEV_NVLINK_SPEED_MBPS_L10", "NVLink Speed in MBps for Link 10"};
    result[137] = {"NVML_FI_DEV_NVLINK_SPEED_MBPS_L11", "NVLink Speed in MBps for Link 11"};
    result[138] = {"NVML_FI_DEV_NVLINK_THROUGHPUT_DATA_TX", "NVLink TX Data throughput in KiB"};
    result[139] = {"NVML_FI_DEV_NVLINK_THROUGHPUT_DATA_RX", "NVLink RX Data throughput in KiB"};
    result[140] = {"NVML_FI_DEV_NVLINK_THROUGHPUT_RAW_TX", "NVLink TX Data + protocol overhead in KiB"};
    result[141] = {"NVML_FI_DEV_NVLINK_THROUGHPUT_RAW_RX", "NVLink RX Data + protocol overhead in KiB"};
    result[142] = {"NVML_FI_DEV_REMAPPED_COR", "Number of remapped rows due to correctable errors"};
    result[143] = {"NVML_FI_DEV_REMAPPED_UNC", "Number of remapped rows due to uncorrectable errors"};
    result[144] = {"NVML_FI_DEV_REMAPPED_PENDING", "If any rows are pending remapping. 1=yes 0=no"};
    result[145] = {"NVML_FI_DEV_REMAPPED_FAILURE", "If any rows failed to be remapped 1=yes 0=no"};
    result[146] = {"NVML_FI_DEV_NVLINK_REMOTE_NVLINK_ID", "Remote device NVLink ID"};
    result[147] = {"NVML_FI_DEV_NVSWITCH_CONNECTED_LINK_COUNT", "Number of NVLinks connected to NVSwitch"};
    result[148] = {"NVML_FI_DEV_NVLINK_ECC_DATA_ERROR_COUNT_L0", "NVLink data ECC Error Counter for Link 0"};
    result[149] = {"NVML_FI_DEV_NVLINK_ECC_DATA_ERROR_COUNT_L1", "NVLink data ECC Error Counter for Link 1"};
    result[150] = {"NVML_FI_DEV_NVLINK_ECC_DATA_ERROR_COUNT_L2", "NVLink data ECC Error Counter for Link 2"};
    result[151] = {"NVML_FI_DEV_NVLINK_ECC_DATA_ERROR_COUNT_L3", "NVLink data ECC Error Counter for Link 3"};
    result[152] = {"NVML_FI_DEV_NVLINK_ECC_DATA_ERROR_COUNT_L4", "NVLink data ECC Error Counter for Link 4"};
    result[153] = {"NVML_FI_DEV_NVLINK_ECC_DATA_ERROR_COUNT_L5", "NVLink data ECC Error Counter for Link 5"};
    result[154] = {"NVML_FI_DEV_NVLINK_ECC_DATA_ERROR_COUNT_L6", "NVLink data ECC Error Counter for Link 6"};
    result[155] = {"NVML_FI_DEV_NVLINK_ECC_DATA_ERROR_COUNT_L7", "NVLink data ECC Error Counter for Link 7"};
    result[156] = {"NVML_FI_DEV_NVLINK_ECC_DATA_ERROR_COUNT_L8", "NVLink data ECC Error Counter for Link 8"};
    result[157] = {"NVML_FI_DEV_NVLINK_ECC_DATA_ERROR_COUNT_L9", "NVLink data ECC Error Counter for Link 9"};
    result[158] = {"NVML_FI_DEV_NVLINK_ECC_DATA_ERROR_COUNT_L10",
                   "NVLink data ECC Error Counter for Link 10"};
    result[159] = {"NVML_FI_DEV_NVLINK_ECC_DATA_ERROR_COUNT_L11",
                   "NVLink data ECC Error Counter for Link 11"};
    result[160] = {"NVML_FI_DEV_NVLINK_ECC_DATA_ERROR_COUNT_TOTAL",
                   "NVLink data ECC Error Counter total for all Links"};
    result[161] = {"NVML_FI_DEV_NVLINK_ERROR_DL_REPLAY", "NVLink Replay Error Counter"};
    result[162] = {"NVML_FI_DEV_NVLINK_ERROR_DL_RECOVERY", "NVLink Recovery Error Counter"};
    result[163] = {"NVML_FI_DEV_NVLINK_ERROR_DL_CRC", "NVLink CRC Error Counter"};
    result[164] = {"NVML_FI_DEV_NVLINK_GET_SPEED", "NVLink Speed in MBps"};
    result[165] = {"NVML_FI_DEV_NVLINK_GET_STATE", "NVLink State - one of NVML_NVLINK_STATE_* values"};
    result[166] = {"NVML_FI_DEV_NVLINK_GET_VERSION", "NVLink Version"};
    result[167] = {"NVML_FI_DEV_NVLINK_GET_POWER_STATE", "NVLink Power state. 0=HIGH_SPEED 1=LOW_SPEED"};
    result[168] = {"NVML_FI_DEV_NVLINK_GET_POWER_THRESHOLD",
                   "NVLink length of idle period (units can be found from"};
    result[169] = {"NVML_FI_DEV_PCIE_L0_TO_RECOVERY_COUNTER", "Device PEX error recovery counter"};
    result[170] = {"NVML_FI_DEV_C2C_LINK_COUNT", "Number of C2C Links present on the device"};
    result[171] = {"NVML_FI_DEV_C2C_LINK_GET_STATUS", "C2C Link Status 0=INACTIVE 1=ACTIVE"};
    result[172] = {"NVML_FI_DEV_C2C_LINK_GET_MAX_BW", "C2C Link Speed in MBps for active links"};
    result[173] = {"NVML_FI_DEV_PCIE_COUNT_CORRECTABLE_ERRORS", "PCIe Correctable Errors Counter"};
    result[174] = {"NVML_FI_DEV_PCIE_COUNT_NAKS_RECEIVED", "PCIe NAK Receive Counter"};
    result[175] = {"NVML_FI_DEV_PCIE_COUNT_RECEIVER_ERROR", "PCIe Receiver Error Counter"};
    result[176] = {"NVML_FI_DEV_PCIE_COUNT_BAD_TLP", "PCIe Bad TLP Counter"};
    result[177] = {"NVML_FI_DEV_PCIE_COUNT_NAKS_SENT", "PCIe NAK Send Counter"};
    result[178] = {"NVML_FI_DEV_PCIE_COUNT_BAD_DLLP", "PCIe Bad DLLP Counter"};
    result[179] = {"NVML_FI_DEV_PCIE_COUNT_NON_FATAL_ERROR", "PCIe Non Fatal Error Counter"};
    result[180] = {"NVML_FI_DEV_PCIE_COUNT_FATAL_ERROR", "PCIe Fatal Error Counter"};
    result[181] = {"NVML_FI_DEV_PCIE_COUNT_UNSUPPORTED_REQ", "PCIe Unsupported Request Counter"};
    result[182] = {"NVML_FI_DEV_PCIE_COUNT_LCRC_ERROR", "PCIe LCRC Error Counter"};
    result[183] = {"NVML_FI_DEV_PCIE_COUNT_LANE_ERROR", "PCIe Per Lane Error Counter."};
    result[184] = {"NVML_FI_DEV_IS_RESETLESS_MIG_SUPPORTED", "Device's Restless MIG Capability"};
    result[185] = {
        "NVML_FI_DEV_POWER_AVERAGE",
        "GPU power averaged over 1 sec interval, supported on Ampere (except GA100) or newer architectures."};
    result[186] = {"NVML_FI_DEV_POWER_INSTANT", "Current GPU power, supported on all architectures."};
    result[187] = {"NVML_FI_DEV_POWER_MIN_LIMIT", "Minimum power limit in milliwatts."};
    result[188] = {"NVML_FI_DEV_POWER_MAX_LIMIT", "Maximum power limit in milliwatts."};
    result[189] = {"NVML_FI_DEV_POWER_DEFAULT_LIMIT",
                   "Default power limit in milliwatts (limit which device boots with)."};
    result[190] = {"NVML_FI_DEV_POWER_CURRENT_LIMIT", "Limit currently enforced in milliwatts (This includes "
                                                      "other limits set elsewhere. E.g. Out-of-band)."};
    result[191] = {"NVML_FI_DEV_ENERGY",
                   "Total energy consumption (in mJ) since the driver was last reloaded. Same as \\ref "
                   "NVML_FI_DEV_TOTAL_ENERGY_CONSUMPTION for the GPU."};
    result[192] = {"NVML_FI_DEV_POWER_REQUESTED_LIMIT",
                   "Power limit requested by NVML or any other userspace client."};
    result[193] = {"NVML_FI_DEV_TEMPERATURE_SHUTDOWN_TLIMIT",
                   "T.Limit temperature after which GPU may shut down for HW protection"};
    result[194] = {"NVML_FI_DEV_TEMPERATURE_SLOWDOWN_TLIMIT",
                   "T.Limit temperature after which GPU may begin HW slowdown"};
    result[195] = {"NVML_FI_DEV_TEMPERATURE_MEM_MAX_TLIMIT",
                   "T.Limit temperature after which GPU may begin SW slowdown due to memory temperature"};
    result[196] = {"NVML_FI_DEV_TEMPERATURE_GPU_MAX_TLIMIT",
                   "T.Limit temperature after which GPU may be throttled below base clock"};
    result[197] = {"NVML_FI_DEV_PCIE_COUNT_TX_BYTES", "PCIe transmit bytes. Value can be wrapped."};
    result[198] = {"NVML_FI_DEV_PCIE_COUNT_RX_BYTES", "PCIe receive bytes. Value can be wrapped."};
    result[199] = {"NVML_FI_DEV_IS_MIG_MODE_INDEPENDENT_MIG_QUERY_CAPABLE",
                   "MIG mode independent, MIG query capable device. 1=yes. 0=no."};
    result[200] = {"NVML_FI_DEV_NVLINK_GET_POWER_THRESHOLD_MAX",
                   "Max Nvlink Power Threshold. See NVML_FI_DEV_NVLINK_GET_POWER_THRESHOLD"};
    result[201] = {"NVML_FI_DEV_NVLINK_COUNT_XMIT_PACKETS", "Total Tx packets on the link in NVLink5"};
    result[202] = {"NVML_FI_DEV_NVLINK_COUNT_XMIT_BYTES", "Total Tx bytes on the link in NVLink5"};
    result[203] = {"NVML_FI_DEV_NVLINK_COUNT_RCV_PACKETS", "Total Rx packets on the link in NVLink5"};
    result[204] = {"NVML_FI_DEV_NVLINK_COUNT_RCV_BYTES", "Total Rx bytes on the link in NVLink5"};
    result[205] = {"NVML_FI_DEV_NVLINK_COUNT_VL15_DROPPED", "Deprecated, do not use"};
    result[206] = {"NVML_FI_DEV_NVLINK_COUNT_MALFORMED_PACKET_ERRORS",
                   "Number of packets Rx on a link where packets are malformed"};
    result[207] = {"NVML_FI_DEV_NVLINK_COUNT_BUFFER_OVERRUN_ERRORS",
                   "Number of packets that were discarded on Rx due to buffer overrun"};
    result[208] = {"NVML_FI_DEV_NVLINK_COUNT_RCV_ERRORS", "Total number of packets with errors Rx on a link"};
    result[209] = {"NVML_FI_DEV_NVLINK_COUNT_RCV_REMOTE_ERRORS",
                   "Total number of packets Rx - stomp/EBP marker"};
    result[210] = {"NVML_FI_DEV_NVLINK_COUNT_RCV_GENERAL_ERRORS",
                   "Total number of packets Rx with header mismatch"};
    result[211] = {"NVML_FI_DEV_NVLINK_COUNT_LOCAL_LINK_INTEGRITY_ERRORS",
                   "Total number of times that the count of local errors exceeded a threshold"};
    result[212] = {"NVML_FI_DEV_NVLINK_COUNT_XMIT_DISCARDS",
                   "Total number of tx error packets that were discarded"};
    result[213] = {"NVML_FI_DEV_NVLINK_COUNT_LINK_RECOVERY_SUCCESSFUL_EVENTS",
                   "Number of times link went from Up to recovery, succeeded and link came back up"};
    result[214] = {"NVML_FI_DEV_NVLINK_COUNT_LINK_RECOVERY_FAILED_EVENTS",
                   "Number of times link went from Up to recovery, failed and link was declared down"};
    result[215] = {"NVML_FI_DEV_NVLINK_COUNT_LINK_RECOVERY_EVENTS",
                   "Number of times link went from Up to recovery, irrespective of the result"};
    result[216] = {"NVML_FI_DEV_NVLINK_COUNT_RAW_BER_LANE0", "Deprecated, do not use"};
    result[217] = {"NVML_FI_DEV_NVLINK_COUNT_RAW_BER_LANE1", "Deprecated, do not use"};
    result[218] = {"NVML_FI_DEV_NVLINK_COUNT_RAW_BER", "Deprecated, do not use"};
    result[219] = {"NVML_FI_DEV_NVLINK_COUNT_EFFECTIVE_ERRORS",
                   "Sum of the number of errors in each Nvlink packet"};
    result[220] = {"NVML_FI_DEV_NVLINK_COUNT_EFFECTIVE_BER", "Effective BER for effective errors"};
    result[221] = {"NVML_FI_DEV_NVLINK_COUNT_SYMBOL_ERRORS", "Number of errors in rx symbols"};
    result[222] = {"NVML_FI_DEV_NVLINK_COUNT_SYMBOL_BER", "BER for symbol errors"};
    result[223] = {"NVML_FI_DEV_NVLINK_GET_POWER_THRESHOLD_MIN",
                   "Min Nvlink Power Threshold. See NVML_FI_DEV_NVLINK_GET_POWER_THRESHOLD"};
    result[224] = {"NVML_FI_DEV_NVLINK_GET_POWER_THRESHOLD_UNITS",
                   "Values are in the form NVML_NVLINK_LOW_POWER_THRESHOLD_UNIT_*"};
    result[225] = {"NVML_FI_DEV_NVLINK_GET_POWER_THRESHOLD_SUPPORTED",
                   "Determine if Nvlink Power Threshold feature is supported"};
    result[226] = {"NVML_FI_DEV_RESET_STATUS",
                   "Depracated, do not use (use NVML_FI_DEV_GET_GPU_RECOVERY_ACTION instead)"};
    result[227] = {"NVML_FI_DEV_DRAIN_AND_RESET_STATUS",
                   "Deprecated, do not use (use NVML_FI_DEV_GET_GPU_RECOVERY_ACTION instead)"};
    result[228] = {"NVML_FI_DEV_PCIE_OUTBOUND_ATOMICS_MASK", ""};
    result[229] = {"NVML_FI_DEV_PCIE_INBOUND_ATOMICS_MASK", ""};
    result[230] = {"NVML_FI_DEV_GET_GPU_RECOVERY_ACTION",
                   "GPU Recovery action. See \\ref nvmlDeviceGpuRecoveryAction_t"};
    result[231] = {"NVML_FI_DEV_C2C_LINK_ERROR_INTR", "C2C Link CRC Error Counter"};
    result[232] = {"NVML_FI_DEV_C2C_LINK_ERROR_REPLAY", "C2C Link Replay Error Counter"};
    result[233] = {"NVML_FI_DEV_C2C_LINK_ERROR_REPLAY_B2B", "C2C Link Back to Back Replay Error Counter"};
    result[234] = {"NVML_FI_DEV_C2C_LINK_POWER_STATE", "C2C Link Power state. See NVML_C2C_POWER_STATE_*"};
    result[235] = {"NVML_FI_DEV_NVLINK_COUNT_FEC_HISTORY_0",
                   "Count of symbol errors that are corrected - bin 0"};
    result[236] = {"NVML_FI_DEV_NVLINK_COUNT_FEC_HISTORY_1",
                   "Count of symbol errors that are corrected - bin 1"};
    result[237] = {"NVML_FI_DEV_NVLINK_COUNT_FEC_HISTORY_2",
                   "Count of symbol errors that are corrected - bin 2"};
    result[238] = {"NVML_FI_DEV_NVLINK_COUNT_FEC_HISTORY_3",
                   "Count of symbol errors that are corrected - bin 3"};
    result[239] = {"NVML_FI_DEV_NVLINK_COUNT_FEC_HISTORY_4",
                   "Count of symbol errors that are corrected - bin 4"};
    result[240] = {"NVML_FI_DEV_NVLINK_COUNT_FEC_HISTORY_5",
                   "Count of symbol errors that are corrected - bin 5"};
    result[241] = {"NVML_FI_DEV_NVLINK_COUNT_FEC_HISTORY_6",
                   "Count of symbol errors that are corrected - bin 6"};
    result[242] = {"NVML_FI_DEV_NVLINK_COUNT_FEC_HISTORY_7",
                   "Count of symbol errors that are corrected - bin 7"};
    result[243] = {"NVML_FI_DEV_NVLINK_COUNT_FEC_HISTORY_8",
                   "Count of symbol errors that are corrected - bin 8"};
    result[244] = {"NVML_FI_DEV_NVLINK_COUNT_FEC_HISTORY_9",
                   "Count of symbol errors that are corrected - bin 9"};
    result[245] = {"NVML_FI_DEV_NVLINK_COUNT_FEC_HISTORY_10",
                   "Count of symbol errors that are corrected - bin 10"};
    result[246] = {"NVML_FI_DEV_NVLINK_COUNT_FEC_HISTORY_11",
                   "Count of symbol errors that are corrected - bin 11"};
    result[247] = {"NVML_FI_DEV_NVLINK_COUNT_FEC_HISTORY_12",
                   "Count of symbol errors that are corrected - bin 12"};
    result[248] = {"NVML_FI_DEV_NVLINK_COUNT_FEC_HISTORY_13",
                   "Count of symbol errors that are corrected - bin 13"};
    result[249] = {"NVML_FI_DEV_NVLINK_COUNT_FEC_HISTORY_14",
                   "Count of symbol errors that are corrected - bin 14"};
    result[250] = {"NVML_FI_DEV_NVLINK_COUNT_FEC_HISTORY_15",
                   "Count of symbol errors that are corrected - bin 15"};
    result[251] = {"NVML_FI_PWR_SMOOTHING_ENABLED", "Enablement (0/DISABLED or 1/ENABLED)"};
    result[252] = {"NVML_FI_PWR_SMOOTHING_PRIV_LVL", "Current privilege level"};
    result[253] = {"NVML_FI_PWR_SMOOTHING_IMM_RAMP_DOWN_ENABLED",
                   "Immediate ramp down enablement (0/DISABLED or 1/ENABLED)"};
    result[254] = {"NVML_FI_PWR_SMOOTHING_APPLIED_TMP_CEIL", "Applied TMP ceiling value in Watts"};
    result[255] = {"NVML_FI_PWR_SMOOTHING_APPLIED_TMP_FLOOR", "Applied TMP floor value in Watts"};
    result[256] = {"NVML_FI_PWR_SMOOTHING_MAX_PERCENT_TMP_FLOOR_SETTING", "Max % TMP Floor value"};
    result[257] = {"NVML_FI_PWR_SMOOTHING_MIN_PERCENT_TMP_FLOOR_SETTING", "Min % TMP Floor value"};
    result[258] = {"NVML_FI_PWR_SMOOTHING_HW_CIRCUITRY_PERCENT_LIFETIME_REMAINING",
                   "HW Circuitry % lifetime remaining"};
    result[259] = {"NVML_FI_PWR_SMOOTHING_MAX_NUM_PRESET_PROFILES", "Max number of preset profiles"};
    result[260] = {"NVML_FI_PWR_SMOOTHING_PROFILE_PERCENT_TMP_FLOOR", "% TMP floor for a given profile"};
    result[261] = {"NVML_FI_PWR_SMOOTHING_PROFILE_RAMP_UP_RATE", "Ramp up rate in mW/s for a given profile"};
    result[262] = {"NVML_FI_PWR_SMOOTHING_PROFILE_RAMP_DOWN_RATE",
                   "Ramp down rate in mW/s for a given profile"};
    result[263] = {"NVML_FI_PWR_SMOOTHING_PROFILE_RAMP_DOWN_HYST_VAL",
                   "Ramp down hysteresis value in ms for a given profile"};
    result[264] = {"NVML_FI_PWR_SMOOTHING_ACTIVE_PRESET_PROFILE", "Active preset profile number"};
    result[265] = {"NVML_FI_PWR_SMOOTHING_ADMIN_OVERRIDE_PERCENT_TMP_FLOOR",
                   "% TMP floor for a given profile"};
    result[266] = {"NVML_FI_PWR_SMOOTHING_ADMIN_OVERRIDE_RAMP_UP_RATE",
                   "Ramp up rate in mW/s for a given profile"};
    result[267] = {"NVML_FI_PWR_SMOOTHING_ADMIN_OVERRIDE_RAMP_DOWN_RATE",
                   "Ramp down rate in mW/s for a given profile"};
    result[268] = {"NVML_FI_PWR_SMOOTHING_ADMIN_OVERRIDE_RAMP_DOWN_HYST_VAL",
                   "Ramp down hysteresis value in ms for a given profile"};
    result[269] = {"NVML_FI_DEV_CLOCKS_EVENT_REASON_SW_THERM_SLOWDOWN",
                   "Throttling to ensure ((GPU temp < GPU Max Operating Temp) && (Memory Temp < Memory Max "
                   "Operating Temp)) in ns"};
    result[270] = {
        "NVML_FI_DEV_CLOCKS_EVENT_REASON_HW_THERM_SLOWDOWN",
        "Throttling due to temperature being too high (reducing core clocks by a factor of 2 or more) in ns"};
    result[271] = {"NVML_FI_DEV_CLOCKS_EVENT_REASON_HW_POWER_BRAKE_SLOWDOWN",
                   "Throttling due to external power brake assertion trigger (reducing core clocks by a "
                   "factor of 2 or more) in ns"};
    result[272] = {"NVML_FI_DEV_POWER_SYNC_BALANCING_FREQ",
                   "Accumulated frequency of the GPU to be used for averaging"};
    result[273] = {"NVML_FI_DEV_POWER_SYNC_BALANCING_AF",
                   "Accumulated activity factor of the GPU to be used for averaging"};
    result[274] = {"NVML_FI_DEV_EDPP_MULTIPLIER", "EDPp multiplier expressed as a percentage"};
    result[275] = {"NVML_FI_PWR_SMOOTHING_PRIMARY_POWER_FLOOR", ""};
    result[276] = {"NVML_FI_PWR_SMOOTHING_SECONDARY_POWER_FLOOR", ""};
    result[277] = {"NVML_FI_PWR_SMOOTHING_MIN_PRIMARY_FLOOR_ACT_OFFSET", ""};
    result[278] = {"NVML_FI_PWR_SMOOTHING_MIN_PRIMARY_FLOOR_ACT_POINT", ""};
    result[279] = {"NVML_FI_PWR_SMOOTHING_WINDOW_MULTIPLIER", ""};
    result[280] = {"NVML_FI_PWR_SMOOTHING_DELAYED_PWR_SMOOTHING_SUPPORTED", ""};
    result[281] = {"NVML_FI_PWR_SMOOTHING_PROFILE_SECONDARY_POWER_FLOOR", ""};
    result[282] = {"NVML_FI_PWR_SMOOTHING_PROFILE_PRIMARY_FLOOR_ACT_WIN_MULT", ""};
    result[283] = {"NVML_FI_PWR_SMOOTHING_PROFILE_PRIMARY_FLOOR_TAR_WIN_MULT", ""};
    result[284] = {"NVML_FI_PWR_SMOOTHING_PROFILE_PRIMARY_FLOOR_ACT_OFFSET", ""};
    result[285] = {"NVML_FI_PWR_SMOOTHING_ADMIN_OVERRIDE_SECONDARY_POWER_FLOOR", ""};
    result[286] = {"NVML_FI_PWR_SMOOTHING_ADMIN_OVERRIDE_PRIMARY_FLOOR_ACT_WIN_MULT", ""};
    result[287] = {"NVML_FI_PWR_SMOOTHING_ADMIN_OVERRIDE_PRIMARY_FLOOR_TAR_WIN_MULT", ""};
    result[288] = {"NVML_FI_PWR_SMOOTHING_ADMIN_OVERRIDE_PRIMARY_FLOOR_ACT_OFFSET", ""};
    result[289] = {"NVML_FI_DEV_NVLINK_COUNT_RAW_ERRORS_LANE0", "NVLINK raw error count for lane 0"};
    result[290] = {"NVML_FI_DEV_NVLINK_COUNT_RAW_ERRORS_LANE1", "NVLINK raw error count for lane 1"};
    result[291] = {"NVML_FI_DEV_NVLINK_COUNT_RAW_BER_LANE0_V2", "NVLINK raw BER for lane 0"};
    result[292] = {"NVML_FI_DEV_NVLINK_COUNT_RAW_BER_LANE1_V2", "NVLINK raw BER for lane 1"};
    result[293] = {"NVML_FI_DEV_NVLINK_COUNT_RAW_BER_V2", "NVLINK total raw BER"};
    result[294] = {"NVML_FI_DEV_NVLINK_PLR_XMIT_BLOCKS", "NVLINK PLR Xmit Blocks"};
    result[295] = {"NVML_FI_DEV_NVLINK_PLR_XMIT_RETRY_BLOCKS", "NVLINK PLR Xmit Retry Blocks"};
    result[296] = {
        "NVML_FI_DEV_NVLINK_GET_DATA_RATE",
        "The Effective Nvlink Data rate available for transactions after accounting for FEC overhead"};
    result[297] = {"NVML_FI_DEV_MMA_STALL_PERCENT", "MMA (Matrix Multiply Accumulate) stall percentage"};
    result[298] = {"NVML_FI_DEV_MCLK_SWITCH_TYPE", "See NVML_MCLK_SWITCH_TYPE_<XYZ> for all enumerations"};
    result[299] = {"NVML_FI_DEV_MCLK_MIN_SWITCH_INTERVAL_MILLISECONDS",
                   "minimum required elapsed time between runtime mclk switches, 0 = no rate limit"};
    result[300] = {"NVML_FI_PWR_SMOOTHING_SOC_POWER_SMOOTHING_ENABLED",
                   "State-Of-Charge Power Smoothing Enabled (0/DISABLED or 1/ENABLED)"};
    result[301] = {"NVML_FI_DEV_REMAPPED_ROWS_COR_INACTIVE",
                   "Number of inactive row remappings due to correctable errors"};
    result[302] = {"NVML_FI_DEV_REMAPPED_ROWS_UNC_INACTIVE",
                   "Number of inactive row remappings due to uncorrectable errors"};
    result[303] = {"NVML_FI_DEV_ACTIVE_BANK_REMAPPINGS", "Number of active bank remappings"};
    result[304] = {"NVML_FI_DEV_INACTIVE_BANK_REMAPPINGS", "Number of inactive bank remappings"};
    result[305] = {"NVML_FI_DEV_BANK_REMAPPER_HISTOGRAM_MAX",
                   "Number of groups with full bank remap availability."};
    result[306] = {"NVML_FI_DEV_BANK_REMAPPER_HISTOGRAM_NONE",
                   "Number of groups with no spare bankremap availability."};
    result[307] = {"NVML_FI_DEV_PENDING_BANK_REMAPPING", "If any banks are pending remapping. 1=yes 0=no"};
    return result;
}();
} // namespace probe
