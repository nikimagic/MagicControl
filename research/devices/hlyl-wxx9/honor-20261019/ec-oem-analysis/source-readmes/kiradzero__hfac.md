# hfanc

Fan control for the HONOR MagicBook BRI-XX under Linux.

The vendor removed fan control from ACPI but left it in the EC firmware,
reachable through a mailbox the firmware wraps in two ACPI methods. This
project provides:

- **`kernel/`** — an out-of-tree kernel module (`honor_ec_fan`) exposing the
  fan and the EC sensors as a standard hwmon device, with the safety
  watchdogs the firmware itself lacks: an overheat guard, a deadman switch,
  and automatic handover on suspend, unload and shutdown.
- **`src/`** — a userspace daemon (Zig) that drives the fan: two-branch
  curves, power-profile quadrants, and a passive (fan-stopped) mode that holds
  temperature with a CPU/GPU frequency ceiling.

How it all works is described in [docs/Architecture.md](docs/Architecture.md).

## Requirements

- HONOR MagicBook BRI-XX (the module checks DMI and refuses other machines;
  `force=1` overrides at your own risk)
- Linux kernel headers, clang/LLVM (the module is built with `LLVM=1`)
- [Zig](https://ziglang.org) for the daemon
- Optional: `ckms` for module install with automatic rebuilds on kernel
  updates, `dinit` for the service

## The kernel module

```sh
doas apk add ckms
doas sh kernel/reinstall.sh
```

The script lays the sources out in `/usr/src`, builds and installs through
ckms, and verifies that the installed module is byte-for-byte the one just
built. On a kernel update ckms rebuilds it automatically.

Install the modprobe ordering hint alongside (the die-temperature watchdog
prefers the `acpitz` thermal zone, which `thermal.ko` must register first):

```sh
doas install -m644 kernel/honor-ec-fan.modprobe.conf /etc/modprobe.d/honor-ec-fan.conf
```

For development, ckms is not needed:

```sh
gmake -C kernel
doas insmod kernel/honor_ec_fan.ko
```

## The daemon

```sh
zig build -Doptimize=ReleaseSafe
```

```sh
hfanc status               # sensors, fan state, what the curves would ask
doas hfanc run             # drive the fan (needs root)
doas hfanc run --dry-run   # same decisions, writes nothing
```

As a service: `doas sh dinit/install.sh`, then `dinitctl start hfanc`. The
log is `/var/log/hfanc.log`, readable without root.

## Tests

```sh
zig build test
```

No test touches the machine: the policy is pure functions, fed with real
readings from instrumented runs.

## Configuration

All tuning constants live in `src/config.zig` as comptime values, each
annotated with the measurement it came from. They are calibrated to this
specific machine and are not user preferences; change them there, deliberately.
