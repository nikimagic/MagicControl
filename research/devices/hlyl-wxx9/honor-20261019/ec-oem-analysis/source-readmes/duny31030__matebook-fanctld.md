# matebook-fanctld

**Linux fan control daemon for Huawei MateBooks** — temperature-curve driven, with manual override and layered safety design.

Built by reverse-engineering the ACPI/WMI layer on a Huawei MateBook (verified model: KLVG-XX / M1010). No vendor driver required — only the `acpi_call` kernel module.

```
  temp → curve → firmware-declared gear → EC manual mode
  95°C → full speed + back to auto curve (emergency protection)
  exit/crash → vendor control restored (fans can never stay locked)
```

## The problem it solves

On Linux, MateBook fans are fully controlled by the EC firmware's built-in curve — conservative at idle and noisy. The kernel's standard fan path (`_FSL`) is an **empty method** in Huawei firmware, so every generic fan-control tool silently fails.

This project reverse-engineered Huawei's private WMI layer (SSDT9) and drives `SFND` directly into EC manual mode. Measured accuracy: ±20–60 RPM, both fans locked in sync. Full write-up: [docs/ec-reverse-engineering.md](docs/ec-reverse-engineering.md) (Chinese).

## Safety design

This is not a bare EC-poking script. Controlling the EC means the firmware's thermal management is suspended — lock low RPM under heavy load and the machine overheats. Hence:

- **Firmware-declared gears only** (3200–12000 RPM, 11 gears from DSDT `TFN1._FPS`) — anything else is rejected
- **Emergency protection**: temp ≥ 95°C → immediately full speed and back to auto curve
- **Hysteresis**: ±1.5°C to prevent gear hunting
- **Restore on exit**: SIGTERM, systemd stop, and crash (`Restart=on-failure` + `ExecStopPost`) all restore vendor auto control
- **Manual mode always times out**: locked gears expire after N minutes and fall back to the curve

## Install

Dependencies: `python3`, `acpi_call-dkms` (AUR) or equivalent, systemd

```bash
git clone https://github.com/duny31030/matebook-fanctld
cd matebook-fanctld
./install.sh
```

## Usage

**Auto mode** (default): follows the built-in curve, no intervention needed.

```python
DEFAULT_CURVE = [
    (0,  3200),   # idle: lowest gear
    (50, 3800),
    (55, 5100),
    (60, 6300),
    (68, 7400),   # capped at 7400 — above that is pure noise
]
```

**Query status** (unix socket, line-based JSON):

```bash
echo '{"cmd": "status"}' | socat - UNIX-CONNECT:~/.cache/fanctld.sock
# {"ok": true, "mode": "auto", "rpm0": 3200, "rpm1": 3200, "temp": 46.5, ...}
```

**Manual lock** (returns to curve after 10 minutes):

```bash
echo '{"cmd": "manual", "rpm": 3200, "minutes": 10}' | socat - UNIX-CONNECT:~/.cache/fanctld.sock
```

**Back to auto**:

```bash
echo '{"cmd": "auto"}' | socat - UNIX-CONNECT:~/.cache/fanctld.sock
```

**Emergency restore** (socket unreachable — send the ACPI command directly):

```bash
scripts/fan-probe.sh restore
```

**Tuning the curve**: edit `DEFAULT_CURVE` in `bin/fanctld`, then `systemctl --user restart fanctld`.

## Status bar integration

`plugins/MateFanControl/` — DankMaterialShell bar widget: live RPM/temperature display with a click-out gear panel.

Other environments: the socket protocol is one JSON line — trivial to integrate anywhere.

## Repository layout

```
bin/fanctld                      daemon (Python, zero third-party deps)
systemd/fanctld.service          user-level service
systemd/fanctld-setup.service    system-level: modprobe acpi_call + permissions
plugins/MateFanControl/          DankMaterialShell status bar plugin
scripts/fan-probe.sh             ACPI probe / emergency restore script
docs/ec-reverse-engineering.md   full EC reverse-engineering report (Chinese)
```

## ⚠️ Read this first

- In manual mode **the EC stops doing thermal management** — locking low RPM under heavy load will overheat and throttle. The 95°C emergency threshold is a last line of defense, not a routine
- Writing EC commands carries a theoretical firmware-bug risk. First-time users: verify read-only with `fan-probe.sh status`, then start from the lowest gear
- While `/proc/acpi/call` is chmod 666, every process on the machine can invoke ACPI methods — acceptable for a single-user personal machine; tighten if you care
- Verified on KLVG-XX / M1010. Other MateBook models (14 2024 etc.) share the SFND format but are unverified — see report §3.4
- No warranty expressed or implied. Use common sense

## Related projects

- [limsaiful42-eng/huawei-matebook-fancontrol](https://github.com/limsaiful42-eng/huawei-matebook-fancontrol) — Windows/PowerShell, watchdog approach
- [Halhadus/matebook-d16-2024-fan-control](https://github.com/Halhadus/matebook-d16-2024-fan-control) — bash script

Differences: native Linux daemon, firmware gear whitelist, emergency thermal protection, crash recovery, IPC for status bar integration.

## License

[MIT](LICENSE)

## 中文说明

本项目是为华为 MateBook（Linux）编写的风扇控制守护进程：按温度曲线自动调速、支持手动锁档（限时自动回落）、紧急温度保护、崩溃/退出自动恢复厂商控制。安装方法与使用协议与上文相同，完整逆向分析报告见 [docs/ec-reverse-engineering.md](docs/ec-reverse-engineering.md)。
