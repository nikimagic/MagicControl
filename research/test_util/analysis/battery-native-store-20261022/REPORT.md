# Wujie: native threshold copy does not retain inhibition on platform7

Hardware run: 22 October2026,06:17:41–06:18:04UTC (09:17–09:18Moscow).
Exact WUJIE Series-X5SP4NAG, identity82/72/26, ROM-ID FF. User approved the
UAC retry. Standalone EcBitTest; MagicControl product was not rebuilt or changed.

Raw evidence: `20261022-061741-884Z-32392/battery-events.jsonl`, initial/final
4096-byte EC RAM snapshots and the empty final `battery-restore.txt`.
Launcher exit code0; complete/restored=true.

## Experiment and result

- Initial platform7, upper10, WMI087F.low7=10, lower90, SOC93,
  charging1360mA. The lower register was not modified.
- Set live7B9.low7=85, then temporarily set7C3=4. No WMI write in the trial.
- At06:17:44, firmware changed the WMI-readable087F from10 to85 and set
 742.2/7B9.7. Current became0mA at06:17:45 and remained0 during platform4.
- Restore7C3=7 at06:17:48. WMI087F stayed85 throughout all10samples.
  At06:17:49,742.2 and7B9.7 cleared. Charging state returned at06:17:50;
  measured current returned at06:17:51 and remained1360–1428mA.
  SOC reached94, already above the configured85.
- Verdict: candidate_working=false. Only2/10 returned-platform7 samples were
  both zero-current and idle; these are transition samples, not retained control.

This disproves the proposed native-store7→4→7 workaround on this machine under
the tested conditions. A retained WMI copy is not physical charge inhibition.
The observed copy does not independently prove EEPROM commit or persistence
after removal of EC power. No AC reconnect test was needed to reject this trial:
charging had already resumed with continuously connected power.

## Restoration and preservation

During cleanup, the firmware copied the original10 back while temporarily on4.
The test then restored platform7. Final live low7=10, WMI low7=10 and lower90.
No WMI cleanup fallback was used; ROM-ID was never written.

Initial/final dumps confirm identical ROM770–77F, all96 FAN table bytesF00–F5F,
APU783–785, FAN-related726/727/741/751/7C5/7C6 and lower7D0.
The final raw7B9 is8A versus initial0A: its firmware-owned Reached bit was still
set in the immediate post-restoration snapshot. The tool restored only low7;
it did not force status bits. Final0mA is an immediate transition on return7,
not evidence of a working platform7 limit. The prior trial showed this transition
clearing after approximately1second. The recorded restoration claim concerns
the owned configuration values, not byte-identical dynamic state.

## Tool validation and remaining work

Before this hardware run, the new WMI instance path was corrected to exactly
match the existing production path. Standalone compilation succeeded and the
offline masked-write selfcheck passed983040 cases without hardware access.

Real platform7 charge inhibition remains unsolved. Next investigation requires
an independently supported inhibition path or the executable EC image matching
Wujie EC1.18; comparative GM6/other-Wujie firmware is not an exact-device map.
Do not implement a permanent4 pulse, ROM-ID substitution or repeated status-bit
reassertion based on this negative result.
