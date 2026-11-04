# Linux backend

Status: **GTK4/Wayland GUI builds; hardware EC transport is not implemented.**
The UI can be previewed in WSLg. Read-only inventory comes from DMI, sysfs and
`/proc`; EC write controls stay disabled until a board-specific transport is verified.

## Why ec_sys does not work

The obvious transport is the `ec_sys` debugfs driver:

```bash
sudo modprobe ec_sys write_support=1
# then /sys/kernel/debug/ec/ec0/io is a 256-byte read/write window
```

Source: [`drivers/acpi/ec_sys.c`](https://git.zx2c4.com/linux-rng/plain/drivers/acpi/ec_sys.c)

That driver hard-codes:

```c
#define EC_SPACE_SIZE 256
```

So it exposes **EC addresses 0x000..0x0FF only**.

Every register this project uses is **above** that window. The lowest is
`kMinWritable = 0x43E`, and the fan tachometers, duty cycles, performance mode,
keyboard backlight and the RAM fan tables all sit between 0x43E and 0xF5F.

> An earlier revision of `EcRegister.h` and of `docs/UI-SPEC.md` claimed the
> opposite — that all registers fit inside 256 bytes. That was wrong: the values
> merely looked small next to a 16-bit address space. The unit test
> `testEcSysWindowDoesNotCoverOurRegisters` now pins the real arithmetic so the
> claim cannot come back silently.

## What the Linux port actually needs

The Windows driver reaches 16-bit EC addresses through an ACPI method. On Linux
the equivalent 16-bit path has to be produced some other way. Candidate routes,
in the order they should be tried on the target machine:

### 1. `uniwill-laptop` kernel driver

Mainline since 6.15. Handles Uniwill/Tongfang-family laptops, which is the
hardware family this board belongs to. Transport is WMI, so it already reaches
the addresses `ec_sys` cannot.

Documentation: [uniwill-laptop.rst](https://raw.githubusercontent.com/torvalds/linux/master/Documentation/admin-guide/laptops/uniwill-laptop.rst)

What it gives:

| Feature | Available |
|---|---|
| CPU and GPU temperature | yes, via hwmon |
| Up to two fans, RPM | yes, read-only |
| **Fan duty cycle control** | **no** |
| Platform performance profile | **documented as not implemented** |
| Battery charge limit | yes, `charge_control_end_threshold` or `charge_types` |
| Keyboard backlight | yes, LED class, `uniwill:multicolor:kbd_backlight` |
| Lightbar | yes, multicolor LED |
| Configurable TGP (NVIDIA) | yes |
| USB powershare, AC auto boot, Fn lock, Super key | yes |

So this driver covers monitoring, battery and lighting, but **not** fan control.
Probe command on the target machine:

```bash
ls /sys/devices/platform/uniwill_wmi/
ls /sys/class/hwmon/
sudo dmesg | grep -i uniwill
```

### 2. A 16-bit EC access path

To be investigated on hardware. Windows passes 16-bit logical OEM addresses;
the ordinary Linux `ec_read`/`ec_write` interface takes an 8-bit offset. The port
needs the board-specific WMI/ACPI transport rather than truncating those addresses.
Candidate interfaces still require verification against this board:

- `/dev/mem` with a map of the EC data port, if the kernel permits it.
- `acpi_call` for direct ACPI method invocation, if the DSDT exposes a usable
  read/write method. The Windows driver's IOCTL takes an arbitrary 16-bit
  address, so the OEM DSDT very likely contains such a method.
- `acpi_ec` transaction tracing via `dyndbg` to learn which method the driver
  actually calls.

### 3. Purpose-built kernel module

Last resort. A small module exposing the same 8-byte packet interface the
Windows driver uses would let the entire register map and every value in
`EcRegister.h` carry over unchanged. It also carries the real cost: it must be
rebuilt for each kernel version.

## ACPI WMI registers need a separate decision

These four live in the WMI address space, not the plain EC window, and none of
them has a direct `ec_sys` equivalent:

| Address | Purpose | Linux replacement |
|---|---|---|
| `0x7A6` | battery charge profile (bits 5:4) | `charge_control_end_threshold` |
| `0x87F` | stored charge limit | same |
| `0x7C3` | platform / activation byte | none found yet |
| `0x742` | voltage protection profile | none found yet |
| `0x770` | ROM id, used to detect an EC reset | none found yet |

## What already works without the port

The core, GTK4 GUI, register map and CLI build on Linux. The GUI and CLI are staged
in the portable project folder; this builds them without enabling hardware writes:

```bash
sudo apt install build-essential cmake pkg-config libgtk-4-dev
cmake -S . -B build/linux -G "Unix Makefiles" \
  -DCMAKE_BUILD_TYPE=Release -DMAGICCONTROL_BUILD_GUI=ON
cmake --build build/linux --parallel
```

The GUI runs from `MagicControl/linux/MagicControl` and reads its saved fan curves
from the same folder. The `mctl` CLI continues to report that hardware access is
unavailable until the Linux EC backend is completed.

## UI parity update — 2026-10-17

The GTK frontend now follows the current Windows layouts for SETs/FANs,
header/menu, donation QR/credits/license and persistent right-edge scrolling.
See [the parity contract](../../../docs/LINUX-UI-PARITY.md). Hardware write controls
remain unavailable until Linux EC transport is verified; this update does not
silently substitute EC writes with unrelated sysfs settings.
# Huawei HLYL-WXX9

Отдельный typed OEM-контроллер находится в huawei-wmi/HuaweiControlLinux.cpp.
Он использует sysfs уже загруженного драйвера ядра huawei-wmi для зарядки/Fn Lock,
не загружает модули, не меняет права и не обращается к debugfs/call или /dev/mem.
Описанные в этом файле границы raw EC относятся к MECHREVO, не к этому отдельному ABI.
