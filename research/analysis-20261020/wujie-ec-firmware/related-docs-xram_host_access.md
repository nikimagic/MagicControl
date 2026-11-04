# IT5571 XRAM 主机可见窗口与隐藏访问路径

## 结论

主机从 `0xFED50000` 读写的不是 IT5571 完整的 64 KiB XRAM，而是 SMFI/H2RAM 暴露的
**DLM lower 4K** 窗口。DSDT 虽然声明了连续 4 KiB MMIO aperture，但 EC 固件又在这
4 KiB 内配置了四个独立的允许窗口；未被窗口覆盖的地址不会转成 EC `MOVX` 事务。

2.12 固件 `bank0:0xE2D7 configure_h2ram_host_windows` 写入的配置可完整解码为：

| 窗口 | EC 基址 | 大小 | 主机可读 | 主机可写 |
|---:|---:|---:|:---:|:---:|
| 0 | `0x000` | 1024 B | 全部 | 全部禁止 |
| 1 | `0x400` | 512 B | 全部 | 仅下半区 `0x400–0x4FF` |
| 2 | `0x600` | 512 B | 全部 | 仅上半区 `0x700–0x7FF` |
| 3 | `0xC00` | 1024 B | 全部 | 全部允许 |

因此理论主机权限图是：

| XRAM | MMIO 读取 | MMIO 写入 | 原因 |
|---:|:---:|:---:|---|
| `0x000–0x3FF` | 是 | 否 | window 0，全窗口 write-protect |
| `0x400–0x4FF` | 是 | 是 | window 1 下半区 |
| `0x500–0x5FF` | 是 | 否 | window 1 上半区 write-protect |
| `0x600–0x6FF` | 是 | 否 | window 2 下半区 write-protect |
| `0x700–0x7FF` | 是 | 是 | window 2 上半区 |
| `0x800–0xBFF` | 否 | 否 | 没有任何 H2RAM window 覆盖 |
| `0xC00–0xFFF` | 是 | 是 | window 3 |
| `>=0x1000` | 否 | 否 | H2RAM 只映射 DLM lower 4K |

这解释了为什么 `0x7FF` 后出现大片不可访问区、到较高页面又重新出现可写地址。若实测只有
`0xDxx/0xExx` 的部分字节能稳定回读，不代表硬件 ACL 只开放这些字节：window 3 的硬件
粒度是整个 `0xC00–0xFFF` 可读写。该区大量地址由 EC 固件周期更新、清零或作为状态机
工作区，主机刚写入的值可能立刻被 firmware owner 覆盖；“总线写事务被允许”和“值能长期
保持”必须分开判断。

## 三个不同的地址空间

当前工具和文档容易把下列对象都称为 “EC RAM”，实际并不相同：

1. 8051 的 `MOVX`/EXTMEM 地址空间是 16 位，固件可直接访问 `0x0000–0xFFFF`；其中还
   混有 RAM、片上外设寄存器和其他映射对象。
2. SMFI 的 H2RAM 功能只允许主机映射 **数据空间最低 4 KiB**，并由四个窗口做权限控制。
3. ACPI `ECRR/ECRW` 和 Linux `/dev/mem` backend 都只是访问物理
   `0xFED50000 + offset`，没有额外的 16 位间接寻址协议。

本机 DSDT 同时提供了两处直接证据：

```asl
OperationRegion (ECMG, SystemMemory, 0xFED50000, 0x1000)

Method (ECRR, 1, NotSerialized) {
    Local0 = (0xFED50000 + Arg0)
    Return (MMRW (Local0, Zero, Zero, Zero))
}

Method (ECRW, 2, NotSerialized) {
    Local0 = (0xFED50000 + Arg0)
    MMRW (Local0, One, Zero, Arg1)
}
```

所以更换 Windows 驱动、`acpi_call` 或 `/dev/mem` 只能改变访问软件，不能越过 EC 硬件
窗口。读取未映射或受读保护地址时，相邻 IT5570 手册规定的 Host Error Response 是
`0xFF`；写入受保护地址被忽略。

## 固件如何配置四个窗口

IT5570 A 手册给出的 SMFI EC-interface base 是 `XRAM[0x1000]`。IT5571 D 固件使用完全
相同的一组寄存器地址和编码：

| XRAM | IT5570 名称 | 2.12 写入值 | 解码 |
|---:|---|---:|---|
| `0x105B` | `HRAMW0BA` | `0x00` | window 0 base `0x000` |
| `0x105C` | `HRAMW1BA` | `0x40` | window 1 base `0x400` |
| `0x1076` | `HRAMW2BA` | `0x60` | window 2 base `0x600` |
| `0x1077` | `HRAMW3BA` | `0xC0` | window 3 base `0xC00` |
| `0x105D` | `HRAMW0AAS` | `0x36` | 1024 B；全部写保护 |
| `0x105E` | `HRAMW1AAS` | `0x25` | 512 B；上半区写保护 |
| `0x1078` | `HRAMW2AAS` | `0x15` | 512 B；下半区写保护 |
| `0x1079` | `HRAMW3AAS` | `0x06` | 1024 B；无读写保护 |
| `0x105A` | `HRAMWC` | `0x0F` | memory/FWH path，四个窗口全部启用 |

原始机器码位于 2.12 `main-bank0.bin` 的 `CODE:0xE2D7–0xE30C`。2.10 固件中同一初始化
迁移到约 `0xE2BA`，但九个配置值完全相同，说明这是稳定的平台访问策略，而不是 2.12 的
偶然初始化残留。

这套划分也符合固件用途：低地址电池和 ACPI 遥测以只读方式公开；`0x04xx`、`0x07xx`
只放行需要 AP 控制的半区；`0x08xx–0x0Bxx` 保存 charger、计数器和 SMBus 工作状态而被
刻意隐藏；高窗口则覆盖 `0x0Dxx` 控制状态及 `0x0Fxx` 风扇表。

## 能否“开启”隐藏的 `0x800–0xBFF`

硬件上可以重新配置，但 stock 主机接口不能自行完成：

- 四个窗口的 base/size/protection 位是 `0x105A–0x1079` 的 **EC-interface registers**，
  手册明确它们只能由 EC 内核访问。
- `0xFED50000` 本身只映射 lower 4K DLM；把 offset 加到 `0x105A` 不会访问 SMFI 配置
  寄存器，而是越过 ACPI 声明的 aperture。
- 当前邮箱命令分析没有发现“任意 XRAM 读/写”服务，GCU 的 WMI/ACPI 方法也只是直接
  MMIO，没有替代的间接读取命令。

可行的工程方案是修改 EC 固件的 `configure_h2ram_host_windows`，把某个窗口暂时移动到
`0x800–0xBFF`，或重新规划四个窗口覆盖完整 4 KiB。代价是必须牺牲/缩小现有窗口或改变
其保护粒度，并可能破坏电池、ACPI、控制台或风扇表接口。H2RAM 无论如何不能暴露
`0x1000–0xFFFF`。

不建议在运行中的机器上直接试写窗口配置。错误的 base、重叠窗口或保护位会使 ACPI 与
EC 同时访问错误对象；至少应准备外置 SPI 恢复路径，并先在离线镜像中做最小补丁和完整
反汇编核验。

## 是否存在完整 XRAM 的隐藏读取通道

### 关于 `0x200D` 的“不可读写”表述

本文前面所说的“`0x200D` 不可读写”是指**当前 stock 主机访问路径**：
`0xFED50000 + 0x200D` 超出 DLM lower-4K H2RAM aperture，`ECRR/ECRW` 和
`/dev/mem` 后端不能用这个地址访问 EC-interface register。它不表示芯片内部没有
`0x200D`，也不表示启用调试型 I2EC 后不能 snoop 该地址。

I2EC 使用独立的 16 位 EC memory address latch（本机 dedicated port 为 `0x681–0x683`），
因此启用 I2EC 后可以把 `0x200D` 作为 EC memory target 读取。读取到 `0xCB` 时，按
`SPCTRL1.I2ECCTRL[1:0]` 定义，低两位 `11b` 表示 I2EC read-write；这与 H2RAM 对
`0x200D` 不可达并不矛盾。

2026-07-19 真机用 I2EC 读取 `0x0454=0x8C`、`0x0490=0x07`，随后用 H2RAM/MMIO 读取
同样得到 `0x8C`、`0x07`。该跨通道一致性确认 `i2ec_rw.py read 0x200D` 返回的 `0xCB`
是有效 EC memory 读值，不是端口残值。它同时证明当前运行状态已经开放 read-write。继续
审计发现两张初始化表都包含 `20 0D 43`：表驱动写入 `0x200D=0x43` 后，两个公共
初始化点的 `ORL #0xC8` 正好得到 `0xCB`，因此 bit 0 的来源已经在固件中找到。

### I2EC：stock 固件已配置 dedicated path

IT5570 的 Debugger/EC Memory Snoop 定义了 I2EC，可通过 LPC I/O 对任意 16 位 EC memory
address 做 snoop。它有两种 host transport：

- PNPCFG `0x2E/0x2F` 的 depth-2 `I2EC_ADDR_H/L/DATA`；
- 可编程的专用四端口窗口。

本机 2.12 固件配置了后一通道：

```text
XRAM[0x2012] SPCTRL2.PI2ECEN = 1
XRAM[0x2014] PI2ECH = 0x06
XRAM[0x2015] PI2ECL = 0x80
```

即专用 base 为 `0x0680`，理论地址高、地址低、数据端口为 `0x681–0x683`。然而真正决定
I2EC 权限的是 `XRAM[0x200D] SPCTRL1.I2ECCTRL[1:0]`：

| 值 | 含义 |
|---:|---|
| `00b` | disabled |
| `10b` | read-only |
| `11b` | read-write |

固件的两个初始化表都包含 `20 0D 43`，分别由 `CODE:0x0FF7–0x1035` 和
`CODE:0xDF66` 写入 `SPCTRL1=0x43`；随后 `CODE:0xA20B`、`CODE:0xC340` 执行
`SPCTRL1 |= 0xC8`，最终得到 `0xCB`。因此 stock 初始化后的 I2EC 是 read-write，
不是 disabled。若只想开放读取，应把两个表项的值 `43` 改为 `42`，使最终值为 `0xCA`。

从固件修改角度，可以把 I2ECCTRL 设成 `10b` 获得只读 snoop；`11b` 会允许写整个 EC
memory，风险显著更高。即使只读，IT5570 手册也警告某些非触发器型外设寄存器的 snoop
结果不保证正确。由于本机是 IT5571 D，真正启用前还必须确认端口没有被其他设备占用、
IT5571 的控制位未变化，并准备硬件恢复手段。

### D2EC/DBGR：物理调试接口，不是普通 OS 后门

相邻芯片还支持 D2EC/DBGR，通过专用 SMBus debug slave 或键盘扫描引脚复用的 EPP 接口
访问 instruction SRAM、data SRAM 和 EC 外设，并支持断点、单步和 ISP。它通常需要 ITE
工具、板级连线或 strap；DBGR/SMB 一旦侦测进入，手册称不能热退出。当前没有证据表明
这台机器从正常 OS 软件路径开放了 DBGR。

### 不能用于读取 live XRAM 的通道

- Follow Mode/SMFI host-indirect path访问 SPI/e-flash；手册明确 H2RAM 不可通过
  host-indirect memory path访问。
- BRAM 是独立的 192 字节 host/EC 共享 SRAM，不是 8051 XRAM 的任意地址窗口。
- 标准 ACPI EC `0x62/0x66` 端口、GCU WMI 和 `ECRR/ECRW` 没有自动扩展为 16 位 XRAM
  snoop。

## 建议的只读验证顺序

在不修改 EC 固件的前提下，最安全的验证是：

1. 连续多次读取每个边界附近，区分稳定 `0xFF`、动态 firmware-owned 值和普通 RAM；
2. 检查 `0x000/0x3FF/0x400/0x4FF/0x500/0x5FF/0x600/0x6FF/0x700/0x7FF/`
   `0x800/0xBFF/0xC00/0xFFF`，验证窗口边界，而不是对未知状态寄存器试写；
3. 如果以后制作实验固件，优先只启用 I2EC read-only，并先确认外置编程器可恢复；
4. 只有在总线抓取或 ITE 工具确认后，才把 IT5570 的 I2EC/DBGR 行为提升为 IT5571 D
   的实机结论。

当前结论的置信度：H2RAM 窗口配置和 `0x800–0xBFF` 缺口为高；I2EC dedicated path
及 stock 的 read-write 初始化为高；DBGR 引脚/SMBus 是否在本板可达为低。
