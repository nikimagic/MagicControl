# GXxHXxx 21.200 改版固件索引（2026-07-23）

改版固件的可执行产物与生成器统一位于仓库根目录
`firmware_mods/GXxHXxx_21.200/`，本文目录只保存调查、设计、维护和验证文档。

## 版本关系

```text
samples/GXxHXxx_21.200（官方基线）
  -> cal-direct-only-exp-20260723（只改容量校准，未实机验证）
  -> int1-r7-20260722（已初步实机验证）
       -> int1-r7-cal-direct-exp-20260723（未实机验证的实验版）
  -> mode-only-voltage-20260723（只保留 0x07A6 模式降额，未实机验证）
```

| 版本目录 | 直接输入 | 修改 | 状态 |
|---|---|---|---|
| `cal-direct-only-exp-20260723` | 官方基线 | 只跳过容量校正 `+/-300 mAh` clamp | 未实机验证 |
| `int1-r7-20260722` | 官方基线 | INT1 保存/恢复 bank2 R7 | 已初步实机验证 |
| `int1-r7-cal-direct-exp-20260723` | INT1-R7 版本 | 跳过容量校正 `+/-300 mAh` clamp | 未实机验证 |
| `mode-only-voltage-20260723` | 官方基线 | D1D1 仅保留 `0x07A6` 模式分档 | 未实机验证 |

每个版本目录包含且只包含对应的 Python 生成器和 256 KiB BIN。生成器锁定直接输入的长度、
SHA-256 与待修改位置原字节，版本必须按上图顺序生成，不能把任意改版固件互换为输入。

## 可重复构建与幂等性

三个生成器都是确定性的：始终从各自不变的直接基线读取并输出到另一个路径时，重复运行会
得到相同 SHA-256，也可以安全覆盖此前生成的同版本输出。

它们不是可对任意固件执行的幂等变换。把生成结果再次作为同一脚本的输入，或把官方基线、
INT1-R7 版和校准实验版交叉传给错误脚本，都会因输入 SHA-256 不符而拒绝。脚本还会拒绝
输入、输出为同一路径，防止第一次执行就覆盖版本基线。这个 fail-closed 行为是刻意的版本
约束，不应改成静默跳过或猜测已有补丁。

## 文档

- `GXxHXxx_21.200_int1_r7_firmware_notes_20260722.md`：INT1-R7 镜像身份和维护入口；
- `firmware_binary_repair_design_20260722.md`：INT1-R7 二进制补丁设计；
- `charging_voltage_isr_repair_20260722.md`：R7 重入故障背景；
- `int1_host_trace_20260722.md`：修改前动态追踪；
- `battery_calibration_direct_commit_feasibility_20260722.md`：直接提交校准候选值的设计、风险和实施记录；
- `battery_calibration_direct_only_firmware_notes_20260723.md`：只改容量校准版本的产物身份与边界。
