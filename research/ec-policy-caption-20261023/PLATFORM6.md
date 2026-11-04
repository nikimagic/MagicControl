# EC platform6: evidence and limits,23.10.2026

Confirmed exact L16 identity: MACHENIKE/L16, BIOS N.1.13MAC10, EC1.16,
Ryzen9 7945HX + NVIDIA RTX5070Ti. Two supplied EC captures observe7C3=06,
770=FF. Sources: research/l16-1zip-20261022/REPORT.txt and
research/l16-2zip-20261022/REPORT.txt, matching raw ec.csv files.
This observation does not identify6 as an economy/turbo/APU mode.

OEM5.60.60.17 DefaultTool MyECIO.MyEcCtrl.IsAMDPlatform (line615) determines
AMD by PCI vendor0x1022, not by7C3=6. BIOS project enumID6 is separately
derived from74C/7CA; do not conflate that enum with this platform byte.
No literal7C3/1987 reference found in readable DefaultTool/GCUService C#.
385 GCUService class files have protected runtime exception stubs; method
names are not recovered implementations. See DECOMPILATION-REPORT.txt.
EC RAM/ACPI snapshots are not executable EC firmware; no exact L16 executable
EC image/control-flow reconstruction was found in the inspected L16 artifacts.

Current upstream uniwill-acpi.c, read23.10.2026, does not name7C3 or decode
value6: https://raw.githubusercontent.com/torvalds/linux/master/drivers/platform/x86/uniwill/uniwill-acpi.c
Leo-stro/mechrevo-charge-limit discusses4/5 gates for its own tested firmware,
not an exact L16 platform6 implementation. Its results do not prove L16 cutoff:
https://github.com/Leo-stro/mechrevo-charge-limit

Already admitted L16 operations use its own identity/driver, tables and CPU
limits. Upper7B9 configuration is separate from physical cutoff proof.
7D0/7D1 are DBD1/DBD2 in exact ACPI, not established restart thresholds.
Platform6/ROMFF remain intact;4/7 ROM ownership writer is not admitted for L16.

UI166 displays observed platform decimal and ROM-ID byte HEX, with unknown
as a dash.4/FF and7/04 buttons describe existing commands, not choices newly
admitted for L16. Exact-pair highlighting avoids treating6/FF or7/FF as4/FF.
No hardware execution, tests or EC writes performed for this analysis.
