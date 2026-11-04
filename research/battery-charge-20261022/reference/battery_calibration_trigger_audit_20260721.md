# 电池容量校正与触发条件审计（2026-07-21）

## 重要更正：bank1 应按 32 KiB window 重定位

主 EC 的 128 KiB 不是两个独立的 64 KiB 代码空间，而是固定的 32 KiB common 加三个
32 KiB bank window。common 中 `0x1100/0x1114/0x1128/0x113C` 四个 thunk 分别设置
`R0=0x0A/0x1E/0x32/0x46` 和 P1 bank 选择位，所有 bank target 均位于逻辑
`0x8000-0xFFFF`。

因此 `main-bank1.bin` 低 32 KiB 的文件偏移必须加 `0x8000` 才是逻辑代码地址：

| 旧 `.d52` 文件偏移 | 正确逻辑地址 | 作用 |
|---:|---:|---|
| `0x47EA` | `0xC7EA` | 容量校正状态分派 |
| `0x4970` | `0xC970` | 候选 factor 限幅和提交 |
| `0x5773` | `0xD773` | 容量积分 worker |
| `0x5A8E` | `0xDA8E` | 电池遥测校验和 FCC 发布 |
| `0x69F7` | `0xE9F7` | CycleCount 发布 |

此前仅因这些地址低于 `0x8000` 而判断其不可执行是错误的。正确重定位后，common wrapper
`0x1930 -> bank1:D9D2` 的有效路径在 `D9DF` 直接调用 `DA8E`；`D067/D0C5` 也会进入
`C7EA` 状态分派。以下结论以重定位后的原始机器码为准。

## 结论

EC 中确实存在一套容量校正和重新发布 FullChargeCapacity（FCC）的状态机。它不是任意
中高电量插电都能触发，而是要求从低 SOC、低包电压的充电会话开始，持续累计充入容量，
最后在满电且充电电流归零后提交新 factor，并重新计算公开 FCC。

用户观察到“从满电放到自动关机，再充满，充电电流变为 0 时 Linux
`charge_full` 更新”与机器码条件高度吻合：

```text
低 SOC/低电压开始有效充电
  -> 建立校正会话并累计充入容量
  -> 到达 100%
  -> ChargingCurrent 请求归零
  -> 终点电流候选 < 360，连续确认 64 次
  -> 提交 factor，重新发布 FCC
  -> ACPI/Linux 刷新 charge_full
```

EC 没有被证明会向 Fuel Gauge 写 learned FCC。当前能确认的是 EC 利用电池侧遥测和自身
累计量计算一个比例因子，再缩放并发布主机看到的 FCC。

## 状态机与触发条件

状态变量为真实 `XRAM[0x03A3] & 7`。旧 `.d52` 的 `dptr_03A2` 比真实 DPTR 小 1。

### 状态 0：等待低电量充电起点

进入状态 1 必须同时满足：

1. common veneer `0x1B40` 返回真。其目标读取 queued ChargingCurrent
   `XRAM[0x0834:0x0835]`，所以充电器必须仍请求非零充电电流。
2. `XRAM[0x0514] <= 12`。机器码是 `SETB C; SUBB A,#0x0C` 后做有符号比较；此前把方向
   解释为 `>=12` 是错误的。真机中 `0x0514` 与 RSOC 同步变化，因此它高度疑似内部 RSOC
   候选，但仍保留“镜像”而非正式 SBS 字段命名。
3. `XRAM[0x03A2].bit3 == 1`，表示相关遥测/统计功能已经启用。
4. 从 `0x0363:0x0364` 或 `0x0384:0x0385` 选择的包电压不高于
   `XRAM[0x03D2:0x03D3]`。现场阈值为 14820 mV，约 3.705 V/cell（4S）。此前把比较方向
   解释成“高于 14820 mV”同样错误。

所以“放到 12% 以下再开始充电”不是经验规则，而是 EC 状态机的明确启动门。自动关机通常
还会保证电压显著低于 14820 mV，因此比只放到 12% 更容易同时满足两个条件。

### 状态 1：建立低压基线

状态 1 比较包电压 `0x0384:0x0385` 与低阈值 `0x03D0:0x03D1`。现场低阈值为
13250 mV，约 3.3125 V/cell：

- 若包电压不高于低阈值，清零 `0x0388:0x0389` 充入容量累计和会话计数；
- 若已高于低阈值，保存当前累计基线；
- 随后进入状态 2。

这解释了“自动关机”比仅到 12% 更可靠：深度放电能使本轮统计从明确的低压基线清零。
仅满足 `RSOC <= 12` 但电压仍高于 13250 mV 时，状态机可能采用旧基线，结果是否可接受还
取决于后续累计和动态限幅。

### 状态 2/3：充电累计与阶段转换

状态 2 在电池方向和 ChargingCurrent gate 有效时维持会话，并使用容量积分路径更新内部
累计量。`D773` 周边机器码维护 16 位余数，以常数 `18000` 做商余分解；结合真机记录，
`0x0388` 是充入容量累计，单位为 mAh 的证据很强：

- 自然充电 63% -> 69% 时，RemainingCapacity 增加 240 mAh，`0x0388` 增加 255；
- 拔电窗口中 `0x0388` 不变，而 `0x038A` 随放电吞吐增加。

ChargingCurrent 异常归零会启动 `0x0361` 去抖；达到 128 次仍未完成中间条件时，本轮进入
状态 7/放弃。状态 2 到 3 的另一容量/方向比较调用了 ROM helper，精确物理门槛尚未完全
恢复，因此不能仅凭伪 C 给它编造百分比名称。状态 3 清计数并进入状态 4。

### 状态 4：满充终点确认

提交前必须满足：

1. 电池方向 gate 仍有效；
2. `0x1B52 -> bank0:A6CE` 返回真，即 queued ChargingCurrent
   `0x0834:0x0835 == 0`；
3. 上述条件连续成立 64 个状态机调度周期；
4. `XRAM[0x0347] == 100`。该字段在实机上与公开 RSOC 一致；
5. `XRAM[0x04AE:0x04AF] < 0x0168 = 360`。结合用途和其他电流路径，它高度疑似滤波后的
   终点电流幅值，单位很可能是 mA；原始 SMBus 命令仍待最终命名。

因此新 FCC 出现在“电流变为 0”附近是代码的直接结果，不是 Linux 恰好延迟刷新。这里
检查的是 EC 排队给 charger 的 ChargingCurrent 请求归零；它通常与用户看到的实际电流
归零接近，但两者不是同一个寄存器。

### 状态 5/6：factor 提交和 FCC 发布

状态 5 从 `0x0388:0x0389` 生成候选 factor，并限制在本轮开始时由旧 FCC 派生的动态上下界
`0x039B/0x039C` 内。随后：

```text
XRAM[0x0342] = accepted_factor
XRAM[0x0340:0x0341] = accepted_factor * 100
XRAM[0x03A1].bit1 = 1        // 请求重新发布
XRAM[0x03A3] = 6
```

边界公式可以进一步确定为：

```text
old_factor = old_FCC_mAh / 100
factor_high = old_factor + 3
factor_low  = old_factor - 3
accepted_factor = clamp(charged_capacity_mAh / 100,
                        factor_low, factor_high)
new_FCC_mAh = accepted_factor * 100
```

内部除法 helper 的逐位舍入方式尚未完全反编译，但单位和限幅由原始常数、缓存值及实机结果
共同确认。现场旧 FCC 3700 mAh 对应 factor 37，本轮上界为 40；完整充电得到的候选至少为
40，最终接受 40 并发布 4000 mAh。这正好解释了观察到的 `3700 -> 4000`，也说明一次循环
最多改变约 300 mAh。若真实容量与旧值相差更大，需要多次合格循环才能逐步收敛。

有效路径 `common:1930 -> bank1:D9D2 -> bank1:DA8E` 消费更新后的缓存。`DA8E` 校验
`0x0342 == 0x03C0` 后，以常数 100 做比例运算并写入公开
`EC[0x0404:0x0405]` LastFullCapacity。状态 6 等待 pending 位被消费，随后设置两个值为 8
的同步计时器并退出本轮。

Linux `charge_full` 对应 battery class 的 FullChargeCapacity。EC 公开字段更新后，ACPI
电池通知或下一次 battery class 查询会让 sysfs 节点出现新值。现有有效代码还确认
SCI query `0x89 -> DSDT _Q89 -> Notify(BAT0, 0x80)` 会请求电池信息刷新，但尚未证明
factor 提交直接调用的就是 `_Q89`；也可能由正常轮询/另一通知路径完成刷新。

## 参与过程的 XRAM 地址

下表地址均为真实 XRAM 地址，已经应用旧 `.d52` 的 `DPTR +1` 修正。`BE16` 表示高字节
在低地址，`LE16` 表示低字节在低地址。标为“工作量/候选”的字段不是标准 SBS 寄存器。

| XRAM | 格式 | 访问阶段 | 作用与已确认行为 |
|---:|---|---|---|
| `0x0340:0x0341` | BE16 | 状态 5、发布 | `accepted_factor * 100` 的内部派生值；现场 factor 40 时为 4000 |
| `0x0342` | u8 | 状态 5、发布 | 最终接受的 FCC factor，单位 100 mAh；`40 -> 4000 mAh` |
| `0x0347` | u8 | 状态 4、发布 | 内部 RSOC 镜像；满充终点必须等于 100 |
| `0x0361` | u8 counter | 状态 2、4 | 主连续条件去抖计数；状态 4 比较是否达到 `0x40`，状态 2 有 `0x80` 放弃门 |
| `0x0362` | u8 auxiliary counter | 状态 1–3 | 与主计数相邻的辅助会话计数，建立/切换阶段时清零；不能按 BE16 与 `0x0361` 合并解释 |
| `0x0363:0x0364` | BE16 | 状态 0 | 会话建立时优先使用的包电压镜像；为 0 时改用 `0x0384:0x0385` |
| `0x0384:0x0385` | BE16 mV | 状态 0、1 | 校正用包电压工作镜像；现场与公开包电压同步 |
| `0x0388:0x0389` | BE16 mAh | 状态 1、2、5 | 本轮充入容量累计；状态 1 可清零，状态 5 用它生成候选 factor |
| `0x038A:0x038B` | BE16 mAh | 积分 worker | 总吞吐量或另一方向累计；充放电均可能增加，不直接用于状态 5 factor |
| `0x0399` | u8 | 状态 2 | 参与中间容量/方向条件的倍率或限值，代码乘以 10 后送入算术 helper；精确物理名称未知 |
| `0x039B` | u8 | 状态 0、5 | factor 上界：`old_FCC / 100 + 3` |
| `0x039C` | u8 | 状态 0、5 | factor 下界：`old_FCC / 100 - 3` |
| `0x03A1` | flags | 状态 5、6、发布 | bit 1 为新 factor 的重新发布请求；状态 6 等待相关 pending 位被消费 |
| `0x03A2` | flags | 状态 0 | bit 3 是启动校正的必要 enable/condition gate |
| `0x03A3` | u8 | 全状态 | `value & 7` 为容量校正状态 0–7 |
| `0x03B2:0x03B3` | BE16 remainder | 积分 worker | 库仑积分余数；与常数 18000 做商余分解，避免每 tick 的小数容量丢失 |
| `0x03B5` | u8 saturating | 状态 2、积分 | 中间方向/持续条件计数，状态 2 与动态阈值比较；达到条件后清零 |
| `0x03B6` | u8 saturating | 积分 worker | 另一方向的饱和计数；与 `0x0388` 更新分支相邻，精确门槛语义未命名 |
| `0x03C0` | u8 | 电池轮询、发布 | factor 镜像；`DA8E` 要求它与 `0x0342` 一致才发布 FCC |
| `0x03D0:0x03D1` | BE16 mV | 状态 1 | 低压清零阈值；现场为 13250 mV |
| `0x03D2:0x03D3` | BE16 mV | 状态 0 | 校正启动电压上限；现场为 14820 mV |
| `0x0404:0x0405` | LE16 mAh | 状态 0、最终发布 | 主机可见 LastFullCapacity/FCC，即 Linux `charge_full` 的固件数据源 |
| `0x04AB` | u8 percent | 遥测发布 | 公开 RSOC；实机上与 `0x0347`、`0x0514` 同步 |
| `0x04AE:0x04AF` | LE16 candidate | 状态 4 | 满充终点候选量，必须 `< 0x0168`；高度疑似滤波电流幅值，可能单位 mA |
| `0x0497` | flags | 状态 4、会话 | 电池 session flags；bit 5 参与 RSOC 非 100 时的异常/退出分支，bit 0 表示数据会话已初始化 |
| `0x0514` | u8 percent candidate | 状态 0、运行时 | 内部 RSOC 候选；启动门为 `<= 12`，发布路径将它复制到 `0x0347` 和 `0x04AB` |
| `0x054C` | u8 | 遥测轮询 | 电池遥测失败/重试计数；连续失败会重启轮询会话 |
| `0x0561` | u8 | 遥测轮询 | `D9D2` 的描述表索引；达到 `0x69` 后进入 `DA8E` 校验和批量发布 |
| `0x0575` | u8 timer | 状态 6 | factor pending 被消费后写 8，启动后续同步窗口 |
| `0x0680` | u8 state | common 调度 | 电池/charger SMBus 调度状态；`D9D2/DA8E` 返回后写入 2 |
| `0x0832` | flags | charger queue | charger 命令 retry/pending；bit 0/1 分别对应 ChargingCurrent/ChargingVoltage |
| `0x0834:0x0835` | LE16 mA | 状态 0、2、4 | queued ChargingCurrent；状态 0 要求非零，状态 4 要求归零 |
| `0x0A56:0x0A5A` | scratch | 多阶段 | 共享算术 scratch，暂存电压、终点量、商和比较操作数；不能跨调用当稳定遥测读取 |
| `0x91F3` | u8 timer | 状态 6 | 与 `0x0575` 同时写 8 的第二同步计时器 |

### 状态与关键 XRAM 对照

| 状态 | 入口 | 主要读取 | 主要写入 |
|---:|---:|---|---|
| 0 | `bank1:C80C` | `0834:0835`, `0514`, `03A2`, `0363:0364`/`0384:0385`, `03D2:03D3`, `0404:0405` | `039B`, `039C`, `03A3=1` |
| 1 | `bank1:C86D` | `0384:0385`, `03D0:03D1` | `0388:0389`、去抖/基线工作区、`03A3=2` |
| 2 | `bank1:C8A5` | ChargingCurrent gate、`0399`, `03B5` 和积分状态 | `0361`, `0362`, `03A3=3/7` |
| 3 | `bank1:C8FB` | 无额外容量输入 | 清去抖计数，`03A3=4` |
| 4 | `bank1:C907` | `0834:0835`, `0347`, `04AE:04AF`, `0497` | `0361`, `03A3=5/7` |
| 5 | `bank1:C96E` | `0388:0389`, `039B`, `039C` | `0342`, `0340:0341`, `03A1.bit1`, `03A3=6` |
| 6 | `bank1:C9B6` | `03A1` pending bits | `0575=8`, `91F3=8`, `03A3=7` |
| 7 | `bank1:C9CC` 附近 | idle/abort 条件 | 本轮结束，等待重新满足状态 0 |

## 参与过程的函数与代码地址

### 地址表示规则

- `common:xxxx`：固定 32 KiB common CODE，不随 bank 切换。
- `bank0:xxxx`、`bank1:xxxx`：CPU 看到的逻辑 CODE 地址。
- 对 `main-bank1.bin` 低 32 KiB，物理文件偏移为 `logical - 0x8000`；例如
  `bank1:C80C` 对应文件偏移 `0x480C`。
- 旧 `.d52` 还存在约 1 字节的符号显示偏差；函数和 DPTR 最终均以原始 opcode/Ghidra
  正确重定位结果为准。

### 核心状态机函数

| 逻辑 CODE | block1 文件偏移 | 函数/基本块 | 作用 |
|---:|---:|---|---|
| `bank1:C7BA` | `0x47BA` | factor bounds helper | 读取旧 FCC，换算 `old_factor` 并生成 `+3/-3` 上下界 |
| `bank1:C7EA` | `0x47EA` | calibration state dispatch | 读取 `0x03A3 & 7` 并通过跳转表分派状态 0–6 |
| `bank1:C80C` | `0x480C` | state 0 | 检查低 SOC、ChargingCurrent、enable flag 和启动电压上限 |
| `bank1:C86D` | `0x486D` | state 1 | 比较低压阈值，清零或建立充入容量基线 |
| `bank1:C8A5` | `0x48A5` | state 2 | 维持充电统计，处理中间门和 128 次异常去抖 |
| `bank1:C8FB` | `0x48FB` | state 3 | 清计数并转入满电等待 |
| `bank1:C907` | `0x4907` | state 4 | 检查 ChargingCurrent=0、64 次、RSOC=100、终点量<360 |
| `bank1:C96E` | `0x496E` | state 5 | 从 `0x0388` 生成候选 factor 并进入提交基本块 |
| `bank1:C970` | `0x4970` | factor commit | 限幅后写 `0x0342`、factor*100 和发布 pending |
| `bank1:C9B6` | `0x49B6` | state 6 | 等待 pending 消费，设置同步计时器并结束本轮 |
| `bank1:C9CC` | `0x49CC` | abort/idle tail | 多个失败分支汇合的退出路径 |

### 积分、遥测和发布函数

| 逻辑 CODE | block1 文件偏移 | 入口来源 | 作用 |
|---:|---:|---|---|
| `bank1:B113` | `0x3113` | `common:1C0C` wrapper | 大型电池周期 worker，轮转刷新温度、RSOC、电流、电压和容量相关状态；与校正 worker 同属电池周期域，二者的直接调度边仍需完善 |
| `bank1:D689` | `0x5689` | bank1 内部状态/跳转 | 容量积分主块，整理方向、余数和每次增量 |
| `bank1:D773` | `0x5773` | `D689` 内部基本块 | 将商累加到 `0x0388`；相邻尾部更新 `0x038A` |
| `bank1:D9D2` | `0x59D2` | `common:1930` wrapper | 按 common `0x632E` 描述表轮询电池 byte 遥测，索引到 `0x69` 后调用 `DA8E` |
| `bank1:DA8E` | `0x5A8E` | `D9D2:D9DF` | 校验缓存和镜像，缩放并发布 FCC、DesignCapacity、RSOC、电压等字段 |
| `bank1:E9F7` | `0x69F7` | `DA8E` 发布流程 | 把内部 cycle-count 缓存发布到 `0x04A6:0x04A7` |

### common veneer、bank wrapper 和外部 helper

| CODE | 目标/类型 | 在校正路径中的作用 |
|---:|---|---|
| `common:1100` | bank0 thunk | 保存上下文并选择 bank0 window |
| `common:1114` | bank1 thunk | 保存上下文并选择 bank1 window；上述 bank1 wrapper 最终跳到这里 |
| `common:1930` | `MOV DPTR,#D9D2; LJMP 1114` | 进入 bank1 电池遥测轮询/发布 worker |
| `common:1B40` | `MOV DPTR,#A6C3; LJMP 1100` | 调用 bank0 helper，返回 queued ChargingCurrent 是否非零 |
| `common:1B52` | `MOV DPTR,#A6CE; LJMP 1100` | 调用 bank0 helper，返回 queued ChargingCurrent 是否为零 |
| `bank0:A6C3` | ChargingCurrent nonzero helper | 状态 0/2 的有效充电 gate |
| `bank0:A6CE` | ChargingCurrent zero helper | 状态 4 的自然充电终止 gate |
| `common:7B36` 附近 | 16 位乘除算术 ROM/common helper | factor*100、容量比例和积分换算；反编译参数恢复不完整 |
| `common:7B9D` 附近 | 16 位饱和累加 helper | 把积分得到的 mAh 增量累加到 `0x0388/0x038A` |
| `bank1:E8xx–ECxx` | 共享字节序/比较/算术 tails | 状态机大量复用的短 helper；存在共享入口，不能按线性相邻代码扩大函数边界 |

### 调用链总览

```text
common/bank1 电池周期域
  -> common:1C0C -> bank1:B113               电池遥测周期维护
  -> bank1:C7EA -> C80C...C9B6               容量校正状态机
  -> bank1:D689/D773                          mAh 积分与方向累计

common 电池 SMBus 调度
  -> common:1930 -> bank1:D9D2               按描述表逐项读取
       -> bank1:DA8E                          缓存校验与批量发布
            -> EC[0x0404:0x0405]              新 FCC
            -> ACPI battery class             Linux charge_full
```

## Fuel Gauge 与 EC 的职责边界

common bank 的 `0x632E` 是电池 byte 遥测轮询描述表，每项包含目标 XRAM 地址和命令索引。
它持续填充 `0x0300...` 缓存；其中相邻命令按高低字节交错落入 `0x03xx`。表中还明确包含：

- 命令 `0x42 -> XRAM[0x03C0]`；
- 命令 `0x47 -> XRAM[0x0514]`；
- 命令 `0x48 -> XRAM[0x0399/0x0398]`；
- 命令 `0x64/0x65 -> XRAM[0x09CA/0x09C9]`。

这些是本机电池遥测块的 byte 命令索引，不能直接套成标准 SBS word 命令。标准 SBS
`FullChargeCapacity (0x10)` 的语义与公开 `EC[0x0404:0x0405]` 一致，但当前物理事务采用
了厂商/平台遥测块，不能据此声称 Gauge 的标准 `0x10` 原值被直接复制到 sysfs。

静态代码尚未发现 EC 向 `ManufacturerAccess` 写 learn/reset/unseal 命令，也没有发现 EC
把新 FCC 写回 Gauge。最稳妥的职责模型是：Gauge 提供原始容量、电流、电压和状态；EC
维护本轮容量积分和受限 factor；EC 计算并发布主机可见 FCC。

## 对用户操作的实际含义

- 只从中高 SOC 充到 100% 不满足状态 0 的 `<=12%` 启动门，不会开始新一轮完整校正。
- 放到 12% 以下是必要条件之一，但不保证成功；还要满足包电压不高于 14820 mV、统计功能
  flag、有效 ChargingCurrent 和后续连续充电条件。
- 放到自动关机更可能低于 13250 mV，使充入容量累计明确清零，因此是更可靠的完整基线。
- 最终必须允许系统自然完成充电，直到 EC 的 ChargingCurrent 请求归零；提前拔电、限制
  最高 SOC 或在 100% 前关机会阻止状态 4 提交。
- 到 100% 后应继续接电，至少等待实际电流归零并让 `charge_full` 更新。64 次是 EC 调度
  次数，尚未精确换算成秒。
- 深度放电会增加电池损耗，不应作为日常维护反复执行；只有在 FCC/续航估算明显失准时才有
  验证价值。

## 仍待恢复

1. 状态 2 进入状态 3 的 ROM helper 精确公式和门槛。
2. `0x04AE:0x04AF` 的原始电池命令、滤波公式和确切单位。
3. 状态机实际调度周期，因此 64/128 次去抖对应的真实秒数。
4. factor 除以 100 时的精确舍入方式；单轮最大调整幅度已确认为约 300 mAh。
5. FCC 写入后触发 Linux battery class 刷新的直接通知调用边。
