# I2EC 电池调查：可见范围、只读补丁与刷后验证

## 目标和结论

当前主机工具通过 `0xFED50000` 的 H2RAM aperture 访问 EC，只能看到 lower 4 KiB 中被四个
窗口开放的部分。电池老化调查最关键的 `0x09C7–0x09CA` 恰好位于不可见窗口，因此现有
H2RAM 工具无法区分真实 high-voltage stress、未初始化 SRAM 和 stale target。

需要特别区分两种“不可读写”：文档中对 `0x200D`、`0x1000` 以上地址的不可读写结论，指
`0xFED50000` H2RAM/MMIO 和 ACPI `ECRR/ECRW` 主机路径；I2EC 启用后使用的是另一套
EC-memory address latch。因而 `i2ec_rw.py read 0x200d` 能读到 `0xCB` 并不推翻 H2RAM
权限结论，它说明 I2EC 通道本身已经开放，而且当前低两位是 `11b`。

原固件已经配置专用 I2EC 端口：

```text
0x681  I2EC_XADDR_H
0x682  I2EC_XADDR_L
0x683  I2EC_XDATA
```

初始化表先写入 `SPCTRL1=0x43`，随后公共代码 OR `0xC8`，所以原厂镜像最终为
`0xCB`，即 I2EC read-write。把两张初始化表的 `43 -> 42` 后，最终才会变成
`0xCA` read-only。无论哪种模式，启用后都可寻址完整的 16 位 EC memory address
`0x0000–0xFFFF`，包括原先 H2RAM 看不到的内部 SRAM。

这里的“EC memory”不等于 64 KiB 普通 RAM：其中混有 SRAM、未实现地址和片上外设寄存器。
I2EC 手册保证读取不会触发 read-clear，但动态、多字节外设值仍可能在逐字节读取之间变化。

## 修改前能读取什么

`/home/texsd/codes/mech-forza-control/tools/ec_rw.py` 使用 H2RAM/MMIO 或等价 ACPI 方法。
它当前的硬件权限是：

| XRAM 范围 | 读取 | 写入 | 电池调查价值 |
|---|:---:|:---:|---|
| `0x0000–0x03FF` | 是 | 否 | 电池遥测缓存和未降额基准 |
| `0x0400–0x04FF` | 是 | 是 | 公开电池状态；多数是 firmware-owned，不应写 |
| `0x0500–0x05FF` | 是 | 否 | desired target、gate、会话计数器 |
| `0x0600–0x06FF` | 是 | 否 | 部分保护和平台状态 |
| `0x0700–0x07FF` | 是 | 是 | 充电模式等 AP 控制区 |
| `0x0800–0x0BFF` | 否 | 否 | stress、charger queue、SMBus 结果均在这里 |
| `0x0C00–0x0FFF` | 是 | 是 | 共享工作区和风扇表；不等于全部适合写入 |
| `>=0x1000` | 否 | 否 | H2RAM aperture 之外 |

修改前已经可以可靠采集的关键量：

| 地址 | 字节序 | 含义 | 限制 |
|---:|---|---|---|
| `0x030E:0x030F` | big-endian | 电池侧未降额目标，当前约 `17600 mV` | 不是 EC 固定常量 |
| `0x0438:0x0439` | little-endian | 实时电池包电压 | 动态值 |
| `0x0490` | byte | AC/电池会话和 aging gate | 多生产者状态字，不应手工控制 |
| `0x0491` | byte | bits 7:6 编码 2S/3S/4S | 当前计算使用 4S |
| `0x0497` | byte | 电池遥测会话状态 | firmware-owned |
| `0x04A2:0x04A3` | little-endian | 电池温度，单位 `0.1 K` | 动态值 |
| `0x04A6:0x04A7` | little-endian | 循环次数 | 来自电池侧遥测路径 |
| `0x0522:0x0523` | little-endian | EC desired charge target | 不是 charger readback |
| `0x05B9` | byte | 独立状态路径 gate；不是 `D1D1` 直接 gate | 只观察 |
| `0x05F1–0x05F5` | bytes | AC/电池会话去抖计数器 | 动态值 |
| `0x0741` | byte | ApExistFlag 等状态 | 只观察 |
| `0x07A6` | byte | bits 5:4 为充电模式下限 | 软件定义可写，但实验期间只读 |

现有工具示例：

```sh
cd /home/texsd/codes/mech-forza-control

# big-endian 17600 mV 基准：高字节地址在前
sudo uv run tools/ec_rw.py read 0x030e 0x030f

# little-endian word：ec_rw.py 的双地址形式要求先传高字节地址
sudo uv run tools/ec_rw.py read 0x0439 0x0438
sudo uv run tools/ec_rw.py read 0x0523 0x0522
sudo uv run tools/ec_rw.py dump 0x0490 8
sudo uv run tools/ec_rw.py dump 0x05b9 1
sudo uv run tools/ec_rw.py dump 0x05f1 5
```

## 修改前不可读取的关键量

| 地址 | 字节序 | 含义 | 为什么关键 |
|---:|---|---|---|
| `0x0832` | byte | charger command retry/pending flags | bit 1 对应 ChargingVoltage 请求 |
| `0x0836:0x0837` | little-endian | queued charge-voltage mirror | 判断 desired target 是否进入队列 |
| `0x09C7` | byte | high-voltage 第一级计数器 | 与 `60` 比较 |
| `0x09C8` | byte | high-voltage 第二级计数器 | 与 `60` 比较 |
| `0x09C9:0x09CA` | big-endian | 温度加权 high-voltage stress | 决定 50–250 mV/cell 老化档位 |
| `0x0A54` | byte | aging 路径共享 scratch | 只适合结合执行时序解释 |
| `0x0A73` | byte | SMBus 合并结果，失败时可为 `0xEE` | 判断 charger transaction 结果 |
| `0x0A74` | byte | 原始 SMBus host status | 区分具体失败原因 |
| `0x0A75` | byte | SMBus success flag | 成功时为 1，失败时为 0 |

这些地址位于 H2RAM 的 `0x0800–0x0BFF` 空洞。更换 `/dev/mem`、ACPI 或 Windows driver
不会突破硬件窗口；必须启用 I2EC 或修改 H2RAM 窗口配置。I2EC 方案不牺牲现有 H2RAM
窗口，因此更适合首轮只读调查。

## 只读固件需要修改什么

原始 256 KiB 镜像 SHA-256：

```text
34c050d30772da07ef262fc7016e0677b9b1b4cdcd90cf43d93f0f15bf6a38c2
```

main EC common bank 有两处完全相同的 RMW：

```text
90 20 0D    MOV  DPTR,#0x200D
E0          MOVX A,@DPTR
44 C8       ORL  A,#0xC8
F0          MOVX @DPTR,A
```

此前建议把两处立即数从 `0xC8` 改为 `0xCA`，但这只能执行
`A = A | 0xCA`，不会清除进入函数前已经为 1 的 bit 0。真机读到 `0x200D = 0xCB`
已经证明这个假设不成立：`CB & 3 == 3`，I2EC 当前是读写模式。该旧补丁不再作为只读
方案使用。

继续追踪固件初始化表后，发现 `0x200D` 有两个实际的表驱动写入者：

```text
CODE 0x0FF7–0x1035：表基址 0x1085，第二项 20 0D 43
CODE 0xDF66：表基址 0x604B，第二项 20 0D 43
```

两张表都使用 `{address_hi, address_lo, value}` 三字节格式。首个表由 `0x0FAF` 调用，
第二个表由 `0xDE7C` 调用；随后两个公共初始化函数再执行 `ORL A,#0xC8`。因此最小、
语义最直接的只读补丁是把两张表的初值 `0x43` 改为 `0x42`：

| 文件/Flash 偏移 | CODE 位置 | 原值 | 新值 |
|---:|---:|---:|---:|
| `0x0108A` | 表项 `20 0D 43` | `43` | `42` |
| `0x06050` | 表项 `20 0D 43` | `43` | `42` |

`0x42 | 0xC8 = 0xCA`，保留原本设置的 port 80/81 相关位，并把 `I2ECCTRL[1:0]` 设置为
`10b`。两字节替换不改变代码长度、代码地址、跳转、bank thunk、复位向量或 PD blocks。
两处都改可以覆盖两条初始化路径。

该补丁依赖 IT5570 A 手册和复位默认 `I2ECCTRL=00b`。由于目标实物是 IT5571 D，静态
分析只能证明控制流不变，不能替代真机验证。首轮实验不得运行任何会向 `0x683` 写数据的
其他工具；本仓库 `i2ec_rw.py` 的普通 `read/dump` 路径不会写 data port。

原固件已经在 `CODE:0xDF47–0xDF59` 设置：

```text
SPCTRL2.PI2ECEN = 1
PI2ECH = 0x06
PI2ECL = 0x80
```

所以不需要修改端口译码初始化、H2RAM 窗口、PD 固件或电池策略常量。

## 制作和离线核验原则

如果刷写器只写官方有效区，可以基于原始 256 KiB 镜像制作补丁。如果刷写器会整片写入
1 MiB，则必须以本机已经双读验证的完整 1 MiB dump 为基底，只修改同样的两个绝对偏移；
不要把另一个机器的 dump 当作整片基底。

制作完成后至少核验：

```sh
# 只有两个 byte 不同
python3 tools/firmware_tool.py diff BASE.bin I2EC-RO.bin

# 两处上下文必须分别出现 44 ca
xxd -g1 -s 0x0a208 -l 16 I2EC-RO.bin
xxd -g1 -s 0x0c33d -l 16 I2EC-RO.bin

# 长度必须与选择的基底完全一致
stat -c '%s %n' BASE.bin I2EC-RO.bin
```

对于这个修正版，期望 diff 只有 2 个字节：

```text
0x0108A: 43 -> 42
0x06050: 43 -> 42
```

对当前仓库中的两个已知基底只做上述两字节替换，期望 SHA-256 为：

| 基底 | 修改后长度 | 修改后 SHA-256 |
|---|---:|---|
| `samples/GXxHXxx_21.200` | 262144 | `3b063effdd583c0a6897bf824583f0d5712b808c9f8a6ceb5b0524d0d5f4efc5` |
| `samples/ec-full-1m.bin` | 1048576 | `149b8b120eac5c9fbc9f0a663cfe78e56d017819f76a909bf27cc6438193deaf` |

这些哈希只适用于当前仓库中的确切基底。若重新从真机取得的 dump 与仓库样本哈希不同，
应以新 dump 做逐字节 diff 和独立留档，不能强求匹配上表，也不能直接刷上表对应镜像。

还应确认：

1. `0x00000–0x3FFFF` 除这两字节外与原镜像一致；
2. 若为 1 MiB 镜像，`0x40000–0xFFFFF` 与本机原始完整 dump 一致；
3. 复位向量 `02 00 70`、PD 边界 `0x20000` 和四个 64 KiB block 均未改变；
4. 保留原始 1 MiB dump、官方 256 KiB 镜像及各自 SHA-256；
5. 在修改版刷写前，已有不依赖正常运行 EC 的恢复路径。

本仓库现在提供 `ec-flash`：写入流程按 EFI 标准设备路径实现，并在每个擦除块
编程后完整读回校验；但该写路径仍未经过自动化真机测试。若需要刷写，只应使用
经过当前镜像、JEDEC 容量和恢复方案核对的版本，不要绕过其对齐、厂商路径与 verify
检查。

## 刷写后的分级验证

### 2026-07-19 真机验证结果

使用本仓库 I2EC 工具实际读取：

```text
I2EC  EC[0x200D] = CB
I2EC  EC[0x0454] = 8C
I2EC  EC[0x0490] = 07
I2EC  EC[0x09C7–0x09CA] = 14 1B 2B DC
```

随后使用原 H2RAM 工具交叉读取：

```text
H2RAM EC[0x0454] = 8C
H2RAM EC[0x0490] = 07
```

两个独立通道对已知地址逐字节一致，证明 `i2ec_rw.py read 0x200d` 的结果具有有效性，
不是 `/dev/port` 残值或 H2RAM 地址混淆。`0xCB & 3 == 3` 表明当前真机 I2EC 是
read-write，而不是预期的 read-only；在刷入两张初始化表 `43->42` 的修正版前，不得执行
I2EC write。

隐藏 stress word 为：

```text
stress = (0x2B << 8) | 0xDC = 0x2BDC = 11228
0x21C0 < 0x2BDC <= 0x2D00
```

这正好落在 150 mV/cell 档。当前为 4S，因此 `4 * 150 = 600 mV`，直接解释了已观测的
`17600 -> 17000 mV`。这把此前“隐藏 stress 或未命名持久状态”的候选收敛为当前时刻
实际存在的 high-voltage stress 档。

### 1. 先确认 EC 正常启动

刷写和 EC reset 后，先不要运行 I2EC dump。确认：

- 机器能够正常上电和进入操作系统；
- 内建键盘、风扇、充电、AC 检测工作；
- EC 固件版本、风扇 RPM、电池电压和温度仍能通过原 H2RAM 工具读取；
- USB-C/PD 行为没有变化；本补丁未修改 PD blocks，但仍应做基本观察。

若出现无法开机、风扇全速、键盘失效、充电异常或 EC 反复复位，应停止调查并恢复原镜像。

### 2. 验证 I2EC 通道确实开放

```sh
cd /home/texsd/Workdir/ec_reverse

sudo python3 tools/i2ec_rw.py read 0x200d
sudo python3 tools/i2ec_rw.py read 0x0454
sudo python3 tools/i2ec_rw.py read 0x0490
```

成功标准：

1. `0x200D` 读值的低两位为 `10b`，典型值应为 `0xCA`；判断条件是
   `(value & 0x03) == 0x02`，不能只依赖完整字节恒等；
2. I2EC 读取的 `0x0454` 固件版本与现有 H2RAM 工具一致；
3. 在相邻时间读取 `0x0490` 时，两条通道结果一致或符合动态变化；
4. 可以重复读取 `0x09C7–0x09CA`；该区可能因为未初始化 SRAM 而真实等于
   `FF FF FF FF`，所以不能用“非 FF”单独判定通道成功。

在制作只读补丁前，可以先记录原厂的 `0xCB` 基线。只要 `0x200D & 3 == 3` 且已知地址
跨通道一致，I2EC 读路径已经有效；即使 stress 四字节全为 `FF`，也应把它解释为待调查的
数据状态，而不是立即判定 I2EC 失败。

### 3. 读取新增的电池关键状态

```sh
sudo python3 tools/i2ec_rw.py dump 0x09c7 4
sudo python3 tools/i2ec_rw.py read 0x0a54
sudo python3 tools/i2ec_rw.py read 0x0832
sudo python3 tools/i2ec_rw.py dump 0x0836 2
sudo python3 tools/i2ec_rw.py dump 0x0a73 3
```

字节组合方式：

```text
stress = (EC[0x09C9] << 8) | EC[0x09CA]       # big-endian
queued = EC[0x0836] | (EC[0x0837] << 8)       # little-endian
```

不要把一次读值直接视为稳定事实。建议连续采样，并把下列量放在同一时间轴：

```text
030E–030F  base target
0438–0439  battery voltage
0490       gate bits
0491       cell count encoding
04A2–04A3  temperature
04A6–04A7  cycle count
0522–0523  desired target
0832       charger pending flags
0836–0837  queued target
09C7–09CA  counters and stress
0A73–0A75  SMBus result/status
```

优先实验是固定温度，分别记录：稳定 AC、拔出 AC、插回 AC、电池会话重建和 EC reset。
这能直接判断 `17000 mV` 是否来自 `0x09C9:0x09CA` 的 150 mV/cell 档，以及冷启动后
stress 是否因未初始化 SRAM 而出现不同值。

## 开启后仍然不能证明什么

即使全部上述地址可读，仍不能仅凭 I2EC 证明：

- `0x0522:0x0523` 是 charger 实际输出；它只是 EC desired target；
- `0x0836:0x0837` 是 charger readback；它只是 queued mirror；
- SMBus worker 报成功就代表模拟环路中的真实 CV 精确等于目标；仍需总线抓取或硬件测量；
- 两次逐字节读取组成的动态 word 必然原子一致；必要时重复读取直到前后稳定；
- 所有 `0x0000–0xFFFF` 地址都是有效 RAM，或适合写入。

首轮只读固件不应测试 `write`。只有另行制作 `I2ECCTRL=11b` 固件后，协议才允许写整个
16 位 EC memory；这会同时开放普通 RAM 和危险外设寄存器，安全边界完全不同，不属于本次
电池只读调查。

## 回退条件

遇到以下任一情况，停止 I2EC 实验并恢复原固件：

- `0x200D & 3` 不是 `2`；
- `0x200D & 3 == 3`，这表示当前已是读写模式，不得继续使用任何写入功能；
- 已知 H2RAM 地址通过 I2EC 读取持续不一致；
- 读取导致明显卡顿、EC watchdog、风扇/键盘/充电异常；
- `0x680–0x683` 与系统现有 I/O resource 冲突；
- 有其他程序同时访问 I2EC 地址或 data port；
- 无法确认刷写镜像只有预期两个字节变化。

I2EC 的地址高、地址低和数据端口共用全局 latch。同一时刻只能运行一个 I2EC 客户端；
`tools/i2ec_rw.py` 的 `/dev/port` lock 只能协调同样遵守该锁的实例，不能阻止其他直接
`outb`/`ioperm` 工具插入事务。
