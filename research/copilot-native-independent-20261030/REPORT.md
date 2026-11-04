# Wujie: search for Copilot blocking with native keyboard idle preserved

30 October 2026. Exact target: Wujie X5SP4NAG, EC identity82/72/26.
Only saved firmware/decompilation and current product source were read. No EC,
WMI, ACPI, keyboard, fan or power command was executed. No product change/build.

## Result and boundary

No independent native Copilot setter preserving the native idle countdown has
been established. The fully traced existing branch requires APP741.0, whereas
the fully traced native idle routine resets its counter whenever APP741.0 is1.
These are unconditional tests within the recovered branches, not a GUI policy.

OEM5.57 solves the functional coexistence by retaining APP and implementing
its OWN host KeyboardCloseTimer; its expiry changes78C.1. That allows native
Copilot blocking plus automatic backlight-off, but does NOT preserve the EC
8A5:8A6 idle counter. No software timer was silently substituted in MagicControl.

The search is not a proof that no undocumented firmware interface can exist.
No experimental writes, new firmware, matrix edits or APP toggling were used
to claim otherwise. In particular the complete BIOS WMI callback ABI and all
computed EC accesses were not recovered in this pass.

## 1. Exact saved EC branch

Source: research/wujie-full-bios-20261024/wujie-embedded-ec-core-candidate-128KiB.bin
SHA256:5d4ec03a4ec9b36938d907fd6fcb51109b445b4a43c5498f018d022d4e6ac7a8.
This is the BIOS-embedded update payload, not an independent live EC-flash read.

Bank0 C478 tests internal key codeF8. On that path:

```asm
C47F MOV DPTR,#0728h
C482 MOVX A,@DPTR
C483 ANL A,#04h
C485 JZ C48F
C487 LCALL C76D
C48A JZ C48F
C48C MOV 67h,#84h
C48F RET

C76D MOV DPTR,#0741h
C770 MOVX A,@DPTR
C771 ANL A,#01h
C773 RET
```

The resulting truth table for this input branch is:

| APP741.0 | Copilot728.2 | F8 remapped to84 | Native idle routine |
|---|---|---|---|
|0|0|No|Not suppressed by APP|
|0|1|No|Not suppressed by APP|
|1|0|No|Counter reset|
|1|1|Yes|Counter reset|

Changing Platform ID or ROM ID does not bypass either of those two local
checks: C47F/C76D does not consult them. Native idle selects30 or600 later,
but its APP exit occurs BEFORE that selector. No platform/ROM write was made.
Mapping F8->84 is interpreted together with the earlier physical validation
of native blocking; it is not asserted that84 is universally a disabled key
code on other firmware.

All literal DPTR728 sites in the three bank views:

- Bank0 C47F: testsbit2 and APP, as above.
- Bank0 CB9D: clearsbit2 with maskFB in a larger initialization path.
- Bank2 DC45: testsbit0, not Copilotbit2.

Neighboring DPTR724..729 sites and bounded decoded windows are preserved in
adjacent-register-references.json. Full computed-pointer/indirect-call closure
is NOT claimed. Only one literal `MOV67h,#84h` pattern was found across the
nonduplicated bank regions. Other computed/table mappings remain a limitation.
keyboard-windows.asm contains bounded linear windows; C200 and DC30 are not
claimed as recovered function entry points. The critical C47F/C76D boundaries
are independently verified from their exact bytes.

The native idle proof is preserved in
../wujie-wake-backlight-20261030/firmware-idle.asm:
bank2 BB34 tests awake; D667 reads741.0; when set BB3E calls DD0E to reset
8A5:8A6 and BB45 clears expired flag741.2. No lower mode/brightness bit is
checked to override that APP branch.

## 2. What the OEM source actually does

Exact Wujie recovered5.57 source:
research/ec-platform-catalogue-20261023/managed-provenance/GCUService-readable-strings.cs
SHA256:79f53a23e8c64b2601c200c8fed0937bedfd033bc7625c41611701e8e2a4361a.

- UserSetCopilotKey76244..76259 persists the preference and calls SetCopilotKey.
- SetCopilotKey76586..76589 invokes SetLCWMI("SetCopilotKey",status).
- The native helper's existing static analysis shows OemWMIfun03/06+bool,
  getter03/05. It was not executed. It is a front end, not evidence of a
  different EC key handler on this exact Wujie.
- Set_APExistToEC14466..14473 changes APP bit0. Startup90613 sets presence;
  exit90592 clears it. The exact source keeps application presence independently
  from its individual Copilot setter.
- KeyboardCloseTimer67378 onward owns System.Timers.Timer, seconds, input
  notifications and its own monitor state.
- Keyboard_MouseEventTrigger67498..67506 restarts that timer and requests
  brightness restoration when needed.
- _TimersTimer_Elapsed67515..67524 stops its timer and raises the blank request.
- SingleZone.CreateInstance_ControlBrigtnessEvent102916..102939 forwards
  expiry/restoration to SetPowerDisable_EC(true/false), the78C.1 host gate.

The readable older reference KeyboardCloseTimer.cs corroborates the host-timer
mechanism, but is not substituted for the exact5.57 source above. oem-methods.json
preserves exact bounded methods and source lines.

Other OEM source leads were checked as comparisons ONLY:

| OEM source | Copilot setter |
|---|---|
|L16 5.60.60.17, lines86514..86525|Read1832(728h), set/clear04, write same byte|
|HYDROC 5.23.51.56 MySettingManager, lines3679..3690|Same RMW728.2|

Their setters alone do not establish firmware behavior on Wujie. No foreign
register mapping, driver command or mode was adopted.

An additional apparent alternative, GCUService.KeyMapping.KeyboardManager
44418 onward, is a PowerToys integration: Microsoft/PowerToys/Keyboard Manager,
default.json, PowerToys.KeyboardManagerEngine.exe (44673..44695). Its remapKeys
data is not a programmable EC key matrix. No PowerToys process was launched.

## 3. BIOS / WMI admission check

Saved OemServiceSmm GUID7C2D1E53-E565-47EA-8A1E-DC97459926B0 was checked.
Its raw FFS at old16MiB-relative656808, length120BA, is byte-identical to the
latest owner's32MiB BIOS at1656808. FFS SHA256:
d7086ddeb5bc1343f1317a0f26857aaeb47d7827969bfaa3ae336a9b2022e295.
Extracted PE SHA256:
eb4fe13277946a1279fb32c94a60daa9b712ffcfd05f6c4c3ed01db41df72767.

Neither raw32-bit literals728/741/FED50728/FED50741 nor immediate operands
matching them were found in the module scan. This DOES NOT rule out computed
EC access. No alternate Copilot command was established from that scan.
smm-literals.json records the exact comparison and limited negative result.

Reviewed saved DSDT/SSDT source does not contain OemWMIfun, OemWMIMethod or
HWMI names. The earlier exact-device read-only WMI probe also reported no
OemWMIMethod class. No inferred SMI packet was sent to bypass this admission.

## 4. Implementation decision still needed

The current request requires BOTH functions. The known unmodified firmware
branch cannot provide native Copilot blocking with the native idle routine
running at the same time. A different native path remains unproven.

The OEM-backed alternative is: keep native Copilot blocking and implement
backlight inactivity on the host through the normal single lighting operation.
Its consequences must be explicit: the app must be running; the displayed
countdown would be host-derived; AC/DC brightness and restoration must stay
model-correct. It must NOT be presented as an EC counter, and would not require
a Copilot hook, a second Copilot implementation or any fan-mode change.

No product code, settings, fan table, power limit or hardware state changed.
No version increment or build was performed for this investigation.

## 5. Additional native candidate72B.0: investigated, not admitted

Bank0 C2CA reads72B.0 before the C47F Copilot lock branch. With this bit set,
the transformation handles several internal key codes:8D, F9 and C6. In
particular C32D testsF9 and C331 replaces it withF8; C33D replacesC6 with51.
Clearing72B.0 bypasses this whole block. Thus this is a keyboard-mode/mapping
candidate, not an independently established per-key disable flag.

Historical29October RAM dump has72B=01. No current write or physical test was
performed. Firmware literals for72B were found at bank0 B0F4,C2CA,CB14 and
bank2 BF35,DDB0; the other sites test/set different bits. No exact OEM setter
for72B.0 was found in the reviewed5.57 or5.60 source (including decimal1835).

The transform is reached through common trampoline15BE: DPTR=C2CA, bank0
gate1100, called at1D5B. Output proceeds through1EC8. Its CODE tables contain:

| Internal key | Table bytes | Interpretation boundary |
|---|---|---|
|F8|51EC: F2 84|Two table alternatives selected by1DB1; not assumed make/break|
|F9|51EE: 8D 84|Different output path when Copilot transform is bypassed|
|C6|519B: 20 51 07|Also affected by the72B.0 transform|

1FBD dispatches high-bit-coded entries through a table of `(value,class)` at
51FA and three-byte generic C51 function descriptors at24A9. These bytes must
NOT be disassembled as ordinary instructions/pointers. Class1 targets2143,
class6 targets245B, class7 targets2378. 245B manages modifier-state bits and
can call2143 with code14. These establish that bypass changes key output,
not that the physical Copilot key becomes a no-op. No physical position ->
internal input-code mapping or wire-level capture was collected in this pass.

The files layout-gate-candidate.asm, output-after-transform.asm,
output-dispatch.asm, macro-dispatch.asm and layout-output-handlers.asm preserve
the bounded windows. Some file spans include data or begin mid-instruction;
only explicitly traced aligned entries are interpreted here. The generic
descriptor bytes at24A9..24C0 and mapping bytes at51BC.. are DATA.

Conclusion:72B.0 is a genuine further research lead independent of APP in the
local transform, but NOT a ready safe replacement for Copilot lock. Blindly
adopting it would violate the requirement to preserve normal key behavior.

## 6. Shared APP also has a confirmed FAN writer

Current src/core/src/FanCurve.cpp, apply RAM-curve path, sets APP741.0 after
table commit (around193). restoreNativeFanMode deliberately preserves APP
(around201). These are existing commands; neither was changed or executed.

Therefore the observed APP=1 cannot be blamed uniquely on Copilot, and merely
removing the Copilot APP writer would NOT guarantee a running native idle
counter while the existing Manual fan path retains APP. The owner explicitly
requires fans untouched; clearing shared APP is not an acceptable workaround.

This strengthens the distinction: an independent Copilot mapping and the
backlight timer's eligibility are two separate problems. The known native
timer's APP guard remains in force regardless of who owns the shared bit.

## 7. Owner selected Default and Copilot Off: APP remains retained

Fresh portable0.1.394 log evidence,30October2026 (UTC; Moscow=UTC+3):

- 09:11:32.690Z: `DEFAULT confirmed`, native CPU/GPU mode confirmed by EC.
- 09:11:53.195Z: action88 `keys.uniwill_copilot`, enabled=false.
- 09:11:53.233Z: same action completes `readback_confirmed`.
- 09:12:01.230Z: `Default: already applied`.
- Subsequent SETs observations retain raw741=129, elapsed0, platform7, DC,
  working brightness4, host_power_disabled=false.

Separate fresh elevated read-only CLI confirms Copilot enabled=false and
keyboard idle raw741=0x81, elapsed0, limit600, on_ac=false. No settings applied.

Thus the user's Off/Default actions succeeded. Active Copilot blocking or
active Manual is NOT needed to explain the retained APP=1 now: existing
restoreNativeFanMode explicitly preserves APP; setCopilotKey(false) clears
728.2 but explicitly preserves APP. Neither operation releases this shared
flag. The native idle routine reacts to the retained EC bit, not the current
UI selection or the process name.

This proves persistence of APP after these two consumers deactivate, not
that clearing it is safe: other possible owners still need admission/state
checks. A future fix must use shared ownership/eligibility rather than make
Copilot Off or Fan Default unconditionally clear APP. No such change was
implemented or hardware-tested here; the owner's fan-preservation constraint
remains in force.
