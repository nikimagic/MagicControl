# EC 固件可读化工作流

## 证据来源

当前符号恢复同时使用三类互相独立的证据：

1. 官方 GCU Service 反编译中的 `ECSpec.cs`、`RamFan1p5_ECSpec.cs` 和实际读写调用；
2. `mech-forza-control/docs/ec-register-map.md` 中经过控制台、ACPI 和真机行为交叉验证的寄存器语义；
3. EC 固件自身对这些 XRAM 地址的 `MOVX` 访问。

提取后的可复现符号表保存在 `tools/ec_registers.tsv`。其中代码地址与 XRAM 地址属于
不同地址空间：例如 `CODE:0751` 与 `EXTMEM:0751` 完全不是同一个对象。旧的
`disasm51 --force` 输出会为两者生成外观相同的数字标签，因此不能做无上下文的全局替换。

## 两种输出

### Ghidra 伪 C

```sh
tools/export_readable_ec.sh samples/disasm/main-bank0.bin build/readable-main0
```

Ghidra 的 8051 模型能区分 CODE、INTMEM、SFR 和 EXTMEM。脚本会建立完整的 64 KiB
EC/XRAM 地址空间，安装已知寄存器名，并导出伪 C。例如主机邮箱分派可以恢复为：

```c
if (ec_host_command_mailbox == 0x22) {
    func_0x012f();
} else if (ec_host_command_mailbox == 0x23) {
    func_0x018b();
} else if (ec_host_command_mailbox == 0x24) {
    func_0x029e();
}
```

这比无语义的 `UNK_EXTMEM_1110` 更适合继续恢复参数和返回值。

### 保守的 `.d52` 注释

```sh
python3 tools/annotate_disassembly.py samples/disasm/main-bank1.d52 \
  -o build/readable/main-bank1.annotated.d52 \
  --xrefs build/readable/main-bank1-xrefs.md
```

对当前 2.12 bank 1，此命令可以标注 691 个常量地址引用，并区分 read、write、
read-modify-write 和“地址被传递/计算”。它只识别 `MOV DPTR,#常量`，不会猜测运行时
计算出的 DPTR，也不会因为同值 CODE 标签而改写其他指令。

这里必须注意 `disasm51.py` 对当前镜像生成了 `org 0-1h`，其 `dptr_NNNN` 符号名系统性
地比指令机器码中的 16 位立即数小 1。例如文本里的 `#dptr_0522` 对应原始字节
`90 05 23`，真实 XRAM 地址是 `0x0523`。注释工具默认用 `--symbol-bias 1` 修正这一点；
分析地址时应以修正后的注释或原始机器码为准，不能直接抄未修正的符号后缀。

还必须把“函数入口”与“顺序相邻的前导指令”分开。当前固件会复用带 `RET` 的公共尾部，
调用者可以直接跳入另一个逻辑块的中间。例如 `CODE:E051` 先读取 `XRAM[0x0857]`，随后
落入 `CODE:E055` 比较 A 与 25；电池老化函数直接调用 `E055`，并在调用前把
`XRAM[0x0A54]` 放入 A。因此不能因为 `E051` 与 `E055` 连续，就把 `E051` 的寄存器读取
归给所有 `E055` 调用者。`tools/ec_functions.tsv` 应记录这种真实共享入口，伪 C 仍须结合
调用点寄存器状态和原始 `LCALL` 目标复核。

## 多版本固件现状

固件集合中可以明确确认两个带发布 changelog 的完整版本：

| 版本 | SHA-256 | 与 2.12 的关系 |
|---|---|---|
| 2.10 | `f1f58956…367014c` | 主 EC 两个 bank 不同 |
| 2.12 | `34c050d3…6a38c2` | 当前真机版本 |

二者共有完全相同的 PD bank 0/1；92,007 个差异字节全部位于主 EC 的前 128 KiB。
这说明 2.12 changelog 中的 `Support FW18_WA` 对当前样本主要体现为主 EC 的整体重编译
或配置变化，不能靠连续字节 diff 直接解释为 92,007 个独立逻辑修改。

BIOS capsule 中还提取出多组 256 KiB EC 候选镜像，包含机械革命 MRO17、MRO50、
XMG、Slimbook GOS05/GOS07 变体。MRO17 和 XMG 候选很可能对应 2.04、2.01，但在把
版本号写入符号数据库前，仍需用镜像内版本字段、byte-sum 或配套发布记录验证；文件所在
目录本身不作为版本证据。

## 当前限制与下一步

- Ghidra 能从带复位向量的 bank 0 建立控制流。`extract_bank_entries.py` 已从 common
  bank 的机器码中恢复 402 个 bank 0、62 个 bank 1、47 个 bank 2 wrapper。bank 1
  不再需要 `--force` 才能寻找代码入口；但相邻的多入口函数、尾调用和共享 epilogue 仍会
  造成部分 Ghidra 函数边界重叠。
- `--force` 反汇编包含数据被误解释成指令的区域。寄存器名能提高可读性，但不能证明每条
  输出都是可执行代码。
- 同一 EC 地址可能被不同硬件代际复用。符号表保留了这些歧义，具体函数命名必须同时检查
  project ID、support flags 和调用上下文。
- 下一阶段应按“访问的寄存器集合”给函数聚类，例如电池遥测、风扇模式、风扇表、键盘
  背光、充电限制，再结合 GCU Service 的写入顺序为函数命名。
- 2.10/2.12 的大规模重编译差异需要使用函数级相似度匹配，而不是仅比较相同文件偏移。

## bank 1 可读化结果

用 62 个恢复入口分析 2.12 `main-bank1.bin`，再安装人工确认的额外函数边界后，Ghidra
共输出 382 个函数记录，成功反编译 381 个；只有 `CODE:0x1C60` 因自由 varnode 错误
失败。补充 SMBus staging、descriptor、result 和 charger queue 字段后，
`summarize_pseudoc.py` 找到 92 个直接引用已命名 EC/XRAM 寄存器的函数。

当前已能由函数体直接确认以下关系：

- `CODE:E456 publish_main_fan_duty` 将内部 PWM 状态 `XRAM[0x1804]` 发布到控制台读取的
  `EC[0x075B]`；大型风扇 worker 同样将 `XRAM[0x1809]` 发布到 `EC[0x075C]`。
- `CODE:E181 is_ap_fan_management_enabled` 检查 `EC[0x07C6].2`。风扇 worker 还同时
  检查 `ApExistFlag`、`EC[0x0727].6` 和 user/turbo mode 位，这从固件侧验证了官方
  控制台写入顺序中的几个关键 gate。
- `CODE:E136`、`CODE:E45F` 分别检查 `EC[0x0751]` 的 user-fan bit 7 和 turbo bit 4。
- `CODE:E2B3 clear_custom_mode_flag` 直接清除 `EC[0x0726].7`。
- `CODE:E2DC is_battery_below_41_percent` 直接把公开 RSOC `EC[0x04AB]` 与 41 比较。
- `CODE:E952 has_rgb_keyboard` 读取官方 capability map 中的 `EC[0x0766].2`。
- `CODE:8EFB service_charger_smbus_queue` 消费 charger 命令位并发送 `0x14/0x15`；
  `CODE:DF0B prepare_smbus_word_descriptor` 将 word-data 指针固定到 `0x00C0`。
- common/bank0 的 `CODE:80D2 execute_smbus_transaction` 保存原始 host status，使用
  `0x00/0xEE` 表示成功/失败并以 `R7=1/0` 返回。bank0 名称单独保存在
  `tools/ec_functions-main0.tsv`，避免污染同地址的 bank1 函数。
- `CODE:1D08` 是 fixed common 区到 `bank1:E64F` 的 wrapper。`E64F` 本身不改 `R7`，
  但现场主线和 INT1 ISR 共用 register bank 2，ISR 未保存 `R0..R7`；`D264` 的同值影子
  `0x039D=0x1B` 证明写 `0x0A54` 前 `R7` 已被中断路径污染。

大型入口 `0x95A6/0x95DD/0x9C91` 都进入风扇 duty 计算和发布路径；
`0x9C78/0xA1FD/0xA265/0xA2AB/0xA2CE/0xA333/0xA34C` 同时访问当前 PL、
BatterySaver PL、GPU D-state、TGP/MyFan 字段和电源来源，应视为一组平台/模式变体，
在确认其入口参数和 project-ID 选择条件前暂不赋予更具体名称。

## 2.10 跨版本验证

2.10 common bank 中恢复出 400/62/47 个 bank 0/1/2 wrapper，bank 1 的入口数量与
2.12 相同，但大部分地址整体迁移。不能把 2.12 的函数地址直接套用到旧版。

`translate_function_symbols.py` 以经过人工确认的短函数完整机器码做唯一匹配：25 个 2.12
符号中有 20 个在 2.10 找到唯一匹配。三个较大的/非标准边界电池函数没有可安全提取的
短签名，另两个电源状态 bit accessor 因出现 2–3 个相同实现而拒绝自动迁移。使用迁移后的
入口和符号，2.10 bank 1 导出 347/347 个函数，其中
62 个直接引用已命名 EC 寄存器。这验证了 AP-present、AP fan management、user/turbo
fan mode、custom flag、公开 fan duty、RSOC 41% 比较以及 battery trip-point 检查等语义
在两个版本间保持稳定，
同时保留了对短重复函数的歧义保护。

生成的伪 C 和注释汇编都是分析产物，不是可重新编译的厂商源码，也不应直接作为补丁地址
依据。任何修改仍须回到原始字节、bank 映射和实际控制流交叉确认。

电池容量学习、循环计数来源和老化充电电压补偿的专项分析见
[`battery_aging.md`](battery_aging.md)。
