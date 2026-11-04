# Wujie battery restore dispatcher correction — 0.1.387

The owner's review identified a real gap in 0.1.386: the architecture audit
covered GUI/CLI and policy helpers but omitted ConfigurationRestoreWindows.
That worker still invoked the raw percentage/policy setters. The previous
claim of no bypasses was broader than the evidence supported.

Both battery restore branches now use executeBatteryResource. Pure
savedBatteryWriteRequest maps the saved intention to the same command type
used by GUI/CLI. Percentage-disabled/native-voltage mode stays a no-op;
7/FF uses Policy, while saved 4/FF and 7/04 use Upper with explicit
requestedPolicy. The dispatcher forwards that optional policy to the original
guarded adapter. An ordinary interactive/CLI request leaves it absent, retaining
the prior policy-selection behaviour. No threshold clamp, new hysteresis or
FAN action is introduced. The lower threshold remains selected by the existing
setter when restart=-1.

Tests check saved 10/60/85/95/100, both percentage policies, 7/FF, disabled mode
and unchanged interactive defaults. The unified audit now explicitly rejects
raw battery setters in ConfigurationRestoreWindows and requires its shared
dispatcher call. Final relevant runs passed 11 Windows and 10 Linux tests.
These checks use fixtures; physical charge cutoff/restart was not measured.
This is a correction of the battery lifecycle path, not a claim that a lexical
count proves all other lifecycle domains have no remaining direct calls.

Build-All completed and verified Windows/Linux GUI+CLI portable 0.1.387.
All 58 existing settings files retained their pre-test SHA-256. The local
GitHub_Releases package is prepared separately after committing the application
inputs, so its provenance can name the exact committed revision.

The unrelated read-only GPU power investigation is documented under
research/gpu-power-check-20261029; it does not change displayed watts or limits
in this build. No bulk source archive is added for this focused correction.
