MagicControl0.1.107 — automatic battery card refresh

Cause: Windows SETs accepted fresh batteryTelemetry/osBattery/ecBattery samples,
but invalidateBatteryStatus only invalidated the old port/flow/cycles row.
The added primary time rows and the diagnostic card remained outside the update
region until exposure/scrolling produced another paint.

Fix: extend the existing shared-model invalidateBatteryStatus to include each
complete time row (label,value,gap) and the enabled diagnostic card. This covers
all renderSelection branches, including Unknown and native controllers.
InvalidateRect uses FALSE on the existing buffered content HWND; buttons and
scrollbar are not invalidated. No geometry/style/font/polling/hardware changes.
Linux already redraws its canvas after accepting a poll result; no second
refresh implementation was added.

Verification: common Windows/Linux Release0.1.107, matching four staged GUI/CLI
hashes and versions,450 application inputs,7 protected UI sources including
Geometry.h,6 personal files,11 archived battery log files. See generated report.
No tests, GUI, hardware or visual run by the assistant. Runtime repaint should
be confirmed by the owner; compilation is not a visual execution check.
