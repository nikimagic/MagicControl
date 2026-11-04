# Wujie X5SP4NAG: age override investigation, 31 October 2026

## Owner-requested history reset:9082 ->0, Full target17800mV

At12:16:45 Moscow (09:16:45UTC), after an explicit owner instruction to reset
history and confirm Full Capacity17800mV, reset-voltage-history.ps1 performed
two I2EC data-byte writes:9C9=00,9CA=00. Each was read back through the known
I2EC path. Full was already selected (7A6=04,mask30=00), so no policy write
was required. A masked ECRW request726.bit0=1 then asked the existing native
E7C6/E3B4 service-save path to persist its own23-field record.

Before any data write, two RAM snapshots were captured and the original
history237A(9082) plus original policy0 were saved to before.json in BOTH:

- research/wujie-age-live-20261031/history-reset-20261031-091644-ff1d9b02/
- MagicControl/windows/logs_WUJIE_Series-X5SP4NAG/battery-age-tests/history-reset-20261031-091644-ff1d9b02/

Copies of before/result/samples/write journal were verified by SHA256. Backup
scope is EC RAM source values, NOT an independent full device50 dump. The
rollback command restores just the saved history and original policy and
requests a fresh native save; it does not replay unrelated dynamic records.

| Field | Before | After |
|---|---|---|
|9C9/9CA history|23/7A =9082|00/00 =0|
|522/523 EC requested target|30/43 =17200mV|88/45 =17800mV|
|7A6 policy|04,Full|unchanged|
|726 shared flags|80|80 after request81 consumed|
|3A1 native save pending|00|08 during sweep,00 at completion|
|36C/39D/39F age|1/1/1|unchanged|
|7F2/7F3 alternative date|5D/21|unchanged|
|314/315 manufacture date|5A/52|unchanged|
|Cycles|44|44|
|741 APP,751 CPU profile|81,10|unchanged|
|7C3/770 platform/ROM|07/FF|unchanged|
|Full/design capacity|5200/6400mAh|unchanged|

The capture shows native save index3C4 advancing through the record, including
controller slaveA0/command65/data00, then ending with command6D/data00 and
3A1=0. This confirms the live native save workflow, not independent readback
from nonvolatile storage or power-loss durability. No direct host writes to
the SMBus controller were made. No restart/power-loss test was performed.

The dedicated I2EC write uses RIOP(681,high),RIOP(682,low),RIOP(683,data).
The final call reads port684 after writing683; that read returnedFF and was
discarded. This is explicitly a data-write operation, unlike the earlier
selector-only probe. No broken three-argument WIOP or PawnIO BAR discovery
was used. Only9C9/9CA are allowed as its data targets. Signed PawnIO LpcIO
source was also reviewed but not used because generic PIO is BAR-gated:
https://github.com/namazso/PawnIO.Modules/blob/0.2.10/LpcIO.p

Source SHA2565889cb78ad3faa4bd0ddfd3a60d1c9f71ffcb7df989e3fc540f907daccf9ddd1.
before.json SHA256184af94637d4a9d445828f872167738bea582d76f568f65780988c1e294deff2.
result.json SHA2566d8432abd606da8f2651f99ec04d8ad7fa02a26f4ff750e047aa898a852d9881.
The run completed09:16:47UTC with success=true,history_after=0,target17800,
two history-write attempts,zero policy writes and one save-request write.

An earlier attempt09:13:19UTC hit a PowerShell name collision (History alias
overrode the helper); it stopped before any EC data write. The helper was
renamed and tested with the actual dictionary type. At09:14:35UTC the next
attempt correctly stopped on DC power490=0E/491=C0, again zero data writes.
The owner then connected AC and the third attempt above succeeded. Those
failed results were preserved separately rather than overwritten.

The partial elapsed-time counters9C7/9C8 were left intact (initial0/58).
History accumulation was not disabled: future eligible intervals can add new
points. Voltage17800mV is the requested EC target, not measured battery voltage.

Rollback is prepared but was not executed because the owner requested leaving
the counter at zero. From an elevated PowerShell, using the original snapshot:

```powershell
& .\research\wujie-age-live-20261031\reset-voltage-history.ps1 -Action Restore `
  -BaselinePath .\research\wujie-age-live-20261031\history-reset-20261031-091644-ff1d9b02\before.json
```

No application source, persisted preferences, fan curves or driver files
were changed; product version remains0.1.414.

## Owner-requested September date applied: live age20 ->1

At11:42:29 Moscow (08:42:29UTC), after the owner's explicit request to make
the battery age September2026, set-september-age.ps1 wrote only alternative
date bytes7F3=21 then7F2=5D using the existing ECRW two-argument path. The
packed date5D21 is2026-09-01; the low-byte-first intermediate0021 takes the
saved-image cached-age fallback. No manufacture-date, cycle/history, FAN/APP,
platform/ROM, power-limit, SMBus-controller or flash fields were host-written.

Fresh identity, installed SYS hash, saved firmware hash/opcode windows, I2EC
configuration, EC clock2026-10, battery manufacture identity and two agreeing
RAM service-source passes gated the writes. A before.json snapshot was saved
before either write; it covers all23 sources in the native save table plus
operating context. It is an EC RAM backup, NOT an independent device50 dump.
An error after a write triggers restoration of the original zero alternative
date. No error occurred, so that rollback was not physically executed.

| Field | Before | Final |
|---|---|---|
| Alternative date7F2/7F3 |00/00|5D/21 =2026-09-01|
| Cached age36C |20|1|
| Applied aging input39D |20|1|
| Age mirror39F |20|1|
| Special bypass36D |00|00|
| Manufacture date314/315 |5A/52 =2025-02-18|unchanged|
| Native save-pending3A1 |00|00 at completion|
| FAN/shared context726/741 |80/81|unchanged|
| Platform/ROM7C3/770 |07/FF|unchanged|
| Cycles |44|44|
| History9C9/9CA |237A|unchanged|
| Full/design |5200/6400mAh|unchanged|
| Charge target522/523 |17000mV|17000mV, still on battery power|

The script finished08:42:31UTC with two date-write attempts and native age
confirmation. Source script SHA256:
4bb1373d4dcee138ab9a955589e6642dde87e347754d7989840967907f67d659.
Result is retained in september-age-20261031-084228-c43d2d71/result.json,
SHA25651b3585b93f0f8ffb349982d87768ddd2e2f545c3939628d461de90641693a65.
before.json SHA256b7016b8766773f9d3610934541da4d80ac343596042fb8622567ef33ba7f14c3.
The folder also contains phase observations and a flushed per-write journal;
raw device evidence stays outside git. The native save queue being idle does
not independently prove device50 persistence across power loss. No reboot
or AC charging test was performed. The application's manufacture-date/age
display still uses the real2025-02-18; this experiment changes the EC's
alternative aging input and does not rewrite that original identity.

The script supports explicit restoration of the original zero alternative
date, after validating the original snapshot and fresh identity:

```powershell
# Run from the repository in an elevated PowerShell. This is a hardware write.
& .\research\wujie-age-live-20261031\set-september-age.ps1 -Action Restore `
  -BaselinePath .\research\wujie-age-live-20261031\september-age-20261031-084228-c43d2d71\before.json
```

Do not run restoration automatically: the owner requested leaving September
as the current age date. The earlier sections below describe the preceding
read-only investigation, not the post-11:42 state.

### AC follow-up and owner-operated voltage buttons,11:46 Moscow

After the owner connected AC, observe-september-ac.ps1 reused only Snapshot
from the exact hashed experiment class. It did not execute Apply/Restore.
The30-second capture08:46:03..08:46:33UTC contains25 sequential frames,
zero host EC-memory writes and zero host SMBus commands. Both AC bits were
set; alternative date5D21 and cached/applied/mirror age1 remained throughout.

The owner reported pressing the voltage buttons. Their ordinary application
events and I2EC policy bits agree with the voltage transitions:

| UTC |7A6 policy bits| EC target | Interpretation |
|---|---|---|---|
|08:46:03|20|17000mV|Healthy|
|08:46:08|00|17000mV retained briefly|Full selected|
|08:46:09|00|17200mV|Full target recalculated|
|08:46:21|10|17200mV|Balanced|
|08:46:25|20|17200mV retained briefly|Healthy selected|
|08:46:26|20|17000mV|Healthy target recalculated|
|08:46:31|00|17200mV|Full again|

The saved-image prediction is corroborated by these live EC targets: BMS
base17800mV minus4*150mV from unchanged history9082 gives17200mV for Full
and Balanced. Healthy's200mV/cell floor gives17000mV. Removing the20-month
age floor therefore does not remove the independent history floor. These
are EC requested targets, not proof of analog CV output. The EC current word
ranged0..2040mA with the charging flag set; this does not establish that the
age edit alone caused any current increase during button/AC transitions.

Raw evidence is september-ac-20261031-084602-0c879ac7/result.json and
samples.jsonl. A matching subset of ordinary application profile/voltage
events was preserved as app-events.jsonl. Copies of the apply backup and
the AC observations are also retained under the working portable log root,
battery-age-tests/september-2026-20261031-114228 and
battery-age-tests/september-ac-20261031-114602 respectively. This is a live
AC transition check; reboot/power-loss persistence remains untested.
Result SHA2564565866c57f264959600c398da0eaca0621fd1cfc75a29076a41ca209a4db31e;
samples SHA2563a59fe18c18a31a1a0bd6a266040910453118ef4fa977d6e433770ea94682c2b.

Clarification requested by the owner: the host changed only the alternative
calendar date7F2/7F3. The firmware itself then updated36C/39D/39F. Manufacture
date314/315, cycle count44 and history9C9/9CA=237A(9082) were preserved.
That history is a temperature-weighted high-voltage exposure accumulator,
not a month counter or a raw unit of elapsed hours. The observed17200mV
therefore demonstrates removal of the calendar floor while preserving the
independent history-based reduction; no history/cycle reset was requested
or performed.

## Correction: previous I2EC probes used a defective driver helper

Fresh disassembly of the exact installed SYS proves that helper1DC0 copies
input offsets4 and8 to the same stack destination+BC (calls1F6A/1FB4).
WIOP therefore cannot be assumed to receive the intended three arguments.
The observed00 from the prior probe does not establish that I2EC is disabled:
its address-high selector was not reliably set to04. Do not interpret the
earlier probe sections below as a valid address0454 accessibility test.

Package identity: INF10.48.11.331 dated2025-10-28, SYS46352bytes,
SHA2566e360ee4a0c42b2a4eaf46208c1592c87f8cd71aca2a6c52831a2ad356e61235;
the installed file was freshly hashed and matches. SYS has no FileVersion.
The two-argument helper1A98 has distinct destinations. The replacement probe
uses only two RIOP calls, preserving the original output files and logging all
IOCTL inputs/replies. It was parsed/compiled without constructing the device.
It subsequently passed the approved live probe described immediately below;
no age/BMS write was made. Full proof and limitations:
[UWACPIDRIVER-ARGUMENTS.md](../../docs/UWACPIDRIVER-ARGUMENTS.md).

### Corrected two-RIOP probe: live success

The first corrected launch was cancelled at Windows UAC and never created a
result. After the owner explicitly requested retry, the corrected script ran
at08:30:08..08:30:09UTC (11:30 Moscow), exact board and driver hash matched.
All three I2EC identity bytes agreed with ECRR:454=52,456=48,740=1A.
The gated configuration reads returned200D=CB,2012=21,2014=06,2015=80.
Thus dedicated I2EC0680 decoding is live and the low SPCTRL1 bits are11;
the earlier WIOP-based mismatch was not a valid accessibility test.

There were17 IOCTL calls (3 ECRR,14 RIOP),14 address-selector writes and zero
I2EC data-port/EC-memory/SMBus writes. Every request and returned buffer is
preserved in i2ec-two-argument-probe.json outside git (SHA256
06c9449c431d2428debebe8b6474dd4b242b7893a3c814efe7659af7a431bde3).
The result verifies
the host I2EC read path, not independent device50 backup/restore or an age reset.

Additional static rollback review: the only literal DPTR0561 reset sites in
the saved candidate are bank1 BB42 (battery initialization) and DB66 (read
failure), while DB84 guards the native service walker at terminal index104.
No host reset of0561 was performed. Resetting an active walker remains unsafe
because DB9B/DBBA sample its index separately; a terminal-state handoff would
need its own admission/firmware-reinitialization race analysis before a test.

Original investigation scope before the explicit September write: read-only
software/log inspection and static decoding of the saved Wujie
EC candidate SHA-256 `5d4ec03a4ec9b36938d907fd6fcb51109b445b4a43c5498f018d022d4e6ac7a8`.
No experimental EC or BMS writes had been made at that stage. The candidate is extracted from
the saved system BIOS; it is not a hash of the active EC flash.

## Current observation and missing live fields

The GUI battery snapshot at 07:12:43 UTC reports EC 490=06 (adapter absent),
741=81, 726=80, 7A6=04, 7C3=07, 770=FF, 30E=17800 mV,
522=17000 mV, cycles=44, full=5200 mAh and design=6400 mAh. The charge
target may be retained on battery power. These logs do not contain live 36C,
36D, 39F, 7F2/7F3 or 9C9/9CA.

At 07:14:25 UTC the elevated read-only reader received two identical samples,
with exact EC identity 82/72/26: 36C=14 (20 months), 36D=00, 39F=14,
3A1=00, 3A3=00, 726=80, 72D/72E=26/10 BCD, 7F2/7F3=00/00 and
9C9/9CA=23/7A. 490=06 (AC absent), 741=81, 7A6=04, 7C3=07, 7CC=80,
30E=17800 mV, 522=17000 mV, cycles=44, design=6400 and full=5200 mAh.
Both passes agree, but the reads are sequential rather than a hardware-atomic
snapshot. The tool made no EC writes; the full output is `age-baseline-dc.json`.

The first result was preserved as `age-baseline-dc.json` before the second
read. At 07:19:35 UTC, after the owner connected AC, a second two-pass read
(`age-baseline-ac.json`) also succeeded with zero write calls. Both passes
agreed. Relative to the DC snapshot only 490=06->0F, 491=C0->C1 and
7CC=80->A4 changed; 36C=20, 36D=00, 39F=20, 3A1=00, 7F2/7F3=00/00,
9C9/9CA=23/7A and 30E=17800 / 522=17000 remained the same.

Last confirmed EC dump, 29 Oct, had 314/315=5A52, 36C=14 (20 decimal),
36D=00, 726=00, 72D/72E=26/10 BCD, 7F2/7F3=00/00, 9C9/9CA=23/61,
491=C0, 490=07, 7A6=00, 741=01. This is historical, not the current state.

Two calls to `mctl dump-ec --read-only` did not attach EC access: the current
CLI dump path constructs a request with no EC access. Both calls nevertheless
initialized the normal three-slot journals and overwrote two older slots.
The live GUI session in slot 3 remains. The first UAC request for the separate
model-checked, read-IOCTL-only `age-read-only.ps1` was cancelled or timed out;
the second succeeded and produced the snapshot above. No EC fields were written.

## Six requested scenarios: firmware prediction only

If the 4-cell base request is 17.80 V, Full Capacity policy is selected and no
cycle/exposure condition imposes a stronger reduction, the *age component alone*
predicts the following, not live observations:

| Months | Reduction per cell | Requested voltage |
|---:|---:|---:|
| 1 | 0 mV | 17.80 V |
| 2 | 0 mV | 17.80 V |
| 3 | 0 mV | 17.80 V |
| 6 | 0 mV | 17.80 V |
| 12 | 100 mV | 17.40 V |
| 24 | 200 mV | 17.00 V |

Cycle count, exposure history and mode can select a stronger tier, and a
requested target is not a measured charger output. Age 0 via 36D=59 is a
separate bypass path, not a six-value numeric age setter. The age function
uses valid packed date 7F2/7F3 preferentially, otherwise 314/315; EC clock
72D/72E supplies BCD year/month. Any host-supplied date would be an experiment,
not a proven supported API.

The current captured 9C9/9CA word is 0x237A = 9082, above the 8640 threshold.
If this history input, base voltage, cell count and Full Capacity mode remain
unchanged when charging logic executes, firmware predicts at least 150 mV per
cell reduction: 17.20 V for ages 1/2/3/6/12, and 17.00 V for age 24. This is
a conditional prediction, not an observed charger result or proof that the
counter will remain unchanged once the adapter is connected.

## Newly traced possible persistence

The age routine bank1 E7C6..E9A5 calls E3B4 when the calculated age changes.
E3B4 sets 3A1 bit3. The consumer E48F..E4D0 dispatches 23 indexed items through
E4F4 -> common 1AE0 -> bank0 882E, with destination parameter A0 and protocol
parameter 88. The CODE table at 593F maps XRAM fields including 9CA, 9C9,
7F3, 7F2, 36C and 36D to separate bus indices. This is a real firmware-side
transaction path, not evidence that restoring two EC RAM bytes reverses all
effects. The physical device behind A0, persistence, acknowledgements and
safe full rollback have not been established. The same queue carries exposure
history, so blanket replay risks overwriting unrelated firmware-owned state.

Additional static verification: bank1 C567..C595 reads vendor command 42 into
342 and command 47 into 4AB using EA19 with address A1. Bank1 E4F4 sends the
age-queue data using A0 to the same seven-bit address 50. Mapping the bank0
controller-register table to the saved IT5570 preliminary specification
(`research/ec-platform-catalogue-20261023/web/ite-text.txt`, HOCTL and TRASLA)
shows direction bit 0 and host control 48, which starts an SMBus Write Byte.
Thus this is a firmware-requested write over the same address used for battery
telemetry, not merely an EC RAM write. Physical device identity, storage
durability, transaction success and restoration still remain unverified.

Do not run writes until the baseline, exact destination, completion, persistence
and restoration protocol are established and the electrical conditions are
understood. The AC baseline is now available; no age-override writes have been
performed.

## Backup and native-read investigation, 31 October

At 07:25:40 UTC `age-shadow-backup.ps1` captured both passes of all 23 XRAM
source bytes in the 593F persistence table, plus age, board identity, charging
and fan context. The two passes matched, model 82/72/26, AC present and zero EC
writes. `age-shadow-backup.json` SHA-256:
`FEC5C91935C8A67E563421B85670F4DE2265EC35DAB84FAAE09F24ED57F7931C`.
This is a *RAM snapshot*, not an independently read backup of device 0x50.

Firmware bank1 DB9B..DBFA uses the 57E9 table for Read Byte through device
A1 (7-bit 50). Table indexes 99/100/101/102 map SMBus commands 68/69/6C/6D
back to XRAM 7F3/7F2/36C/36D. The same four fields occur in the write table.
On a successful transfer the byte is copied to its XRAM target and 54C clears;
on a failure DB4F resets 561 and updates failure state. A simple 561 advance
alone does not prove the success of each individual transaction.

At 07:27:59–07:28:44 UTC the separate `age-poll-read-only.ps1` captured 41
frames, zero EC writes. All observed 561 values were 104 (0x68), 54C=0,
3A1=0 and age/override bytes stayed 20/00/00/00. This supports that the
current observed session had finished its initialization pass, but does not
prove automatic re-reading or transaction acknowledgements during a later
experiment. `age-poll-read-only.json` SHA-256:
`67B2C6009C4BEE0EBB15357A2668FD852512D9B7856BF25BF32E576BF59EFB79`.

The Wujie ACPI ECMG host window is 0x1000 bytes; the EC's own SMBus controller
registers are at XRAM 1C00+, outside the reviewed diagnostic window. The
existing PawnIO SMBus path reads DIMM SPD on the host bus; a matching 7-bit
address 50 does not prove it is the EC's battery bus. No independently reviewed
host route for reading or restoring the target device bytes was found.
Forcing firmware poll state or writing host-controller registers would itself
be an unreviewed hardware operation. Avoiding these is necessary to preserve
the user's safety constraint and not misrepresent a RAM-only restore as full.

The saved bank1 code gives an additional reason not to reset 561 from the host:
DB9B uses 561 to select the next device command, issues the transaction via
1AE0, then DBBA reads 561 again to select the destination XRAM address. A host
reset between those two firmware reads could cause a returned byte to be stored
at the wrong location. The host's Global EcTransactions mutex serializes host
requests; it does not stop the EC instruction scheduler. No reviewed way to
pause this firmware path, independently read the 0x50 device and verify all
23 nonvolatile values was found. Direct forcing of 561 or the host controller
therefore is not a safe substitute for a verified backup/restore transaction.

`age-scenarios-offline.ps1` validates the exact image hash, verified board
identity, two agreeing RAM snapshots, idle 3A1, AC and BCD EC clock; it derives
the 23 entries directly from the saved firmware table and applies the reviewed
voltage tier algebra to the six requested ages. Its output is explicitly a
conditional calculation, not firmware emulation, live readback, a backup of
device 0x50 or permission to write. With the captured 9082 history input it
returns 17200 mV for 1/2/3/6/12 months and 17000 mV for 24 months. No hardware
access is made while running it.

## One-month path and intermediate-byte hazard

The EC age routine entry is bank1 E7C6, reached from common 1CA8. It tests
726 bits 0/1/2, then treats 36D=59 as a special early return of age zero.
Otherwise it prefers a valid packed date at 7F2/7F3 and falls back to the
battery manufacture date 314/315. The saved AC snapshot has 726=80,
36D=00, 7F2/7F3=00/00, 36C=14 and 39F=14; the EC clock is BCD 26/10.

With that clock the offline candidate for one calendar month is 2026-09-01,
packed SBS date 0x5D21, high byte 5D at 7F2 and low byte 21 at 7F3. The
routine checks/decodes the date and computes year/month difference; it does
not accept the literal number 1 as an age override. This is static analysis,
not a supported API or an authorization to write those bytes.

`one-month-static-replay.ps1` verifies the image hash, selected opcodes and
two agreeing saved snapshots, then replays only the relevant branch algebra.
It produces `one-month-static-replay.json`, without hardware or firmware
execution. For this exact baseline it reports:

* 0000: manufacture-date branch, 20 months;
* hypothetical high byte first: intermediate 5D00. The observed month/year
  branch can accept month 8 with day 0 and calculate 2 months, scheduling the
  age-save queue before the intended second byte arrives;
* hypothetical low byte first: intermediate 0021, invalid year for the later
  calculation and cached-age fallback of 20 months;
* final candidate 5D21: alternative-date branch, 1 month and age-save queue.

The byte-order examples are hazard analysis, not a safe write sequence. The EC
continues executing asynchronously between separate host writes, and another
queue event could persist an intermediate state. Neither ordering guarantees
rollback, target-device consistency or the absence of collateral changes.

## Candidate I2EC read route, not executed

Own saved Wujie image bank0 EAA0..EAB2 sets PI2ECEN and port base 0680; common
0FDF..101B consumes the table at 106D, including `200D 43`, and bank0 A9C5
ORs C8 into 200D. The usual resulting 200D=CB would enable I2EC read/write, but
the active EC image/configuration and reachability of every initialization
path have not been attested. No claim that live I2EC is enabled follows from
saved bytes. Windows PnP reports 0680..0683 inside the broad PCI Express Root
Complex resource 03E0..0CF7, not an independently identified I2EC owner.

Installed `C:\Windows\System32\drivers\UWACPIDriver.sys` SHA-256 is
`6E360EE4A0C42B2A4EAF46208C1592C87F8CD71ACA2A6C52831A2AD356E61235`, matching
the reviewed 10.48.11.331 driver. Its dispatch maps A4CC to ACPI WIOP and A4C8
to RIOP. The board's DSDT implements WIOP as two byte I/O port writes and RIOP
as one port write then the adjacent port read. If the firmware's expected
mapping is active, WIOP on 681 sets I2EC address high/low through 681/682;
RIOP on 682 repeats low and reads the data port 683. Host-selector writes are
real I/O writes even though they do not intentionally write EC memory.

`i2ec-identity-probe.ps1` is prepared but **not executed**. It checks the
SMBIOS board and installed driver hash, serializes with the existing host EC
mutex and verifies standard ECRR identity 82/72/26 before touching the I/O
selectors. It is fixed to ports 681..683 and first tests only identity
454/456/740; a mismatch aborts before any other target. Only after all three
match does it read 200D/2012/2014/2015. It contains no write to port 683,
EC RAM, SMBus registers, 0x50 or firmware. PowerShell parsing and C# compile
were checked offline without instantiating the object or opening a device.

Risk: if 681/682 are not the expected I2EC selectors on the running machine,
their writes could instead reach a different device. The broad PnP claim and
saved firmware reduce but do not eliminate that uncertainty. This probe does
not access 0x50 and cannot by itself prove a safe backup or restore; SMBus
controller arbitration with EC firmware remains unresolved.

At 08:05:14 UTC the owner explicitly approved and one restricted probe ran.
Ordinary ECRR identity passed, but the I2EC comparison failed at the very first
byte 454, so no later identity or I2EC configuration bytes were read. The
first implementation discarded the actual mismatching byte when throwing: it
is unknown, not assumed FF or zero. Both selector calls returned far enough
to produce the mismatch; no I2EC data-port write, EC-memory write or SMBus
command was issued by the probe. The source was amended to record expected and
observed bytes if the owner later authorizes a separate retry; it was not
re-executed. `i2ec-identity-probe.json` is retained outside git.

An ordinary ECRR read-only snapshot at 08:05:43 UTC still reports the same
identity, age 20, 36D=0, 7F2/7F3=00/00, history 237A and same 726,741,7A6,
7C3, 30E and 522 fields as the 07:19:35 AC baseline. The AC indicators
changed 490 0F->0E, 491 C1->C0 and 7CC A4->80: external power is no longer
confirmed. The samples are 46 minutes apart and do not establish when power
changed. The owner subsequently confirmed the machine was intentionally
running on battery; do not attribute this change to the port probe.

### External reference search and minimal retry preparation

The original upstream `minortex/ec_reverse` I2EC reader uses byte writes to
base+1/base+2 followed by a byte read from base+3; its documented 0680 example
was observed on a different Wujie-family unit and does not validate this one:
https://github.com/minortex/ec_reverse/blob/main/tools/i2ec_rw.py
https://gist.github.com/w568w/957976b59906e0ce5d6c13ad342e1593?permalink_comment_id=6259792

An independent Xingyao 14 P916F-STX study rejected its 0380 I2EC hypothesis
when candidate reads returned FF while a separate memory-window sample was
plausible. It is another model and port, so it is an example of a possible
failure, not evidence that this Wujie returned FF:
https://github.com/YMGPwcca/Mechrevo-Xingyao-14/blob/main/docs/embedded-controller.md

Probe source was changed to include the actual observed byte in a mismatch
error. `-Attempt retry` uses a separate result filename and stops immediately
after the first I2EC byte even if it matches. The PowerShell and embedded C#
were parsed/compiled offline; no probe object or device was opened. The owner
authorized the original single attempt, not another physical I/O cycle. The
original result remained preserved; the later retry is described below.

At 08:08:49 UTC the owner approved one additional first-byte-only retry. The
ordinary ECRR identity again passed, but the I2EC candidate returned `00` for
address 454, where the verified EC value is `52`. The script stopped there;
it did not read I2EC configuration or access SMBus device 50, and it issued no
data-port, EC-memory, BMS or firmware writes. The first attempt's actual byte
remains unknown. The mismatch alone cannot distinguish disabled I2EC, a wrong
port route or differing I/O/driver semantics.

The standard ECRR-only post-read at 08:09:18 UTC matched the prior 08:05:43
sample for all checked identity, age, voltage, platform, power-input and
726/741 fields. Both report battery power, as confirmed by the owner. This is
not a full diagnostic of every device or a physical fan-speed validation.

### External I2EC comparison

`minortex/ec_reverse` documents the dedicated I2EC port sequence and a
working 0680 example on another Wujie-family machine. An independent Xingyao
14 P916F-STX report found its different 0380 candidate returned FF and did
not match its host-visible EC reading. Neither result establishes this unit's
port mapping; this unit's one approved retry returned 00, not FF.

The Minisforum N5 I2EC research source uses a distinct PNPCFG depth-2 route
through ports 2E/2F and selector indices 11/10/12:
https://github.com/ltdstudio/minisforum-n5-it5571/blob/main/research-tools/n5_i2ec_read.c
The retained IT5570 preliminary specification also describes depth-2 I2EC;
it does not establish that this X5SP4NAG exposes it, which device decodes
2E/2F, or whether access is contention-free. No depth-2 probe was performed.
The observed 00 could result from unavailable decoding, the host-driver path,
or a different cause; no one explanation has been confirmed.

### Device 50 versus the standard Smart Battery address

Fresh static decoding of the saved exact Wujie candidate distinguishes two
logical addresses on EC SMBus channel 0. Bank1 A513/A52E/A549 and the callers
at A421/A438/A44F/A466/A47D issue Read Word operations to 8-bit address 17
(7-bit 0B), with commands 08/0F/18/19/0D respectively, and publish responses
into battery telemetry fields. These command meanings and address 0B agree
with SBS/TI Smart Battery definitions; they are not commands to address 50.

Separately, bank1 DB9B..DBE3 reads byte-mapped service data from 8-bit A1
(7-bit 50), while E4F4 requests writes to 8-bit A0 at that same 7-bit address.
These are distinct logical SMBus addresses, even though both relate to battery
information. Static code does not prove whether address 50 is an EEPROM, a
second address of the battery controller, a separate proxy or another device;
do not claim separate physical chips or blindly access a host DIMM at 50.
Primary protocol references:
https://software-dl.ti.com/msp430/esd/MSPM0-SDK/latest/docs/english/middleware/smbus/doxygen/api_guide/html/smbus_8h_source.html
https://sbs-forum.org/specs/sbdat110.pdf
