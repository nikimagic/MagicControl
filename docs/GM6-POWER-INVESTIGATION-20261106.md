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
