# Hardware-free regression suite

`Run-ModelPreview.ps1` builds and runs the focused Windows/Linux model-preview
matrix in isolated build directories. `magiccontrol_model_preview_tests` uses
the real profile registry, catalog presentation, shared KEYs owner, and the
platform SETs preview owner with injected board identity and memory-only
callbacks. It checks every actual/selected catalog pair, all 11 KEYs actions in
both positions against independent Windows/Linux routing expectations, blocked
preview hardware commands, SETs actions, card parity, and compact geometry. It also
runs the existing FAN, GPU and SETs preview/geometry checks. INFO is intentionally
about the real host, so the test only asserts that preview suppresses FAN probes.
The source-boundary check also verifies that both GUI factories pass the chosen
presentation profile while deriving the hardware owner from the actual board.
There is no GUI/app launch, physical EC, microphone or driver access. This does
not replace an actual-device or pixel-level acceptance check.

`settings_geometry_offline_contract` (owner request24.10.2026) checks every SETs
outer card and nested battery/Logger edges across7DPI,5widths, Logger on/off,
manual/automatic power controls, brightness ranges and diagnostic row counts.
Regression4c4a95e must fail: Logger outer sides may not use the inner content
inset. This target links only C++ runtime and real shared Geometry.h, no GUI/OS/EC.

Battery/card targets link only C++ runtime and five immutable profile-data sources. The log-slot target also compiles the real file logger with fatal discovery stubs.
It does **not** link magiccontrol_core/platform, instantiate MonitoringReader,
create a MagicControl GUI, call SMBIOS/WMI/EC/SMBus/SPI or load a driver.
The additional Windows paint test links only standard user32/gdi32/comctl32,
creates an offscreen no-activate native window and renders shared controls; no
FansPage/SettingsPage, worker, hardware service or app startup is instantiated. The battery journal
uses a capture-only logMessage stub and injected monotonic time. Read/write
callbacks in contract tests operate on local variables only.

Windows + Linux: `Tools/Test-Offline.ps1 -PythonExecutable <python.exe>`.
Standalone: `cmake -S tests/offline -B build/offline/local`, build, then
`ctest --test-dir build/offline/local --output-on-failure` (Release on MSVC).
The normal Build-All command still does not implicitly run GUI/hardware tests.

Four cross-platform tests and one Windows paint regression:

1. battery_offline_contract: real production pure decoder and write contracts,
   saved fast/slow samples, units/endian/sentinels, zero-vs-unknown, identity
   short-circuit, request/platform/stored guards, EC/WMI address-space allowlist,
   neighbouring bits/bytes and owned rollback. Journal:70→5 and skipped SOC,
   30→31, flag deltas, missing/stale samples,10s periodic and disabled logging.
2. settings_cards_offline_parity: actual Windows/Linux value adapters used by
   real pages, independent expected order/rows/counts/sources; five profiles,
   four languages, logging on/off and EC available/unavailable =80 cases,
   geometry at96/144/192DPI. Sources Windows/Linux normalize to OS for semantic
   comparison; the current label explicitly allows calculated W/V on Windows
   versus battery current on Linux. Values/providers need not be equal.
3. battery_evidence_integrity: immutable source/archive hashes,36 normalized
   saved RAM/validity snapshots,2 Wujie fixtures and3 GM6 fixtures with original
   ZIP member hashes, and actual renderer adapter wiring.

GM6 fixtures cover unplugged/charging/full, raw capacity units/state/current/
voltage/cycles, firmware design substitution, disabled0/0 threshold, no borrowed
temperature/PD units, unknown/not-ready guards and register/SOC journal changes.
The same card test expects63 GM6 EC rows versus104 Wujie and27 OS rows; GPU load
selection covers higher usage, missing usage, ties, out-of-range and all unknown.

22.10.2026 decoder audit additionally covers XIF0=0 while voltage/cycles remain
valid, FF/absent/not-ready data, idle with nonzero transient current, raw full
capacity4300→4000 and sampled remaining without per-tick journal growth.
An explicitly unavailable health value must not be restored from cache.
Wujie read-only capacity-unit catalogue adds one row; ROM04 policy allowlist
expectations reflect the explicit owner-approved production contract.

This proves the covered pure contracts and card composition. It does not prove
live transport, WMI settling, full transaction rollback after every injected OS
failure, resume, physical charge cutoff or pixel/scroll/font parity. No test is
registered here that requires hardware. Keep separate build directories so this
suite cannot accidentally run unrelated registered tests from the main project.

4. native_controls_offline_paint (Windows): shared real NumericFieldTheme and
   DiscreteSlider,5 positions in enabled/disabled mode, glyph/background pixels,
   no-op/range updates, native clamping, erase preservation and numeric sizes
   at96/144/192 scale. This is an isolated offscreen HWND, not the application.
   It does not certify disappearance/flicker is eliminated in the live app.

5. paired_log_slots_offline (both OSes): actual Logging.cpp with explicit unique
   build folders; fatal tripwires for default path/SMBIOS discovery. Checks three
   synchronized slot pairs, session IDs, disabled collection metadata, oldest-pair
   replacement, unsegmented files exceeding8MiB, dump copy and preserved legacy
   migration. It never opens the user's active log folder.
