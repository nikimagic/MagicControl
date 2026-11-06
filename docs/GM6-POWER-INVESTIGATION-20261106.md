# GM6IR0C: Custom power investigation, 6 November 2026

Evidence: user archive `Users_logs/logs_GM6IR0C (20).zip`, MagicControl
0.1.483, EC1.12 decompilation in
`research/devices/gm6ir0c/battery-all-modes-20261021/ec-handoff-audit/v112-bank2-platform-clean.c`.
Times below are Moscow local time (UTC+3).

At 11:54:35 EC readback confirmed Custom PL1/PL2/PL4 requests 30/30/30.
At 11:54:41–11:54:51 CPU load was 100% and Intel Package RAPL was about
17.8–18.2 W. This is observed package power, not an independently read PL1.
The capture does not report active CPU package limits or limit reasons.

The EC decompilation conditionally copies 0x783/0x784/0x785 into intermediate
0x8C4/0x8C5/0x8C6; other branches replace or limit those values before
0x873/0x87B/0x88B. The capture does not contain those fields during the load,
so the responsible branch and actual CPU consumer remain unproven. There is
no basis for increasing requested power or writing intermediate EC fields.
An independent read of Intel package limits and limit reasons, together with
timestamped EC intermediate values under the same load, is needed to identify
the limiting mechanism. EC readback, CPU-enforced limits and RAPL power must
remain distinct in the UI and logs.

## Follow-up: PL4 and missing evidence

The same trace shows a second transition at 11:57:43 from 140/165/250 to
30/30/30 with CPU load at 100%; package power dropped from about 165 W to
about 18 W. At 11:57:56 the entire triple changed again to 140/165/250 and
power rose. Since PL1, PL2 and PL4 changed together, this does not isolate
which limit caused the drop. Intel describes PL4 as a proactive instantaneous
limit, unlike the average-power PL1/PL2 limits. A PL4 request as low as PL1
is therefore a candidate, not a proven explanation or a reason to silently
alter the user's PL4. Intel 13th/14th gen datasheet:
https://cdrdv2-public.intel.com/743844/743844-015.pdf .

Correlating only CPU samples with load at least 95%, a fresh EC triple read
within four seconds, and at least five seconds since the triple changed gives
14 samples at 45/45/200 with package RAPL 44.3–45.6 W (median 44.9 W), and
16 samples at 30/30/30 with 17.8–18.1 W (median 18.0 W). These are separately
sampled observations, not a controlled comparison of PL4 alone. The older
archives 18 and 19 did not contain comparable high-load records in this schema.

The EC diagnostic snapshots in this archive read `FF` throughout 0x800–0xBFF,
including the internal firmware candidates 0x8C4/0x8C5/0x8C6 and 0x873/0x87B/
0x88B. The transport validity bitmap confirms read completion, not that these
host-visible bytes expose the firmware's internal XDATA. Those bytes were read
before the load, so they cannot explain the later 18 W event either.

The saved GM6 BIOS DSDT also has an independent conditional PL1 route: `GUAM`
may call `SPL1`, which saves the current MCHBAR 0x59A0 PL1/enable/clamp and
writes a value derived from GNVS `PLVL`; `RPL1` restores those fields. The
PEPD path invokes GUAM for platform state transitions, with additional guards.
All three AML method packages in the 06.11 saved DSDT match the disassembled
MRO09 template byte-for-byte at their respective offsets. This proves the code
is present, not that `PLCS` was set, the path ran, or its target was 18 W while
the load ran. The collected MMIO observation can distinguish an active lower
MCHBAR PL1 from the separate MSR setting, subject to sampling timing.

The shared Windows monitoring collector now captures MSR 0x610 and MCHBAR
0x59A0/0x59A4 plus MSR 0x601 on reviewed Raptor Lake CPUs during existing
logging bursts; Lenovo retains its previous baseline. MSR 0x601's reviewed
PL4 current field is reported in amperes and raw bits, not converted to watts.
The package readings, EC requests and RAPL power remain distinct and are not
atomic. This is observational only and has not been run on the GM6 hardware.
Intel SDM volume 4, tables 2-45 and 2-46:
https://cdrdv2-public.intel.com/874253/335592-090-sdm-vol-4.pdf .
