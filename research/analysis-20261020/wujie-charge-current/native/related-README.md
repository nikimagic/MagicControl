# EC reverse-engineering utilities

This repository contains static-analysis notes plus conservative host tools.

## EC Flash hardware utility

Build the development binary with `make -C ec-flash`. The resulting
`build/ec-flash` supports Follow Mode, JEDEC-ID, SPI reads, and an explicitly
selected erase/program path reconstructed from `samples/ifux64.efi`.

```sh
sudo build/ec-flash --id
sudo build/ec-flash --dump dumps/machine-ec.bin
sudo build/ec-flash --verify samples/GXxHXxx_21.200
sudo build/ec-flash --dump dumps/machine-ec.bin --verify samples/GXxHXxx_21.200
sudo build/ec-flash --dump dumps/region.bin --address 0x20000 --length 0x20000
```

Dump and verify perform two reads by default and fail if they differ. Every I/O
wait has a timeout, SIGINT/SIGTERM trigger cleanup, and KBC is re-enabled on exit.
The default address range is the complete 256 KiB image (`0` through `0x3ffff`).
`--dump` and `--verify` may be combined: the dump is installed only after both
Flash reads agree and the bytes match the reference file.

The JEDEC-ID transaction is closed with the two direct `0x05` writes observed
in `ifux64.efi`, and begins with a direct `0x01` before the opcode selector.
Top-level cleanup sends `0xfc`, drains KBC, then sends `0xae`. The normal CLI has
no immediate-reset option: a verified write instead arms a one-shot reset for
the final system-shutdown stage.

JEDEC-ID and data-read transactions have different endings. Data dump follows
the EFI verify path: Fast Read `0x0b`, three address bytes, one dummy byte,
continuous reads up to a 64 KiB boundary, then the EFI status handshake.
Use `--single-read` only when explicitly accepting weaker consistency checking.

Dump persistence uses a same-directory temporary file, complete write,
`fdatasync`, atomic rename, directory `fsync`, and filesystem `syncfs` before
returning success.

Writing is destructive and is never implied by dump/verify options. Arguments
and the input image are validated first; root and `iopl(3)` permission are then
checked before the delay. Only after permission succeeds does the tool announce
the default shutdown-reset behavior and wait three seconds so the operator can
release all keyboard keys before KBC is disabled. The input range must consist
of complete aligned 64 KiB blocks. For each changed block, the tool follows the
EFI standard-device path: clear status-register protection, erase with sixteen
`0x20` 4 KiB sector commands, verify all bytes are `0xff`, program 256-byte pages
with `0x02`, then read back and compare every byte. Unchanged blocks are skipped.
EFI vendor-specific SST/unknown and old MXIC programming branches are rejected
rather than guessed.

```sh
# Program and verify a complete 256 KiB image.
# Success automatically arms the late-shutdown EC reset; it does not reset now.
sudo build/ec-flash --write firmware.bin

# Program one complete 64 KiB block at an aligned offset.
sudo build/ec-flash --write block1.bin --address 0x10000
```

Do not interrupt power during erase/program. If a command fails, the tool waits
for an in-flight SPI operation when possible, requests write-disable, exits
Follow Mode, and does not create the shutdown-reset marker. This path has been
matched statically to the EFI implementation but has not been exercised by the
automated test suite against real hardware.

Build the Arch package from the repository instead of installing through the
Makefile:

```sh
cd ec-flash
makepkg -f
sudo pacman -U ./ec-flash-*.pkg.tar.zst
```

The package installs `/usr/bin/ec-flash` and the late-shutdown helper at
`/usr/lib/systemd/system-shutdown/ec-flash-reset`. The helper is inert unless a
successful write creates its one-shot marker. To program and then begin shutdown
without another command needing to be typed after flashing, use an absolute
firmware path:

```sh
sudo sh -c '/usr/bin/ec-flash --write /absolute/path/firmware.bin && systemctl poweroff'
```

The marker is `/run/ec-flash-reset-on-shutdown`, so it cannot survive a reboot.
The helper consumes it on `poweroff`, `halt`, or `reboot`, then invokes the
internal `ec-flash --reset-only` path; it ignores `kexec`. It can be manually
disarmed with `sudo rm -f /run/ec-flash-reset-on-shutdown`.

As documented by `man systemd-shutdown`, executables in
`/usr/lib/systemd/system-shutdown/` run shortly before the final power action,
after services have stopped and most filesystems have been unmounted. They run
in parallel and are subject to a timeout, so this EC reset is a best-effort late
shutdown integration rather than a strictly ordered UPS-style kill-power step.

## Offline firmware tool

The analyzer requires only Python 3's standard library.

```sh
python3 tools/firmware_tool.py analyze samples/GXxHXxx_21.200 -o manifest.json
python3 tools/firmware_tool.py split samples/GXxHXxx_21.200 split-output
python3 tools/firmware_tool.py diff old.bin new.bin -o diff.json
```

`analyze` records whole-image/block SHA-256, byte sum, non-FF usage, long FF
ranges, prefixes, and printable strings. `split` writes 64 KiB blocks plus a
manifest. `diff` reports exact changed-byte counts and contiguous difference
clusters with block attribution and byte previews.

Run all offline checks with `make -C ec-flash test`. Hardware commands are never run
by the test target.

The reproducible INT1 bank2-R7 repair for `GXxHXxx_21.200`, its exact 29-byte
diff, hashes, raw-opcode control flow, validation status, and rules for deriving
later versions are documented in
[`docs/firmware_mods/GXxHXxx_21.200/README_20260723.md`](docs/firmware_mods/GXxHXxx_21.200/README_20260723.md).
Generate it only with the version-local patch script under `firmware_mods/GXxHXxx_21.200/`;
the script rejects a source image
whose size, SHA-256, or original patch bytes differ from the reviewed baseline.

## Readable 8051 export

Ghidra 12's 8051 decompiler can keep code addresses separate from EC/XRAM
addresses.  The export helper installs register names recovered from the
official GCU Service, then emits pseudo-C plus a static register-reference
index:

```sh
tools/export_readable_ec.sh samples/disasm/main-bank0.bin readable-main0 \
  - tools/ec_functions-main0.tsv
```

The bundled register vocabulary is in `tools/ec_registers.tsv`. Bank0/common
SMBus symbols are kept in `tools/ec_functions-main0.tsv`; the default
`tools/ec_functions.tsv` is for bank1 because equal 16-bit code addresses name
different functions after a bank switch. Generated
pseudo-C is an analysis aid, not buildable vendor source: indirect DPTR access,
bank switching, tables, and incorrectly discovered function boundaries still
require manual review.  Analyze each 64 KiB bank separately so the 8051's
overlapping bank addresses are not conflated.

For banked or force-disassembled `.d52` files without reliable entry points,
use the conservative text annotator.  It only labels constant DPTR loads and
therefore does not confuse an equal numeric code address with an XRAM address:

```sh
python3 tools/annotate_disassembly.py samples/disasm/main-bank1.d52 \
  -o build/main-bank1.annotated.d52 --xrefs build/main-bank1-xrefs.md
```

Bank images without vectors can be seeded from the Keil C51 wrapper table in
the common bank:

```sh
python3 tools/extract_bank_entries.py samples/disasm/main-bank0.bin \
  -o build/bank-entries.json
entries=$(python3 tools/extract_bank_entries.py samples/disasm/main-bank0.bin \
  --entries bank1)
tools/export_readable_ec.sh samples/disasm/main-bank1.bin \
  build/readable-main1 "$entries"
python3 tools/summarize_pseudoc.py \
  build/readable-main1/main-bank1.bin.c \
  -o build/readable-main1/semantic-index.md
```

Short, uniquely matching functions can carry their reviewed names to another
firmware version without assuming stable addresses:

```sh
python3 tools/translate_function_symbols.py \
  samples/disasm/main-bank1.bin build/fw210/block1.bin \
  -o build/fw210/ec-functions.tsv
```

Evidence, version comparison, limitations, and the next symbol-recovery steps
are documented in [`docs/readable_firmware.md`](docs/readable_firmware.md).
The distinction between internal 64-KiB XRAM and the protected 4-KiB host
H2RAM aperture is documented in [`docs/xram_host_access.md`](docs/xram_host_access.md).

## Experimental I2EC utility

`tools/i2ec_rw.py` accesses the IT557x dedicated I2EC ports at `0x681–0x683`.
It is separate from the 4 KiB H2RAM/MMIO interface and can address the complete
16-bit EC memory space. The stock image configures I2EC as read-write during its
initialization tables; a `43 -> 42` table patch can reduce it to read-only.
`write` requires I2EC read-write mode and two explicit CLI confirmations:

```sh
sudo python3 tools/i2ec_rw.py read 0x09c9
sudo python3 tools/i2ec_rw.py dump 0x09c7 4
sudo python3 tools/i2ec_rw.py write 0x0000 0x00 \
  --allow-write --confirm I_UNDERSTAND_I2EC_WRITES
```

The write example documents syntax only. Do not write unknown RAM or peripheral
registers: writable addressing does not imply safe RAM semantics. Run only one
I2EC client at a time because the high-address, low-address, and data cycles are
one logical transaction; the tool lock cannot coordinate unrelated `outb` tools.
The battery-focused read-only firmware patch, pre-flash checks, and post-flash
validation procedure are documented in

## Modded Firmware 

Modifying the EC firmware binary requires extreme caution, as the binary itself is the only source of truth. Disassembled `.d52` files and pseudo-C code are for analysis only and may contain errors due to incorrect bank mapping or overlapping code.

Example of applying a reviewed, version-local patch:

```sh
# Run only the version-specific patch script
./firmware_mods/GXxHXxx_21.200/apply_patch.sh samples/GXxHXxx_21.200.bin
```

The resulting binary should be compared against the original using the offline `diff` tool to confirm the exactness of the modification:

```sh
python3 tools/firmware_tool.py diff original.bin patched.bin
```

For detailed examples of validated patches, including raw-opcode control flow analysis, refer to `docs/firmware_mods/`.

## Repository layout

- `src/`: C source and headers only.
- `build/`: compiler output; ignored by Git.
- `tools/`: offline analyzer and disassembler definitions.
- `tests/`: offline tests.
- `docs/`: maintained analysis and protocol documentation.
- `samples/`: vendor inputs and generated reverse-engineering output; retained
  in Git so the analysis remains reproducible.
- `dumps/`: machine-specific hardware reads; always ignored by Git.
- `ref/`: reference document, it5570 datasheet.
- `firmware_mods`/`: scripts to mod original firmware and results.


[`docs/i2ec_battery_investigation.md`](docs/i2ec_battery_investigation.md).
