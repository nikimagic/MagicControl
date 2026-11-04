# EC 固件分析报告 (Slimbook GOS07 / EC 2.12)

固件: `samples/GXxHXxx_21.200` (256KB)
主控芯片: IT5571VG-128 (ITE 增强 8052 内核)
PD EC:    ITE8850-PD V0.27
反汇编工具: disasm51 1.0.1, 输出见各 `*.d52` 与 `it5571.mcu`

## 1. 镜像布局

256KB 镜像 = 两块共驻一片 SPI flash 的独立 8051 程序:

| 文件区间 | 内容 | 说明 |
|---|---|---|
| `0x00000-0x1FFFF` | 主 EC 程序 (IT5571) | 2×64KB code bank, 复位向量在 `0x0`(LJMP 0x0070) |
| `0x20000-0x3FFFF` | PD EC 程序 (ITE8850) | 自带复位向量 `0x20000`(LJMP 0x83B7) 与完整 IVT |

切片位于 `samples/disasm/`: `main-bank0.bin`/`main-bank1.bin`/`pd-bank0.bin`/`pd-bank1.bin`。
PD 侧字符串直接给出身份: 文件偏移 `0x20040` 处 `ITE8850-PD.V0.27`，与 CHANGELOG 中 "PD FW: v27" 一致。

## 2. 主 EC 启动序列 (RESET @ jump_0070)

`main-bank0.d52` `jump_0070`:

1. 栈指针 `SP ← #0xC0`
2. 写控制寄存器 `data[0x1001] ← 0x3F`
3. 调用四个初始化子程序: `0x110A`(选 bank0)、`0x159A`、`0x0FC3`、`0x15A0`
4. 复制 `data[0x2006] → data[0x0004]`
5. 进入 INIT 解释器 `jump_00D2`: 从 `data[0x79C6]` 读取清零/赋值表，遍历清 RAM/XRAM —— 典型 Keil/C51 启动
6. `ljmp jump_0200` 进入主循环

## 3. Bank 切换

`P1.0/P1.1/P1.2` 用作外部 code-space 地址线 A16/A17/A18。三个 thunk 固定入口:

- `jump_1100` (bank0): `R0←0x0A`, `P1=000`
- `jump_1114` (bank1): `R0←0x1E`, `P1=001` (P1.0 置位)
- `jump_1128` (bank2): `R0←0x32`, `P1=010`

模式: `push 保存原 R0/bank → 设新 bank → ret` 落到新 bank 的目标地址, 返回再用逆 thunk 恢复 —— Keil 的「common+banked」分组方案, R0(`data[8]`) 记当前 bank 号。

## 4. 主循环与调度器

主循环 `jump_0272`–`jump_028F` (`main-bank0.d52`):

```text
jump_0272: lcall 任务派发(0x1522/0x0CF8/0x063D...)
           若 26h/27h 全 0 → mov PCON,#01  (IDLE)  → 等中断唤醒
           中断置标志 → 主循环再次跑 jump_0CF8
```

`jump_0CF8` 是位标志任务调度器: `26h`/`27h` 每一位代表一个任务请求, 优先级从低到高逐位清零调用:

| 标志 | 处理函数 | 含义 |
|---|---|---|
| `26h.5` | `jump_0DCA` | Timer0 ms-tick 工作者 (最常触发) |
| `26h.2` | `jump_0AB6` | Timer1 超时事件 |
| `26h.0` | `jump_3F8C` | (从 `0x1304.1` 推动的高优先级任务) |
| `26h.6` | `jump_0808` | (从 `0x1500.1` 推动) |
| `27h.0` | `jump_0D97` | 适配器/电源状态变化采集 |

`jump_0DCA` 是 ms 级相位调度器: 按 `44h`(ms 计数)、`45h`/`46h` 在不同 tick 相位上调用
`jump_1540/1546/154C/1552/1558/155E/1564/156A/1570/1576`, 把费时的周期性工作摊到 tick 相位上, 避免单次 ISR 拖延。
这些相位函数多数是 `mov DPTR,#0x8700… ; ljmp jump_1128`, 即 far-call 进 bank2 共享库。
`jump_0E70` 在每个 tick 处扫 `0x40h.4`→`26h.7` 推电源任务。

## 5. 中断

| 向量 | 地址 | 作用 |
|---|---|---|
| INT0  `0x03`→`0x0532` | `reti` | 未用 |
| TIMER0 `0x0B`→`0x0533` | 心跳 | `jump_0EAC` 重装 TL0/TH0, 置 `26h.5`, 递减 `data[0x0A00]` |
| INT1  `0x13`→`0x0559` | 主机邮箱 | 读 `data[0x1110]` 命令字并派发 |
| TIMER1 `0x1B`→`0x05BF` | 超时/看门狗喂 | 处理 `41h.4`→`26h.2`, 调 `0x0EC0/0ECB` |
| SERIAL `0x23`→`0x05E7` | `reti` | bank0 未用 |
| TIMER2 `0x2B`→`0x05E8` | `reti` | 未用 |

Timer0 重装值 `TL0=#06h/TH0=#0F1h`, `TMOD=#11h`(T0/T1 均模式1)。

## 6. 主机命令邮箱 (INT1 / data[0x1110])

`jump_0559` 读 `data[0x1110]`, 按命令字派发:

- `0x22` → `jump_012F`
- `0x23` → `jump_018B`
- `0x24` → `jump_029E`  (含充电源 0x1708 判定, 见 §7)
- 其余 → 查表 `data[0x0637]` 间接跳转 (扩展命令表)

`jump_029E` 末尾 `lcall jump_063D` 触发主机响应: 置 `data[0x3290].7`、`data[0x1151].4`,
`lcall jump_05F0`(setb EA)/`05F3`(置 0x1107.0)/`05FB`(置 0x1107.2 + 0x114D.6), `ljmp jump_14CE`。
`jump_060A` 清邮箱 `data[0x1100..1103]←0xFF`。这是标准主-EC 通知 host 的状态位协议。

## 7. 外设子系统

DPTR 访问集中区 (硬件寄存器位于外部 MOVX 空间):

| 区间 | 功能 |
|---|---|
| `0x0Axx` | RAM 支撑的状态/配置变量 (DS1302/系统状态, 最多引用) |
| `0x04xx`/`0x06xx` | 查找表、配置数据 |
| `0x08xx` | XRAM 工作缓冲 |
| `0x16xx`/`0x18xx`/`0x19xx` | 风扇 / 温控 / PWM / SMBus |
| `0x17xx` | 充电器 / 适配器 |
| `0x1Exx`/`0x1Fxx` | GPIO 控制 / 第二 SMBus 通道 |
| `0x90xx`/`0x91xx`/`0x94xx` | ADC / GPIO 组 |
| `0x32xx` | (少量) 看门狗/复位控制 |

**SMBus 主机 (双通道)**:
通道 A 在 `0x1803`(控制)/`0x1808`(数据), 通道 B 在 `0x1F05`/`0x1F08`。
初始化 `main-bank1.d52 ~1C77`: `0x1803←0x78`, `0x1808←0x78`, `0x1F05←0x00`, `0x1F08←0xF0`。
电池/充电器/温度传感器经此读写。

**充电 / 适配器**: `data[0x1708].3` 判定适配器在线 (`jump_029E`), `data[0x16E7]` 复位时置 `.7`,
`data[0x1500].1` 推动 `26h.6`(充电管理任务)。`0x1304.1` 推 `26h.0`。

**风扇 PWM 驱动** (`main-bank1.d52` `~jump_8001`):
`0x1652`→fan1 PWM duty, `0x1654`→fan2 duty, `0x1655/1656`→fan 配置;
`0x18FF←0x88; 0x18FF←0x80`(tach/控制), `0x1900←0xA0`, `0x1901←0x15(21)`, `0x1904←0x80`(使能)。
这是 IT5571 内置风扇模块寄存器组。风速查表经 `0xA13F`(`dptr_03CB` 子程序: `lcall 0x8499; movc A,@A+DPTR; 写 0x1500`)。

**电源/休眠**: `0x1E05` GPIO 比较+`0x1E02←0x01`, `clr IE.7`, `orl PCON,#02h`(PD 模式)
(`main-bank1.d52 ~jump_059A`) —— 进入 power-down, 由事件唤醒。

## 8. PD EC (ITE8850-PD V0.27)

独立 8051 程序, 镜像 `0x20000-0x3FFFF`, 自有 IVT:

| 向量 | 目标 | | 向量 | 目标 |
|---|---|---|---|---|
| RESET `0x0`→`0x83B7` | `0x83B7` 标准C51启动(清内RAM `0x00-0x7F`、清XRAM、`SP←#0x17`、`lcall 0x500A`、`ljmp 0x8415`) | | INT0→`0x56` | 完整保存上下文(push ACC/B/DPH/DPL/PSW, R0-7…) |
| T0→`0x94` | 完整 ISR | | INT1→`0xB2` | 完整 ISR |
| T1→`0xF0` | | Serial→`0x10E` | |

复位序列 `0x83B7`(文件 `0x283B7`)逐字节解出为:
`mov R0,#7F; clr A; @R0←A; djnz R0` (清内RAM) → `mov R0,#1Fh; ... mov 09h,#FFh` (清XRAM方向)
→ `mov SP,#17h; lcall 0x500A; ljmp 0x8415`。
PD EC 负责电池充电管理、USB-C/PD 协议等。它和主 EC 通过板上总线通信, 但复位/中断/主循环各自独立,
只是共用同一片 SPI flash 存放二进制。

## 9. 复现命令

```bash
cd samples/disasm
# 主 EC bank0 (带 IT5571 SFR 与中断向量)
disasm51 --include ../../tools/it5571.mcu \
  --entry RESET --entry 0x3 --entry 0xB --entry 0x13 --entry 0x1B --entry 0x23 --entry 0x2B \
  main-bank0.bin > main-bank0.d52
# 主 EC bank1 / PD bank1: --force 全覆盖
disasm51 --force main-bank1.bin > main-bank1.d52
disasm51 --force pd-bank1.bin   > pd-bank1.d52
# PD bank0 (自带向量 + 主 EC far-call 入口)
disasm51 --include ../../tools/it5571.mcu \
  --entry 0x56 --entry 0x94 --entry 0xB2 --entry 0xF0 --entry 0x10E \
  --entry 0x8700 --entry 0x8A11 --entry 0x8A2C --entry 0x8A63 --entry 0x8AB4 \
  --entry 0x8ABE --entry 0x8B6B --entry 0x8CF6 --entry 0x8DDE --entry 0x8E0E \
  pd-bank0.bin > pd-bank0c.d52
```

## 10. 待深入

- 主机邮箱命令字 `0x22/0x23/0x24` 各自语义 (需跟踪 `jump_012F/018B/029E`)。
- `data[0x0637]` 扩展命令表完整条目。
- `jump_3B63/0E70/E7C/CF8` 等 phase worker 的真实硬件动作。
- PD 主循环 `0x8415` 与 PD/SMBus 路由细节。
- 电压/电流/温度的具体寄存器字段(需 IT5571 datasheet 核对 0x90xx/0x91xx/0x17xx)。
