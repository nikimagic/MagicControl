FirmwareDiscovery — explicit read-only unknown-laptop experiment

Source: ../src/FirmwareDiscoveryWindows.cpp and this directory's runner.
Build: cmake -S tests/util/discovery -B build/firmware-discovery -A x64
       cmake --build build/firmware-discovery --config Release --target firmware-discovery

Uses existing Release magiccontrol_platform/core/storage/runtime libraries,
the diagnostic-only EC factory and the same cross-process transaction mutex.
No MainWindow, configuration restore, profile selection or driver setup.
Existing source changes in the main product are not rebuilt/overwritten.

Run from elevated PowerShell:
  ./tests/util/discovery/Run-FirmwareDiscovery.ps1

The runner needs the local existing WSL Ubuntu-26.04 / ACPICA iasl research
tool at build/acpica-gm6-healthy/package/usr/bin/iasl. It only disassembles
saved OS ACPI tables. No AML interpreter or arbitrary ACPI method invocation.
When that tool is absent, metadata is retained and SMRW is not queried.

Two helper phases: inventory, then read-only protocol candidates.
The model catalogue is not consulted. Installed SYS hashes authorize the
reviewed transport ABI only; they do not authorize control of an unknown board.
Only the fixed EC-read IOCTL and SMRW read prefix BB may be sent. No ECRW,
SMRW AA, PWUP, mailbox trigger, fan profile, RGB setter or UEFI setter.
SMRW is queried only with a unique INOU/SMRW(1) declaration in live ACPI.

Output: a new out/test_util/discovery/runs/<UTC>-<suffix> directory under
the project root, preserving earlier runs. -OutputDirectory overrides it.
inventory.json: SMBIOS registry view, actual ACPI PnP, raw ACPI file hashes.
acpi-methods.json: IASL declarations, argument counts, source and errors.
wmi-metadata.json: classes/method names only; no setters or FireULong invoked.
read-results.json: double reads, raw capability/default blocks and RAM tables,
                   raw SMRW replies, installed driver ABI evidence.
summary.json / REPORT.txt: availability, scope and unresolved ranges.

Method names and capability bits are not self-describing device protocols.
Unknown methods are not executed. Transport success is not semantic success:
in particular a SMRW reply that echoes BB/key is not a valid firmware limit.
RAM fan table bytes are not claimed to be factory ROM tables.
API ACPI enumeration returns only the first instance per duplicate signature;
the report does not claim full ACPI namespace coverage.

No full product build, release number, portable replacement or publication.
