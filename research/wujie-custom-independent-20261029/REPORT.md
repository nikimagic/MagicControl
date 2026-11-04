# Wujie Custom independence, MagicControl 0.1.391

The 0.1.390 live session retained the same requested STAPM/Slow/Fast
80/80/85 W, but Office left 15/15/15 accepted, Gaming 25/25/30 and Turbo
45/45/45. Internal EC request copies also held 80/80/85. The prior native
CPU policy, not a settings-file rollback, still selected a lower budget.

## OEM and firmware evidence

Exact Wujie 5.57 managed source:
`research/ec-platform-catalogue-20261023/managed-provenance/GCUService-readable-strings.cs`,
class `MyFanManager_RamFan1p5`, `SetFanMode` around line 123762:
Custom case 3 selects CPU base 00 in 0x751 before applying the custom limits.
The source SHA256 is
`79f53a23e8c64b2601c200c8fed0937bedfd033bc7625c41611701e8e2a4361a`.
Other OEM actions (GPU, VRM current, NVRAM and FAN gates) were not transplanted.

The embedded EC update selects the minimum of request values and additional
budgets; the battery/SOC branch selects a table through the current CPU policy.
`firmware-dc-selection.asm` preserves the relevant static excerpt.
This update payload is not a captured running EC flash: live A38/A39=8325
differs from the analysed update table address 6925. No ROM-table addresses
or undocumented budget fields are written by this change.

`history-comparison.json` compares 0.1.388 and 0.1.389: their Custom writer,
SMU reader and FAN controller were identical. The display source changed.
That comparison does not establish the date of the user's earlier successful
physical observation, or disprove it.

## Implementation

One Custom transaction owns CPU mask B0 in 0x751 and overrides 783/784/785.
It selects base 00 while preserving Boost40 and other bits, writes the
original requested values, verifies readback and rolls back only its fields
on failure. The existing FAN controller queue coordinates this with its
ownership and expected-mode state. Curves, gates, PWM and platform/ROM-ID
are not modified by this power operation. The existing 5..255 W input range
is preserved; accepted watts can still be lower.

GUI, CLI and startup/resume Custom restoration enter the same power resource
operation. The unused direct Custom override helper was removed. Standalone
CLI keeps the existing refusal for an unowned retained Manual/Max/RAM state.
GM6, Honor and HPBSD power writers were not changed.

## Validation

Windows CTest 4/4 and Linux CTest 2/2 passed; saved output is alongside this
report. Tests include exact rollback on failed writes, no-op repeat, invalid
inputs, stock/Custom transitions with existing FAN ownership, full fake-EC
table preservation, settings queue failures and existing sleep recovery.

The user operated the actual 0.1.391 Windows portable. In session
2026-10-29T19:53:54.161Z/54045586134100, actions 18 (Office -> Custom) and
21 (Turbo -> Custom) both settled at Fast/Slow/STAPM **30/25/25 W**.
EC and fresh PawnIO/SMU matched in both cases. Requested values remained
85/80/80 W. Each action wrote only 751=00, 783=80, 784=80, 785=85;
see `physical-transitions.json`. This establishes independence from the
previous profile on this Wujie and supply state, not an ability to bypass
manufacturer battery/SOC/power ceilings or apply 225 W.

The read-only post-test probe recorded zero writes. FAN flags 726/727/741/
7C5/7C6 and Boost matched the startup backup. That backup contains only six
mode/flag bytes, so a physical before/after equality claim for all curve
bytes is unavailable. `fan-preservation.json` explicitly records that limit;
the full table equality check is from fake-EC controller tests. The selected
Custom power actions have no table writes in their real audit trail.

Both OS GUI/CLI artifacts match the completed build hashes; see
`verification-0.1.391.json`. Linux baseline settings files were unchanged;
Windows device-settings.ini changed during the user's GUI test. Live settings
were preserved, not replaced to match the old hash. No other model was
physically retested in this task. No GitHub publication/package was requested.
