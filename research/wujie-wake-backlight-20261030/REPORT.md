# Wujie: wake, native keyboard idle and AC/DC brightness

30 October 2026. Read-only elevated diagnosis of the actual X5SP4NAG.
MagicControl GUI and OEM Control Center were not launched. No product source,
settings, firmware, platform, ROM-ID or lighting register was changed.
The diagnostic contains only read IOCTL `0x9C40A488`, with the existing
8-byte input/16-byte output ABI and shared EC transaction mutex. Its handle
needs read/write access because of the driver's access requirements; there is
no write IOCTL or EC-write method in the diagnostic.

## Live evidence

All elevated launches record `elevated=true`. JSONL snapshots preserve UTC,
raw bytes, the 16-bit counter, mode flags and, in later runs, Windows power
status. `live-summary.json` contains derived transitions and input hashes;
`summarize.py` only reads saved files. Multiple bounded read-only runs were
handed over as the address set was refined; `initial/stop.request` closes that
reader cooperatively, without touching EC state.

The Windows power event records wake at **05:00:33 Moscow time** from a sleep
entered at 03:41:48. This event predates the monitor; no trace of the original
physical light-off instant was available. The last MagicControl 0.1.391 session
ended through the tray at **29 October 22:57:25 Moscow time**. A process-name
snapshot found no MagicControl/GCUService/GCUBridge/AiStone/ControlCenter process.

The owner toggled the adapter eight times during the first continuous run,
and once more after the captured expiry.
`0x7CC` alternated A4/80; **Platform ID stayed 7 and ROM-ID stayed FF**.
Several adapter transitions occurred within a monotonically increasing idle
counter interval, without resetting it. Input activity caused separate resets.
These statements describe sampled states, not exclusion of a sub-sample transient.

## Actual brightness has AC/DC slots

The saved EC core and live reads agree:

| EC address | Meaning in the traced native path | Live value on AC |
|---|---|---:|
| `0x825` | saved AC brightness | 4 |
| `0x827` | saved DC brightness | 1 |
| `0x826` | current working brightness | 4 |
| `0x78C[7:5]` | host/Fn command-level field | 1 |

The owner's observation of different AC/DC brightness is supported. A stable
`0x78C` does not prove stable effective brightness in the native path.

Bank1 CODE 938C copies 827 -> 826 for the DC path; CODE 9463 copies 825 -> 826
for AC. Both paths check awake state 06E6==1 and APP presence741.0==0 before
the copy. Bank2 B48C consumes a level-update strobe from78C, saves the level
in826 and then writes825 or827 according to490.0. This diagnosis does not
claim those RAM slots are independently persisted into flash/NVRAM.

## Native idle flag and timer

Bank2 CODE BB34..BBD0:

1. If06E6!=1 or741.0 APP presence is set, reset8A5/8A6 and clear741.2.
2. Input flags in internal bit RAM24.3/24.4 also reset the counter and unblank.
3. Otherwise compare the big-endian counter8A5:8A6 with fixed code constants:
   **30 (`001E`) if7C3==4; 600 (`0258`) otherwise**.
4. At expiry set **741.2**; request refresh through86F.3 or take the direct
   blank path B47B, which zeros RGB PWM registers1803/1805/1808 in firmware.

The selector D7FA reads the entire Platform ID7C3 and XORs4. It does not read
ROM-ID770 or AC/DC. No10-second constant or separate writable timeout-seconds
field was found in this handler.600 counts correspond to10 minutes at the
observed roughly1Hz rate. Platform4's30-count branch was not selected or
physically retested: no platform write was authorized.

APP741.0 is shared with other control domains. Its effect on this handler does
not make it a dedicated safe lighting toggle. The monitor leaves it unchanged.

## OEM software timer is distinct

Exact recovered Wujie5.57 `SingleZone.CreateInstance_ControlBrigtnessEvent`
calls `SetPowerDisable_EC(true)` at host-timer expiry. That setter changes
**78C.1**, as also visible in the readable reference. `KeyboardCloseTimer`
uses a host System.Timers.Timer and input callbacks. This is distinct from
native741.2 blanking;78C.1 remained clear in the monitored interval.

No verified automatic OEM4<->7 switch on AC/DC was found in the reviewed
managed paths. Mode names such as Office/Gaming/Turbo or AMDPlatform must not
be mistaken for writes to EC Platform ID7C3. The OEM service was not executed
to test that hypothesis, since doing so would write hardware configuration.

## Source provenance and limits

`source-provenance.json` records SHA256 of the full saved32MiB BIOS and the
128KiB embedded EC core, found byte-for-byte at BIOS offset137DB4C. This is
the embedded update payload, not a live EC-flash readback. Bounded fresh
disassembly is in `firmware-idle.asm` and `firmware-ac-dc-brightness.asm`.
The exact Wujie managed source SHA256 is
79f53a23e8c64b2601c200c8fed0937bedfd033bc7625c41611701e8e2a4361a;
selected timer callbacks are preserved in `oem-timer-excerpts.txt`.

## Captured expiry and physical confirmation

At **06:55:32.237 Moscow time**, the live counter was600 (8A5=02,8A6=58)
and741 changed **80 ->84**. The previous sample at06:55:31.265 still had
741=80 and counter600. The counter then stayed600. Windows AC=1,
Platform=7, ROM=FF,78C=20,826=4 and78C.1=0 throughout that expiry.
The owner explicitly confirmed **"Да, погасла"**. This is an actual native
idle expiry and physical light-off, rather than a static-only inference.
No host EC write occurred in the diagnostic.

The owner then unplugged AC and reported that the light came back on:

| Moscow time | Captured state |
|---|---|
|06:56:27.890|826 changed4 ->1;741 still84,counter600|
|06:56:28.855|Windows AC1 ->0;7CC A4 ->80;Platform7,ROMFF unchanged|
|06:56:32.708|74184 ->80 and counter reset0|

The snapshots are sequential register reads, not simultaneous bus-level
samples; a sub-second ordering of adapter sensing and brightness loading
cannot be inferred from them. They do establish that the saved DC level was
loaded while idle-blank remained set, before a later unblank/counter reset.
That later reset must not be attributed exclusively to adapter removal:
the owner's input was also occurring then.

Bank1 DC CODE938C..93AB loads827 ->826 and calls common1762, which dispatches
to bank2 B7BF. The traced mono restore path checks host power-disable78C.1
(helperDC93) and renders the current level; it does not use native idle741.2
as an admission gate on this path. In contrast, BB34 sees an already-set741.2
and returns, rather than repeatedly blanking the outputs. This explains how
the adapter path can restore the visible light with741.2 initially still set.
`firmware-restore.asm` preserves the relevant instructions.

All four diagnostic processes completed and closed their driver handles.
The product remains0.1.391, unmodified. Physical platform4/30-second timing
and the contents of the live EC flash were not tested or changed.
