# Wujie X5SP4NAG: 5200 mAh reported full capacity

31 October 2026. Saved EC snapshots, application logs, the device's saved DSDT,
and static inspection of the EC candidate embedded in BIOS N.1.20MRO15. No
device access, EC writes, charger commands or firmware execution were performed.

## Observed

The 0.1.413 battery log, 2026-10-31 06:24:37 UTC, records EC 0x402=6400,
0x404=5200, 0x51C=5200, 0x436=4940 mAh, 0x4AB=95%, 0x4A6=44 cycles,
0x490=0x06, 0x497=0x45, 0x7A6=0x04, 0x30E=17800, 0x522=17000.
0x490 indicates no AC at that sample; 0x522 may be a retained request.
The 0.1.412 Windows Battery Class sample at 05:24:41 UTC instead reports
99072/99072 mWh. These are distinct publications, not independent pack tests.

Saved Wujie EC dumps, with ec-valid masks confirming all quoted bytes:

| Snapshot | 0x402 | 0x404 | 0x51C | 0x340/341 BE | 0x342 | 0x3C0 | cycles |
|---|---:|---:|---:|---:|---:|---:|---:|
| 24 Oct 19:29 | 6400 | 5800 | 5800 | 5800 | 58 | 58 | 37 |
| 25 Oct 14:25 | 6400 | 5800 | 5800 | 5800 | 58 | 58 | 38 |
| 29 Oct 05:07 | 6400 | 5500 | 5500 | 5500 | 55 | 55 | 41 |
| 31 Oct 09:24 log | 6400 | 5200 | 5200 | not sampled | not sampled | not sampled | 44 |

The exact time, trigger and internal factor of the 5500->5200 transition
are absent from available logs. A 52 factor now is a hypothesis, not an observed
byte. Changes could have occurred between captured snapshots.

## Exact saved firmware candidate

`research/wujie-full-bios-20261024/wujie-embedded-ec-core-candidate-128KiB.bin`
SHA-256 `5d4ec03a4ec9b36938d907fd6fcb51109b445b4a43c5498f018d022d4e6ac7a8`.
This is a candidate extracted from the saved BIOS, not attestation of every
byte currently executing in EC flash. Logical addresses below are bank 1.

* `DC61..DC8D` tests factor 0x342 nonzero and equality with 0x3C0; `EBA3`
  loads the factor, `DC7C..DC80` multiplies by 100, and `EA42..EA4B`
  publishes the result to little-endian 0x404/405. The observed 58/5800 and
  55/5500 corroborate the path. `DC90..DCAF` also updates 0x51C/51D.
* `C807..C836` derives bounds from old published FCC and uses offsets +3/-3;
  `C9BB..CA07` derives a candidate from 0x388, clamps it between 0x39B and
  0x39C, commits 0x342, multiplies by 100 for 0x340/341, sets 0x3A1.1 and
  advances 0x3A3. This supports approximately +/-300 mAh per such learning
  update. It does not prove this path caused the two observed drops; other
  initialization/telemetry paths, e.g. `DC3E..DC60`, also affect the factor.
* The reference state-machine audit under `research/battery-charge-20261022/
  reference/battery_calibration_trigger_audit_20260721.md` describes low-SOC
  starting and charge-completion gates for a related image. Do not transfer
  exact thresholds or insist on a deep discharge for this unit without tracing
  the corresponding paths and live conditions on this exact board.

## Voltage policy and Windows reporting

The saved Wujie EC candidate has a separate calendar/cycle-based charging-voltage
reduction. At calendar age 20 months the applicable tier is 200 mV per cell;
the saved 4-cell calculation 17800-4*200=17000 mV is corroborated by prior AC
readbacks. It is a requested voltage, not actual terminal measurement or proof
that this caused a 600 mAh loss. Detailed code evidence is in
`research/native-voltage-recheck-20261026/REPORT.txt`.

The Wujie DSDT `tests/test_util/analysis/EC_20261012_073054/acpi/DSDT.dsl`
maps XIF1 to 0x402, XIF2 to 0x404 and ISDB to 0x497 bit 0. `_BIF` substitutes
XIF1 for XIF2 when ISDB=1 and cycle count <50. The observed 0x497=0x45 and
cycles=44 select that branch. Therefore Windows showing 100% does not
independently refute the EC result or measure full physical capacity.

## What is not yet established

No available trace captures the transition to 5200 or shows the cause of its
candidate/underlying gauge values. These readings do not distinguish physical
capacity loss, changes in usable voltage window, inaccurate calibration or a
combination. They do not establish that calendar age directly forces 5200 or
that raising a factor restores physical capacity. For a future read-only log,
correlate coherent 0x340..342, 0x3C0, 0x388/389, 0x39B/39C, 0x3A1..3A3,
0x404/405, charging state/current/voltage and timestamp, with identity and
freshness gates. Do not write the factor or recommend repeated deep discharges.
