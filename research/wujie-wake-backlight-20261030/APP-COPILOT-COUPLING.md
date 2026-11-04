# Shared APP gate: native Copilot blocking and keyboard idle

30 October 2026. Static read-only recheck of the exact saved Wujie embedded
EC payload, SHA256 5d4ec03a4ec9b36938d907fd6fcb51109b445b4a43c5498f018d022d4e6ac7a8.
No hardware write, product modification or new build.

Bank0 CODE C47F:

```asm
C47F 900728   mov DPTR,#0728h
C482 e0       movx A,@DPTR
C483 5404     anl A,#04h
C485 6008     jz C48F
C487 12c76d   lcall C76D
C48A 6003     jz C48F
C48C 756784   mov 67h,#84h
C48F 22       ret

C76D 900741   mov DPTR,#0741h
C770 e0       movx A,@DPTR
C771 5401     anl A,#01h
C773 22       ret
```

The native keyboard handler's special treatment requires BOTH 728.2 and
741.0. Earlier physical validation of native blocking is preserved under
research/copilot-key-20261027 and research/keys-copilot-cleanup-20261030
(where present). It is not established that 728.2 alone blocks Copilot.

Independently, bank2 BB34 calls D667; D667 reads 741.0. If set, BB3E calls
DD0E to reset counter8A5:8A6, then BB45 clears expired flag741.2 and returns.
The existing firmware-idle.asm preserves that branch. This coupling exists
inside the saved EC payload, not in the GUI countdown formatter.

Current source setCopilotKey in src/protocols/uniwill/QuickSwitch.cpp sets
shared APP741.0 before enabling728.2. Native Off clears728.2 but deliberately
does not clear shared APP: it has consumers outside Copilot. readCopilotKey
requires both flags, in accordance with the traced branch. Removing APP from
that operation cannot be presented as a harmless UI/timer correction.

Fresh elevated read-only `mctl keyboard status --read-only` on30October2026
returned raw741=129 (0x81), elapsed0, limit600, platform7, on_ac=false,
working_level4, ac_level4, dc_level4, host_power_disabled=false. The GUI's
Disabled branch therefore reflects the firmware's disabled idle countdown;
it does not mean keyboard brightness is off.

Setting APP at Copilot On is one confirmed writer. This snapshot alone does
NOT identify which writer last set or retained APP. It is not a dedicated
Copilot, lighting, fan or power-mode bit and must not be unconditionally
cleared by a timer/GUI correction.

Evidence scope: embedded EC update payload, not an independently read live
EC flash image. No new experiment clearing APP was performed.
